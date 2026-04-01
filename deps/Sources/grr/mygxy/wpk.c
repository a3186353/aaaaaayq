#include "sdl_proxy.h"
#include "wpk.h"

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
    SDL_RWops *rwops = (SDL_RWFromFile)(path, mode);
    if (!rwops)
    {
        WPK_LogOpenFailure("sdl", path, SDL_GetError());
    }
    return rwops;
}
#define SDL_RWFromFile WPK_SDL_RWFromFile
#endif

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
    Uint8 RoundKey[176];
} WPK_Aes128Ctx;

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

typedef struct
{
    Uint32 number;
    WPK_FileInfo* list;

    int list_ref;

    SDL_RWops** wpk_files;
    Uint32 wpk_files_count;

    ZSTD_DCtx* zstd_dctx;
    ZSTD_DDict* zstd_ddict;
    Uint32 zstd_dict_id;
    Uint8* zstd_dict_buf;
    size_t zstd_dict_size;

    Uint8 idx_is_skpw;

    Uint8 idx_is_skpe;

    char idx_path[256];
    char base_dir[256];
    char base_name[128];

    Uint32 skpw_unknown;
    Uint32 skpw_version;
} WPK_UserData;

static int WPK_WriteFileAll(const char* path, const void* data, size_t size)
{
    if (!path || !data || size == 0)
        return 0;
    SDL_RWops* fp = SDL_RWFromFile(path, "wb");
    if (!fp)
        return 0;
    size_t wrote = SDL_RWwrite(fp, data, 1, size);
    SDL_RWclose(fp);
    return wrote == size;
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

static int WPK_EnsureWpkFiles(WPK_UserData* ud, Uint32 wpkid)
{
    if (!ud)
        return 0;
    if (wpkid < ud->wpk_files_count)
        return 1;

    Uint32 newCount = wpkid + 1;
    if (newCount == 0)
        return 0;

    SDL_RWops** p = (SDL_RWops**)SDL_realloc(ud->wpk_files, sizeof(SDL_RWops*) * newCount);
    if (!p)
        return 0;

    for (Uint32 i = ud->wpk_files_count; i < newCount; i++)
        p[i] = NULL;
    ud->wpk_files = p;
    ud->wpk_files_count = newCount;
    return 1;
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

static int WPK_FindByMd5(const WPK_UserData* ud, const char md5Lower33[33])
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

static int WPK_TryParseThx24Header(const Uint8* data, size_t size, Uint32* outCount)
{
    if (!data || !outCount || size < 12)
        return 0;
    if (!(data[0] == 'T' && data[1] == 'H' && data[2] == 'D' && data[3] == 'O'))
        return 0;
    if (!(data[4] == 'S' && data[5] == 'K' && data[6] == 'P' && data[7] == 'E'))
        return 0;
    Uint32 count = WPK_ReadU32LE(data + 8);
    if (count == 0)
        return 0;
    size_t need = 12 + (size_t)count * 24;
    if (need != size)
        return 0;
    *outCount = count;
    return 1;
}

static int THD_FindHex32(const Uint8* p, size_t n, size_t* outOff);
static void THD_Xor5A(Uint8* dst, const Uint8* src, size_t n);

static int WPK_TryParseThdRecord(const Uint8* rec, char md5[33], Uint32* outHash)
{
    size_t md5Off = 0;
    const Uint8* use = rec;

    int ok = THD_FindHex32(rec, 0x40, &md5Off);
    Uint8 decoded[0x40];
    if (!ok)
    {
        THD_Xor5A(decoded, rec, 0x40);
        if (THD_FindHex32(decoded, 0x40, &md5Off))
        {
            use = decoded;
            ok = 1;
        }
    }
    if (!ok)
    {
        int allZero = 1;
        for (int j = 0; j < 0x40; j++)
        {
            if (rec[j] != 0)
            {
                allZero = 0;
                break;
            }
        }
        if (allZero)
            return 0;

        WPK_BinToLowerHex32(md5, rec);
        *outHash = WPK_ReadU32LE(rec + 16);
        return 1;
    }

    WPK_ToLowerHex32(md5, use + md5Off);
    if (md5Off + 36 <= 0x40)
        *outHash = WPK_ReadU32LE(use + md5Off + 32);
    else
        *outHash = WPK_ReadU32LE(use + 0x3C);
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

static SDL_RWops* WPK_OpenWpkFile(WPK_UserData* ud, Uint32 wpkid)
{
    if (wpkid >= ud->wpk_files_count)
        return NULL;
    if (ud->wpk_files[wpkid])
        return ud->wpk_files[wpkid];

    char lower_base_name[128];
    SDL_strlcpy(lower_base_name, ud->base_name, sizeof(lower_base_name));
    for (size_t i = 0; lower_base_name[i]; i++)
    {
        lower_base_name[i] = (char)SDL_tolower((unsigned char)lower_base_name[i]);
    }

    char path[512];
    SDL_snprintf(path, sizeof(path), "%s" WPK_SEP_STR "%s%u.wpk", ud->base_dir, lower_base_name, (unsigned)wpkid);
    const char* openPath = path;
#if defined(__ANDROID__)
    char localPath[512];
    if (WPK_CopyToInternalStorage(path, localPath))
        openPath = localPath;
#endif
    SDL_RWops* fp = SDL_RWFromFile(openPath, "rb");
    if (!fp)
    {
        if (wpkid != 0)
        {
            fprintf(stderr, "[wpk] open data pack failed: base_dir='%s' base_name='%s' wpkid=%u\n",
                ud->base_dir,
                ud->base_name,
                (unsigned)wpkid);
            return NULL;
        }
        SDL_snprintf(path, sizeof(path), "%s" WPK_SEP_STR "%s.wpk", ud->base_dir, lower_base_name);
        openPath = path;
#if defined(__ANDROID__)
        if (WPK_CopyToInternalStorage(path, localPath))
            openPath = localPath;
#endif
        fp = SDL_RWFromFile(openPath, "rb");
    }
    if (!fp)
    {
        fprintf(stderr, "[wpk] open data pack failed: base_dir='%s' base_name='%s' wpkid=%u\n",
            ud->base_dir,
            ud->base_name,
            (unsigned)wpkid);
        return NULL;
    }

    ud->wpk_files[wpkid] = fp;
    return fp;
}

static SDL_RWops* WPK_ReopenWpkFile(WPK_UserData* ud, Uint32 wpkid)
{
    if (wpkid >= ud->wpk_files_count)
        return NULL;
    if (ud->wpk_files[wpkid])
    {
        SDL_RWclose(ud->wpk_files[wpkid]);
        ud->wpk_files[wpkid] = NULL;
    }
    return WPK_OpenWpkFile(ud, wpkid);
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
static int WPK_TryPushNeoxDecompress(lua_State* L, WPK_UserData* ud, const Uint8* src, size_t srcSize);

static int WPK_TryPushAcXcDecoded(lua_State* L, WPK_UserData* ud, const Uint8* in, size_t inSize)
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

    Uint8* dec = (Uint8*)SDL_malloc(dwDataSize);
    if (!dec)
        return 0;
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

    if (WPK_TryPushNeoxDecompress(L, ud, dec, dwDataSize))
    {
        SDL_free(dec);
        return 1;
    }

    WPK_PushBytesAndSize(L, dec, dwDataSize);
    SDL_free(dec);
    return 1;
}

static int WPK_TryPushZstdDecompress(lua_State* L, WPK_UserData* ud, const Uint8* src, size_t srcSize);
static int WPK_TryPushLz4fDecompress(lua_State* L, const Uint8* src, size_t srcSize);
static int WPK_TryPushZlibDecompress(lua_State* L, const Uint8* src, size_t srcSize);
static int WPK_TryInflateWithWindowBits(lua_State* L, const Uint8* src, size_t srcSize, int windowBits);

static int WPK_TryPushNeoxDecompress(lua_State* L, WPK_UserData* ud, const Uint8* src, size_t srcSize)
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
            {
                WPK_PushBytesAndSize(L, src + 8, srcSize - 8);
                return 1;
            }
        }
        WPK_PushBytesAndSize(L, src + 4, srcSize - 4);
        return 1;
    }

    if (magic == 0x5A535444u)
    {
        if (srcSize <= 4)
            return 0;
        if (WPK_TryPushZstdDecompress(L, ud, src + 4, srcSize - 4))
            return 1;
        if (srcSize > 8)
            return WPK_TryPushZstdDecompress(L, ud, src + 8, srcSize - 8);
        return 0;
    }

    if (magic == 0x4C5A3446u)
    {
        if (srcSize <= 4)
            return 0;
        if (WPK_TryPushLz4fDecompress(L, src + 4, srcSize - 4))
            return 1;
        if (srcSize > 8)
            return WPK_TryPushLz4fDecompress(L, src + 8, srcSize - 8);
        return 0;
    }

    if (magic == 0x5A4C4942u || magic == 0x5A4C4941u)
    {
        if (srcSize <= 4)
            return 0;
        if (WPK_TryPushZlibDecompress(L, src + 4, srcSize - 4))
            return 1;
        if (srcSize > 8)
            return WPK_TryPushZlibDecompress(L, src + 8, srcSize - 8);
        return 0;
    }

    return 0;
}

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

static int WPK_TryPushZstdDecompress(lua_State* L, WPK_UserData* ud, const Uint8* src, size_t srcSize)
{
    if (!WPK_IsZstdFrameMagic(src, srcSize))
        return 0;

    Uint32 wantDictId = 0;
    if (ud)
    {
        wantDictId = (Uint32)ZSTD_getDictID_fromFrame(src, srcSize);
        int hasDict = 0;
        if (wantDictId)
            hasDict = 1;
        else
            hasDict = WPK_ZstdFrameGetDictID(src, srcSize, &wantDictId);

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

    if (ud && !ud->zstd_dctx)
        ud->zstd_dctx = ZSTD_createDCtx();
    if (ud && ud->zstd_dctx)
        ZSTD_DCtx_setMaxWindowSize(ud->zstd_dctx, capLimit);

    Uint8* tmp = (Uint8*)SDL_malloc(outCap);
    if (!tmp)
        return 0;

    for (int attempt = 0; attempt < 6; attempt++)
    {
        size_t outSize = (size_t)-1;
        if (ud && ud->zstd_dctx)
        {
            if (ud->zstd_ddict && (!wantDictId || ud->zstd_dict_id == wantDictId))
                outSize = ZSTD_decompress_usingDDict(ud->zstd_dctx, tmp, outCap, src, srcSize, ud->zstd_ddict);
            else
                outSize = ZSTD_decompressDCtx(ud->zstd_dctx, tmp, outCap, src, srcSize);
        }
        else
        {
            outSize = ZSTD_decompress(tmp, outCap, src, srcSize);
        }

        if (!ZSTD_isError(outSize) && outSize <= outCap)
        {
            WPK_PushBytesAndSize(L, tmp, outSize);
            SDL_free(tmp);
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

static int WPK_TryPushLz4fDecompress(lua_State* L, const Uint8* src, size_t srcSize)
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
    size_t outSize = 0;
    int ok = 0;

    while (1)
    {
        if (outSize >= outCap)
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

        size_t dstSize = outCap - outSize;
        if (dstSize > (size_t)0xFFFFFFFFu)
            dstSize = (size_t)0xFFFFFFFFu;

        size_t srcSizeChunk = inRemaining;
        size_t ret = LZ4F_decompress(dctx, outBuf + outSize, &dstSize, inPtr, &srcSizeChunk, NULL);
        if (LZ4F_isError(ret))
            break;

        if (srcSizeChunk == 0 && dstSize == 0)
            break;

        inPtr += srcSizeChunk;
        inRemaining -= srcSizeChunk;
        outSize += dstSize;

        if (ret == 0)
        {
            ok = 1;
            break;
        }

        if (inRemaining == 0)
            break;
    }

    if (!ok)
    {
        for (int flush = 0; flush < 4; flush++)
        {
            if (outSize >= outCap)
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

            size_t dstSize = outCap - outSize;
            if (dstSize > (size_t)0xFFFFFFFFu)
                dstSize = (size_t)0xFFFFFFFFu;

            size_t srcSizeChunk = 0;
            size_t ret = LZ4F_decompress(dctx, outBuf + outSize, &dstSize, inPtr, &srcSizeChunk, NULL);
            if (LZ4F_isError(ret))
                break;

            if (srcSizeChunk == 0 && dstSize == 0)
                break;

            outSize += dstSize;

            if (ret == 0)
            {
                ok = 1;
                break;
            }
        }
    }

    LZ4F_freeDecompressionContext(dctx);

    if (!ok)
    {
        SDL_free(outBuf);
        return 0;
    }

    WPK_PushBytesAndSize(L, outBuf, outSize);
    SDL_free(outBuf);
    return 1;
}

static int WPK_TryInflateWithWindowBits(lua_State* L, const Uint8* src, size_t srcSize, int windowBits)
{
    const size_t capLimit = (size_t)(256u * 1024u * 1024u);

    if (!src || srcSize == 0)
        return 0;

    z_stream strm;
    SDL_memset(&strm, 0, sizeof(strm));
    strm.next_in = (const Bytef*)src;
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

    size_t outSize = 0;
    int ok = 0;
    int safety = 0;

    while (1)
    {
        if (outSize >= outCap)
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

        strm.next_out = (Bytef*)(outBuf + outSize);
        size_t availOut = outCap - outSize;
        if (availOut > (size_t)0xFFFFFFFFu)
            availOut = (size_t)0xFFFFFFFFu;
        strm.avail_out = (uInt)availOut;

        ret = inflate(&strm, 4);
        outSize = (size_t)((Uint8*)strm.next_out - outBuf);

        if (ret == 1)
        {
            ok = 1;
            break;
        }

        if (ret != 0 && ret != -5)
            break;

        if (strm.avail_out != 0 && ret == -5)
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

    WPK_PushBytesAndSize(L, outBuf, outSize);
    SDL_free(outBuf);
    return 1;
}

static int WPK_TryPushZlibDecompress(lua_State* L, const Uint8* src, size_t srcSize)
{
    if (!WPK_IsZlibHeader(src, srcSize) && !WPK_IsGzipMagic(src, srcSize))
        return 0;

    if (WPK_TryInflateWithWindowBits(L, src, srcSize, 15 + 32))
        return 1;

    return WPK_TryInflateWithWindowBits(L, src, srcSize, -15);
}

static int WPK_TryPushDecompress(lua_State* L, WPK_UserData* ud, const Uint8* src, size_t srcSize)
{
    if (WPK_TryPushNeoxDecompress(L, ud, src, srcSize))
        return 1;
    if (WPK_TryPushZstdDecompress(L, ud, src, srcSize))
        return 1;
    if (WPK_TryPushLz4fDecompress(L, src, srcSize))
        return 1;
    if (WPK_TryPushZlibDecompress(L, src, srcSize))
        return 1;
    return 0;
}

static int WPK_SetZstdDict(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);

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

    if (ud->zstd_ddict)
        ZSTD_freeDDict(ud->zstd_ddict);
    ud->zstd_ddict = ddict;
    ud->zstd_dict_id = (Uint32)ZSTD_getDictID_fromDDict(ud->zstd_ddict);
    if (ud->zstd_dict_buf)
        SDL_free(ud->zstd_dict_buf);
    ud->zstd_dict_buf = dictCopy;
    ud->zstd_dict_size = dictSize;

    lua_pushinteger(L, (lua_Integer)ud->zstd_dict_id);
    return 1;
}

static int WPK_GetData(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);
    Uint32 i = (Uint32)luaL_checkinteger(L, 2) - 1;

    if (i < ud->number)
    {
        const WPK_FileInfo* fi = &ud->list[i];

        Uint32 wpkid = fi->wpkid;
        Uint8* raw = NULL;
        size_t inSize = 0;

        if (WPK_WpkIdAsS32(wpkid) < 0)
        {
            char path[512];
            SDL_snprintf(path, sizeof(path), "%s" WPK_SEP_STR "%s" WPK_SEP_STR "%s", ud->base_dir, ud->base_name, fi->md5);
            SDL_RWops* fp = SDL_RWFromFile(path, "rb");
            if (!fp)
                return WPK_PushEmptyBytesAndSize(L);
            if (SDL_RWseek(fp, 0, RW_SEEK_END) < 0)
            {
                SDL_RWclose(fp);
                return 0;
            }
            Sint64 sz = SDL_RWtell(fp);
            if (sz < 0)
            {
                SDL_RWclose(fp);
                return 0;
            }
            if (SDL_RWseek(fp, 0, RW_SEEK_SET) < 0)
            {
                SDL_RWclose(fp);
                return 0;
            }

            inSize = (size_t)sz;
            if (inSize == 0)
            {
                SDL_RWclose(fp);
                return WPK_PushEmptyBytesAndSize(L);
            }

            raw = (Uint8*)SDL_malloc(inSize);
            if (!raw)
            {
                SDL_RWclose(fp);
                return 0;
            }

            size_t readCount = WPK_RWreadAll(fp, raw, inSize);
            SDL_RWclose(fp);
            if (readCount != inSize)
            {
                SDL_free(raw);
                return 0;
            }
        }
        else
        {
            if (fi->size == 0)
                return WPK_PushEmptyBytesAndSize(L);

            SDL_RWops* fp = WPK_OpenWpkFile(ud, wpkid);
            if (!fp)
                return 0;

            inSize = (size_t)fi->size;
            for (int attempt = 0; attempt < 2; attempt++)
            {
                if (attempt > 0)
                {
                    fp = WPK_ReopenWpkFile(ud, wpkid);
                    if (!fp)
                        break;
                }

                if (SDL_RWseek(fp, (Sint64)fi->offset, RW_SEEK_SET) < 0)
                {
                    fprintf(stderr, "[wpk] seek data pack failed: idx='%s' base_name='%s' wpkid=%u id=%u offset=%u size=%u attempt=%d\n",
                        ud->idx_path,
                        ud->base_name,
                        (unsigned)wpkid,
                        (unsigned)(i + 1),
                        (unsigned)fi->offset,
                        (unsigned)fi->size,
                        attempt + 1);
                    continue;
                }

                raw = (Uint8*)SDL_malloc(inSize);
                if (!raw)
                    return 0;

                size_t readCount = WPK_RWreadAll(fp, raw, inSize);
                if (readCount == inSize)
                    break;

                fprintf(stderr, "[wpk] read data pack failed: idx='%s' base_name='%s' wpkid=%u id=%u offset=%u size=%u read=%u attempt=%d\n",
                    ud->idx_path,
                    ud->base_name,
                    (unsigned)wpkid,
                    (unsigned)(i + 1),
                    (unsigned)fi->offset,
                    (unsigned)fi->size,
                    (unsigned)readCount,
                    attempt + 1);
                SDL_free(raw);
                raw = NULL;
            }
            if (!raw)
                return 0;
            if (SDL_RWtell(fp) < 0)
            {
                fprintf(stderr, "[wpk] tell data pack failed: idx='%s' base_name='%s' wpkid=%u id=%u\n",
                    ud->idx_path,
                    ud->base_name,
                    (unsigned)wpkid,
                    (unsigned)(i + 1));
            }
        }

        if (WPK_TryPushAcXcDecoded(L, ud, raw, inSize))
        {
            SDL_free(raw);
            return 2;
        }

        if (WPK_TryPushNeoxDecompress(L, ud, raw, inSize))
        {
            SDL_free(raw);
            return 2;
        }

        if (ud->list[i].hash && inSize > 4)
        {
            Uint8 key[4];
            key[0] = (Uint8)((ud->list[i].hash >> 0) & 0xFF);
            key[1] = (Uint8)((ud->list[i].hash >> 8) & 0xFF);
            key[2] = (Uint8)((ud->list[i].hash >> 16) & 0xFF);
            key[3] = (Uint8)((ud->list[i].hash >> 24) & 0xFF);

            Uint8* dec = (Uint8*)SDL_malloc(inSize);
            if (dec)
            {
                SDL_memcpy(dec, raw, 4);
                WPK_XorRepeat4(dec + 4, raw + 4, inSize - 4, key);

                if (WPK_TryPushAcXcDecoded(L, ud, dec, inSize))
                {
                    SDL_free(dec);
                    SDL_free(raw);
                    return 2;
                }

                if (WPK_TryPushNeoxDecompress(L, ud, dec, inSize))
                {
                    SDL_free(dec);
                    SDL_free(raw);
                    return 2;
                }

                if (WPK_LooksLikeCompressed(dec, inSize))
                {
                    WPK_PushBytesAndSize(L, dec, inSize);
                    SDL_free(dec);
                    SDL_free(raw);
                    return 2;
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

                if (WPK_TryPushAcXcDecoded(L, ud, dec, inSize))
                {
                    SDL_free(dec);
                    SDL_free(raw);
                    return 2;
                }

                if (WPK_TryPushNeoxDecompress(L, ud, dec, inSize))
                {
                    SDL_free(dec);
                    SDL_free(raw);
                    return 2;
                }

                if (WPK_LooksLikeCompressed(dec, inSize))
                {
                    WPK_PushBytesAndSize(L, dec, inSize);
                    SDL_free(dec);
                    SDL_free(raw);
                    return 2;
                }

                SDL_free(dec);
            }
        }

        WPK_PushBytesAndSize(L, raw, inSize);
        SDL_free(raw);
        return 2;
    }
    return 0;
}

static int WPK_GetList(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);

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
                return 1;
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

    int idx = WPK_FindByMd5(ud, md5Lower);
    int isNew = 0;
    if (idx < 0)
    {
        Uint32 newCount = ud->number + 1;
        WPK_FileInfo* p = (WPK_FileInfo*)SDL_realloc(ud->list, sizeof(WPK_FileInfo) * newCount);
        if (!p)
            return 0;
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

    if (swpkid >= 0)
        WPK_EnsureWpkFiles(ud, (Uint32)swpkid);

    WPK_InvalidateListCache(L, ud);

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

    int idx = WPK_FindByMd5(ud, md5Lower);
    if (idx < 0)
        return 0;

    ud->list[idx].hash = hash;
    lua_pushinteger(L, (lua_Integer)(idx + 1));
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

    if (!ud->idx_is_skpw)
        return 0;
    if (!outPath || !outPath[0])
        return 0;
    if (!ud->list || ud->number == 0)
        return 0;

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
        return 0;
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
            return 0;
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

static int WPK_GC(lua_State* L)
{
    WPK_UserData* ud = (WPK_UserData*)luaL_checkudata(L, 1, WPK_NAME);
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
    if (ud->wpk_files)
    {
        for (Uint32 i = 0; i < ud->wpk_files_count; i++)
        {
            if (ud->wpk_files[i])
                SDL_RWclose(ud->wpk_files[i]);
        }
        SDL_free(ud->wpk_files);
        ud->wpk_files = NULL;
        ud->wpk_files_count = 0;
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

static int WPK_NEW(lua_State* L)
{
    Uint8* data = NULL;
    size_t size = 0;
    const char* idxPath = NULL;

    if (lua_gettop(L) >= 2 && lua_type(L, 1) == LUA_TSTRING && lua_type(L, 2) == LUA_TSTRING)
    {
        /* Mode B: Lua layer already read idx via PhysFS — WPK_NEW(dataStr, absPath) */
        size_t dataLen = 0;
        const char* luaData = lua_tolstring(L, 1, &dataLen);
        idxPath = lua_tostring(L, 2);
        if (!luaData || dataLen == 0 || !idxPath || !idxPath[0])
            return 0;
        data = (Uint8*)SDL_malloc(dataLen);
        if (!data)
            return 0;
        SDL_memcpy(data, luaData, dataLen);
        size = dataLen;
    }
    else
    {
        /* Mode A: C layer reads file directly — WPK_NEW(filePath) */
        idxPath = luaL_checkstring(L, 1);
        if (!WPK_ReadFileAll(idxPath, &data, &size))
            return 0;
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
            SDL_free(data);
            return 0;
        }
        SDL_memset(ud->list, 0, sizeof(WPK_FileInfo) * ud->number);

        Uint32 maxWpkId = 0;
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
                    if ((Uint32)wpkid16 > maxWpkId)
                        maxWpkId = (Uint32)wpkid16;
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

        ud->wpk_files_count = maxWpkId + 1;
        ud->wpk_files = (SDL_RWops**)SDL_malloc(sizeof(SDL_RWops*) * ud->wpk_files_count);
        if (!ud->wpk_files)
        {
            SDL_free(ud->list);
            ud->list = NULL;
            ud->number = 0;
            return 0;
        }
        SDL_memset(ud->wpk_files, 0, sizeof(SDL_RWops*) * ud->wpk_files_count);

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
        SDL_free(data);
        return 0;
    }
    SDL_memset(ud->list, 0, sizeof(WPK_FileInfo) * ud->number);

    Uint32 maxWpkId = 0;
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
        if (wpkid > maxWpkId)
            maxWpkId = wpkid;
    }
    SDL_free(data);

    ud->wpk_files_count = maxWpkId + 1;
    ud->wpk_files = (SDL_RWops**)SDL_malloc(sizeof(SDL_RWops*) * ud->wpk_files_count);
    if (!ud->wpk_files)
    {
        SDL_free(ud->list);
        ud->list = NULL;
        ud->number = 0;
        return 0;
    }
    SDL_memset(ud->wpk_files, 0, sizeof(SDL_RWops*) * ud->wpk_files_count);

    lua_pushinteger(L, (lua_Integer)ud->number);
    return 2;
}

static int THD_FindHex32(const Uint8* p, size_t n, size_t* outOff)
{
    if (n < 32)
        return 0;
    for (size_t i = 0; i + 32 <= n; i++)
    {
        if (WPK_IsHex32(p + i))
        {
            *outOff = i;
            return 1;
        }
    }
    return 0;
}

static void THD_Xor5A(Uint8* dst, const Uint8* src, size_t n)
{
    for (size_t i = 0; i < n; i++)
        dst[i] = (Uint8)(src[i] ^ 0x5A);
}

static int THD_Parse(lua_State* L)
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
    if (thx24)
    {
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

    static const size_t headerCandidates[] = {8, 12, 16, 20, 24, 32, 40, 64};
    size_t header = 0;
    size_t number = 0;
    for (size_t i = 0; i < (sizeof(headerCandidates) / sizeof(headerCandidates[0])); i++)
    {
        size_t h = headerCandidates[i];
        if (len <= h)
            continue;
        size_t payload = len - h;
        if (payload % 0x40 != 0)
            continue;
        size_t n = payload / 0x40;
        if (n == 0)
            continue;
        header = h;
        number = n;
        break;
    }
    if (!number)
        return 0;

    lua_createtable(L, (int)number, 0);
    lua_Integer outIndex = 0;
    for (size_t i = 0; i < number; i++)
    {
        const Uint8* rec = data + header + i * 0x40;

        char md5[33];
        Uint32 hash = 0;
        if (!WPK_TryParseThdRecord(rec, md5, &hash))
            continue;

        lua_createtable(L, 0, 2);
        lua_pushstring(L, md5);
        lua_setfield(L, -2, "md5");
        lua_pushinteger(L, (lua_Integer)hash);
        lua_setfield(L, -2, "hash");
        outIndex++;
        lua_rawseti(L, -2, outIndex);
    }

    if (tmp)
        SDL_free(tmp);

    return 1;
}

MYGXY_API int luaopen_mygxy_wpk(lua_State* L)
{
    const luaL_Reg funcs[] = {
        {"__gc", WPK_GC},
        {"__close", WPK_GC},
        {"GetData", WPK_GetData},
        {"GetList", WPK_GetList},
        {"Upsert", WPK_Upsert},
        {"SetHash", WPK_SetHash},
        {"SaveIdx", WPK_SaveIdx},
        {"SetZstdDict", WPK_SetZstdDict},
        {NULL, NULL},
    };
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
    lua_pushcfunction(L, THD_Parse);
    return 1;
}
