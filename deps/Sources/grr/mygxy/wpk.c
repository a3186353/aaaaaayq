#include "sdl_proxy.h"
#include "wpk.h"
#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
#include <winternl.h>
#else
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#if __has_include("physfs.h")
#define WPK_USE_PHYSFS 1
#include "physfs.h"
#endif

#define WPK_SEP_STR "/"
#define WPK_DECODED_CACHE_MAX_ENTRIES 64u
#define WPK_DECODED_CACHE_MAX_BYTES ((size_t)32u * 1024u * 1024u)
#define WPK_WRITE_MAX_PACK_BYTES ((Sint64)1024 * 1024 * 1024)
#define WPK_PERF_SAMPLE_CAP 1024

typedef struct WPK_DebugStats
{
    Uint64 idx_mode_file;
    Uint64 idx_mode_buffer;
    Uint64 idx_open_failures;
    Uint64 getdata_raw_hits;
    Uint64 getdata_acxc_hits;
    Uint64 getdata_neox_hits;
    Uint64 getdata_hashxor_hits;
    Uint64 getdata_xor5a_hits;
    Uint64 getdata_failures;
    Uint64 decoded_lru_hits;
    Uint64 decoded_lru_misses;
    Uint64 decoded_lru_inserts;
    Uint64 decoded_lru_evictions;
    Uint64 decoded_lru_skips;
} WPK_DebugStats;

static WPK_DebugStats g_wpk_stats = {0};

typedef struct WPK_TimeStats
{
    Uint64 count;
    Uint64 total_us;
    Uint32 samples[WPK_PERF_SAMPLE_CAP];
    int sample_pos;
    int sample_count;
} WPK_TimeStats;

typedef struct WPK_PerfStats
{
    SDL_mutex* mutex;
    WPK_TimeStats parse_us;
    WPK_TimeStats write_queue_us;
} WPK_PerfStats;

static WPK_PerfStats g_wpk_perf = {0};

static void WPK_PerfEnsure(void)
{
    if (!g_wpk_perf.mutex)
        g_wpk_perf.mutex = SDL_CreateMutex();
}

static Uint64 WPK_NowUS(void)
{
    Uint64 freq = SDL_GetPerformanceFrequency();
    if (!freq)
        return 0;
    return (SDL_GetPerformanceCounter() * 1000000ULL) / freq;
}

static void WPK_TimeRecord(WPK_TimeStats* s, Uint64 elapsed_us)
{
    if (!s)
        return;
    s->count++;
    s->total_us += elapsed_us;
    s->samples[s->sample_pos] = (Uint32)(elapsed_us > 0xFFFFFFFFULL ? 0xFFFFFFFFu : elapsed_us);
    s->sample_pos = (s->sample_pos + 1) % WPK_PERF_SAMPLE_CAP;
    if (s->sample_count < WPK_PERF_SAMPLE_CAP)
        s->sample_count++;
}

static void WPK_RecordTime(WPK_TimeStats* s, Uint64 elapsed_us)
{
    WPK_PerfEnsure();
    if (g_wpk_perf.mutex)
        SDL_LockMutex(g_wpk_perf.mutex);
    WPK_TimeRecord(s, elapsed_us);
    if (g_wpk_perf.mutex)
        SDL_UnlockMutex(g_wpk_perf.mutex);
}

static int WPK_Uint32Compare(const void* a, const void* b)
{
    Uint32 av = *(const Uint32*)a;
    Uint32 bv = *(const Uint32*)b;
    return (av > bv) - (av < bv);
}

static Uint32 WPK_Percentile(Uint32* values, int count, int pct)
{
    int idx;
    if (!values || count <= 0)
        return 0;
    qsort(values, (size_t)count, sizeof(Uint32), WPK_Uint32Compare);
    idx = (count * pct + 99) / 100;
    if (idx < 1) idx = 1;
    if (idx > count) idx = count;
    return values[idx - 1];
}

#ifdef WPK_USE_PHYSFS
static void WPK_LogOpenFailure(const char* stage, const char* path, const char* detail) {
    fprintf(stderr, "[wpk] %s failed: path='%s' detail='%s'\n",
        stage ? stage : "open",
        path ? path : "",
        detail ? detail : "");
}

static Sint64 SDLCALL WPK_physfs_size(SDL_RWops *context) {
    return (Sint64)PHYSFS_fileLength((PHYSFS_File *)context->hidden.unknown.data1);
}
static Sint64 SDLCALL WPK_physfs_seek(SDL_RWops *context, Sint64 offset, int whence) {
    PHYSFS_File *handle = (PHYSFS_File *)context->hidden.unknown.data1;
    Sint64 pos = 0;
    if (whence == RW_SEEK_SET) pos = offset;
    else if (whence == RW_SEEK_CUR) pos = PHYSFS_tell(handle) + offset;
    else if (whence == RW_SEEK_END) pos = PHYSFS_fileLength(handle) + offset;
    if (pos < 0 || !PHYSFS_seek(handle, (PHYSFS_uint64)pos)) return -1;
    return PHYSFS_tell(handle);
}
static size_t SDLCALL WPK_physfs_read(SDL_RWops *context, void *ptr, size_t size, size_t maxnum) {
    PHYSFS_File *handle = (PHYSFS_File *)context->hidden.unknown.data1;
    if (size == 0 || maxnum == 0) return 0;
    PHYSFS_sint64 rc = PHYSFS_readBytes(handle, ptr, (PHYSFS_uint64)(size * maxnum));
    return rc < 0 ? 0 : (size_t)(rc / size);
}
static size_t SDLCALL WPK_physfs_write(SDL_RWops *context, const void *ptr, size_t size, size_t num) {
    PHYSFS_File *handle = (PHYSFS_File *)context->hidden.unknown.data1;
    if (size == 0 || num == 0) return 0;
    PHYSFS_sint64 rc = PHYSFS_writeBytes(handle, ptr, (PHYSFS_uint64)(size * num));
    return rc < 0 ? 0 : (size_t)(rc / size);
}
static int SDLCALL WPK_physfs_close(SDL_RWops *context) {
    if (context) {
        if (context->hidden.unknown.data1) PHYSFS_close((PHYSFS_File *)context->hidden.unknown.data1);
        SDL_FreeRW(context);
    }
    return 0;
}
static SDL_RWops* WPK_SDL_RWFromFile(const char* path, const char* mode) {
    if (PHYSFS_isInit() && !SDL_strchr(path, ':') && path[0] != '/') {
        PHYSFS_File *handle = NULL;
        int wants_write = SDL_strchr(mode, 'w') || SDL_strchr(mode, 'a');
        if (!wants_write || PHYSFS_getWriteDir()) {
            if (SDL_strchr(mode, 'w')) handle = PHYSFS_openWrite(path);
            else if (SDL_strchr(mode, 'a')) handle = PHYSFS_openAppend(path);
            else handle = PHYSFS_openRead(path);
            if (handle) {
                SDL_RWops *rwops = SDL_AllocRW();
                if (rwops) {
                    rwops->size = WPK_physfs_size; rwops->seek = WPK_physfs_seek;
                    rwops->read = WPK_physfs_read; rwops->write = WPK_physfs_write;
                    rwops->close = WPK_physfs_close; rwops->type = SDL_RWOPS_UNKNOWN;
                    rwops->hidden.unknown.data1 = handle;
                    return rwops;
                }
                PHYSFS_close(handle);
            }
            else
            {
                WPK_LogOpenFailure("physfs", path, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
            }
        }
    }
    SDL_RWops *rwops = (SDL_RWFromFile)(path, mode);
    if (!rwops)
    {
        WPK_LogOpenFailure("sdl", path, SDL_GetError());
    }
    return rwops;
}
#define SDL_RWFromFile WPK_SDL_RWFromFile
#endif

static int WPK_EnsureParentDirForWrite(const char* path)
{
    if (!path || !path[0])
        return 0;

    char dir[512];
    SDL_strlcpy(dir, path, sizeof(dir));
    char* lastSlash = SDL_strrchr(dir, '/');
    char* lastBackslash = SDL_strrchr(dir, '\\');
    char* last = lastSlash;
    if (!last || (lastBackslash && lastBackslash > last))
        last = lastBackslash;
    if (!last)
        return 1;
    *last = 0;
    if (!dir[0])
        return 1;

    for (char* p = dir + 1; *p; ++p)
    {
        if (*p != '/' && *p != '\\')
            continue;
        if (p == dir + 2 && dir[1] == ':')
            continue;
        char saved = *p;
        *p = 0;
#if defined(_WIN32)
        if (!CreateDirectoryA(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
            return 0;
#else
        if (mkdir(dir, 0777) != 0 && errno != EEXIST)
            return 0;
#endif
        *p = saved;
    }
#if defined(_WIN32)
    if (!CreateDirectoryA(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return 0;
#else
    if (mkdir(dir, 0777) != 0 && errno != EEXIST)
        return 0;
#endif
    return 1;
}

#if defined(__ANDROID__)
static int WPK_EnsureParentDir(const char* path)
{
    if (!path || !path[0])
        return 0;

    char dir[512];
    SDL_strlcpy(dir, path, sizeof(dir));
    char* last = SDL_strrchr(dir, '/');
    if (!last)
        return 1;
    *last = 0;
    if (!dir[0])
        return 1;

    for (char* p = dir + 1; *p; ++p)
    {
        if (*p != '/')
            continue;
        *p = 0;
        if (mkdir(dir, 0777) != 0 && errno != EEXIST)
            return 0;
        *p = '/';
    }
    if (mkdir(dir, 0777) != 0 && errno != EEXIST)
        return 0;
    return 1;
}

static int WPK_GetLocalFileSize(const char* path, Sint64* outSize)
{
    if (outSize)
        *outSize = -1;
    if (!path || !path[0])
        return 0;

    SDL_RWops* fp = (SDL_RWFromFile)(path, "rb");
    if (!fp)
        return 0;

    Sint64 size = SDL_RWsize(fp);
    SDL_RWclose(fp);
    if (outSize)
        *outSize = size;
    return 1;
}

static SDL_bool WPK_CopyToInternalStorage(const char* path, char outPath[512])
{
    const char* internalPath = SDL_AndroidGetInternalStoragePath();
    if (!path || !path[0] || !internalPath || !internalPath[0] || !outPath)
        return SDL_FALSE;

    if (SDL_snprintf(outPath, 512, "%s/%s", internalPath, path) >= 512)
        return SDL_FALSE;

    SDL_RWops* src = SDL_RWFromFile(path, "rb");
    if (!src)
        return SDL_FALSE;

    Sint64 srcSize = SDL_RWsize(src);
    Sint64 dstSize = -1;
    if (srcSize > 0 && WPK_GetLocalFileSize(outPath, &dstSize) && dstSize == srcSize)
    {
        SDL_RWclose(src);
        return SDL_TRUE;
    }

    if (!WPK_EnsureParentDir(outPath))
    {
        SDL_RWclose(src);
        return SDL_FALSE;
    }

    SDL_RWops* dst = (SDL_RWFromFile)(outPath, "wb");
    if (!dst)
    {
        SDL_RWclose(src);
        return SDL_FALSE;
    }

    const size_t chunkSize = 64 * 1024;
    Uint8* buffer = (Uint8*)SDL_malloc(chunkSize);
    if (!buffer)
    {
        SDL_RWclose(dst);
        SDL_RWclose(src);
        return SDL_FALSE;
    }

    size_t total = 0;
    SDL_bool ok = SDL_TRUE;
    for (;;)
    {
        size_t got = SDL_RWread(src, buffer, 1, chunkSize);
        if (got == 0)
            break;
        if (SDL_RWwrite(dst, buffer, 1, got) != got)
        {
            ok = SDL_FALSE;
            break;
        }
        total += got;
    }

    if (ok && srcSize >= 0 && total != (size_t)srcSize)
        ok = SDL_FALSE;

    SDL_free(buffer);
    SDL_RWclose(dst);
    SDL_RWclose(src);

    if (!ok)
    {
        remove(outPath);
        return SDL_FALSE;
    }
    return SDL_TRUE;
}
#endif

#if defined(_WIN32)
#define WPK_DICT_LIB_A "ggelua.dll"
#define WPK_DICT_LIB_B "mygxy.dll"
#elif defined(__APPLE__)
/* iOS builds produce .framework bundles, not bare .dylib files.
 * dladdr() returns a path like ".../libmygxy.framework/libmygxy",
 * so WPK_GetSelfDir gives ".../libmygxy.framework".
 * We need TWO search strategies:
 *   a) Direct file name inside the framework dir (WPK_DICT_LIB_A/B)
 *   b) Full framework-relative path from the Frameworks/ container
 */
#define WPK_DICT_LIB_A "libggelua"
#define WPK_DICT_LIB_B "libmygxy"
/* Extra: framework bundle paths for scanning from Frameworks/ parent dir */
#define WPK_DICT_FW_A  "libggelua.framework/libggelua"
#define WPK_DICT_FW_B  "libmygxy.framework/libmygxy"
#else
#define WPK_DICT_LIB_A "libggelua.so"
#define WPK_DICT_LIB_B "libmygxy.so"
#endif

/* ---- ZSTD (decompress-only, statically linked) ---- */
#define ZSTD_STATIC_LINKING_ONLY   /* needed for ZSTD_DCtx_setMaxWindowSize */
#include "zstd.h"

/* ---- LZ4 / LZ4-Frame ---- */
#include "lz4.h"
#include "lz4frame.h"

/* ---- zlib (inflate) ---- */
#include "zlib.h"

#if defined(_WIN32)
#define MYGXY_API __declspec(dllexport)
#else
#define MYGXY_API LUAMOD_API
#endif

typedef struct
{
    char md5[33];
    Uint32 hash;
    Uint32 wpkid;
    Uint32 offset;
    Uint32 size;
    Uint16 file_index;
    Uint16 _pad;
} WPK_FileInfo;

typedef struct
{
    char md5[33];
    Uint32 index;
} WPK_Md5LookupEntry;

typedef struct
{
    Uint32 hash;
    Uint32 index;
} WPK_HashLookupEntry;

typedef struct
{
    Uint8 RoundKey[176];
} WPK_Aes128Ctx;

typedef struct WPK_DecodedCacheEntry
{
    Uint32 index;
    Uint32 wpkid;
    Uint32 offset;
    Uint32 packed_size;
    Uint32 hash;
    char md5[33];
    Uint8* data;
    size_t size;
    Uint64 tick;
    struct WPK_DecodedCacheEntry* prev;
    struct WPK_DecodedCacheEntry* next;
} WPK_DecodedCacheEntry;

typedef struct WPK_WriteTask
{
    char md5[33];
    Uint8* data;
    size_t size;
    Uint32 hash;
    int has_hash;
    struct WPK_WriteTask* next;
} WPK_WriteTask;

#if defined(_WIN32)
typedef NTSTATUS(WINAPI* WPK_BCryptOpenAlgorithmProvider_Fn)(void** phAlgorithm, const wchar_t* pszAlgId, const wchar_t* pszImplementation, unsigned long dwFlags);
typedef NTSTATUS(WINAPI* WPK_BCryptSetProperty_Fn)(void* hObject, const wchar_t* pszProperty, unsigned char* pbInput, unsigned long cbInput, unsigned long dwFlags);
typedef NTSTATUS(WINAPI* WPK_BCryptGetProperty_Fn)(void* hObject, const wchar_t* pszProperty, unsigned char* pbOutput, unsigned long cbOutput, unsigned long* pcbResult, unsigned long dwFlags);
typedef NTSTATUS(WINAPI* WPK_BCryptGenerateSymmetricKey_Fn)(void* hAlgorithm, void** phKey, unsigned char* pbKeyObject, unsigned long cbKeyObject, unsigned char* pbSecret, unsigned long cbSecret, unsigned long dwFlags);
typedef NTSTATUS(WINAPI* WPK_BCryptDecrypt_Fn)(void* hKey, unsigned char* pbInput, unsigned long cbInput, void* pPaddingInfo, unsigned char* pbIV, unsigned long cbIV, unsigned char* pbOutput, unsigned long cbOutput, unsigned long* pcbResult, unsigned long dwFlags);
typedef NTSTATUS(WINAPI* WPK_BCryptDestroyKey_Fn)(void* hKey);
typedef NTSTATUS(WINAPI* WPK_BCryptCloseAlgorithmProvider_Fn)(void* hAlgorithm, unsigned long dwFlags);

static int WPK_Aes128DecryptEcb_Windows(Uint8* data, Uint32 n, const Uint8 key[16])
{
    if (!data || !key || n == 0 || (n & 0x0Fu) != 0)
        return 0;

    HMODULE h = LoadLibraryW(L"bcrypt.dll");
    if (!h)
        return 0;

    WPK_BCryptOpenAlgorithmProvider_Fn pOpen = (WPK_BCryptOpenAlgorithmProvider_Fn)GetProcAddress(h, "BCryptOpenAlgorithmProvider");
    WPK_BCryptSetProperty_Fn pSet = (WPK_BCryptSetProperty_Fn)GetProcAddress(h, "BCryptSetProperty");
    WPK_BCryptGetProperty_Fn pGet = (WPK_BCryptGetProperty_Fn)GetProcAddress(h, "BCryptGetProperty");
    WPK_BCryptGenerateSymmetricKey_Fn pGen = (WPK_BCryptGenerateSymmetricKey_Fn)GetProcAddress(h, "BCryptGenerateSymmetricKey");
    WPK_BCryptDecrypt_Fn pDec = (WPK_BCryptDecrypt_Fn)GetProcAddress(h, "BCryptDecrypt");
    WPK_BCryptDestroyKey_Fn pDestroy = (WPK_BCryptDestroyKey_Fn)GetProcAddress(h, "BCryptDestroyKey");
    WPK_BCryptCloseAlgorithmProvider_Fn pClose = (WPK_BCryptCloseAlgorithmProvider_Fn)GetProcAddress(h, "BCryptCloseAlgorithmProvider");

    if (!pOpen || !pSet || !pGet || !pGen || !pDec || !pDestroy || !pClose)
    {
        FreeLibrary(h);
        return 0;
    }

    void* hAlg = NULL;
    void* hKey = NULL;
    Uint8* keyObj = NULL;
    unsigned long keyObjLen = 0;
    unsigned long cb = 0;
    int ok = 0;

    if (pOpen(&hAlg, L"AES", NULL, 0) != 0 || !hAlg)
        goto done;

    {
        static const wchar_t kMode[] = L"ChainingModeECB";
        if (pSet(hAlg, L"ChainingMode", (unsigned char*)kMode, (unsigned long)(sizeof(kMode) - sizeof(wchar_t)), 0) != 0)
            goto done;
    }

    if (pGet(hAlg, L"ObjectLength", (unsigned char*)&keyObjLen, (unsigned long)sizeof(keyObjLen), &cb, 0) != 0 || keyObjLen == 0)
        goto done;

    keyObj = (Uint8*)SDL_malloc((size_t)keyObjLen);
    if (!keyObj)
        goto done;

    if (pGen(hAlg, &hKey, (unsigned char*)keyObj, keyObjLen, (unsigned char*)key, 16, 0) != 0 || !hKey)
        goto done;

    {
        unsigned long outLen = 0;
        if (pDec(hKey, (unsigned char*)data, n, NULL, NULL, 0, (unsigned char*)data, n, &outLen, 0) != 0)
            goto done;
        if (outLen != n)
            goto done;
    }

    ok = 1;

done:
    if (hKey)
        pDestroy(hKey);
    if (hAlg)
        pClose(hAlg, 0);
    if (keyObj)
        SDL_free(keyObj);
    FreeLibrary(h);
    return ok;
}
#endif

static Uint8 WPK_AesMul2(Uint8 x)
{
    return (Uint8)((x << 1) ^ ((x & 0x80) ? 0x1B : 0x00));
}

static Uint8 WPK_AesMul3(Uint8 x)
{
    return (Uint8)(WPK_AesMul2(x) ^ x);
}

static Uint8 WPK_AesMul9(Uint8 x)
{
    Uint8 x2 = WPK_AesMul2(x);
    Uint8 x4 = WPK_AesMul2(x2);
    Uint8 x8 = WPK_AesMul2(x4);
    return (Uint8)(x8 ^ x);
}

static Uint8 WPK_AesMul11(Uint8 x)
{
    Uint8 x2 = WPK_AesMul2(x);
    Uint8 x4 = WPK_AesMul2(x2);
    Uint8 x8 = WPK_AesMul2(x4);
    return (Uint8)(x8 ^ x2 ^ x);
}

static Uint8 WPK_AesMul13(Uint8 x)
{
    Uint8 x2 = WPK_AesMul2(x);
    Uint8 x4 = WPK_AesMul2(x2);
    Uint8 x8 = WPK_AesMul2(x4);
    return (Uint8)(x8 ^ x4 ^ x);
}

static Uint8 WPK_AesMul14(Uint8 x)
{
    Uint8 x2 = WPK_AesMul2(x);
    Uint8 x4 = WPK_AesMul2(x2);
    Uint8 x8 = WPK_AesMul2(x4);
    return (Uint8)(x8 ^ x4 ^ x2);
}

static const Uint8 WPK_AES_SBOX[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static const Uint8 WPK_AES_RSBOX[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d,
};

static void WPK_AesKeyExpansion(const Uint8* key, Uint8* RoundKey)
{
    static const Uint8 Rcon[11] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36};
    SDL_memcpy(RoundKey, key, 16);
    Uint8 temp[4];
    int i = 4;
    while (i < 44)
    {
        temp[0] = RoundKey[(i - 1) * 4 + 0];
        temp[1] = RoundKey[(i - 1) * 4 + 1];
        temp[2] = RoundKey[(i - 1) * 4 + 2];
        temp[3] = RoundKey[(i - 1) * 4 + 3];
        if ((i % 4) == 0)
        {
            Uint8 t = temp[0];
            temp[0] = temp[1];
            temp[1] = temp[2];
            temp[2] = temp[3];
            temp[3] = t;
            temp[0] = WPK_AES_SBOX[temp[0]];
            temp[1] = WPK_AES_SBOX[temp[1]];
            temp[2] = WPK_AES_SBOX[temp[2]];
            temp[3] = WPK_AES_SBOX[temp[3]];
            temp[0] = (Uint8)(temp[0] ^ Rcon[i / 4]);
        }
        RoundKey[i * 4 + 0] = (Uint8)(RoundKey[(i - 4) * 4 + 0] ^ temp[0]);
        RoundKey[i * 4 + 1] = (Uint8)(RoundKey[(i - 4) * 4 + 1] ^ temp[1]);
        RoundKey[i * 4 + 2] = (Uint8)(RoundKey[(i - 4) * 4 + 2] ^ temp[2]);
        RoundKey[i * 4 + 3] = (Uint8)(RoundKey[(i - 4) * 4 + 3] ^ temp[3]);
        i++;
    }
}

static void WPK_AesAddRoundKey(Uint8* state, const Uint8* roundKey)
{
    for (int i = 0; i < 16; i++)
        state[i] = (Uint8)(state[i] ^ roundKey[i]);
}

static void WPK_AesInvSubBytes(Uint8* state)
{
    for (int i = 0; i < 16; i++)
        state[i] = WPK_AES_RSBOX[state[i]];
}

static void WPK_AesInvShiftRows(Uint8* state)
{
    Uint8 tmp;
    tmp = state[13];
    state[13] = state[9];
    state[9] = state[5];
    state[5] = state[1];
    state[1] = tmp;

    tmp = state[2];
    state[2] = state[10];
    state[10] = tmp;
    tmp = state[6];
    state[6] = state[14];
    state[14] = tmp;

    tmp = state[3];
    state[3] = state[7];
    state[7] = state[11];
    state[11] = state[15];
    state[15] = tmp;
}

static void WPK_AesInvMixColumns(Uint8* state)
{
    for (int i = 0; i < 4; i++)
    {
        Uint8* col = state + i * 4;
        Uint8 a = col[0], b = col[1], c = col[2], d = col[3];
        col[0] = (Uint8)(WPK_AesMul14(a) ^ WPK_AesMul11(b) ^ WPK_AesMul13(c) ^ WPK_AesMul9(d));
        col[1] = (Uint8)(WPK_AesMul9(a) ^ WPK_AesMul14(b) ^ WPK_AesMul11(c) ^ WPK_AesMul13(d));
        col[2] = (Uint8)(WPK_AesMul13(a) ^ WPK_AesMul9(b) ^ WPK_AesMul14(c) ^ WPK_AesMul11(d));
        col[3] = (Uint8)(WPK_AesMul11(a) ^ WPK_AesMul13(b) ^ WPK_AesMul9(c) ^ WPK_AesMul14(d));
    }
}

static void WPK_Aes128Init(WPK_Aes128Ctx* ctx, const Uint8 key[16])
{
    if (!ctx)
        return;
    WPK_AesKeyExpansion(key, ctx->RoundKey);
}

static void WPK_Aes128DecryptBlock(const WPK_Aes128Ctx* ctx, Uint8 block[16])
{
    if (!ctx || !block)
        return;

    Uint8 state[16];
    SDL_memcpy(state, block, 16);

    WPK_AesAddRoundKey(state, ctx->RoundKey + 160);
    for (int round = 9; round >= 1; round--)
    {
        WPK_AesInvShiftRows(state);
        WPK_AesInvSubBytes(state);
        WPK_AesAddRoundKey(state, ctx->RoundKey + round * 16);
        WPK_AesInvMixColumns(state);
    }
    WPK_AesInvShiftRows(state);
    WPK_AesInvSubBytes(state);
    WPK_AesAddRoundKey(state, ctx->RoundKey);

    SDL_memcpy(block, state, 16);
}

static Sint32 WPK_WpkIdAsS32(Uint32 v)
{
    return (Sint32)v;
}

struct WPK_UserData
{
    Uint32 number;
    WPK_FileInfo* list;
    WPK_Md5LookupEntry* md5_lookup;
    WPK_HashLookupEntry* hash_lookup;
    Uint32 lookup_count;
    Uint8 lookup_dirty;

    int list_ref;

    SDL_RWops** write_files;
    Uint32 write_files_count;
    WPK_DecodedCacheEntry* decoded_cache_head;
    WPK_DecodedCacheEntry* decoded_cache_tail;
    Uint32 decoded_cache_count;
    size_t decoded_cache_bytes;
    Uint64 decoded_cache_tick;
    WPK_WriteTask* write_head;
    WPK_WriteTask* write_tail;
    Uint32 write_queue_count;
    Uint64 write_queued_total;
    Uint64 write_flushed_total;
    Uint64 write_failed_total;

    ZSTD_DCtx* zstd_dctx;
    ZSTD_DDict* zstd_ddict;
    Uint32 zstd_dict_id;
    Uint8* zstd_dict_buf;
    size_t zstd_dict_size;
    SDL_mutex* native_mutex;

    Uint8 idx_is_skpw;

    Uint8 idx_is_skpe;

    char idx_path[256];
    char base_dir[256];
    char write_base_dir[256];
    char base_name[128];

    Uint32 skpw_unknown;
    Uint32 skpw_version;
};

static int WPK_InitNativeState(WPK_UserData* ud)
{
    if (!ud)
        return 0;
    ud->native_mutex = SDL_CreateMutex();
    return ud->native_mutex != NULL;
}

static void WPK_DestroyNativeState(WPK_UserData* ud)
{
    if (!ud || !ud->native_mutex)
        return;
    SDL_DestroyMutex(ud->native_mutex);
    ud->native_mutex = NULL;
}

static int WPK_LockNativeState(WPK_UserData* ud)
{
    if (!ud || !ud->native_mutex)
        return 0;
    SDL_LockMutex(ud->native_mutex);
    return 1;
}

static void WPK_UnlockNativeState(WPK_UserData* ud)
{
    if (ud && ud->native_mutex)
        SDL_UnlockMutex(ud->native_mutex);
}

#define WPK_LOCK_OR_RETURN(ud, ret) do { if (!WPK_LockNativeState(ud)) return (ret); } while (0)
#define WPK_UNLOCK_RETURN(ud, ret) do { int _wpk_unlock_ret = (ret); WPK_UnlockNativeState(ud); return _wpk_unlock_ret; } while (0)

static int WPK_WriteFileAll(const char* path, const void* data, size_t size)
{
    if (!path || !data || size == 0)
        return 0;
    WPK_EnsureParentDirForWrite(path);
    SDL_RWops* fp = SDL_RWFromFile(path, "wb");
    if (!fp)
        return 0;
    size_t wrote = SDL_RWwrite(fp, data, 1, size);
    SDL_RWclose(fp);
    return wrote == size;
}

static void WPK_FreeWriteTask(WPK_WriteTask* task)
{
    if (!task)
        return;
    if (task->data)
        SDL_free(task->data);
    SDL_free(task);
}

static void WPK_ClearWriteQueue(WPK_UserData* ud)
{
    WPK_WriteTask* task;
    WPK_WriteTask* next;
    if (!ud)
        return;
    task = ud->write_head;
    while (task)
    {
        next = task->next;
        WPK_FreeWriteTask(task);
        task = next;
    }
    ud->write_head = NULL;
    ud->write_tail = NULL;
    ud->write_queue_count = 0;
}

static void WPK_InvalidateListCache(lua_State* L, WPK_UserData* ud)
{
    if (!L || !ud)
        return;
    if (ud->list_ref > 0)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, ud->list_ref);
        ud->list_ref = LUA_NOREF;
    }
}


static int WPK_EnsureWriteFiles(WPK_UserData* ud, Uint32 wpkid)
{
    if (!ud)
        return 0;
    if (wpkid < ud->write_files_count)
        return 1;

    Uint32 newCount = wpkid + 1;
    if (newCount == 0)
        return 0;

    SDL_RWops** p = (SDL_RWops**)SDL_realloc(ud->write_files, sizeof(SDL_RWops*) * newCount);
    if (!p)
        return 0;

    for (Uint32 i = ud->write_files_count; i < newCount; i++)
        p[i] = NULL;
    ud->write_files = p;
    ud->write_files_count = newCount;
    return 1;
}

static void WPK_CloseCachedWriteFile(WPK_UserData* ud, Uint32 wpkid)
{
    if (!ud || wpkid >= ud->write_files_count || !ud->write_files[wpkid])
        return;
    SDL_RWclose(ud->write_files[wpkid]);
    ud->write_files[wpkid] = NULL;
}

static void WPK_CloseCachedWriteFiles(WPK_UserData* ud)
{
    if (!ud || !ud->write_files)
        return;
    for (Uint32 i = 0; i < ud->write_files_count; i++)
    {
        if (ud->write_files[i])
        {
            SDL_RWclose(ud->write_files[i]);
            ud->write_files[i] = NULL;
        }
    }
}

static int WPK_HexNibble(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int WPK_HexToBin16(const char hex32[33], Uint8 out16[16])
{
    if (!hex32 || !out16)
        return 0;
    for (int i = 0; i < 16; i++)
    {
        int hi = WPK_HexNibble((unsigned char)hex32[i * 2 + 0]);
        int lo = WPK_HexNibble((unsigned char)hex32[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return 0;
        out16[i] = (Uint8)((hi << 4) | lo);
    }
    return 1;
}

static int WPK_IsHexChar(int c);

static int WPK_NormalizeMd5Hex32(char outLower33[33], const char* md5)
{
    if (!outLower33 || !md5)
        return 0;
    size_t n = SDL_strlen(md5);
    if (n != 32)
        return 0;
    for (int i = 0; i < 32; i++)
    {
        int c = (unsigned char)md5[i];
        if (!WPK_IsHexChar(c))
            return 0;
        if (c >= 'A' && c <= 'F')
            c = c - 'A' + 'a';
        outLower33[i] = (char)c;
    }
    outLower33[32] = 0;
    return 1;
}

static int WPK_CompareMd5Lookup(const void* a, const void* b)
{
    const WPK_Md5LookupEntry* la = (const WPK_Md5LookupEntry*)a;
    const WPK_Md5LookupEntry* lb = (const WPK_Md5LookupEntry*)b;
    int cmp = SDL_memcmp(la->md5, lb->md5, 32);
    if (cmp != 0)
        return cmp;
    if (la->index < lb->index)
        return -1;
    if (la->index > lb->index)
        return 1;
    return 0;
}

static int WPK_CompareHashLookup(const void* a, const void* b)
{
    const WPK_HashLookupEntry* la = (const WPK_HashLookupEntry*)a;
    const WPK_HashLookupEntry* lb = (const WPK_HashLookupEntry*)b;
    if (la->hash < lb->hash)
        return -1;
    if (la->hash > lb->hash)
        return 1;
    if (la->index < lb->index)
        return -1;
    if (la->index > lb->index)
        return 1;
    return 0;
}

static void WPK_ClearLookupIndexes(WPK_UserData* ud)
{
    if (!ud)
        return;
    if (ud->md5_lookup)
        SDL_free(ud->md5_lookup);
    if (ud->hash_lookup)
        SDL_free(ud->hash_lookup);
    ud->md5_lookup = NULL;
    ud->hash_lookup = NULL;
    ud->lookup_count = 0;
    ud->lookup_dirty = 1;
}

static void WPK_MarkLookupDirty(WPK_UserData* ud)
{
    if (ud)
        ud->lookup_dirty = 1;
}

static int WPK_RebuildLookupIndexes(WPK_UserData* ud)
{
    if (!ud)
        return 0;

    if (!ud->list || ud->number == 0)
    {
        WPK_ClearLookupIndexes(ud);
        ud->lookup_dirty = 0;
        return 1;
    }

    WPK_Md5LookupEntry* md5_lookup = (WPK_Md5LookupEntry*)SDL_malloc(sizeof(WPK_Md5LookupEntry) * ud->number);
    WPK_HashLookupEntry* hash_lookup = (WPK_HashLookupEntry*)SDL_malloc(sizeof(WPK_HashLookupEntry) * ud->number);
    if (!md5_lookup || !hash_lookup)
    {
        if (md5_lookup)
            SDL_free(md5_lookup);
        if (hash_lookup)
            SDL_free(hash_lookup);
        WPK_ClearLookupIndexes(ud);
        return 0;
    }

    for (Uint32 i = 0; i < ud->number; i++)
    {
        SDL_memcpy(md5_lookup[i].md5, ud->list[i].md5, 33);
        md5_lookup[i].index = i;
        hash_lookup[i].hash = ud->list[i].hash;
        hash_lookup[i].index = i;
    }
    qsort(md5_lookup, ud->number, sizeof(WPK_Md5LookupEntry), WPK_CompareMd5Lookup);
    qsort(hash_lookup, ud->number, sizeof(WPK_HashLookupEntry), WPK_CompareHashLookup);

    if (ud->md5_lookup)
        SDL_free(ud->md5_lookup);
    if (ud->hash_lookup)
        SDL_free(ud->hash_lookup);
    ud->md5_lookup = md5_lookup;
    ud->hash_lookup = hash_lookup;
    ud->lookup_count = ud->number;
    ud->lookup_dirty = 0;
    return 1;
}

static int WPK_EnsureLookupIndexes(WPK_UserData* ud)
{
    if (!ud)
        return 0;
    if (!ud->lookup_dirty && ud->lookup_count == ud->number && ud->md5_lookup && ud->hash_lookup)
        return 1;
    return WPK_RebuildLookupIndexes(ud);
}

static int WPK_FindByMd5Linear(const WPK_UserData* ud, const char md5Lower33[33])
{
    if (!ud || !ud->list || ud->number == 0 || !md5Lower33)
        return -1;
    for (Uint32 i = 0; i < ud->number; i++)
    {
        if (SDL_memcmp(ud->list[i].md5, md5Lower33, 32) == 0)
            return (int)i;
    }
    return -1;
}

static int WPK_FindByMd5(WPK_UserData* ud, const char md5Lower33[33])
{
    if (!ud || !md5Lower33)
        return -1;
    if (WPK_EnsureLookupIndexes(ud))
    {
        Uint32 lo = 0;
        Uint32 hi = ud->lookup_count;
        while (lo < hi)
        {
            Uint32 mid = lo + (hi - lo) / 2;
            int cmp = SDL_memcmp(ud->md5_lookup[mid].md5, md5Lower33, 32);
            if (cmp < 0)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo < ud->lookup_count && SDL_memcmp(ud->md5_lookup[lo].md5, md5Lower33, 32) == 0)
            return (int)ud->md5_lookup[lo].index;
    }
    return WPK_FindByMd5Linear(ud, md5Lower33);
}

static int WPK_FindByHashLinear(const WPK_UserData* ud, Uint32 hash)
{
    if (!ud || !ud->list || ud->number == 0)
        return -1;
    for (Uint32 i = 0; i < ud->number; i++)
    {
        if (ud->list[i].hash == hash)
            return (int)i;
    }
    return -1;
}

static int WPK_FindByHash(WPK_UserData* ud, Uint32 hash)
{
    if (!ud)
        return -1;
    if (WPK_EnsureLookupIndexes(ud))
    {
        Uint32 lo = 0;
        Uint32 hi = ud->lookup_count;
        while (lo < hi)
        {
            Uint32 mid = lo + (hi - lo) / 2;
            if (ud->hash_lookup[mid].hash < hash)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo < ud->lookup_count && ud->hash_lookup[lo].hash == hash)
            return (int)ud->hash_lookup[lo].index;
    }
    return WPK_FindByHashLinear(ud, hash);
}

static void WPK_RevXor5AInplace(Uint8* data, size_t n)
{
    if (!data || n == 0)
        return;
    for (size_t i = 0; i < n; i++)
        data[i] = (Uint8)(data[i] ^ 0x5A);
    for (size_t i = 0; i < n / 2; i++)
    {
        Uint8 t = data[i];
        data[i] = data[n - 1 - i];
        data[n - 1 - i] = t;
    }
}

static int WPK_BuildSkpeBlob(const Uint8* plain, size_t plainSize, Uint8** outBlob, size_t* outSize)
{
    if (!plain || plainSize == 0 || !outBlob || !outSize)
        return 0;
    *outBlob = NULL;
    *outSize = 0;

    size_t total = 4 + plainSize;
    Uint8* blob = (Uint8*)SDL_malloc(total);
    if (!blob)
        return 0;

    blob[0] = 'S';
    blob[1] = 'K';
    blob[2] = 'P';
    blob[3] = 'E';
    SDL_memcpy(blob + 4, plain, plainSize);
    WPK_RevXor5AInplace(blob + 4, plainSize);

    *outBlob = blob;
    *outSize = total;
    return 1;
}

static Uint32 WPK_ReadU32LE(const Uint8* p)
{
    return (Uint32)p[0] | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16) | ((Uint32)p[3] << 24);
}

static Uint16 WPK_ReadU16LE(const Uint8* p)
{
    return (Uint16)p[0] | ((Uint16)p[1] << 8);
}

static void WPK_BinToLowerHex32(char out[33], const Uint8* in16)
{
    static const char* hex = "0123456789abcdef";
    for (int i = 0; i < 16; i++)
    {
        Uint8 b = in16[i];
        out[i * 2 + 0] = hex[(b >> 4) & 0x0F];
        out[i * 2 + 1] = hex[b & 0x0F];
    }
    out[32] = 0;
}

static int WPK_IsHexChar(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int WPK_IsHex32(const Uint8* p)
{
    for (int i = 0; i < 32; i++)
    {
        if (!WPK_IsHexChar(p[i]))
            return 0;
    }
    return 1;
}

static void WPK_ToLowerHex32(char out[33], const Uint8* in)
{
    for (int i = 0; i < 32; i++)
    {
        char c = (char)in[i];
        if (c >= 'A' && c <= 'F')
            c = (char)(c - 'A' + 'a');
        out[i] = c;
    }
    out[32] = 0;
}

static void THX_XorRev64Inplace(Uint8* buf, size_t size)
{
    if (!buf || size < 68)
        return;
    Uint8* p = buf + 4;
    for (size_t i = 0; i < 64; i++)
        p[i] = (Uint8)(p[i] ^ 0x5A);
    for (size_t i = 0; i < 32; i++)
    {
        Uint8 t = p[i];
        p[i] = p[63 - i];
        p[63 - i] = t;
    }
}

static int WPK_HasOnlyZeroPadding(const Uint8* data, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        if (data[i] != 0)
            return 0;
    }
    return 1;
}

static int WPK_HasOfficialThxTail(const Uint8* data, size_t size)
{
    if (WPK_HasOnlyZeroPadding(data, size))
        return 1;
    if (size < 20 || WPK_ReadU32LE(data) != 0 || WPK_ReadU32LE(data + 4) != 0x7Bu)
        return 0;

    Uint32 count = WPK_ReadU32LE(data + 8);
    if (count == 0 || (size - 12) / 8 < count)
        return 0;
    size_t need = 12 + (size_t)count * 8;
    return WPK_HasOnlyZeroPadding(data + need, size - need);
}

static int WPK_TryParseThx24Header(const Uint8* data, size_t size, Uint32* outCount)
{
    if (!data || !outCount || size < 12)
        return 0;
    if (!(data[0] == 'T' && data[1] == 'H' && data[2] == 'D' && data[3] == 'O'))
        return 0;
    int isSkpe = data[4] == 'S' && data[5] == 'K' && data[6] == 'P' && data[7] == 'E';
    int isEnon = data[4] == 'E' && data[5] == 'N' && data[6] == 'O' && data[7] == 'N';
    if (!isSkpe && !isEnon)
        return 0;
    Uint32 count = WPK_ReadU32LE(data + 8);
    if (count == 0 || (size - 12) / 24 < count)
        return 0;
    size_t need = 12 + (size_t)count * 24;
    if (need != size && (!isEnon || !WPK_HasOfficialThxTail(data + need, size - need)))
        return 0;
    *outCount = count;
    return 1;
}

static void WPK_ExtractBaseDir(char out[256], const char* path)
{
    size_t n = SDL_strlen(path);
    size_t last = (size_t)-1;
    for (size_t i = 0; i < n; i++)
    {
        char c = path[i];
        if (c == '/' || c == '\\')
            last = i;
    }
    if (last == (size_t)-1)
    {
        SDL_strlcpy(out, ".", 256);
        return;
    }
    size_t len = last;
    if (len >= 255)
        len = 255;
    SDL_memcpy(out, path, len);
    out[len] = 0;
}

static void WPK_ExtractBaseName(char out[128], const char* path)
{
    const char* p = path;
    const char* lastSlash = NULL;
    const char* lastDot = NULL;
    while (*p)
    {
        if (*p == '/' || *p == '\\')
            lastSlash = p;
        if (*p == '.')
            lastDot = p;
        p++;
    }
    const char* nameStart = lastSlash ? lastSlash + 1 : path;
    const char* nameEnd = (lastDot && lastDot > nameStart) ? lastDot : p;
    size_t len = (size_t)(nameEnd - nameStart);
    if (len >= 127)
        len = 127;
    SDL_memcpy(out, nameStart, len);
    out[len] = 0;
}




static void WPK_BuildDataPackPath(WPK_UserData* ud, Uint32 wpkid, char out[512])
{
    char lower_base_name[128];
    const char* base_dir = ".";
    SDL_strlcpy(lower_base_name, ud && ud->base_name[0] ? ud->base_name : "cache", sizeof(lower_base_name));
    for (size_t i = 0; lower_base_name[i]; i++)
        lower_base_name[i] = (char)SDL_tolower((unsigned char)lower_base_name[i]);
    if (ud && ud->write_base_dir[0])
        base_dir = ud->write_base_dir;
    else if (ud && ud->base_dir[0])
        base_dir = ud->base_dir;
    SDL_snprintf(out, 512, "%s" WPK_SEP_STR "%s%u.wpk",
        base_dir,
        lower_base_name,
        (unsigned)wpkid);
}

static SDL_RWops* WPK_OpenWriteFile(WPK_UserData* ud, Uint32 wpkid);

static Sint64 WPK_GetDataPackSize(WPK_UserData* ud, Uint32 wpkid)
{
    if (ud && wpkid < ud->write_files_count && ud->write_files[wpkid])
    {
        Sint64 pos = SDL_RWseek(ud->write_files[wpkid], 0, RW_SEEK_END);
        return pos > 0 ? pos : 0;
    }

    SDL_RWops* fp = WPK_OpenWriteFile(ud, wpkid);
    if (!fp)
        return -1;
    Sint64 size = SDL_RWseek(fp, 0, RW_SEEK_END);
    return size > 0 ? size : 0;
}

static SDL_RWops* WPK_OpenWriteFile(WPK_UserData* ud, Uint32 wpkid)
{
    char path[512];
    if (!ud || !WPK_EnsureWriteFiles(ud, wpkid))
        return NULL;
    if (ud->write_files[wpkid])
        return ud->write_files[wpkid];

    WPK_BuildDataPackPath(ud, wpkid, path);
    WPK_EnsureParentDirForWrite(path);
    SDL_RWops* fp = SDL_RWFromFile(path, "ab");
    if (!fp)
        return NULL;
    ud->write_files[wpkid] = fp;
    return fp;
}

static int WPK_AppendDataPack(WPK_UserData* ud, Uint32 wpkid, const void* data, size_t size, Uint32* outOffset)
{
    if (!ud || !data || size == 0 || size > (size_t)0xFFFFFFFFu)
        return 0;
    SDL_RWops* fp = WPK_OpenWriteFile(ud, wpkid);
    if (!fp)
        return 0;

    Sint64 offset = SDL_RWseek(fp, 0, RW_SEEK_END);
    if (offset < 0 || offset > (Sint64)0xFFFFFFFFu)
        return 0;

    size_t wrote = SDL_RWwrite(fp, data, 1, size);
    if (wrote != size)
    {
        WPK_CloseCachedWriteFile(ud, wpkid);
        return 0;
    }
    if (outOffset)
        *outOffset = (Uint32)offset;
    return 1;
}

static Uint32 WPK_SelectWritePack(WPK_UserData* ud, size_t size)
{
    if (!ud || size > (size_t)WPK_WRITE_MAX_PACK_BYTES)
        return 255u;

    for (Uint32 wpkid = 0; wpkid < 255u; wpkid++)
    {
        Sint64 packSize = WPK_GetDataPackSize(ud, wpkid);
        if (packSize >= 0 && packSize + (Sint64)size <= WPK_WRITE_MAX_PACK_BYTES)
            return wpkid;
    }
    return 255u;
}

static void WPK_XorRepeat4(Uint8* dst, const Uint8* src, size_t n, const Uint8 key[4])
{
    for (size_t i = 0; i < n; i++)
        dst[i] = (Uint8)(src[i] ^ key[i & 3]);
}

static void WPK_XorByte(Uint8* dst, const Uint8* src, size_t n, Uint8 key)
{
    for (size_t i = 0; i < n; i++)
        dst[i] = (Uint8)(src[i] ^ key);
}

static int WPK_IsZstdFrameMagic(const Uint8* p, size_t n)
{
    if (n < 4)
        return 0;
    return p[0] == 0x28 && p[1] == 0xB5 && p[2] == 0x2F && p[3] == 0xFD;
}

static int WPK_IsLz4FrameMagic(const Uint8* p, size_t n)
{
    if (n < 4)
        return 0;
    return p[0] == 0x04 && p[1] == 0x22 && p[2] == 0x4D && p[3] == 0x18;
}

static int WPK_IsGzipMagic(const Uint8* p, size_t n)
{
    if (n < 2)
        return 0;
    return p[0] == 0x1F && p[1] == 0x8B;
}

static int WPK_IsZlibHeader(const Uint8* p, size_t n)
{
    if (n < 2)
        return 0;
    const Uint8 cmf = p[0];
    const Uint8 flg = p[1];
    if ((cmf & 0x0F) != 8)
        return 0;
    const Uint32 chk = ((Uint32)cmf << 8) | (Uint32)flg;
    if ((chk % 31u) != 0u)
        return 0;
    if ((flg & 0x20) != 0)
    {
        if (n < 6)
            return 0;
    }
    return 1;
}

static int WPK_LooksLikeCompressed(const Uint8* p, size_t n)
{
    return WPK_IsZstdFrameMagic(p, n) || WPK_IsLz4FrameMagic(p, n) || WPK_IsGzipMagic(p, n) || WPK_IsZlibHeader(p, n);
}

static int WPK_IsNeoxMagicU32(Uint32 magic)
{
    return magic == 0x5A535444u || magic == 0x5A4C4942u || magic == 0x5A4C4941u || magic == 0x4C5A3446u ||
           magic == 0x4E4F4E45u;
}

static void WPK_DeobfuscateXor5AReverse64(Uint8* data, size_t n)
{
    if (!data || n == 0)
        return;
    size_t block = n < 64 ? n : 64;
    Uint8 tmp[64];
    for (size_t j = 0; j < block; j++)
        tmp[j] = (Uint8)(data[block - 1 - j] ^ 0x5A);
    SDL_memcpy(data, tmp, block);
}

static void WPK_GenerateAesKeyFromHeader(const Uint8* in, size_t inSize, Uint8 outKey[16])
{
    if (!in || inSize < 8 || !outKey)
        return;
    Uint32 dwDataSize = (Uint32)(inSize - 8);
    outKey[0] = (Uint8)(dwDataSize % 0xFDu);
    outKey[1] = (Uint8)(in[3] + dwDataSize);
    outKey[2] = (Uint8)((dwDataSize >> 8) & 0xFFu);
    outKey[3] = (Uint8)((dwDataSize >> 16) & 0xFFu);
    outKey[4] = 0x6Au;
    outKey[5] = 0x6Bu;
    outKey[6] = 0x2Eu;
    outKey[7] = 0x7Cu;
    outKey[8] = 0x30u;
    outKey[9] = 0x36u;
    outKey[10] = (Uint8)(outKey[1] ^ 0x33u);
    outKey[11] = (Uint8)(outKey[1] | 0x2Eu);
    outKey[12] = 0x6Eu;
    outKey[13] = 0x65u;
    outKey[14] = 0x74u;
    outKey[15] = 0x5Cu;
}

static void WPK_GenerateNcKeyFromHeader(const Uint8* in, size_t inSize, Uint8 outKey[16])
{
    if (!in || inSize < 8 || !outKey)
        return;

    Uint32 dwDataSize = (Uint32)(inSize - 8);
    Uint32 s = dwDataSize;
    s ^= (Uint32)in[3] | ((Uint32)in[4] << 8) | ((Uint32)in[5] << 16) | ((Uint32)in[6] << 24);
    s ^= ((Uint32)in[7] << 11);
    s ^= 0x9E3779B9u;

    for (int i = 0; i < 16; i++)
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        outKey[i] = (Uint8)(s & 0xFFu);
        s += 0x7F4A7C15u + (Uint32)i * 0x85EBCA6Bu;
    }
}

static void WPK_DeobfuscateNc(Uint8* data, size_t n, const Uint8* header)
{
    if (!data || !header || n == 0)
        return;
    size_t block = 64 + (size_t)(header[3] & 0x1Fu);
    if (block > n)
        block = n;
    if (block == 0)
        return;
    if (block > 96)
        block = 96;
    Uint8 k = (Uint8)(0xA5u ^ header[4] ^ header[7]);
    Uint8 tmp[96];
    for (size_t j = 0; j < block; j++)
        tmp[j] = (Uint8)(data[block - 1 - j] ^ k);
    SDL_memcpy(data, tmp, block);
}

static void WPK_XorDecrypt_AC(Uint8* buf, Uint32 dwDataSize, Uint32 dwActualSize, Uint32 dwBlockSize, const Uint8 key[16])
{
    if (!buf || !key)
        return;
    if (dwBlockSize > dwDataSize)
        dwBlockSize = dwDataSize;

    if (dwActualSize == 0)
        return;
    if (dwActualSize > dwBlockSize)
        dwActualSize = dwBlockSize;

    if (dwActualSize >= dwBlockSize)
        return;

    Uint32 tailCount = dwBlockSize - dwActualSize;

    Uint8 k = key[1];
    Uint8* dst = buf + dwActualSize;
    for (Uint32 i = 0; i < tailCount; i++)
        dst[i] = (Uint8)(dst[i] ^ (Uint8)(k + (Uint8)i + buf[i]));
}

static void WPK_XorDecrypt_XC(Uint8* buf, Uint32 dwDataSize, Uint32 dwBlockSize)
{
    if (!buf)
        return;
    if (dwBlockSize > dwDataSize)
        dwBlockSize = dwDataSize;
    Uint8 xorkey[128];
    for (int i = 0; i < 128; i++)
        xorkey[i] = (Uint8)(dwDataSize + (Uint32)i);
    for (Uint32 i = 0; i < dwBlockSize; i++)
        buf[i] = (Uint8)(buf[i] ^ xorkey[i & 0x7F]);
}

static void WPK_PushBytesAndSize(lua_State* L, const void* data, size_t size);
static int WPK_NativeDecodeBuffer(WPK_UserData* ud, Uint32 index, const WPK_FileInfo* fi,
                                  const Uint8* raw, size_t inSize, Uint8** outData, size_t* outSize,
                                  char* err, size_t errSize);

static size_t WPK_ZstdOutputBound(const Uint8* src, size_t srcSize);
static int WPK_ZstdFrameGetDictID(const Uint8* src, size_t srcSize, Uint32* outId);


static Uint32 WPK_ZstdDictGetID(const Uint8* dict, size_t dictSize)
{
    if (!dict || dictSize < 8)
        return 0;
    if (!(dict[0] == 0x37 && dict[1] == 0xA4 && dict[2] == 0x30 && dict[3] == 0xEC))
        return 0;
    return WPK_ReadU32LE(dict + 4);
}

static size_t WPK_ZstdOutputBound(const Uint8* src, size_t srcSize)
{
    const unsigned long long contentSize = ZSTD_getFrameContentSize(src, srcSize);
    if (contentSize != (unsigned long long)-1 && contentSize != (unsigned long long)-2)
        return (size_t)contentSize;

    return ZSTD_decompressBound(src, srcSize);
}

static int WPK_ZstdFrameGetDictID(const Uint8* src, size_t srcSize, Uint32* outId)
{
    if (!outId)
        return 0;
    *outId = 0;
    if (!WPK_IsZstdFrameMagic(src, srcSize))
        return 0;
    if (srcSize < 6)
        return 0;

    const Uint8 fhd = src[4];
    const Uint8 dictFlag = (Uint8)(fhd & 0x03);
    const Uint8 singleSegment = (Uint8)((fhd >> 5) & 0x01);

    if (dictFlag == 0)
        return 0;

    size_t pos = 5;
    if (!singleSegment)
        pos++;

    size_t dictSize = 0;
    if (dictFlag == 1)
        dictSize = 1;
    else if (dictFlag == 2)
        dictSize = 2;
    else if (dictFlag == 3)
        dictSize = 4;

    if (dictSize == 0)
        return 0;

    if (pos + dictSize > srcSize)
        return 0;

    Uint32 id = 0;
    for (size_t i = 0; i < dictSize; i++)
        id |= ((Uint32)src[pos + i]) << (8 * (Uint32)i);
    *outId = id;
    return 1;
}

static size_t WPK_RWreadAll(SDL_RWops* fp, void* dst, size_t size)
{
    if (!fp || !dst || size == 0)
        return 0;

    size_t total = 0;
    Uint8* out = (Uint8*)dst;
    const size_t chunkSize = 64 * 1024;
    while (total < size)
    {
        size_t want = size - total;
        if (want > chunkSize)
            want = chunkSize;
        size_t chunk = SDL_RWread(fp, out + total, 1, want);
        if (chunk == 0)
            break;
        total += chunk;
    }
    return total;
}

static int WPK_ReadFileAll(const char* path, Uint8** outData, size_t* outSize)
{
    if (!outData || !outSize)
        return 0;
    *outData = NULL;
    *outSize = 0;

    SDL_RWops* fp = SDL_RWFromFile(path, "rb");
    if (!fp)
    {
        fprintf(stderr, "[wpk] read file failed: path='%s'\n", path ? path : "");
        return 0;
    }
    if (SDL_RWseek(fp, 0, RW_SEEK_END) < 0)
    {
        SDL_RWclose(fp);
        return 0;
    }
    Sint64 sz = SDL_RWtell(fp);
    if (sz <= 0 || (Uint64)sz > (Uint64)((size_t)-1))
    {
        SDL_RWclose(fp);
        return 0;
    }
    if (SDL_RWseek(fp, 0, RW_SEEK_SET) < 0)
    {
        SDL_RWclose(fp);
        return 0;
    }

    size_t size = (size_t)sz;
    Uint8* data = (Uint8*)SDL_malloc(size);
    if (!data)
    {
        SDL_RWclose(fp);
        return 0;
    }
    size_t readCount = WPK_RWreadAll(fp, data, size);
    SDL_RWclose(fp);
    if (readCount != size)
    {
        SDL_free(data);
        return 0;
    }
    *outData = data;
    *outSize = size;
    return 1;
}

static int WPK_PathDirname(char out[512], const char* path)
{
    if (!out || !path)
        return 0;
    size_t n = SDL_strlen(path);
    size_t last = (size_t)-1;
    for (size_t i = 0; i < n; i++)
    {
        char c = path[i];
        if (c == '/' || c == '\\')
            last = i;
    }
    if (last == (size_t)-1)
        return 0;
    if (last >= 511)
        last = 511;
    SDL_memcpy(out, path, last);
    out[last] = 0;
    return 1;
}

static int WPK_GetSelfDir(char out[512])
{
#if defined(_WIN32)
    HMODULE h = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&WPK_GetSelfDir, &h))
        return 0;

    char full[MAX_PATH];
    DWORD n = GetModuleFileNameA(h, full, (DWORD)sizeof(full));
    if (n == 0 || n >= sizeof(full))
        return 0;
    full[n] = 0;
    return WPK_PathDirname(out, full);
#else
    if (!out)
        return 0;
    Dl_info info;
    SDL_memset(&info, 0, sizeof(info));
    if (dladdr((void*)&WPK_GetSelfDir, &info) == 0)
        return 0;
    if (!info.dli_fname || !info.dli_fname[0])
        return 0;
    return WPK_PathDirname(out, info.dli_fname);
#endif
}

static int WPK_TryLoadZstdDictFromFile(WPK_UserData* ud, Uint32 wantDictId, const char* path)
{
    if (!ud || !path)
        return 0;
    if (ud->zstd_ddict)
        return 0;

    Uint8* blob = NULL;
    size_t blobSize = 0;
    if (!WPK_ReadFileAll(path, &blob, &blobSize))
        return 0;

    static const Uint8 dictMagicLE[4] = {0x37, 0xA4, 0x30, 0xEC};
    static const size_t dictSizeCandidates[] = {
        4u * 1024u,
        8u * 1024u,
        12u * 1024u,
        16u * 1024u,
        24u * 1024u,
        32u * 1024u,
        48u * 1024u,
        64u * 1024u,
        96u * 1024u,
        128u * 1024u,
        160u * 1024u,
        192u * 1024u,
        256u * 1024u,
        320u * 1024u,
        384u * 1024u,
        512u * 1024u,
        768u * 1024u,
        1024u * 1024u,
        1536u * 1024u,
        2048u * 1024u,
    };

    int ok = 0;
    for (size_t i = 0; i + 8 <= blobSize; i++)
    {
        if (blob[i] != dictMagicLE[0] || blob[i + 1] != dictMagicLE[1] || blob[i + 2] != dictMagicLE[2] || blob[i + 3] != dictMagicLE[3])
            continue;

        const Uint8* base = blob + i;
        const size_t remain = blobSize - i;

        for (size_t si = 0; si < (sizeof(dictSizeCandidates) / sizeof(dictSizeCandidates[0])); si++)
        {
            size_t dictSize = dictSizeCandidates[si];
            if (dictSize > remain)
                continue;

            Uint32 dictId = WPK_ZstdDictGetID(base, dictSize);
            if (dictId != wantDictId)
                continue;

            Uint32 zstdId = (Uint32)ZSTD_getDictID_fromDict(base, dictSize);
            if (zstdId != wantDictId)
                continue;

            Uint8* dictCopy = (Uint8*)SDL_malloc(dictSize);
            if (!dictCopy)
                continue;
            SDL_memcpy(dictCopy, base, dictSize);

            ZSTD_DDict* ddict = ZSTD_createDDict(dictCopy, dictSize);
            if (!ddict)
            {
                SDL_free(dictCopy);
                continue;
            }
            Uint32 ddictId = (Uint32)ZSTD_getDictID_fromDDict(ddict);
            if (ddictId != wantDictId)
            {
                ZSTD_freeDDict(ddict);
                SDL_free(dictCopy);
                continue;
            }

            ud->zstd_ddict = ddict;
            ud->zstd_dict_id = dictId;
            ud->zstd_dict_buf = dictCopy;
            ud->zstd_dict_size = dictSize;
            ok = 1;
            break;
        }
        if (ok)
            break;
    }

    SDL_free(blob);
    return ok;
}

static void WPK_TryLoadZstdDictFromDir(WPK_UserData* ud, Uint32 wantDictId, const char* dir)
{
    if (!ud || ud->zstd_ddict || !dir || !dir[0])
        return;

    char path[512];
    SDL_snprintf(path, sizeof(path), "%s" WPK_SEP_STR "%s", dir, WPK_DICT_LIB_A);
    WPK_TryLoadZstdDictFromFile(ud, wantDictId, path);
    if (ud->zstd_ddict)
        return;

    SDL_snprintf(path, sizeof(path), "%s" WPK_SEP_STR "%s", dir, WPK_DICT_LIB_B);
    WPK_TryLoadZstdDictFromFile(ud, wantDictId, path);
    if (ud->zstd_ddict)
        return;

#if defined(_WIN32)
    for (int pass = 0; pass < 2; pass++)
    {
        if (ud->zstd_ddict)
            break;

        char pattern[512];
        if (pass == 0)
            SDL_snprintf(pattern, sizeof(pattern), "%s\\*.dll", dir);
        else
            SDL_snprintf(pattern, sizeof(pattern), "%s\\*.wpk", dir);

        WIN32_FIND_DATAA ffd;
        HANDLE hFind = FindFirstFileA(pattern, &ffd);
        if (hFind == INVALID_HANDLE_VALUE)
            continue;

        int scanned = 0;
        do
        {
            if (ud->zstd_ddict)
                break;
            if (scanned++ > 128)
                break;
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            if (!ffd.cFileName[0])
                continue;

            SDL_snprintf(path, sizeof(path), "%s\\%s", dir, ffd.cFileName);
            WPK_TryLoadZstdDictFromFile(ud, wantDictId, path);
        } while (FindNextFileA(hFind, &ffd));

        FindClose(hFind);
    }
#else
    DIR* d = opendir(dir);
    if (!d)
        return;

    int scanned = 0;
    for (;;)
    {
        if (ud->zstd_ddict)
            break;
        if (scanned++ > 128)
            break;

        struct dirent* ent = readdir(d);
        if (!ent)
            break;
        if (!ent->d_name || !ent->d_name[0])
            continue;

        const char* name = ent->d_name;
        size_t n = SDL_strlen(name);
        int okExt = 0;
        if (n >= 3 && SDL_strcasecmp(name + (n - 3), ".so") == 0)
            okExt = 1;
        if (n >= 6 && SDL_strcasecmp(name + (n - 6), ".dylib") == 0)
            okExt = 1;
        if (n >= 4 && SDL_strcasecmp(name + (n - 4), ".wpk") == 0)
            okExt = 1;
#if defined(__APPLE__)
        /* iOS: .framework bundles are directories containing a bare Mach-O.
         * Scan entries ending with ".framework" and try the inner binary. */
        if (n >= 10 && SDL_strcasecmp(name + (n - 10), ".framework") == 0)
        {
            /* e.g. dir=".../Frameworks", name="libggelua.framework"
             * → try ".../Frameworks/libggelua.framework/libggelua" */
            char fwBinary[128];
            size_t baseLen = n - 10; /* strip ".framework" */
            if (baseLen > 0 && baseLen < sizeof(fwBinary))
            {
                SDL_memcpy(fwBinary, name, baseLen);
                fwBinary[baseLen] = 0;
                SDL_snprintf(path, sizeof(path), "%s/%s/%s", dir, name, fwBinary);
                WPK_TryLoadZstdDictFromFile(ud, wantDictId, path);
            }
            continue;
        }
#endif
        if (!okExt)
            continue;

        SDL_snprintf(path, sizeof(path), "%s/%s", dir, name);
        WPK_TryLoadZstdDictFromFile(ud, wantDictId, path);
    }

    closedir(d);
#endif
}

static void WPK_TryLoadZstdDictNearSelf(WPK_UserData* ud, Uint32 wantDictId)
{
    if (!ud || ud->zstd_ddict)
        return;

    char selfDir[512];
    if (!WPK_GetSelfDir(selfDir))
    {
#if defined(__ANDROID__)
        /* Android: dladdr may fail if .so is memory-mapped from APK
         * (extractNativeLibs=false). Fall back to the app's native lib dir.
         * SDL_AndroidGetInternalStoragePath() → "/data/data/pkg/files",
         * native libs are at "/data/data/pkg/lib/" or nativeLibraryDir. */
        const char* intPath = SDL_AndroidGetInternalStoragePath();
        if (intPath && intPath[0])
        {
            char nativeLibDir[512];
            /* Go from .../files → .../lib/<abi> */
            char appDir[512];
            if (WPK_PathDirname(appDir, intPath))
            {
                SDL_snprintf(nativeLibDir, sizeof(nativeLibDir), "%s/lib", appDir);
                WPK_TryLoadZstdDictFromDir(ud, wantDictId, nativeLibDir);
            }
        }
#endif
        if (ud->zstd_ddict)
            return;
        goto try_base_dir;
    }

    WPK_TryLoadZstdDictFromDir(ud, wantDictId, selfDir);
    if (ud->zstd_ddict)
        return;

#if defined(__APPLE__)
    /* iOS: selfDir is something like ".../GGELUA.app/Frameworks/libmygxy.framework".
     * The other frameworks (libggelua.framework) are siblings under Frameworks/.
     * Try loading dictionary directly from the framework binaries. */
    {
        char fwParent[512];
        if (WPK_PathDirname(fwParent, selfDir))
        {
            /* fwParent = ".../Frameworks" — try known framework paths */
            char fwPath[512];
            SDL_snprintf(fwPath, sizeof(fwPath), "%s" WPK_SEP_STR WPK_DICT_FW_A, fwParent);
            WPK_TryLoadZstdDictFromFile(ud, wantDictId, fwPath);
            if (ud->zstd_ddict)
                return;
            SDL_snprintf(fwPath, sizeof(fwPath), "%s" WPK_SEP_STR WPK_DICT_FW_B, fwParent);
            WPK_TryLoadZstdDictFromFile(ud, wantDictId, fwPath);
            if (ud->zstd_ddict)
                return;
            /* Also scan the Frameworks/ directory for any .framework bundles */
            WPK_TryLoadZstdDictFromDir(ud, wantDictId, fwParent);
            if (ud->zstd_ddict)
                return;
        }
    }
#endif

    char parent[512];
    if (!WPK_PathDirname(parent, selfDir))
        goto try_base_dir;

    WPK_TryLoadZstdDictFromDir(ud, wantDictId, parent);
    if (ud->zstd_ddict)
        return;

    char libDir[512];
    SDL_snprintf(libDir, sizeof(libDir), "%s" WPK_SEP_STR "lib", parent);
    WPK_TryLoadZstdDictFromDir(ud, wantDictId, libDir);
    if (ud->zstd_ddict)
        return;

try_base_dir:
    if (ud->base_dir[0])
    {
        WPK_TryLoadZstdDictFromDir(ud, wantDictId, ud->base_dir);
        if (ud->zstd_ddict)
            return;

        char baseParent[512];
        if (WPK_PathDirname(baseParent, ud->base_dir))
            WPK_TryLoadZstdDictFromDir(ud, wantDictId, baseParent);
    }
}

static void WPK_PushBytesAndSize(lua_State* L, const void* data, size_t size)
{
    lua_pushlstring(L, (const char*)data, size);
    lua_pushinteger(L, (lua_Integer)size);
}

static int WPK_PushEmptyBytesAndSize(lua_State* L)
{
    WPK_PushBytesAndSize(L, "", 0);
    return 2;
}

static int WPK_DecodedCacheKeyMatches(const WPK_DecodedCacheEntry* e, Uint32 index, const WPK_FileInfo* fi)
{
    if (!e || !fi)
        return 0;
    return e->index == index
        && e->wpkid == fi->wpkid
        && e->offset == fi->offset
        && e->packed_size == fi->size
        && e->hash == fi->hash
        && SDL_memcmp(e->md5, fi->md5, 32) == 0;
}

static void WPK_DecodedCacheDetach(WPK_UserData* ud, WPK_DecodedCacheEntry* e)
{
    if (!ud || !e)
        return;
    if (e->prev)
        e->prev->next = e->next;
    else
        ud->decoded_cache_head = e->next;
    if (e->next)
        e->next->prev = e->prev;
    else
        ud->decoded_cache_tail = e->prev;
    e->prev = NULL;
    e->next = NULL;
}

static void WPK_DecodedCacheAttachHead(WPK_UserData* ud, WPK_DecodedCacheEntry* e)
{
    if (!ud || !e)
        return;
    e->prev = NULL;
    e->next = ud->decoded_cache_head;
    if (ud->decoded_cache_head)
        ud->decoded_cache_head->prev = e;
    else
        ud->decoded_cache_tail = e;
    ud->decoded_cache_head = e;
}

static void WPK_DecodedCacheTouch(WPK_UserData* ud, WPK_DecodedCacheEntry* e)
{
    if (!ud || !e)
        return;
    e->tick = ++ud->decoded_cache_tick;
    if (ud->decoded_cache_head == e)
        return;
    WPK_DecodedCacheDetach(ud, e);
    WPK_DecodedCacheAttachHead(ud, e);
}

static void WPK_DecodedCacheFreeEntry(WPK_UserData* ud, WPK_DecodedCacheEntry* e)
{
    if (!ud || !e)
        return;
    WPK_DecodedCacheDetach(ud, e);
    if (ud->decoded_cache_bytes >= e->size)
        ud->decoded_cache_bytes -= e->size;
    else
        ud->decoded_cache_bytes = 0;
    if (ud->decoded_cache_count > 0)
        ud->decoded_cache_count--;
    if (e->data)
        SDL_free(e->data);
    SDL_free(e);
}

static WPK_DecodedCacheEntry* WPK_DecodedCacheFind(WPK_UserData* ud, Uint32 index, const WPK_FileInfo* fi)
{
    WPK_DecodedCacheEntry* e;
    if (!ud || !fi)
        return NULL;
    e = ud->decoded_cache_head;
    while (e)
    {
        if (WPK_DecodedCacheKeyMatches(e, index, fi))
            return e;
        e = e->next;
    }
    return NULL;
}

static void WPK_DecodedCachePrune(WPK_UserData* ud)
{
    if (!ud)
        return;
    while (ud->decoded_cache_tail
        && (ud->decoded_cache_count > WPK_DECODED_CACHE_MAX_ENTRIES
            || ud->decoded_cache_bytes > WPK_DECODED_CACHE_MAX_BYTES))
    {
        WPK_DecodedCacheFreeEntry(ud, ud->decoded_cache_tail);
        g_wpk_stats.decoded_lru_evictions++;
    }
}

static void WPK_DecodedCacheClear(WPK_UserData* ud)
{
    if (!ud)
        return;
    while (ud->decoded_cache_head)
        WPK_DecodedCacheFreeEntry(ud, ud->decoded_cache_head);
    ud->decoded_cache_tail = NULL;
    ud->decoded_cache_count = 0;
    ud->decoded_cache_bytes = 0;
}


static int WPK_DecodedCacheCopy(WPK_UserData* ud, Uint32 index, const WPK_FileInfo* fi, Uint8** outData, size_t* outSize)
{
    WPK_DecodedCacheEntry* e = WPK_DecodedCacheFind(ud, index, fi);
    if (!outData || !outSize)
        return 0;
    *outData = NULL;
    *outSize = 0;
    if (!e)
    {
        g_wpk_stats.decoded_lru_misses++;
        return 0;
    }

    WPK_DecodedCacheTouch(ud, e);
    g_wpk_stats.decoded_lru_hits++;
    Uint8* copy = (Uint8*)SDL_malloc(e->size ? e->size : 1);
    if (!copy)
        return 0;
    if (e->size)
        SDL_memcpy(copy, e->data, e->size);
    else
        copy[0] = 0;
    *outData = copy;
    *outSize = e->size;
    return 1;
}

static void WPK_DecodedCacheStoreBuffer(WPK_UserData* ud, Uint32 index, const WPK_FileInfo* fi, const void* data, size_t size)
{
    WPK_DecodedCacheEntry* old;
    Uint8* copy;
    WPK_DecodedCacheEntry* e;
    if (!ud || !fi || (!data && size > 0))
    {
        g_wpk_stats.decoded_lru_skips++;
        return;
    }
    if (size > WPK_DECODED_CACHE_MAX_BYTES)
    {
        g_wpk_stats.decoded_lru_skips++;
        return;
    }

    old = WPK_DecodedCacheFind(ud, index, fi);
    if (old)
        WPK_DecodedCacheFreeEntry(ud, old);

    copy = (Uint8*)SDL_malloc(size ? size : 1);
    if (!copy)
    {
        g_wpk_stats.decoded_lru_skips++;
        return;
    }
    if (size)
        SDL_memcpy(copy, data, size);
    else
        copy[0] = 0;

    e = (WPK_DecodedCacheEntry*)SDL_malloc(sizeof(WPK_DecodedCacheEntry));
    if (!e)
    {
        SDL_free(copy);
        g_wpk_stats.decoded_lru_skips++;
        return;
    }
    SDL_memset(e, 0, sizeof(WPK_DecodedCacheEntry));
    e->index = index;
    e->wpkid = fi->wpkid;
    e->offset = fi->offset;
    e->packed_size = fi->size;
    e->hash = fi->hash;
    SDL_memcpy(e->md5, fi->md5, 32);
    e->md5[32] = 0;
    e->data = copy;
    e->size = size;
    e->tick = ++ud->decoded_cache_tick;

    WPK_DecodedCacheAttachHead(ud, e);
    ud->decoded_cache_count++;
    ud->decoded_cache_bytes += size;
    g_wpk_stats.decoded_lru_inserts++;
    WPK_DecodedCachePrune(ud);
}


static int WPK_NativeCopyBuffer(const Uint8* data, size_t size, Uint8** outData, size_t* outSize)
{
    if (!outData || !outSize || (!data && size > 0))
        return 0;
    Uint8* copy = (Uint8*)SDL_malloc(size ? size : 1);
    if (!copy)
        return 0;
    if (size)
        SDL_memcpy(copy, data, size);
    else
        copy[0] = 0;
    *outData = copy;
    *outSize = size;
    return 1;
}

static int WPK_NativeTryInflateWithWindowBits(const Uint8* src, size_t srcSize, int windowBits,
                                              Uint8** outData, size_t* outSize)
{
    const size_t capLimit = (size_t)(256u * 1024u * 1024u);

    if (!src || srcSize == 0 || !outData || !outSize)
        return 0;

    z_stream strm;
    SDL_memset(&strm, 0, sizeof(strm));
    strm.next_in = (Bytef*)src;
    strm.avail_in = (uInt)((srcSize > (size_t)0xFFFFFFFFu) ? (size_t)0xFFFFFFFFu : srcSize);

    int ret = inflateInit2_(&strm, windowBits, zlibVersion(), (int)sizeof(strm));
    if (ret != 0)
        return 0;

    size_t outCap = srcSize * 8;
    if (outCap < 65536)
        outCap = 65536;
    if (outCap > capLimit)
        outCap = capLimit;

    Uint8* outBuf = (Uint8*)SDL_malloc(outCap);
    if (!outBuf)
    {
        inflateEnd(&strm);
        return 0;
    }

    size_t produced = 0;
    int ok = 0;
    int safety = 0;

    while (1)
    {
        if (produced >= outCap)
        {
            if (outCap >= capLimit)
                break;
            size_t newCap = outCap * 2;
            if (newCap > capLimit)
                newCap = capLimit;
            Uint8* newBuf = (Uint8*)SDL_realloc(outBuf, newCap);
            if (!newBuf)
                break;
            outBuf = newBuf;
            outCap = newCap;
        }

        strm.next_out = (Bytef*)(outBuf + produced);
        size_t availOut = outCap - produced;
        if (availOut > (size_t)0xFFFFFFFFu)
            availOut = (size_t)0xFFFFFFFFu;
        strm.avail_out = (uInt)availOut;

        ret = inflate(&strm, Z_NO_FLUSH);
        produced = (size_t)((Uint8*)strm.next_out - outBuf);

        if (ret == Z_STREAM_END)
        {
            ok = 1;
            break;
        }
        if (ret != Z_OK && ret != Z_BUF_ERROR)
            break;
        if (strm.avail_out != 0 && ret == Z_BUF_ERROR)
            break;
        if (++safety > 2048)
            break;
    }

    inflateEnd(&strm);
    if (!ok)
    {
        SDL_free(outBuf);
        return 0;
    }
    *outData = outBuf;
    *outSize = produced;
    return 1;
}

/* THX background inflate (root fix for mid-game freeze):
 * submit to worker thread, poll dispatches lua callbacks on main thread.
 * Worker never touches lua_State: input deep-copied on submit,
 * output freed after dispatch. Mutex-guarded done list.
 * Max 2 concurrent; overflow nil -> lua falls back to sync path. */
typedef struct THX_ASYNC_JOB {
    int cb_ref;
    Uint8* src;
    size_t src_size;
    Uint8* out;
    size_t out_size;
    int ok;
    const char* err;
    struct THX_ASYNC_JOB* next_done;
} THX_ASYNC_JOB;

static SDL_Mutex* g_thxa_lock = NULL;
static THX_ASYNC_JOB* g_thxa_done = NULL;
static int g_thxa_active = 0;
#define THXA_MAX_ACTIVE 2

static int SDLCALL THXA_Worker(void* arg)
{
    THX_ASYNC_JOB* job = (THX_ASYNC_JOB*)arg;
    static const int windows[] = { 15 + 32, -15 };
    int i;
    for (i = 0; i < 2; ++i)
    {
        if (WPK_NativeTryInflateWithWindowBits(job->src, job->src_size,
                                               windows[i], &job->out, &job->out_size))
        {
            job->ok = 1;
            break;
        }
    }
    if (!job->ok)
        job->err = "inflate_failed";
    SDL_free(job->src);
    job->src = NULL;

    if (SDL_LockMutex(g_thxa_lock) == 0)
    {
        job->next_done = g_thxa_done;
        g_thxa_done = job;
        --g_thxa_active;
        SDL_UnlockMutex(g_thxa_lock);
    }
    return 0;
}

static int LUA_ThxAsyncUncompress(lua_State* L)
{
    size_t src_size = 0;
    const char* src = luaL_checklstring(L, 1, &src_size);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (!g_thxa_lock)
        g_thxa_lock = SDL_CreateMutex();
    if (!g_thxa_lock)
    {
        lua_pushnil(L);
        return 1;
    }

    int active = 0;
    if (SDL_LockMutex(g_thxa_lock) == 0)
    {
        active = g_thxa_active;
        SDL_UnlockMutex(g_thxa_lock);
    }
    if (active >= THXA_MAX_ACTIVE)
    {
        lua_pushnil(L);                      /* full: lua falls back to sync */
        return 1;
    }

    THX_ASYNC_JOB* job = (THX_ASYNC_JOB*)SDL_calloc(1, sizeof(THX_ASYNC_JOB));
    if (!job)
    {
        lua_pushnil(L);
        return 1;
    }
    job->src = (Uint8*)SDL_malloc(src_size ? src_size : 1);
    if (!job->src)
    {
        SDL_free(job);
        lua_pushnil(L);
        return 1;
    }
    if (src_size)
        SDL_memcpy(job->src, src, src_size);
    job->src_size = src_size;

    lua_pushvalue(L, 2);
    job->cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    if (SDL_LockMutex(g_thxa_lock) == 0)
    {
        ++g_thxa_active;
        SDL_UnlockMutex(g_thxa_lock);
    }
    SDL_Thread* th = SDL_CreateThread(THXA_Worker, "mygxy-thx", job);
    if (!th)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, job->cb_ref);
        SDL_free(job->src);
        SDL_free(job);
        if (SDL_LockMutex(g_thxa_lock) == 0)
        {
            --g_thxa_active;
            SDL_UnlockMutex(g_thxa_lock);
        }
        lua_pushnil(L);
        return 1;
    }
    SDL_DetachThread(th);
    lua_pushinteger(L, 1);
    return 1;
}

static int LUA_ThxAsyncPoll(lua_State* L)
{
    if (!g_thxa_lock)
    {
        lua_pushinteger(L, 0);
        return 1;
    }
    THX_ASYNC_JOB* list = NULL;
    if (SDL_LockMutex(g_thxa_lock) == 0)
    {
        list = g_thxa_done;
        g_thxa_done = NULL;
        SDL_UnlockMutex(g_thxa_lock);
    }
    int dispatched = 0;
    while (list)
    {
        THX_ASYNC_JOB* job = list;
        list = job->next_done;
        lua_rawgeti(L, LUA_REGISTRYINDEX, job->cb_ref);
        if (lua_isfunction(L, -1))
        {
            if (job->ok)
            {
                lua_pushlstring(L, (const char*)job->out, job->out_size);
                lua_pushnil(L);
            }
            else
            {
                lua_pushnil(L);
                lua_pushstring(L, job->err ? job->err : "failed");
            }
            if (lua_pcall(L, 2, 0, 0) != LUA_OK)
            {
                SDL_Log("ThxAsync callback error: %s", lua_tostring(L, -1));
                lua_pop(L, 1);
            }
            ++dispatched;
        }
        else
        {
            lua_pop(L, 1);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, job->cb_ref);
        if (job->out)
            SDL_free(job->out);
        SDL_free(job);
    }
    lua_pushinteger(L, dispatched);
    return 1;
}

MYGXY_API int luaopen_mygxy_thx_async(lua_State* L)
{
    const luaL_Reg funcs[] = {
        {"异步解压", LUA_ThxAsyncUncompress},
        {"取解压结果", LUA_ThxAsyncPoll},
        {NULL, NULL},
    };
    if (!g_thxa_lock)
        g_thxa_lock = SDL_CreateMutex();
    lua_createtable(L, 0, 2);
    luaL_setfuncs(L, funcs, 0);
    return 1;
}

static int WPK_NativeTryZlibDecompress(const Uint8* src, size_t srcSize, Uint8** outData, size_t* outSize)
{
    if (!WPK_IsZlibHeader(src, srcSize) && !WPK_IsGzipMagic(src, srcSize))
        return 0;
    if (WPK_NativeTryInflateWithWindowBits(src, srcSize, 15 + 32, outData, outSize))
        return 1;
    return WPK_NativeTryInflateWithWindowBits(src, srcSize, -15, outData, outSize);
}

static int WPK_NativeTryLz4fDecompress(const Uint8* src, size_t srcSize, Uint8** outData, size_t* outSize)
{
    if (!WPK_IsLz4FrameMagic(src, srcSize))
        return 0;

    const size_t capLimit = (size_t)(256u * 1024u * 1024u);
    LZ4F_decompressionContext_t dctx = NULL;
    size_t r = LZ4F_createDecompressionContext(&dctx, LZ4F_getVersion());
    if ((LZ4F_isError(r) || !dctx))
    {
        dctx = NULL;
        r = LZ4F_createDecompressionContext(&dctx, 100);
        if (LZ4F_isError(r) || !dctx)
            return 0;
    }

    size_t outCap = srcSize * 8;
    if (outCap < 65536)
        outCap = 65536;
    if (outCap > capLimit)
        outCap = capLimit;

    Uint8* outBuf = (Uint8*)SDL_malloc(outCap);
    if (!outBuf)
    {
        LZ4F_freeDecompressionContext(dctx);
        return 0;
    }

    const Uint8* inPtr = src;
    size_t inRemaining = srcSize;
    size_t produced = 0;
    int ok = 0;

    while (1)
    {
        if (produced >= outCap)
        {
            if (outCap >= capLimit)
                break;
            size_t newCap = outCap * 2;
            if (newCap > capLimit)
                newCap = capLimit;
            Uint8* newBuf = (Uint8*)SDL_realloc(outBuf, newCap);
            if (!newBuf)
                break;
            outBuf = newBuf;
            outCap = newCap;
        }

        size_t dstSize = outCap - produced;
        if (dstSize > (size_t)0xFFFFFFFFu)
            dstSize = (size_t)0xFFFFFFFFu;

        size_t srcChunk = inRemaining;
        size_t ret = LZ4F_decompress(dctx, outBuf + produced, &dstSize, inPtr, &srcChunk, NULL);
        if (LZ4F_isError(ret))
            break;
        if (srcChunk == 0 && dstSize == 0)
            break;
        inPtr += srcChunk;
        inRemaining -= srcChunk;
        produced += dstSize;
        if (ret == 0)
        {
            ok = 1;
            break;
        }
        if (inRemaining == 0)
            break;
    }

    LZ4F_freeDecompressionContext(dctx);
    if (!ok)
    {
        SDL_free(outBuf);
        return 0;
    }
    *outData = outBuf;
    *outSize = produced;
    return 1;
}

static int WPK_NativeTryZstdDecompress(WPK_UserData* ud, const Uint8* src, size_t srcSize,
                                       Uint8** outData, size_t* outSize)
{
    if (!WPK_IsZstdFrameMagic(src, srcSize))
        return 0;

    Uint32 wantDictId = 0;
    if (ud)
    {
        wantDictId = (Uint32)ZSTD_getDictID_fromFrame(src, srcSize);
        int hasDict = wantDictId ? 1 : WPK_ZstdFrameGetDictID(src, srcSize, &wantDictId);
        if (hasDict)
        {
            if (ud->zstd_ddict && ud->zstd_dict_id != wantDictId)
            {
                ZSTD_freeDDict(ud->zstd_ddict);
                ud->zstd_ddict = NULL;
                ud->zstd_dict_id = 0;
                if (ud->zstd_dict_buf)
                {
                    SDL_free(ud->zstd_dict_buf);
                    ud->zstd_dict_buf = NULL;
                    ud->zstd_dict_size = 0;
                }
            }
            WPK_TryLoadZstdDictNearSelf(ud, wantDictId);
        }
    }

    const size_t capLimit = (size_t)(256u * 1024u * 1024u);
    size_t outCap = WPK_ZstdOutputBound(src, srcSize);
    if (outCap == 0 || outCap > capLimit)
        outCap = srcSize * 8;
    if (outCap < 65536)
        outCap = 65536;
    if (outCap > capLimit)
        outCap = capLimit;

    Uint8* tmp = (Uint8*)SDL_malloc(outCap);
    if (!tmp)
        return 0;

    if (ud && !ud->zstd_dctx)
        ud->zstd_dctx = ZSTD_createDCtx();
    if (ud && ud->zstd_dctx)
        ZSTD_DCtx_setMaxWindowSize(ud->zstd_dctx, capLimit);

    for (int attempt = 0; attempt < 6; attempt++)
    {
        size_t produced = (size_t)-1;
        if (ud && ud->zstd_dctx)
        {
            if (ud->zstd_ddict && (!wantDictId || ud->zstd_dict_id == wantDictId))
                produced = ZSTD_decompress_usingDDict(ud->zstd_dctx, tmp, outCap, src, srcSize, ud->zstd_ddict);
            else
                produced = ZSTD_decompressDCtx(ud->zstd_dctx, tmp, outCap, src, srcSize);
        }
        else
        {
            produced = ZSTD_decompress(tmp, outCap, src, srcSize);
        }

        if (!ZSTD_isError(produced) && produced <= outCap)
        {
            *outData = tmp;
            *outSize = produced;
            return 1;
        }

        if (outCap >= capLimit)
            break;
        size_t newCap = outCap * 2;
        if (newCap > capLimit)
            newCap = capLimit;
        Uint8* newBuf = (Uint8*)SDL_realloc(tmp, newCap);
        if (!newBuf)
            break;
        tmp = newBuf;
        outCap = newCap;
    }

    SDL_free(tmp);
    return 0;
}

static int WPK_NativeTryNeoxDecompress(WPK_UserData* ud, const Uint8* src, size_t srcSize,
                                       Uint8** outData, size_t* outSize)
{
    if (!src || srcSize < 4)
        return 0;
    Uint32 magic = WPK_ReadU32LE(src);
    if (!WPK_IsNeoxMagicU32(magic))
        return 0;

    if (magic == 0x4E4F4E45u)
    {
        if (srcSize <= 4)
            return 0;
        if (srcSize >= 8)
        {
            Uint32 maybeSize = WPK_ReadU32LE(src + 4);
            if (maybeSize == (Uint32)(srcSize - 8))
                return WPK_NativeCopyBuffer(src + 8, srcSize - 8, outData, outSize);
        }
        return WPK_NativeCopyBuffer(src + 4, srcSize - 4, outData, outSize);
    }

    if (magic == 0x5A535444u)
    {
        if (srcSize <= 4)
            return 0;
        if (WPK_NativeTryZstdDecompress(ud, src + 4, srcSize - 4, outData, outSize))
            return 1;
        if (srcSize > 8)
            return WPK_NativeTryZstdDecompress(ud, src + 8, srcSize - 8, outData, outSize);
        return 0;
    }

    if (magic == 0x4C5A3446u)
    {
        if (srcSize <= 4)
            return 0;
        if (WPK_NativeTryLz4fDecompress(src + 4, srcSize - 4, outData, outSize))
            return 1;
        if (srcSize > 8)
            return WPK_NativeTryLz4fDecompress(src + 8, srcSize - 8, outData, outSize);
        return 0;
    }

    if (magic == 0x5A4C4942u || magic == 0x5A4C4941u)
    {
        if (srcSize <= 4)
            return 0;
        if (WPK_NativeTryZlibDecompress(src + 4, srcSize - 4, outData, outSize))
            return 1;
        if (srcSize > 8)
            return WPK_NativeTryZlibDecompress(src + 8, srcSize - 8, outData, outSize);
        return 0;
    }
    return 0;
}

static int WPK_NativeTryAcXcDecoded(WPK_UserData* ud, const Uint8* in, size_t inSize,
                                    Uint8** outData, size_t* outSize)
{
    if (!in || inSize < 10)
        return 0;

    Uint16 m = WPK_ReadU16LE(in);
    if (m != 0x4341u && m != 0x4358u && m != 0x434Eu)
        return 0;

    Uint32 dwDataSize = (Uint32)(inSize - 8);
    Uint32 factor = (Uint32)in[2];
    Uint32 dwBlockSize = 0;
    if (factor > 0)
        dwBlockSize = (Uint32)128u << (factor - 1u);
    if (dwBlockSize > dwDataSize)
        dwBlockSize = dwDataSize;

    Uint32 dwActualSize = dwBlockSize & 0xFFFFFFF0u;
    Uint8* dec = (Uint8*)SDL_malloc(dwDataSize ? dwDataSize : 1);
    if (!dec)
        return 0;
    if (dwDataSize)
        SDL_memcpy(dec, in + 8, dwDataSize);

    if (m == 0x4341u || m == 0x434Eu)
    {
        Uint8 key[16];
        if (m == 0x434Eu)
            WPK_GenerateNcKeyFromHeader(in, inSize, key);
        else
            WPK_GenerateAesKeyFromHeader(in, inSize, key);
        int usedWin = 0;
#if defined(_WIN32)
        if (dwActualSize != 0)
            usedWin = WPK_Aes128DecryptEcb_Windows(dec, dwActualSize, key);
#endif
        if (!usedWin)
        {
            WPK_Aes128Ctx ctx;
            WPK_Aes128Init(&ctx, key);
            for (Uint32 off = 0; off + 16 <= dwActualSize; off += 16)
                WPK_Aes128DecryptBlock(&ctx, dec + off);
        }
        WPK_XorDecrypt_AC(dec, dwDataSize, dwActualSize, dwBlockSize, key);
    }
    else
    {
        WPK_XorDecrypt_XC(dec, dwDataSize, dwBlockSize);
    }

    if (m == 0x434Eu)
        WPK_DeobfuscateNc(dec, dwDataSize, in);
    WPK_DeobfuscateXor5AReverse64(dec, dwDataSize);

    if (WPK_NativeTryNeoxDecompress(ud, dec, dwDataSize, outData, outSize))
    {
        SDL_free(dec);
        return 1;
    }

    *outData = dec;
    *outSize = dwDataSize;
    return 1;
}

static int WPK_NativeDecodeBuffer(WPK_UserData* ud, Uint32 index, const WPK_FileInfo* fi,
                                  const Uint8* raw, size_t inSize, Uint8** outData, size_t* outSize,
                                  char* err, size_t errSize)
{
    if (!raw && inSize > 0)
    {
        if (err && errSize) SDL_snprintf(err, errSize, "empty raw buffer");
        return 0;
    }

    if (WPK_NativeTryAcXcDecoded(ud, raw, inSize, outData, outSize))
    {
        g_wpk_stats.getdata_acxc_hits++;
        if (ud)
            WPK_DecodedCacheStoreBuffer(ud, index, fi, *outData, *outSize);
        return 1;
    }

    if (WPK_NativeTryNeoxDecompress(ud, raw, inSize, outData, outSize))
    {
        g_wpk_stats.getdata_neox_hits++;
        if (ud)
            WPK_DecodedCacheStoreBuffer(ud, index, fi, *outData, *outSize);
        return 1;
    }

    if (fi && fi->hash && inSize > 4)
    {
        Uint8 key[4];
        key[0] = (Uint8)((fi->hash >> 0) & 0xFF);
        key[1] = (Uint8)((fi->hash >> 8) & 0xFF);
        key[2] = (Uint8)((fi->hash >> 16) & 0xFF);
        key[3] = (Uint8)((fi->hash >> 24) & 0xFF);
        Uint8* dec = (Uint8*)SDL_malloc(inSize);
        if (dec)
        {
            SDL_memcpy(dec, raw, 4);
            WPK_XorRepeat4(dec + 4, raw + 4, inSize - 4, key);
            if (WPK_NativeTryAcXcDecoded(ud, dec, inSize, outData, outSize)
                || WPK_NativeTryNeoxDecompress(ud, dec, inSize, outData, outSize)
                || (WPK_LooksLikeCompressed(dec, inSize) && WPK_NativeCopyBuffer(dec, inSize, outData, outSize)))
            {
                g_wpk_stats.getdata_hashxor_hits++;
                if (ud)
                    WPK_DecodedCacheStoreBuffer(ud, index, fi, *outData, *outSize);
                SDL_free(dec);
                return 1;
            }
            SDL_free(dec);
        }
    }

    if (inSize >= 2)
    {
        Uint8* dec = (Uint8*)SDL_malloc(inSize);
        if (dec)
        {
            WPK_XorByte(dec, raw, inSize, 0x5A);
            if (WPK_NativeTryAcXcDecoded(ud, dec, inSize, outData, outSize)
                || WPK_NativeTryNeoxDecompress(ud, dec, inSize, outData, outSize)
                || (WPK_LooksLikeCompressed(dec, inSize) && WPK_NativeCopyBuffer(dec, inSize, outData, outSize)))
            {
                g_wpk_stats.getdata_xor5a_hits++;
                if (ud)
                    WPK_DecodedCacheStoreBuffer(ud, index, fi, *outData, *outSize);
                SDL_free(dec);
                return 1;
            }
            SDL_free(dec);
        }
    }

    g_wpk_stats.getdata_raw_hits++;
    if (!WPK_NativeCopyBuffer(raw, inSize, outData, outSize))
    {
        if (err && errSize) SDL_snprintf(err, errSize, "out of memory");
        return 0;
    }
    if (ud)
        WPK_DecodedCacheStoreBuffer(ud, index, fi, *outData, *outSize);
    return 1;
}






static int WPK_SetZstdDict(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);
    Uint32 result_id;

    size_t dictSize = 0;
    const char* dict = luaL_checklstring(L, 2, &dictSize);
    if (!dict || dictSize == 0)
        return 0;

    Uint8* dictCopy = (Uint8*)SDL_malloc(dictSize);
    if (!dictCopy)
        return 0;
    SDL_memcpy(dictCopy, dict, dictSize);

    ZSTD_DDict* ddict = ZSTD_createDDict(dictCopy, dictSize);
    if (!ddict)
    {
        SDL_free(dictCopy);
        return 0;
    }

    if (!WPK_LockNativeState(ud))
    {
        ZSTD_freeDDict(ddict);
        SDL_free(dictCopy);
        return 0;
    }
    if (ud->zstd_ddict)
        ZSTD_freeDDict(ud->zstd_ddict);
    ud->zstd_ddict = ddict;
    ud->zstd_dict_id = (Uint32)ZSTD_getDictID_fromDDict(ud->zstd_ddict);
    if (ud->zstd_dict_buf)
        SDL_free(ud->zstd_dict_buf);
    ud->zstd_dict_buf = dictCopy;
    ud->zstd_dict_size = dictSize;
    result_id = ud->zstd_dict_id;
    WPK_UnlockNativeState(ud);

    lua_pushinteger(L, (lua_Integer)result_id);
    return 1;
}

static int WPK_GetData(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);
    unsigned int id = (unsigned int)luaL_checkinteger(L, 2);
    unsigned char* data = NULL;
    size_t size = 0;
    char err[128] = {0};

    if (!WPK_NativeReadData(ud, id, &data, &size, err, sizeof(err)))
    {
        if (SDL_strcmp(err, "open loose file failed") == 0)
            return WPK_PushEmptyBytesAndSize(L);
        return 0;
    }

    WPK_PushBytesAndSize(L, data, size);
    SDL_free(data);
    return 2;
}

static int WPK_DecodeBuffer(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);
    const char* md5 = luaL_checkstring(L, 2);
    size_t inSize = 0;
    const char* raw = luaL_checklstring(L, 3, &inSize);
    Uint32 hash = 0;
    char md5Lower[33];
    WPK_FileInfo fi;
    Uint8* outData = NULL;
    size_t outSize = 0;
    char err[128] = {0};
    int ok;

    if (!WPK_NormalizeMd5Hex32(md5Lower, md5) || (!raw && inSize > 0) || inSize > (size_t)0xFFFFFFFFu)
        return 0;
    if (!lua_isnoneornil(L, 4))
        hash = (Uint32)luaL_checkinteger(L, 4);

    SDL_memset(&fi, 0, sizeof(fi));
    SDL_memcpy(fi.md5, md5Lower, 33);
    fi.hash = hash;
    fi.wpkid = (Uint32)(Sint32)-1;
    fi.offset = 0;
    fi.size = (Uint32)inSize;

    if (!WPK_LockNativeState(ud))
    {
        g_wpk_stats.getdata_failures++;
        return 0;
    }
    if (WPK_DecodedCacheCopy(ud, 0xFFFFFFFFu, &fi, &outData, &outSize))
    {
        WPK_UnlockNativeState(ud);
        lua_pushlstring(L, (const char*)outData, outSize);
        SDL_free(outData);
        return 1;
    }
    ok = WPK_NativeDecodeBuffer(ud, 0xFFFFFFFFu, &fi, (const Uint8*)raw, inSize, &outData, &outSize, err, sizeof(err));
    WPK_UnlockNativeState(ud);
    if (!ok)
    {
        g_wpk_stats.getdata_failures++;
        return 0;
    }

    lua_pushlstring(L, (const char*)outData, outSize);
    SDL_free(outData);
    return 1;
}

int WPK_NativeReadData(WPK_UserData* ud, unsigned int id, unsigned char** outData, size_t* outSize,
                       char* err, size_t errSize)
{
    Uint32 i;
    WPK_FileInfo fi_copy;
    const WPK_FileInfo* fi;
    Uint32 wpkid;
    char base_dir[256];
    char base_name[128];
    Uint8* raw = NULL;
    size_t inSize = 0;
    int ok;
    size_t readCount;
    if (outData) *outData = NULL;
    if (outSize) *outSize = 0;
    if (!ud || !outData || !outSize)
    {
        if (err && errSize) SDL_snprintf(err, errSize, "invalid wpk handle");
        return 0;
    }
    if (!WPK_LockNativeState(ud))
    {
        if (err && errSize) SDL_snprintf(err, errSize, "native mutex missing");
        g_wpk_stats.getdata_failures++;
        return 0;
    }
    if (id == 0 || id > ud->number)
    {
        WPK_UnlockNativeState(ud);
        if (err && errSize) SDL_snprintf(err, errSize, "wpk id out of range");
        return 0;
    }

    i = (Uint32)id - 1;
    fi_copy = ud->list[i];
    SDL_strlcpy(base_dir, ud->base_dir, sizeof(base_dir));
    SDL_strlcpy(base_name, ud->base_name, sizeof(base_name));
    fi = &fi_copy;
    wpkid = fi->wpkid;
    if (WPK_DecodedCacheCopy(ud, i, fi, (Uint8**)outData, outSize))
    {
        WPK_UnlockNativeState(ud);
        return 1;
    }
    WPK_UnlockNativeState(ud);

    if (WPK_WpkIdAsS32(wpkid) < 0)
    {
        char path[512];
        SDL_RWops* fp;
        Sint64 sz;
        SDL_snprintf(path, sizeof(path), "%s" WPK_SEP_STR "%s" WPK_SEP_STR "%s", base_dir, base_name, fi->md5);
        fp = SDL_RWFromFile(path, "rb");
        if (!fp)
        {
            g_wpk_stats.getdata_failures++;
            if (err && errSize) SDL_snprintf(err, errSize, "open loose file failed");
            return 0;
        }
        if (SDL_RWseek(fp, 0, RW_SEEK_END) < 0 || (sz = SDL_RWtell(fp)) < 0
            || (Uint64)sz > (Uint64)((size_t)-1) || SDL_RWseek(fp, 0, RW_SEEK_SET) < 0)
        {
            SDL_RWclose(fp);
            g_wpk_stats.getdata_failures++;
            if (err && errSize) SDL_snprintf(err, errSize, "read loose file failed");
            return 0;
        }
        inSize = (size_t)sz;
        raw = (Uint8*)SDL_malloc(inSize ? inSize : 1);
        if (!raw)
        {
            SDL_RWclose(fp);
            g_wpk_stats.getdata_failures++;
            if (err && errSize) SDL_snprintf(err, errSize, "out of memory");
            return 0;
        }
        readCount = WPK_RWreadAll(fp, raw, inSize);
        SDL_RWclose(fp);
        if (readCount != inSize)
        {
            SDL_free(raw);
            g_wpk_stats.getdata_failures++;
            if (err && errSize) SDL_snprintf(err, errSize, "read loose file failed");
            return 0;
        }
    }
    else
    {
        char lower_base_name[128];
        char path[512];
        const char* openPath;
        SDL_RWops* fp;
#if defined(__ANDROID__)
        char localPath[512];
#endif
        if (fi->size == 0)
        {
            g_wpk_stats.getdata_raw_hits++;
            return WPK_NativeCopyBuffer((const Uint8*)"", 0, (Uint8**)outData, outSize);
        }

        SDL_strlcpy(lower_base_name, base_name, sizeof(lower_base_name));
        for (size_t n = 0; lower_base_name[n]; n++)
            lower_base_name[n] = (char)SDL_tolower((unsigned char)lower_base_name[n]);

        for (int attempt = 0; attempt < 2; attempt++)
        {
            SDL_snprintf(path, sizeof(path), "%s" WPK_SEP_STR "%s%u.wpk", base_dir, lower_base_name, (unsigned)wpkid);
            openPath = path;
#if defined(__ANDROID__)
            if (WPK_CopyToInternalStorage(path, localPath))
                openPath = localPath;
#endif
            fp = SDL_RWFromFile(openPath, "rb");
            if (!fp && wpkid == 0)
            {
                SDL_snprintf(path, sizeof(path), "%s" WPK_SEP_STR "%s.wpk", base_dir, lower_base_name);
                openPath = path;
#if defined(__ANDROID__)
                if (WPK_CopyToInternalStorage(path, localPath))
                    openPath = localPath;
#endif
                fp = SDL_RWFromFile(openPath, "rb");
            }
            if (!fp)
            {
                g_wpk_stats.getdata_failures++;
                if (err && errSize) SDL_snprintf(err, errSize, "open data pack failed");
                return 0;
            }

            inSize = (size_t)fi->size;
            if (SDL_RWseek(fp, (Sint64)fi->offset, RW_SEEK_SET) < 0)
            {
                SDL_RWclose(fp);
                continue;
            }

            raw = (Uint8*)SDL_malloc(inSize);
            if (!raw)
            {
                SDL_RWclose(fp);
                g_wpk_stats.getdata_failures++;
                if (err && errSize) SDL_snprintf(err, errSize, "out of memory");
                return 0;
            }
            readCount = WPK_RWreadAll(fp, raw, inSize);
            SDL_RWclose(fp);
            if (readCount == inSize)
                break;
            SDL_free(raw);
            raw = NULL;
        }
        if (!raw)
        {
            g_wpk_stats.getdata_failures++;
            if (err && errSize) SDL_snprintf(err, errSize, "read data pack failed");
            return 0;
        }
    }

    if (!WPK_LockNativeState(ud))
    {
        SDL_free(raw);
        if (err && errSize) SDL_snprintf(err, errSize, "native mutex missing");
        g_wpk_stats.getdata_failures++;
        return 0;
    }
    ok = WPK_NativeDecodeBuffer(ud, i, fi, raw, inSize, (Uint8**)outData, outSize, err, errSize);
    WPK_UnlockNativeState(ud);
    SDL_free(raw);
    if (!ok)
        g_wpk_stats.getdata_failures++;
    return ok;
}

static void WPK_PushInfoTable(lua_State* L, WPK_UserData* ud, const WPK_FileInfo* info, int idx)
{
    lua_createtable(L, 0, 9);
    lua_pushinteger(L, (lua_Integer)(idx + 1));
    lua_setfield(L, -2, "id");
    lua_pushlstring(L, info->md5, 32);
    lua_setfield(L, -2, "md5");
    lua_pushinteger(L, (lua_Integer)WPK_WpkIdAsS32(info->wpkid));
    lua_setfield(L, -2, "wpkid");
    lua_pushinteger(L, (lua_Integer)info->offset);
    lua_setfield(L, -2, "offset");
    lua_pushinteger(L, (lua_Integer)info->size);
    lua_setfield(L, -2, "size");
    lua_pushinteger(L, (lua_Integer)info->file_index);
    lua_setfield(L, -2, "fileindex");
    lua_pushinteger(L, (lua_Integer)info->hash);
    lua_setfield(L, -2, "hash");
    lua_pushvalue(L, 1);
    lua_setfield(L, -2, "wpk");
    lua_pushstring(L, ud->idx_path);
    lua_setfield(L, -2, "path");
}

static int WPK_GetInfoByMd5(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);
    const char* md5 = luaL_checkstring(L, 2);
    WPK_FileInfo info;
    int idx;

    char md5Lower[33];
    if (!WPK_NormalizeMd5Hex32(md5Lower, md5))
        return 0;

    WPK_LOCK_OR_RETURN(ud, 0);
    idx = WPK_FindByMd5(ud, md5Lower);
    if (idx < 0)
        WPK_UNLOCK_RETURN(ud, 0);
    info = ud->list[idx];
    WPK_UnlockNativeState(ud);

    WPK_PushInfoTable(L, ud, &info, idx);
    return 1;
}

static int WPK_GetInfoByHash(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);
    Uint32 hash = (Uint32)luaL_checkinteger(L, 2);
    WPK_FileInfo info;

    int idx = -1;
    WPK_LOCK_OR_RETURN(ud, 0);
    idx = WPK_FindByHash(ud, hash);
    if (idx < 0)
        WPK_UNLOCK_RETURN(ud, 0);
    info = ud->list[idx];
    WPK_UnlockNativeState(ud);

    WPK_PushInfoTable(L, ud, &info, idx);
    return 1;
}

static int WPK_GetInfoByHashBatch(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_createtable(L, 0, (int)ud->number);
    WPK_LOCK_OR_RETURN(ud, 1);

    lua_pushnil(L);
    while (lua_next(L, 2) != 0)
    {
        Uint32 hash = 0;
        int has_hash = 0;
        if (lua_isinteger(L, -1) || lua_isnumber(L, -1))
        {
            hash = (Uint32)lua_tointeger(L, -1);
            has_hash = 1;
        }
        else if (lua_isinteger(L, -2) || lua_isnumber(L, -2))
        {
            hash = (Uint32)lua_tointeger(L, -2);
            has_hash = 1;
        }

        if (has_hash)
        {
            int idx = WPK_FindByHash(ud, hash);
            if (idx >= 0)
            {
                WPK_FileInfo info = ud->list[idx];
                lua_pushinteger(L, (lua_Integer)hash);
                WPK_PushInfoTable(L, ud, &info, idx);
                lua_settable(L, 3);
            }
        }
        lua_pop(L, 1);
    }

    WPK_UnlockNativeState(ud);
    return 1;
}

static int WPK_GetList(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);

    WPK_LOCK_OR_RETURN(ud, 0);
    if (lua_istable(L, 2))
    {
        lua_pushvalue(L, 2);
    }
    else
    {
        if (ud->list_ref > 0)
        {
            lua_rawgeti(L, LUA_REGISTRYINDEX, ud->list_ref);
            if (lua_istable(L, -1))
                WPK_UNLOCK_RETURN(ud, 1);
            lua_pop(L, 1);
            luaL_unref(L, LUA_REGISTRYINDEX, ud->list_ref);
            ud->list_ref = LUA_NOREF;
        }
        lua_createtable(L, (int)ud->number, 0);
    }

    for (Uint32 i = 0; i < ud->number; i++)
    {
        lua_createtable(L, 0, 8);
        lua_pushinteger(L, (lua_Integer)(i + 1));
        lua_setfield(L, -2, "id");
        lua_pushlstring(L, ud->list[i].md5, 32);
        lua_setfield(L, -2, "md5");
        lua_pushinteger(L, (lua_Integer)WPK_WpkIdAsS32(ud->list[i].wpkid));
        lua_setfield(L, -2, "wpkid");
        lua_pushinteger(L, (lua_Integer)ud->list[i].offset);
        lua_setfield(L, -2, "offset");
        lua_pushinteger(L, (lua_Integer)ud->list[i].size);
        lua_setfield(L, -2, "size");
        lua_pushinteger(L, (lua_Integer)ud->list[i].file_index);
        lua_setfield(L, -2, "fileindex");
        lua_pushvalue(L, 1);
        lua_setfield(L, -2, "wpk");
        lua_pushstring(L, ud->idx_path);
        lua_setfield(L, -2, "path");
        lua_rawseti(L, -2, (lua_Integer)(i + 1));
    }

    if (!lua_istable(L, 2) && ud->list_ref == LUA_NOREF)
    {
        lua_pushvalue(L, -1);
        ud->list_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    WPK_UnlockNativeState(ud);
    return 1;
}

static int WPK_Upsert(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);
    const char* md5 = luaL_checkstring(L, 2);
    Sint32 swpkid = (Sint32)luaL_checkinteger(L, 3);
    Uint32 offset = (Uint32)luaL_checkinteger(L, 4);
    Uint32 size = (Uint32)luaL_checkinteger(L, 5);

    Uint32 hash = 0;
    int hasHash = 0;
    if (!lua_isnoneornil(L, 6))
    {
        hash = (Uint32)luaL_checkinteger(L, 6);
        hasHash = 1;
    }

    char md5Lower[33];
    if (!WPK_NormalizeMd5Hex32(md5Lower, md5))
        return 0;

    WPK_LOCK_OR_RETURN(ud, 0);
    int idx = WPK_FindByMd5(ud, md5Lower);
    int isNew = 0;
    if (idx < 0)
    {
        Uint32 newCount = ud->number + 1;
        WPK_FileInfo* p = (WPK_FileInfo*)SDL_realloc(ud->list, sizeof(WPK_FileInfo) * newCount);
        if (!p)
            WPK_UNLOCK_RETURN(ud, 0);
        ud->list = p;
        idx = (int)ud->number;
        ud->number = newCount;
        SDL_memset(&ud->list[idx], 0, sizeof(WPK_FileInfo));
        isNew = 1;

        Uint16 maxIndex = 0;
        for (Uint32 i = 0; i + 1 < ud->number; i++)
        {
            if (ud->list[i].file_index > maxIndex)
                maxIndex = ud->list[i].file_index;
        }
        ud->list[idx].file_index = (Uint16)(maxIndex + 1);
    }

    SDL_memcpy(ud->list[idx].md5, md5Lower, 33);
    ud->list[idx].wpkid = (Uint32)(Sint32)swpkid;
    ud->list[idx].offset = offset;
    ud->list[idx].size = size;
    if (hasHash)
        ud->list[idx].hash = hash;

    WPK_MarkLookupDirty(ud);
    WPK_DecodedCacheClear(ud);
    WPK_InvalidateListCache(L, ud);
    WPK_UnlockNativeState(ud);

    lua_pushinteger(L, (lua_Integer)(idx + 1));
    lua_pushboolean(L, isNew);
    return 2;
}

static int WPK_SetHash(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);
    const char* md5 = luaL_checkstring(L, 2);
    Uint32 hash = (Uint32)luaL_checkinteger(L, 3);

    char md5Lower[33];
    if (!WPK_NormalizeMd5Hex32(md5Lower, md5))
        return 0;

    WPK_LOCK_OR_RETURN(ud, 0);
    int idx = WPK_FindByMd5(ud, md5Lower);
    if (idx < 0)
        WPK_UNLOCK_RETURN(ud, 0);

    ud->list[idx].hash = hash;
    WPK_MarkLookupDirty(ud);
    WPK_DecodedCacheClear(ud);
    WPK_UnlockNativeState(ud);
    lua_pushinteger(L, (lua_Integer)(idx + 1));
    return 1;
}

static int WPK_QueueWriteImpl(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);
    const char* md5 = luaL_checkstring(L, 2);
    size_t size = 0;
    const char* data = luaL_checklstring(L, 3, &size);
    Uint32 hash = 0;
    int hasHash = 0;
    char md5Lower[33];
    WPK_WriteTask* old;
    WPK_WriteTask* task;

    if (!WPK_NormalizeMd5Hex32(md5Lower, md5) || !data || size == 0
        || size > (size_t)0xFFFFFFFFu || size > (size_t)WPK_WRITE_MAX_PACK_BYTES)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!lua_isnoneornil(L, 4))
    {
        hash = (Uint32)luaL_checkinteger(L, 4);
        hasHash = 1;
    }

    old = ud->write_head;
    while (old)
    {
        if (SDL_memcmp(old->md5, md5Lower, 33) == 0)
        {
            Uint8* copy = (Uint8*)SDL_malloc(size);
            if (!copy)
                return luaL_error(L, "wpk: out of memory");
            SDL_memcpy(copy, data, size);
            if (old->data)
                SDL_free(old->data);
            old->data = copy;
            old->size = size;
            old->hash = hash;
            old->has_hash = hasHash;
            lua_pushboolean(L, 1);
            lua_pushinteger(L, (lua_Integer)ud->write_queue_count);
            return 2;
        }
        old = old->next;
    }

    task = (WPK_WriteTask*)SDL_calloc(1, sizeof(WPK_WriteTask));
    if (!task)
        return luaL_error(L, "wpk: out of memory");
    task->data = (Uint8*)SDL_malloc(size);
    if (!task->data)
    {
        SDL_free(task);
        return luaL_error(L, "wpk: out of memory");
    }
    SDL_memcpy(task->md5, md5Lower, 33);
    SDL_memcpy(task->data, data, size);
    task->size = size;
    task->hash = hash;
    task->has_hash = hasHash;

    if (ud->write_tail)
        ud->write_tail->next = task;
    else
        ud->write_head = task;
    ud->write_tail = task;
    ud->write_queue_count++;
    ud->write_queued_total++;

    lua_pushboolean(L, 1);
    lua_pushinteger(L, (lua_Integer)ud->write_queue_count);
    return 2;
}

static int WPK_QueueWrite(lua_State* L)
{
    Uint64 start_us = WPK_NowUS();
    int ret = WPK_QueueWriteImpl(L);
    WPK_RecordTime(&g_wpk_perf.write_queue_us, WPK_NowUS() - start_us);
    return ret;
}

static int WPK_SetWriteBaseDir(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);
    const char* path = luaL_optstring(L, 2, NULL);
    char normalized[256];
    normalized[0] = 0;
    if (!path || !path[0])
    {
        normalized[0] = 0;
    }
    else
    {
        SDL_strlcpy(normalized, path, sizeof(normalized));
        size_t n = SDL_strlen(normalized);
        while (n > 0 && (normalized[n - 1] == '/' || normalized[n - 1] == '\\'))
        {
            normalized[n - 1] = 0;
            n--;
        }
    }
    if (SDL_strcmp(ud->write_base_dir, normalized) != 0)
    {
        WPK_CloseCachedWriteFiles(ud);
        SDL_strlcpy(ud->write_base_dir, normalized, sizeof(ud->write_base_dir));
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int WPK_FlushWriteQueue(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);
    int limit = (int)luaL_optinteger(L, 2, 1);
    int force = lua_toboolean(L, 3);
    Uint64 byte_limit = 0;
    Uint64 processed_bytes = 0;
    int processed = 0;
    int failed = 0;
    if (force || limit <= 0)
        limit = 0x7fffffff;
    if (!force && !lua_isnoneornil(L, 4))
    {
        lua_Integer n = luaL_checkinteger(L, 4);
        if (n > 0)
            byte_limit = (Uint64)n;
    }

    while (ud->write_head && processed < limit)
    {
        WPK_WriteTask* task = ud->write_head;
        Uint64 task_size = (Uint64)task->size;
        Uint32 wpkid;
        Uint32 offset = 0;
        int ok = 0;
        if (!force && byte_limit > 0 && processed > 0 && processed_bytes + task_size > byte_limit)
            break;

        ud->write_head = task->next;
        if (!ud->write_head)
            ud->write_tail = NULL;
        if (ud->write_queue_count > 0)
            ud->write_queue_count--;

        wpkid = WPK_SelectWritePack(ud, task->size);
        if (wpkid < 255u && WPK_AppendDataPack(ud, wpkid, task->data, task->size, &offset))
        {
            int top = lua_gettop(L);
            lua_pushcfunction(L, WPK_Upsert);
            lua_pushvalue(L, 1);
            lua_pushstring(L, task->md5);
            lua_pushinteger(L, (lua_Integer)wpkid);
            lua_pushinteger(L, (lua_Integer)offset);
            lua_pushinteger(L, (lua_Integer)task->size);
            if (task->has_hash)
                lua_pushinteger(L, (lua_Integer)task->hash);
            else
                lua_pushnil(L);
            if (lua_pcall(L, 6, 2, 0) == LUA_OK)
            {
                ok = lua_toboolean(L, -2) || lua_toboolean(L, -1);
                lua_settop(L, top);
            }
            else
            {
                fprintf(stderr, "[wpk] QueueWrite Upsert failed: %s\n", lua_tostring(L, -1));
                lua_settop(L, top);
            }
        }

        if (ok)
        {
            ud->write_flushed_total++;
            WPK_FreeWriteTask(task);
        }
        else
        {
            failed++;
            ud->write_failed_total++;
            WPK_FreeWriteTask(task);
        }
        processed++;
        processed_bytes += task_size;
        if (!force && byte_limit > 0 && processed_bytes >= byte_limit)
            break;
    }

    lua_createtable(L, 0, 4);
    lua_pushinteger(L, (lua_Integer)processed);
    lua_setfield(L, -2, "processed");
    lua_pushinteger(L, (lua_Integer)failed);
    lua_setfield(L, -2, "failed");
    lua_pushinteger(L, (lua_Integer)ud->write_queue_count);
    lua_setfield(L, -2, "queued");
    lua_pushboolean(L, ud->write_queue_count > 0);
    lua_setfield(L, -2, "busy");
    return 1;
}

static int WPK_SaveIdx(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);
    const char* outPath = ud->idx_path;
    if (lua_type(L, 2) == LUA_TSTRING)
        outPath = lua_tostring(L, 2);

    int encrypt = 1;
    if (!lua_isnoneornil(L, 3))
        encrypt = lua_toboolean(L, 3) ? 1 : 0;
    else
        encrypt = ud->idx_is_skpe ? 1 : 1;

    WPK_CloseCachedWriteFiles(ud);

    WPK_LOCK_OR_RETURN(ud, 0);
    if (!ud->idx_is_skpw)
        WPK_UNLOCK_RETURN(ud, 0);
    if (!outPath || !outPath[0])
        WPK_UNLOCK_RETURN(ud, 0);
    if (!ud->list || ud->number == 0)
        WPK_UNLOCK_RETURN(ud, 0);

    const Uint32 recordCount = ud->number;
    const size_t headerStart = 0x20;
    const size_t recordSize = 28;
    size_t base = headerStart + (size_t)recordCount * recordSize;
    size_t plainSize = base + 4;
    int hasCrc = (ud->skpw_version == 1);
    if (hasCrc)
        plainSize = base + 4 + (size_t)recordCount * 4;

    Uint8* plain = (Uint8*)SDL_malloc(plainSize);
    if (!plain)
        WPK_UNLOCK_RETURN(ud, 0);
    SDL_memset(plain, 0, plainSize);

    plain[0] = 'S';
    plain[1] = 'K';
    plain[2] = 'P';
    plain[3] = 'W';

    {
        Uint32 unknown = ud->skpw_unknown;
        Uint32 version = ud->skpw_version;
        Uint32 count = recordCount;
        plain[4] = (Uint8)(unknown & 0xFF);
        plain[5] = (Uint8)((unknown >> 8) & 0xFF);
        plain[6] = (Uint8)((unknown >> 16) & 0xFF);
        plain[7] = (Uint8)((unknown >> 24) & 0xFF);
        plain[8] = (Uint8)(version & 0xFF);
        plain[9] = (Uint8)((version >> 8) & 0xFF);
        plain[10] = (Uint8)((version >> 16) & 0xFF);
        plain[11] = (Uint8)((version >> 24) & 0xFF);
        plain[12] = (Uint8)(count & 0xFF);
        plain[13] = (Uint8)((count >> 8) & 0xFF);
        plain[14] = (Uint8)((count >> 16) & 0xFF);
        plain[15] = (Uint8)((count >> 24) & 0xFF);
    }

    {
        static const Uint8 padding[16] = {
            0x06, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C,
            0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C,
        };
        SDL_memcpy(plain + 16, padding, 16);
    }

    for (Uint32 i = 0; i < recordCount; i++)
    {
        Uint8 bin16[16];
        if (!WPK_HexToBin16(ud->list[i].md5, bin16))
        {
            SDL_free(plain);
            WPK_UNLOCK_RETURN(ud, 0);
        }

        Uint8* rec = plain + headerStart + (size_t)i * recordSize;
        SDL_memcpy(rec, bin16, 16);

        Uint32 fsize = ud->list[i].size;
        Uint32 off = ud->list[i].offset;
        rec[16] = (Uint8)(fsize & 0xFF);
        rec[17] = (Uint8)((fsize >> 8) & 0xFF);
        rec[18] = (Uint8)((fsize >> 16) & 0xFF);
        rec[19] = (Uint8)((fsize >> 24) & 0xFF);
        rec[20] = (Uint8)(off & 0xFF);
        rec[21] = (Uint8)((off >> 8) & 0xFF);
        rec[22] = (Uint8)((off >> 16) & 0xFF);
        rec[23] = (Uint8)((off >> 24) & 0xFF);

        Sint32 swpkid = (Sint32)ud->list[i].wpkid;
        Uint8 archive = (swpkid < 0) ? 255u : (Uint8)((Uint32)swpkid & 0xFFu);
        rec[24] = archive;
        rec[25] = 0;
        Uint16 fileIndex = ud->list[i].file_index;
        rec[26] = (Uint8)(fileIndex & 0xFF);
        rec[27] = (Uint8)((fileIndex >> 8) & 0xFF);
    }

    if (hasCrc)
    {
        Uint32 unknown = ud->skpw_unknown;
        size_t checkOff = base;
        plain[checkOff + 0] = (Uint8)(unknown & 0xFF);
        plain[checkOff + 1] = (Uint8)((unknown >> 8) & 0xFF);
        plain[checkOff + 2] = (Uint8)((unknown >> 16) & 0xFF);
        plain[checkOff + 3] = (Uint8)((unknown >> 24) & 0xFF);
        size_t crcOff = checkOff + 4;
        for (Uint32 i = 0; i < recordCount; i++)
        {
            Uint32 h = ud->list[i].hash;
            plain[crcOff + (size_t)i * 4 + 0] = (Uint8)(h & 0xFF);
            plain[crcOff + (size_t)i * 4 + 1] = (Uint8)((h >> 8) & 0xFF);
            plain[crcOff + (size_t)i * 4 + 2] = (Uint8)((h >> 16) & 0xFF);
            plain[crcOff + (size_t)i * 4 + 3] = (Uint8)((h >> 24) & 0xFF);
        }
    }
    WPK_UnlockNativeState(ud);

    int ok = 0;
    if (encrypt)
    {
        Uint8* blob = NULL;
        size_t blobSize = 0;
        if (WPK_BuildSkpeBlob(plain, plainSize, &blob, &blobSize))
        {
            ok = WPK_WriteFileAll(outPath, blob, blobSize);
            SDL_free(blob);
        }
    }
    else
    {
        ok = WPK_WriteFileAll(outPath, plain, plainSize);
    }

    SDL_free(plain);
    lua_pushboolean(L, ok);
    return 1;
}

static int WPK_GetStats(lua_State* L)
{
    WPK_UserData* ud = NULL;
    if (lua_gettop(L) >= 1)
        ud = (WPK_UserData*)luaL_testudata(L, 1, WPK_NAME);

    lua_createtable(L, 0, 16);
    lua_pushinteger(L, (lua_Integer)g_wpk_stats.idx_mode_file);
    lua_setfield(L, -2, "idx_mode_file");
    lua_pushinteger(L, (lua_Integer)g_wpk_stats.idx_mode_buffer);
    lua_setfield(L, -2, "idx_mode_buffer");
    lua_pushinteger(L, (lua_Integer)g_wpk_stats.idx_open_failures);
    lua_setfield(L, -2, "idx_open_failures");
    lua_pushinteger(L, (lua_Integer)g_wpk_stats.getdata_raw_hits);
    lua_setfield(L, -2, "getdata_raw_hits");
    lua_pushinteger(L, (lua_Integer)g_wpk_stats.getdata_acxc_hits);
    lua_setfield(L, -2, "getdata_acxc_hits");
    lua_pushinteger(L, (lua_Integer)g_wpk_stats.getdata_neox_hits);
    lua_setfield(L, -2, "getdata_neox_hits");
    lua_pushinteger(L, (lua_Integer)g_wpk_stats.getdata_hashxor_hits);
    lua_setfield(L, -2, "getdata_hashxor_hits");
    lua_pushinteger(L, (lua_Integer)g_wpk_stats.getdata_xor5a_hits);
    lua_setfield(L, -2, "getdata_xor5a_hits");
    lua_pushinteger(L, (lua_Integer)g_wpk_stats.getdata_failures);
    lua_setfield(L, -2, "getdata_failures");
    lua_pushinteger(L, (lua_Integer)g_wpk_stats.decoded_lru_hits);
    lua_setfield(L, -2, "decoded_lru_hits");
    lua_pushinteger(L, (lua_Integer)g_wpk_stats.decoded_lru_misses);
    lua_setfield(L, -2, "decoded_lru_misses");
    lua_pushinteger(L, (lua_Integer)g_wpk_stats.decoded_lru_inserts);
    lua_setfield(L, -2, "decoded_lru_inserts");
    lua_pushinteger(L, (lua_Integer)g_wpk_stats.decoded_lru_evictions);
    lua_setfield(L, -2, "decoded_lru_evictions");
    lua_pushinteger(L, (lua_Integer)g_wpk_stats.decoded_lru_skips);
    lua_setfield(L, -2, "decoded_lru_skips");
    if (ud)
    {
        lua_pushinteger(L, (lua_Integer)ud->decoded_cache_count);
        lua_setfield(L, -2, "decoded_lru_count");
        lua_pushinteger(L, (lua_Integer)ud->decoded_cache_bytes);
        lua_setfield(L, -2, "decoded_lru_bytes");
        lua_pushinteger(L, (lua_Integer)ud->write_queue_count);
        lua_setfield(L, -2, "write_queue");
        lua_pushinteger(L, (lua_Integer)ud->write_queued_total);
        lua_setfield(L, -2, "write_queued_total");
        lua_pushinteger(L, (lua_Integer)ud->write_flushed_total);
        lua_setfield(L, -2, "write_flushed_total");
        lua_pushinteger(L, (lua_Integer)ud->write_failed_total);
        lua_setfield(L, -2, "write_failed_total");
        lua_pushstring(L, ud->write_base_dir);
        lua_setfield(L, -2, "write_base_dir");
        Uint32 write_open_files = 0;
        if (ud->write_files)
        {
            for (Uint32 i = 0; i < ud->write_files_count; i++)
            {
                if (ud->write_files[i])
                    write_open_files++;
            }
        }
        lua_pushinteger(L, (lua_Integer)write_open_files);
        lua_setfield(L, -2, "write_open_files");
    }
    return 1;
}

static void WPK_PushTimeStatsSnapshot(lua_State* L, const WPK_TimeStats* s)
{
    Uint32 samples[WPK_PERF_SAMPLE_CAP];
    int sample_count = 0;
    Uint32 p95 = 0;
    Uint32 p99 = 0;
    Uint64 avg_us = 0;

    if (s) {
        sample_count = s->sample_count;
        if (sample_count > WPK_PERF_SAMPLE_CAP)
            sample_count = WPK_PERF_SAMPLE_CAP;
        if (sample_count > 0) {
            SDL_memcpy(samples, s->samples, sizeof(Uint32) * (size_t)sample_count);
            p95 = WPK_Percentile(samples, sample_count, 95);
            SDL_memcpy(samples, s->samples, sizeof(Uint32) * (size_t)sample_count);
            p99 = WPK_Percentile(samples, sample_count, 99);
        }
        if (s->count)
            avg_us = s->total_us / s->count;
    }

    lua_createtable(L, 0, 10);
    lua_pushinteger(L, s ? (lua_Integer)s->count : 0);
    lua_setfield(L, -2, "count");
    lua_pushinteger(L, s ? (lua_Integer)s->total_us : 0);
    lua_setfield(L, -2, "total_us");
    lua_pushinteger(L, s ? (lua_Integer)s->total_us : 0);
    lua_setfield(L, -2, "total");
    lua_pushinteger(L, (lua_Integer)avg_us);
    lua_setfield(L, -2, "avg_us");
    lua_pushinteger(L, (lua_Integer)sample_count);
    lua_setfield(L, -2, "sample_count");
    lua_pushinteger(L, (lua_Integer)sample_count);
    lua_setfield(L, -2, "samples");
    lua_pushinteger(L, (lua_Integer)p95);
    lua_setfield(L, -2, "p95_us");
    lua_pushinteger(L, (lua_Integer)p95);
    lua_setfield(L, -2, "p95");
    lua_pushinteger(L, (lua_Integer)p99);
    lua_setfield(L, -2, "p99_us");
    lua_pushinteger(L, (lua_Integer)p99);
    lua_setfield(L, -2, "p99");
}

void WPK_PushPerfStats(lua_State* L)
{
    WPK_TimeStats parse_us;
    WPK_TimeStats write_queue_us;

    SDL_memset(&parse_us, 0, sizeof(parse_us));
    SDL_memset(&write_queue_us, 0, sizeof(write_queue_us));

    WPK_PerfEnsure();
    if (g_wpk_perf.mutex)
        SDL_LockMutex(g_wpk_perf.mutex);
    parse_us = g_wpk_perf.parse_us;
    write_queue_us = g_wpk_perf.write_queue_us;
    if (g_wpk_perf.mutex)
        SDL_UnlockMutex(g_wpk_perf.mutex);

    lua_createtable(L, 0, 2);
    WPK_PushTimeStatsSnapshot(L, &parse_us);
    lua_setfield(L, -2, "parse_us");
    WPK_PushTimeStatsSnapshot(L, &write_queue_us);
    lua_setfield(L, -2, "write_queue_us");
}

static int WPK_GC(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);
    WPK_ClearWriteQueue(ud);
    WPK_DecodedCacheClear(ud);
    WPK_ClearLookupIndexes(ud);
    if (ud->list_ref > 0)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, ud->list_ref);
        ud->list_ref = LUA_NOREF;
    }
    if (ud->zstd_dctx)
    {
        ZSTD_freeDCtx(ud->zstd_dctx);
        ud->zstd_dctx = NULL;
    }
    if (ud->zstd_ddict)
    {
        ZSTD_freeDDict(ud->zstd_ddict);
        ud->zstd_ddict = NULL;
        ud->zstd_dict_id = 0;
    }
    if (ud->zstd_dict_buf)
    {
        SDL_free(ud->zstd_dict_buf);
        ud->zstd_dict_buf = NULL;
        ud->zstd_dict_size = 0;
    }
    WPK_DestroyNativeState(ud);
    if (ud->write_files)
    {
        WPK_CloseCachedWriteFiles(ud);
        SDL_free(ud->write_files);
        ud->write_files = NULL;
        ud->write_files_count = 0;
    }
    if (ud->list)
    {
        SDL_free(ud->list);
        ud->list = NULL;
        ud->number = 0;
    }
    return 0;
}

static int WPK_ParseIdx(const Uint8* data, size_t size, size_t* outHeaderSize, size_t* outRecordSize)
{
    static const size_t headerCandidates[] = {8, 12, 16, 20, 24, 32, 40, 64};
    static const size_t recordCandidates[] = {44, 48, 52, 56, 64, 72, 80};

    int bestScore = -1;
    size_t bestHeader = 0;
    size_t bestRecord = 0;

    for (size_t hi = 0; hi < (sizeof(headerCandidates) / sizeof(headerCandidates[0])); hi++)
    {
        size_t header = headerCandidates[hi];
        if (size <= header)
            continue;

        for (size_t ri = 0; ri < (sizeof(recordCandidates) / sizeof(recordCandidates[0])); ri++)
        {
            size_t rec = recordCandidates[ri];
            size_t payload = size - header;
            if (payload % rec != 0)
                continue;
            size_t n = payload / rec;
            if (n == 0)
                continue;

            size_t sample = n < 32 ? n : 32;
            int score = 0;
            for (size_t i = 0; i < sample; i++)
            {
                const Uint8* r = data + header + i * rec;
                int found = 0;
                for (size_t off = 0; off + 32 <= rec; off++)
                {
                    if (WPK_IsHex32(r + off))
                    {
                        found = 1;
                        break;
                    }
                }
                score += found;
            }
            if (score > bestScore)
            {
                bestScore = score;
                bestHeader = header;
                bestRecord = rec;
            }
        }
    }

    if (bestScore <= 0)
        return 0;

    *outHeaderSize = bestHeader;
    *outRecordSize = bestRecord;
    return 1;
}

static int WPK_NEWImpl(lua_State* L)
{
    Uint8* data = NULL;
    size_t size = 0;
    const char* idxPath = NULL;

    if (lua_gettop(L) >= 2 && lua_type(L, 1) == LUA_TSTRING && lua_type(L, 2) == LUA_TSTRING)
    {
        /* Mode B: Lua layer already read idx via PhysFS — WPK_NEW(dataStr, absPath) */
        g_wpk_stats.idx_mode_buffer++;
        size_t dataLen = 0;
        const char* luaData = lua_tolstring(L, 1, &dataLen);
        idxPath = lua_tostring(L, 2);
        if (!luaData || dataLen == 0 || !idxPath || !idxPath[0])
        {
            g_wpk_stats.idx_open_failures++;
            return 0;
        }
        data = (Uint8*)SDL_malloc(dataLen);
        if (!data)
        {
            g_wpk_stats.idx_open_failures++;
            return 0;
        }
        SDL_memcpy(data, luaData, dataLen);
        size = dataLen;
    }
    else
    {
        /* Mode A: C layer reads file directly — WPK_NEW(filePath) */
        g_wpk_stats.idx_mode_file++;
        idxPath = luaL_checkstring(L, 1);
        if (!WPK_ReadFileAll(idxPath, &data, &size))
        {
            g_wpk_stats.idx_open_failures++;
            return 0;
        }
    }

    int idxIsSkpe = 0;

    if (size > 4 && data[0] == 'S' && data[1] == 'K' && data[2] == 'P' && data[3] == 'E')
    {
        idxIsSkpe = 1;
        size_t n = size - 4;
        Uint8* plain = (Uint8*)SDL_malloc(n);
        if (!plain)
        {
            SDL_free(data);
            return 0;
        }
        SDL_memcpy(plain, data + 4, n);
        for (size_t i = 0; i < n / 2; i++)
        {
            Uint8 t = plain[i];
            plain[i] = plain[n - 1 - i];
            plain[n - 1 - i] = t;
        }
        for (size_t i = 0; i < n; i++)
            plain[i] = (Uint8)(plain[i] ^ 0x5A);
        SDL_free(data);
        data = plain;
        size = n;
    }

    if (size >= 0x20 && data[0] == 'S' && data[1] == 'K' && data[2] == 'P' && data[3] == 'W')
    {
        const size_t headerStart = 0x20;
        const size_t recordSize = 28;
        Uint32 recordCount = WPK_ReadU32LE(data + 12);
        Uint32 version = WPK_ReadU32LE(data + 8);
        Uint32 unknown = WPK_ReadU32LE(data + 4);

        if (recordCount == 0)
        {
            SDL_free(data);
            return 0;
        }

        size_t need = headerStart + (size_t)recordCount * recordSize;
        if (size < need)
        {
            SDL_free(data);
            return 0;
        }

        WPK_UserData* ud = (WPK_UserData*)lua_newuserdata(L, sizeof(WPK_UserData));
        SDL_memset(ud, 0, sizeof(WPK_UserData));
        if (!WPK_InitNativeState(ud))
        {
            SDL_free(data);
            return 0;
        }
        ud->idx_is_skpw = 1;
        ud->idx_is_skpe = (Uint8)idxIsSkpe;
        ud->skpw_unknown = unknown;
        ud->skpw_version = version;
        ud->list_ref = LUA_NOREF;
        luaL_setmetatable(L, WPK_NAME);
        SDL_strlcpy(ud->idx_path, idxPath, sizeof(ud->idx_path));
        WPK_ExtractBaseDir(ud->base_dir, idxPath);
        WPK_ExtractBaseName(ud->base_name, idxPath);

        ud->number = recordCount;
        ud->list = (WPK_FileInfo*)SDL_malloc(sizeof(WPK_FileInfo) * ud->number);
        if (!ud->list)
        {
            ud->number = 0;
            ud->list = NULL;
            WPK_DestroyNativeState(ud);
            SDL_free(data);
            return 0;
        }
        SDL_memset(ud->list, 0, sizeof(WPK_FileInfo) * ud->number);

        for (Uint32 i = 0; i < ud->number; i++)
        {
            const Uint8* rec = data + headerStart + (size_t)i * recordSize;
            WPK_BinToLowerHex32(ud->list[i].md5, rec);
            Uint32 dwSize = WPK_ReadU32LE(rec + 16);
            ud->list[i].offset = WPK_ReadU32LE(rec + 20);

            Uint32 pack = WPK_ReadU32LE(rec + 24);
            ud->list[i].size = dwSize;
            Uint16 wpkid16 = (Uint16)(pack & 0xFFu);
            ud->list[i].file_index = (Uint16)((pack >> 16) & 0xFFFFu);
            if (wpkid16 == 255u)
            {
                ud->list[i].wpkid = (Uint32)(Sint32)-1;
            }
            else
            {
                Sint16 swpkid16 = (Sint16)wpkid16;
                if (swpkid16 < 0)
                {
                    ud->list[i].wpkid = (Uint32)(Sint32)swpkid16;
                }
                else
                {
                    ud->list[i].wpkid = (Uint32)wpkid16;
                }
            }
        }

        size_t checkOff = need;
        if (checkOff + 4 <= size)
        {
            Uint32 check = WPK_ReadU32LE(data + checkOff);
            if (check == unknown && version == 1)
            {
                size_t crcOff = checkOff + 4;
                size_t crcNeed = crcOff + (size_t)recordCount * 4;
                if (crcNeed <= size)
                {
                    for (Uint32 i = 0; i < ud->number; i++)
                        ud->list[i].hash = WPK_ReadU32LE(data + crcOff + (size_t)i * 4);
                }
            }
        }
        SDL_free(data);

        ud->lookup_dirty = 1;
        WPK_RebuildLookupIndexes(ud);

        lua_pushinteger(L, (lua_Integer)ud->number);
        return 2;
    }

    size_t headerSize = 0;
    size_t recordSize = 0;
    if (!WPK_ParseIdx(data, size, &headerSize, &recordSize))
    {
        SDL_free(data);
        return 0;
    }

    size_t payload = size - headerSize;
    size_t number = payload / recordSize;

    WPK_UserData* ud = (WPK_UserData*)lua_newuserdata(L, sizeof(WPK_UserData));
    SDL_memset(ud, 0, sizeof(WPK_UserData));
    if (!WPK_InitNativeState(ud))
    {
        SDL_free(data);
        return 0;
    }
    ud->idx_is_skpw = 0;
        ud->idx_is_skpe = (Uint8)idxIsSkpe;
    ud->list_ref = LUA_NOREF;
    luaL_setmetatable(L, WPK_NAME);
    SDL_strlcpy(ud->idx_path, idxPath, sizeof(ud->idx_path));
    WPK_ExtractBaseDir(ud->base_dir, idxPath);
    WPK_ExtractBaseName(ud->base_name, idxPath);

    ud->number = (Uint32)number;
    ud->list = (WPK_FileInfo*)SDL_malloc(sizeof(WPK_FileInfo) * ud->number);
    if (!ud->list)
    {
        ud->number = 0;
        ud->list = NULL;
        WPK_DestroyNativeState(ud);
        SDL_free(data);
        return 0;
    }
    SDL_memset(ud->list, 0, sizeof(WPK_FileInfo) * ud->number);

    for (Uint32 i = 0; i < ud->number; i++)
    {
        const Uint8* rec = data + headerSize + (size_t)i * recordSize;
        size_t md5Off = (size_t)-1;
        for (size_t off = 0; off + 32 <= recordSize; off++)
        {
            if (WPK_IsHex32(rec + off))
            {
                md5Off = off;
                break;
            }
        }
        if (md5Off == (size_t)-1)
        {
            SDL_free(ud->list);
            ud->list = NULL;
            ud->number = 0;
            WPK_DestroyNativeState(ud);
            SDL_free(data);
            return 0;
        }
        WPK_ToLowerHex32(ud->list[i].md5, rec + md5Off);

        size_t after = md5Off + 32;
        Uint32 wpkid = 0, offset = 0, fsize = 0;
        if (after + 12 <= recordSize)
        {
            wpkid = WPK_ReadU32LE(rec + after);
            offset = WPK_ReadU32LE(rec + after + 4);
            fsize = WPK_ReadU32LE(rec + after + 8);
        }
        else if (recordSize >= 12)
        {
            wpkid = WPK_ReadU32LE(rec + recordSize - 12);
            offset = WPK_ReadU32LE(rec + recordSize - 8);
            fsize = WPK_ReadU32LE(rec + recordSize - 4);
        }
        ud->list[i].wpkid = wpkid;
        ud->list[i].offset = offset;
        ud->list[i].size = fsize;
        ud->list[i].file_index = (Uint16)i;
    }
    SDL_free(data);

    ud->lookup_dirty = 1;
    WPK_RebuildLookupIndexes(ud);

    lua_pushinteger(L, (lua_Integer)ud->number);
    return 2;
}

static int WPK_NEW(lua_State* L)
{
    Uint64 start_us = WPK_NowUS();
    int ret = WPK_NEWImpl(L);
    WPK_RecordTime(&g_wpk_perf.parse_us, WPK_NowUS() - start_us);
    return ret;
}

static int THD_ParseImpl(lua_State* L)
{
    size_t len = 0;
    const Uint8* data = (const Uint8*)luaL_checklstring(L, 1, &len);
    if (len < 8)
        return 0;
    if (!(data[0] == 'T' && data[1] == 'H' && data[2] == 'D' && data[3] == 'O'))
        return 0;

    Uint32 thxCount = 0;
    const Uint8* useData = data;
    Uint8* tmp = NULL;
    int thx24 = WPK_TryParseThx24Header(data, len, &thxCount);
    if (!thx24 && len >= 68)
    {
        tmp = (Uint8*)SDL_malloc(len);
        if (tmp)
        {
            SDL_memcpy(tmp, data, len);
            THX_XorRev64Inplace(tmp, len);
            thx24 = WPK_TryParseThx24Header(tmp, len, &thxCount);
            if (thx24)
                useData = tmp;
            else
            {
                SDL_free(tmp);
                tmp = NULL;
            }
        }
    }
    if (!thx24)
        return 0;

    lua_createtable(L, (int)thxCount, 0);
    for (Uint32 i = 0; i < thxCount; i++)
    {
        const Uint8* rec = useData + 12 + (size_t)i * 24;
        Uint32 hash = WPK_ReadU32LE(rec);
        char md5[33];
        WPK_BinToLowerHex32(md5, rec + 8);

        lua_createtable(L, 0, 2);
        lua_pushstring(L, md5);
        lua_setfield(L, -2, "md5");
        lua_pushinteger(L, (lua_Integer)hash);
        lua_setfield(L, -2, "hash");
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    if (tmp)
        SDL_free(tmp);

    return 1;
}

static int THD_Parse(lua_State* L)
{
    Uint64 start_us = WPK_NowUS();
    int ret = THD_ParseImpl(L);
    WPK_RecordTime(&g_wpk_perf.parse_us, WPK_NowUS() - start_us);
    return ret;
}

MYGXY_API int luaopen_mygxy_wpk(lua_State* L)
{
    const luaL_Reg funcs[] = {
        {"__gc", WPK_GC},
        {"__close", WPK_GC},
        {"GetData", WPK_GetData},
        {"DecodeBuffer", WPK_DecodeBuffer},
        {"GetList", WPK_GetList},
        {"GetInfoByMd5", WPK_GetInfoByMd5},
        {"GetInfoByHash", WPK_GetInfoByHash},
        {"GetInfoByHashBatch", WPK_GetInfoByHashBatch},
        {"Upsert", WPK_Upsert},
        {"SetHash", WPK_SetHash},
        {"QueueWrite", WPK_QueueWrite},
        {"SetWriteBaseDir", WPK_SetWriteBaseDir},
        {"FlushWriteQueue", WPK_FlushWriteQueue},
        {"SaveIdx", WPK_SaveIdx},
        {"SetZstdDict", WPK_SetZstdDict},
        {"GetStats", WPK_GetStats},
        {NULL, NULL},
    };
    WPK_PerfEnsure();

    luaL_newmetatable(L, WPK_NAME);
    luaL_setfuncs(L, funcs, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
    lua_pushcfunction(L, WPK_NEW);
    return 1;
}

MYGXY_API int luaopen_mygxy_wpk_thd(lua_State* L)
{
    WPK_PerfEnsure();

    lua_pushcfunction(L, THD_Parse);
    return 1;
}
