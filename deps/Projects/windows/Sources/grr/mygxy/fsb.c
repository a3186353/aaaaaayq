#include "sdl_proxy.h"
#include "fsb.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "libvgmstream.h"

#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#endif

#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
#endif

#if defined(_WIN32)
#define MYGXY_API __declspec(dllexport)
#else
#define MYGXY_API LUAMOD_API
#endif

#if !defined(_WIN32)
#if defined(__APPLE__)
#if TARGET_OS_IPHONE
#define FSB_LIBVGMSTREAM_DYN_NAME "libvgmstream"
#define FSB_LIBVGMSTREAM_FW_NAME  "libvgmstream.framework/libvgmstream"
#else
#define FSB_LIBVGMSTREAM_DYN_NAME "libvgmstream.dylib"
#endif
#else
#define FSB_LIBVGMSTREAM_DYN_NAME "libvgmstream.so"
#endif
#endif

static char g_fsb_last_error[1024];

static void fsb_set_last_error(const char* fmt, ...)
{
    if (!fmt)
    {
        g_fsb_last_error[0] = '\0';
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_fsb_last_error, sizeof(g_fsb_last_error), fmt, ap);
    g_fsb_last_error[sizeof(g_fsb_last_error) - 1] = '\0';
    va_end(ap);
}

static int fsb_has_last_error(void)
{
    return g_fsb_last_error[0] != '\0';
}

static const uint16_t fsb_mpeg_bitrates[4][4][16] = {
  { // Version 2.5
    { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Reserved
    { 0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, 0 }, // Layer 3
    { 0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, 0 }, // Layer 2
    { 0,  32,  48,  56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256, 0 }  // Layer 1
  },
  { // Reserved
    { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Invalid
    { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Invalid
    { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Invalid
    { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }  // Invalid
  },
  { // Version 2
    { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Reserved
    { 0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, 0 }, // Layer 3
    { 0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, 0 }, // Layer 2
    { 0,  32,  48,  56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256, 0 }  // Layer 1
  },
  { // Version 1
    { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Reserved
    { 0,  32,  40,  48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 0 }, // Layer 3
    { 0,  32,  48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384, 0 }, // Layer 2
    { 0,  32,  64,  96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0 }, // Layer 1
  }
};

static const uint16_t fsb_mpeg_srates[4][4] = {
    { 11025, 12000,  8000, 0 },
    {     0,     0,     0, 0 },
    { 22050, 24000, 16000, 0 },
    { 44100, 48000, 32000, 0 }
};

static const uint16_t fsb_mpeg_frame_samples[4][4] = {
        {    0,  576, 1152,  384 },
        {    0,    0,    0,    0 },
        {    0,  576, 1152,  384 },
        {    0, 1152, 1152,  384 }
};

static const uint8_t fsb_mpeg_slot_size[4] = { 0, 1, 1, 4 };

static uint16_t mpg_get_frame_size(const char* hdr)
{
    if (!hdr)
        return 0;

    if ((((unsigned char)hdr[0] & 0xFF) != 0xFF)
        || (((unsigned char)hdr[1] & 0xE0) != 0xE0)
        || (((unsigned char)hdr[1] & 0x18) == 0x08)
        || (((unsigned char)hdr[1] & 0x06) == 0x00)
        || (((unsigned char)hdr[2] & 0xF0) == 0xF0))
        return 0;

    uint8_t ver = (uint8_t)((hdr[1] & 0x18) >> 3);
    uint8_t lyr = (uint8_t)((hdr[1] & 0x06) >> 1);
    uint8_t pad = (uint8_t)((hdr[2] & 0x02) >> 1);
    uint8_t brx = (uint8_t)((hdr[2] & 0xF0) >> 4);
    uint8_t srx = (uint8_t)((hdr[2] & 0x0C) >> 2);

    uint32_t bitrate = (uint32_t)fsb_mpeg_bitrates[ver][lyr][brx] * 1000u;
    uint32_t samprate = (uint32_t)fsb_mpeg_srates[ver][srx];
    uint16_t samples = fsb_mpeg_frame_samples[ver][lyr];
    uint8_t slot_size = fsb_mpeg_slot_size[lyr];

    float bps = (float)samples / 8.0f;
    float fsize = ((bps * (float)bitrate) / (float)samprate) + ((pad) ? slot_size : 0);
    return (uint16_t)fsize;
}

int mywav_writehead(luaL_Buffer* b, mywav_fmtchunk* fmt, uint32_t data_size, uint8_t* more, int morelen)
{
    mywav_chunk *chunk = (mywav_chunk*)luaL_prepbuffsize(b,sizeof(mywav_chunk));
    SDL_memcpy(chunk->id, "RIFF", 4);
    chunk->size =
        4 +
        sizeof(mywav_chunk) +
        sizeof(mywav_fmtchunk) +
        morelen +
        sizeof(mywav_chunk) +
        data_size;
    luaL_addsize(b, sizeof(mywav_chunk));

    luaL_addlstring(b, "WAVE", 4);

    chunk = (mywav_chunk*)luaL_prepbuffsize(b, sizeof(mywav_chunk));
    SDL_memcpy(chunk->id, "fmt ", 4);
    chunk->size = sizeof(mywav_fmtchunk) + morelen;
    luaL_addsize(b, sizeof(mywav_chunk));

    luaL_addlstring(b, (char*)fmt, sizeof(mywav_fmtchunk));
    if (morelen)
        luaL_addlstring(b, (char*)more, morelen);

    chunk = (mywav_chunk*)luaL_prepbuffsize(b, sizeof(mywav_chunk));
    SDL_memcpy(chunk->id, "data", 4);
    chunk->size = data_size;
    luaL_addsize(b, sizeof(mywav_chunk));
    return(0);
}

static Uint32 fsb_read_u32le(const Uint8* p)
{
    return ((Uint32)p[0]) | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16) | ((Uint32)p[3] << 24);
}

static Uint64 fsb_read_u64le(const Uint8* p)
{
    Uint64 lo = fsb_read_u32le(p);
    Uint64 hi = fsb_read_u32le(p + 4);
    return lo | (hi << 32);
}

static Uint32 fsb_bits_u32(Uint32 v, int start, int len)
{
    if (len <= 0)
        return 0;
    if (start < 0 || start >= 32)
        return 0;
    if (len >= 32)
        return v >> start;
    if (len >= 32 - start)
        return v >> start;
    return (v >> start) & ((1u << len) - 1u);
}

static Uint64 fsb_bits_u64(Uint64 v, int start, int len)
{
    if (len <= 0)
        return 0;
    if (start < 0 || start >= 64)
        return 0;
    if (len >= 64)
        return v >> start;
    if (len >= 64 - start)
        return v >> start;
    return (v >> start) & ((1ull << len) - 1ull);
}

static Uint32 fsb5_frequency_from_code(Uint32 code)
{
    switch (code)
    {
    case 1:
        return 8000;
    case 2:
        return 11000;
    case 3:
        return 11025;
    case 4:
        return 16000;
    case 5:
        return 22050;
    case 6:
        return 24000;
    case 7:
        return 32000;
    case 8:
        return 44100;
    case 9:
        return 48000;
    default:
        return 0;
    }
}

static const char* fsb_guess_ext(const Uint8* data, Uint32 size)
{
    if (!data || size < 2)
        return "bin";
    if (size >= 4 && SDL_memcmp(data, "RIFF", 4) == 0)
        return "wav";
    if (size >= 4 && SDL_memcmp(data, "OggS", 4) == 0)
        return "ogg";
    if (size >= 4 && SDL_memcmp(data, "fLaC", 4) == 0)
        return "flac";

    if (size >= 3 && SDL_memcmp(data, "ID3", 3) == 0)
        return "mp3";

    {
        Uint32 limit = size;
        if (limit > 4096u)
            limit = 4096u;
        for (Uint32 i = 0; i + 4u <= limit; i++)
        {
            if (data[i] != 0xFF || (data[i + 1] & 0xE0) != 0xE0)
                continue;

            Uint32 remain = size - i;
            if (remain < 8u)
                continue;

            Uint32 f0 = (Uint32)mpg_get_frame_size((char*)(data + i));
            if (f0 < 4u || f0 > remain)
                continue;

            if (f0 + 4u > remain)
                continue;
            Uint32 f1 = (Uint32)mpg_get_frame_size((char*)(data + i + f0));
            if (f1 == 0)
                continue;

            return "mp3";
        }
    }
    return "bin";
}

static Uint32 fsb_read_u16le(const Uint8* p)
{
    return ((Uint32)p[0]) | ((Uint32)p[1] << 8);
}

static void fsb_write_u16le(Uint8* p, Uint32 v)
{
    p[0] = (Uint8)(v & 0xFFu);
    p[1] = (Uint8)((v >> 8) & 0xFFu);
}

static void fsb_write_u32le(Uint8* p, Uint32 v)
{
    p[0] = (Uint8)(v & 0xFFu);
    p[1] = (Uint8)((v >> 8) & 0xFFu);
    p[2] = (Uint8)((v >> 16) & 0xFFu);
    p[3] = (Uint8)((v >> 24) & 0xFFu);
}

static void fsb_write_u64le(Uint8* p, Uint64 v)
{
    p[0] = (Uint8)(v & 0xFFull);
    p[1] = (Uint8)((v >> 8) & 0xFFull);
    p[2] = (Uint8)((v >> 16) & 0xFFull);
    p[3] = (Uint8)((v >> 24) & 0xFFull);
    p[4] = (Uint8)((v >> 32) & 0xFFull);
    p[5] = (Uint8)((v >> 40) & 0xFFull);
    p[6] = (Uint8)((v >> 48) & 0xFFull);
    p[7] = (Uint8)((v >> 56) & 0xFFull);
}

#if defined(_WIN32)
static int fsb_write_win32_file(void* ctx, const void* data, Uint32 size);
#endif

static int fsb5_pcm_bits_from_format(Uint32 format)
{
    switch (format)
    {
    case FMOD_SOUND_FORMAT_PCM8:
        return 8;
    case FMOD_SOUND_FORMAT_PCM16:
        return 16;
    case FMOD_SOUND_FORMAT_PCM24:
        return 24;
    case FMOD_SOUND_FORMAT_PCM32:
        return 32;
    case FMOD_SOUND_FORMAT_PCMFLOAT:
        return 32;
    default:
        return 0;
    }
}

static Uint32 fsb5_format_from_mode(Uint32 mode)
{
    if ((mode & 0xFFu) == 0x0Fu)
        return (Uint32)FMOD_SOUND_FORMAT_VORBIS;

    Uint32 low = mode & 0xFFu;
    if (low < (Uint32)FMOD_SOUND_FORMAT_MAX)
        return low;

    if (mode < (Uint32)FMOD_SOUND_FORMAT_MAX)
        return mode;

    if ((mode & (Uint32)FSOUND_IMAADPCM) != 0u)
        return (Uint32)FMOD_SOUND_FORMAT_IMAADPCM;
    if ((mode & (Uint32)FSOUND_DELTA) != 0u)
        return (Uint32)FMOD_SOUND_FORMAT_MPEG;
    if ((mode & (Uint32)FSOUND_GCADPCM) != 0u)
        return (Uint32)FMOD_SOUND_FORMAT_GCADPCM;
    if ((mode & (Uint32)FSOUND_XMA) != 0u)
        return (Uint32)FMOD_SOUND_FORMAT_XMA;
    if ((mode & 0x10000000u) != 0u)
        return (Uint32)FMOD_SOUND_FORMAT_MPEG;

    return (Uint32)FMOD_SOUND_FORMAT_NONE;
}

static int fsb_lua_push_wav(lua_State* L, Uint16 tag, Uint16 bits, Uint32 channels, Uint32 freq, const void* data, Uint32 size)
{
    if (!L || !data)
        return 0;

    if (channels == 0)
        channels = 1u;
    if (freq == 0)
        freq = 44100u;
    if (bits == 0)
        return 0;

    mywav_fmtchunk fmt;
    fmt.wFormatTag = (int16_t)tag;
    fmt.wChannels = (Uint16)channels;
    fmt.dwSamplesPerSec = freq;
    fmt.wBitsPerSample = bits;
    fmt.wBlockAlign = (Uint16)((bits / 8u) * (Uint16)channels);
    fmt.dwAvgBytesPerSec = fmt.dwSamplesPerSec * fmt.wBlockAlign;

    luaL_Buffer b;
    luaL_buffinit(L, &b);
    mywav_writehead(&b, &fmt, size, NULL, 0);
    if (size)
        luaL_addlstring(&b, (const char*)data, size);
    luaL_pushresult(&b);
    return 1;
}

#if defined(_WIN32)
static int fsb_win32_is_file_exists(const char* path)
{
    if (!path || !path[0])
        return 0;
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return 0;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        return 0;
    return 1;
}

static void* fsb_dyn_load_library(const char* path)
{
    if (!path || !path[0])
        return NULL;
    return (void*)LoadLibraryA(path);
}

static void* fsb_dyn_get_proc(void* mod, const char* name)
{
    if (!mod || !name || !name[0])
        return NULL;
    return (void*)GetProcAddress((HMODULE)mod, name);
}

static int fsb_win32_get_module_dir(char* out_dir, Uint32 out_dir_size)
{
    if (!out_dir || out_dir_size == 0)
        return 0;

    HMODULE mod = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&fsb_win32_get_module_dir, &mod))
        return 0;

    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(mod, path, (DWORD)sizeof(path));
    if (n == 0 || n >= (DWORD)sizeof(path))
        return 0;

    for (DWORD i = n; i > 0; i--)
    {
        char c = path[i - 1];
        if (c == '\\' || c == '/')
        {
            path[i - 1] = '\0';
            break;
        }
    }

    size_t dir_len = strlen(path);
    if (dir_len + 1 > (size_t)out_dir_size)
        return 0;
    memcpy(out_dir, path, dir_len + 1);
    return 1;
}

static int fsb_win32_find_libvgmstream_dll(char* out_path, Uint32 out_path_size)
{
    if (!out_path || out_path_size == 0)
        return 0;

    out_path[0] = '\0';

    {
        const char* envs[] = {
            "LIBVGMSTREAM_DLL_PATH",
            "VGMSTREAM_DLL_PATH",
        };

        for (int i = 0; i < (int)(sizeof(envs) / sizeof(envs[0])); i++)
        {
            char env_path[MAX_PATH];
            DWORD got = GetEnvironmentVariableA(envs[i], env_path, (DWORD)sizeof(env_path));
            if (got > 0 && got < (DWORD)sizeof(env_path) && fsb_win32_is_file_exists(env_path))
            {
                size_t n = strlen(env_path);
                if (n + 1 <= (size_t)out_path_size)
                {
                    memcpy(out_path, env_path, n + 1);
                    return 1;
                }
            }
        }
    }

    {
        char cwd[MAX_PATH];
        DWORD got = GetCurrentDirectoryA((DWORD)sizeof(cwd), cwd);
        if (got > 0 && got < (DWORD)sizeof(cwd))
        {
            char cand[MAX_PATH];
            snprintf(cand, sizeof(cand), "%s\\libvgmstream.dll", cwd);
            if (fsb_win32_is_file_exists(cand))
            {
                size_t n = strlen(cand);
                if (n + 1 <= (size_t)out_path_size)
                {
                    memcpy(out_path, cand, n + 1);
                    return 1;
                }
            }
        }
    }

    {
        char mod_dir[MAX_PATH];
        if (fsb_win32_get_module_dir(mod_dir, (Uint32)sizeof(mod_dir)))
        {
            const char* rels[] = {
                "libvgmstream.dll",
                "vgm\\libvgmstream.dll",
            };

            for (int i = 0; i < (int)(sizeof(rels) / sizeof(rels[0])); i++)
            {
                char cand[MAX_PATH];
                snprintf(cand, sizeof(cand), "%s\\%s", mod_dir, rels[i]);
                if (!fsb_win32_is_file_exists(cand))
                    continue;
                size_t n = strlen(cand);
                if (n + 1 <= (size_t)out_path_size)
                {
                    memcpy(out_path, cand, n + 1);
                    return 1;
                }
            }
        }
    }

    return 0;
}

#endif

typedef struct fsb_memsf_data {
    const uint8_t* data;
    int64_t size;
    char name[64];
} fsb_memsf_data;

static int fsb_memsf_read(void* user_data, uint8_t* dst, int64_t offset, int length)
{
    fsb_memsf_data* m = (fsb_memsf_data*)user_data;
    if (!m || !m->data || !dst)
        return 0;
    if (length <= 0)
        return 0;
    if (offset < 0 || offset >= m->size)
        return 0;

    int64_t remain64 = m->size - offset;
    if (remain64 <= 0)
        return 0;
    int remain = (remain64 > INT32_MAX) ? INT32_MAX : (int)remain64;
    if (length > remain)
        length = remain;

    memcpy(dst, m->data + offset, (size_t)length);
    return length;
}

static int64_t fsb_memsf_get_size(void* user_data)
{
    fsb_memsf_data* m = (fsb_memsf_data*)user_data;
    if (!m)
        return 0;
    return m->size;
}

static const char* fsb_memsf_get_name(void* user_data)
{
    fsb_memsf_data* m = (fsb_memsf_data*)user_data;
    if (!m)
        return "";
    return m->name;
}

static libstreamfile_t* fsb_memsf_open(void* user_data, const char* filename);

static void fsb_memsf_close(libstreamfile_t* libsf)
{
    if (!libsf)
        return;
    if (libsf->user_data)
        SDL_free(libsf->user_data);
    SDL_free(libsf);
}

static libstreamfile_t* fsb_memsf_create(const uint8_t* data, int64_t size, const char* name)
{
    if (!data || size <= 0)
        return NULL;

    libstreamfile_t* sf = (libstreamfile_t*)SDL_malloc(sizeof(libstreamfile_t));
    if (!sf)
        return NULL;
    SDL_memset(sf, 0, sizeof(*sf));

    fsb_memsf_data* m = (fsb_memsf_data*)SDL_malloc(sizeof(fsb_memsf_data));
    if (!m)
    {
        SDL_free(sf);
        return NULL;
    }
    SDL_memset(m, 0, sizeof(*m));
    m->data = data;
    m->size = size;
    if (!name)
        name = "memory.fsb";
    SDL_snprintf(m->name, (int)sizeof(m->name), "%s", name);

    sf->user_data = m;
    sf->read = fsb_memsf_read;
    sf->get_size = fsb_memsf_get_size;
    sf->get_name = fsb_memsf_get_name;
    sf->open = fsb_memsf_open;
    sf->close = fsb_memsf_close;
    return sf;
}

static libstreamfile_t* fsb_memsf_open(void* user_data, const char* filename)
{
    fsb_memsf_data* m = (fsb_memsf_data*)user_data;
    if (!m || !m->data)
        return NULL;
    if (!filename || !filename[0])
        filename = m->name;

    const char* ext = strrchr(filename, '.');
    if (!ext || SDL_strcasecmp(ext, ".fsb") != 0)
    {
        if (SDL_strcasecmp(filename, m->name) != 0)
            return NULL;
    }

    return fsb_memsf_create(m->data, m->size, filename);
}

typedef struct fsb_libvgmstream_api {
    int loaded;
#if defined(_WIN32)
    HMODULE mod;
#else
    void* mod;
#endif

    uint32_t (*get_version)(void);
    libvgmstream_t* (*init)(void);
    void (*free_lib)(libvgmstream_t* lib);
    void (*setup)(libvgmstream_t* lib, libvgmstream_config_t* cfg);
    int (*open_stream)(libvgmstream_t* lib, libstreamfile_t* libsf, int subsong);
    void (*close_stream)(libvgmstream_t* lib);
    int (*render)(libvgmstream_t* lib);
    void (*set_log)(libvgmstream_loglevel_t level, void (*callback)(int level, const char* str));
    libstreamfile_t* (*open_buffered)(libstreamfile_t* ext_libsf);
} fsb_libvgmstream_api;

static fsb_libvgmstream_api g_fsb_vgm_dll;

#if defined(_WIN32)
static int fsb_win32_load_libvgmstream(fsb_libvgmstream_api* api, int want_error)
{
    if (!api)
        return 0;
    if (api->loaded)
        return api->mod != NULL;

    api->loaded = 1;
    api->mod = NULL;

    char dll_path[MAX_PATH];
    if (fsb_win32_find_libvgmstream_dll(dll_path, (Uint32)sizeof(dll_path)))
    {
        api->mod = LoadLibraryExA(dll_path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!api->mod)
            api->mod = LoadLibraryA(dll_path);
    }
    else
        api->mod = LoadLibraryA("libvgmstream.dll");

    if (!api->mod)
    {
        if (want_error && !fsb_has_last_error())
            fsb_set_last_error("libvgmstream.dll not found");
        return 0;
    }

    api->get_version = (uint32_t(*)(void))fsb_dyn_get_proc((void*)api->mod, "libvgmstream_get_version");
    api->init = (libvgmstream_t*(*)(void))fsb_dyn_get_proc((void*)api->mod, "libvgmstream_init");
    api->free_lib = (void(*)(libvgmstream_t*))fsb_dyn_get_proc((void*)api->mod, "libvgmstream_free");
    api->setup = (void(*)(libvgmstream_t*, libvgmstream_config_t*))fsb_dyn_get_proc((void*)api->mod, "libvgmstream_setup");
    api->open_stream = (int(*)(libvgmstream_t*, libstreamfile_t*, int))fsb_dyn_get_proc((void*)api->mod, "libvgmstream_open_stream");
    api->close_stream = (void(*)(libvgmstream_t*))fsb_dyn_get_proc((void*)api->mod, "libvgmstream_close_stream");
    api->render = (int(*)(libvgmstream_t*))fsb_dyn_get_proc((void*)api->mod, "libvgmstream_render");
    api->set_log = (void(*)(libvgmstream_loglevel_t, void(*)(int, const char*)))fsb_dyn_get_proc((void*)api->mod, "libvgmstream_set_log");
    api->open_buffered = (libstreamfile_t*(*)(libstreamfile_t*))fsb_dyn_get_proc((void*)api->mod, "libstreamfile_open_buffered");

    if (!api->get_version || !api->init || !api->free_lib || !api->setup || !api->open_stream || !api->close_stream || !api->render)
    {
        if (want_error && !fsb_has_last_error())
            fsb_set_last_error("libvgmstream.dll missing required exports");
        return 0;
    }

    uint32_t v = api->get_version();
    if ((v & 0xFF000000u) != 0x01000000u)
    {
        if (want_error && !fsb_has_last_error())
            fsb_set_last_error("libvgmstream.dll API version mismatch: 0x%08X", (unsigned)v);
        return 0;
    }

    if (api->set_log)
        api->set_log(LIBVGMSTREAM_LOG_LEVEL_NONE, NULL);
    return 1;
}
#else
static int fsb_posix_is_file_exists(const char* path)
{
    if (!path || !path[0])
        return 0;
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    if (!S_ISREG(st.st_mode))
        return 0;
    return 1;
}

static int fsb_path_dirname(char* out, Uint32 out_size, const char* path)
{
    if (!out || out_size == 0 || !path)
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
    if (last + 1 > (size_t)out_size)
        last = (size_t)out_size - 1;
    SDL_memcpy(out, path, last);
    out[last] = 0;
    return 1;
}

static int fsb_posix_get_self_dir(char* out_dir, Uint32 out_dir_size)
{
    if (!out_dir || out_dir_size == 0)
        return 0;

    Dl_info info;
    SDL_memset(&info, 0, sizeof(info));
    if (dladdr((void*)&fsb_posix_get_self_dir, &info) == 0)
        return 0;
    if (!info.dli_fname || !info.dli_fname[0])
        return 0;
    return fsb_path_dirname(out_dir, out_dir_size, info.dli_fname);
}

static int fsb_posix_find_libvgmstream_so(char* out_path, Uint32 out_path_size)
{
    if (!out_path || out_path_size == 0)
        return 0;

    out_path[0] = '\0';

    {
        const char* envs[] = {
            "LIBVGMSTREAM_SO_PATH",
            "VGMSTREAM_SO_PATH",
#if defined(__APPLE__)
            "LIBVGMSTREAM_DYLIB_PATH",
            "VGMSTREAM_DYLIB_PATH",
#endif
        };

        for (int i = 0; i < (int)(sizeof(envs) / sizeof(envs[0])); i++)
        {
            const char* p = getenv(envs[i]);
            if (p && p[0] && fsb_posix_is_file_exists(p))
            {
                size_t n = SDL_strlen(p);
                if (n + 1 <= (size_t)out_path_size)
                {
                    SDL_memcpy(out_path, p, n + 1);
                    return 1;
                }
            }
        }
    }

    {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL)
        {
            char cand[1200];
            SDL_snprintf(cand, (int)sizeof(cand), "%s/%s", cwd, FSB_LIBVGMSTREAM_DYN_NAME);
            if (fsb_posix_is_file_exists(cand))
            {
                size_t n = SDL_strlen(cand);
                if (n + 1 <= (size_t)out_path_size)
                {
                    SDL_memcpy(out_path, cand, n + 1);
                    return 1;
                }
            }
        }
    }

    {
        char self_dir[1024];
        if (fsb_posix_get_self_dir(self_dir, (Uint32)sizeof(self_dir)))
        {
            const char* rels[] = {
                FSB_LIBVGMSTREAM_DYN_NAME,
                "vgm/" FSB_LIBVGMSTREAM_DYN_NAME,
                "lib/" FSB_LIBVGMSTREAM_DYN_NAME,
            };

            for (int i = 0; i < (int)(sizeof(rels) / sizeof(rels[0])); i++)
            {
                char cand[1400];
                SDL_snprintf(cand, (int)sizeof(cand), "%s/%s", self_dir, rels[i]);
                if (fsb_posix_is_file_exists(cand))
                {
                    size_t n = SDL_strlen(cand);
                    if (n + 1 <= (size_t)out_path_size)
                    {
                        SDL_memcpy(out_path, cand, n + 1);
                        return 1;
                    }
                }
            }

            char parent[1024];
            if (fsb_path_dirname(parent, (Uint32)sizeof(parent), self_dir))
            {
#if defined(__APPLE__) && TARGET_OS_IPHONE
                /* iOS framework layout:
                 * self_dir  = .../Frameworks/libmygxy.framework/
                 * parent    = .../Frameworks/
                 * target    = .../Frameworks/libvgmstream.framework/libvgmstream
                 */
                {
                    char cand[1400];
                    SDL_snprintf(cand, (int)sizeof(cand), "%s/%s", parent, FSB_LIBVGMSTREAM_FW_NAME);
                    if (fsb_posix_is_file_exists(cand))
                    {
                        size_t n = SDL_strlen(cand);
                        if (n + 1 <= (size_t)out_path_size)
                        {
                            SDL_memcpy(out_path, cand, n + 1);
                            return 1;
                        }
                    }
                }
#endif
                for (int i = 0; i < (int)(sizeof(rels) / sizeof(rels[0])); i++)
                {
                    char cand[1400];
                    SDL_snprintf(cand, (int)sizeof(cand), "%s/%s", parent, rels[i]);
                    if (fsb_posix_is_file_exists(cand))
                    {
                        size_t n = SDL_strlen(cand);
                        if (n + 1 <= (size_t)out_path_size)
                        {
                            SDL_memcpy(out_path, cand, n + 1);
                            return 1;
                        }
                    }
                }
            }
        }
    }

    return 0;
}

static void* fsb_dyn_load_library(const char* path)
{
    if (!path || !path[0])
        return NULL;
    return dlopen(path, RTLD_NOW);
}

static void* fsb_dyn_get_proc(void* mod, const char* name)
{
    if (!mod || !name || !name[0])
        return NULL;
    return dlsym(mod, name);
}

static int fsb_posix_load_libvgmstream(fsb_libvgmstream_api* api, int want_error)
{
    if (!api)
        return 0;
    if (api->loaded)
        return api->mod != NULL;

    api->loaded = 1;
    api->mod = NULL;

    char so_path[1024];
    if (fsb_posix_find_libvgmstream_so(so_path, (Uint32)sizeof(so_path)))
        api->mod = fsb_dyn_load_library(so_path);
    else
        api->mod = fsb_dyn_load_library(FSB_LIBVGMSTREAM_DYN_NAME);

    if (!api->mod)
    {
        if (want_error && !fsb_has_last_error())
            fsb_set_last_error("%s not found", FSB_LIBVGMSTREAM_DYN_NAME);
        return 0;
    }

    api->get_version = (uint32_t(*)(void))fsb_dyn_get_proc((void*)api->mod, "libvgmstream_get_version");
    api->init = (libvgmstream_t*(*)(void))fsb_dyn_get_proc((void*)api->mod, "libvgmstream_init");
    api->free_lib = (void(*)(libvgmstream_t*))fsb_dyn_get_proc((void*)api->mod, "libvgmstream_free");
    api->setup = (void(*)(libvgmstream_t*, libvgmstream_config_t*))fsb_dyn_get_proc((void*)api->mod, "libvgmstream_setup");
    api->open_stream = (int(*)(libvgmstream_t*, libstreamfile_t*, int))fsb_dyn_get_proc((void*)api->mod, "libvgmstream_open_stream");
    api->close_stream = (void(*)(libvgmstream_t*))fsb_dyn_get_proc((void*)api->mod, "libvgmstream_close_stream");
    api->render = (int(*)(libvgmstream_t*))fsb_dyn_get_proc((void*)api->mod, "libvgmstream_render");
    api->set_log = (void(*)(libvgmstream_loglevel_t, void(*)(int, const char*)))fsb_dyn_get_proc((void*)api->mod, "libvgmstream_set_log");
    api->open_buffered = (libstreamfile_t*(*)(libstreamfile_t*))fsb_dyn_get_proc((void*)api->mod, "libstreamfile_open_buffered");

    if (!api->get_version || !api->init || !api->free_lib || !api->setup || !api->open_stream || !api->close_stream || !api->render)
    {
        if (want_error && !fsb_has_last_error())
            fsb_set_last_error("%s missing required exports", FSB_LIBVGMSTREAM_DYN_NAME);
        return 0;
    }

    uint32_t v = api->get_version();
    if ((v & 0xFF000000u) != 0x01000000u)
    {
        if (want_error && !fsb_has_last_error())
            fsb_set_last_error("%s API version mismatch: 0x%08X", FSB_LIBVGMSTREAM_DYN_NAME, (unsigned)v);
        return 0;
    }

    if (api->set_log)
        api->set_log(LIBVGMSTREAM_LOG_LEVEL_NONE, NULL);
    return 1;
}
#endif

static int fsb_load_libvgmstream(fsb_libvgmstream_api* api, int want_error)
{
#if defined(_WIN32)
    return fsb_win32_load_libvgmstream(api, want_error);
#else
    return fsb_posix_load_libvgmstream(api, want_error);
#endif
}

static void fsb_close_libvgmstream(libvgmstream_t* lib, libstreamfile_t* sf, libstreamfile_t* memsf, int sf_owns_memsf)
{
    if (sf)
        libstreamfile_close(sf);
    if (!sf_owns_memsf && memsf && sf != memsf)
        libstreamfile_close(memsf);
    if (lib)
        g_fsb_vgm_dll.free_lib(lib);
}

static int fsb_decode_libvgmstream_to_wav(lua_State* L, const Uint8* fsb_data, Uint32 fsb_size, Uint32 subsong_1based, int want_error)
{
    if (!L || !fsb_data || fsb_size == 0 || subsong_1based == 0)
        return 0;

    if (!fsb_load_libvgmstream(&g_fsb_vgm_dll, want_error))
        return 0;

    libstreamfile_t* memsf = fsb_memsf_create((const uint8_t*)fsb_data, (int64_t)fsb_size, "memory.fsb");
    if (!memsf)
    {
        if (want_error && !fsb_has_last_error())
            fsb_set_last_error("out of memory");
        return 0;
    }

    libstreamfile_t* sf = memsf;
    int sf_owns_memsf = 0;

    libvgmstream_t* lib = g_fsb_vgm_dll.init();
    if (!lib)
    {
        fsb_close_libvgmstream(NULL, sf, memsf, sf_owns_memsf);
        if (want_error && !fsb_has_last_error())
            fsb_set_last_error("libvgmstream_init failed");
        return 0;
    }

    libvgmstream_config_t cfg;
    SDL_memset(&cfg, 0, sizeof(cfg));
    cfg.ignore_loop = true;
    cfg.ignore_fade = true;
    cfg.loop_count = 1.0;
    cfg.fade_time = 0.0;
    cfg.fade_delay = 0.0;
    cfg.force_sfmt = LIBVGMSTREAM_SFMT_PCM16;
    g_fsb_vgm_dll.setup(lib, &cfg);

    int open_rc = g_fsb_vgm_dll.open_stream(lib, sf, (int)subsong_1based);
    if (open_rc < 0 || !lib->format || !lib->decoder)
    {
        fsb_close_libvgmstream(lib, sf, memsf, sf_owns_memsf);
        if (want_error && !fsb_has_last_error())
            fsb_set_last_error("libvgmstream_open_stream failed (subsong=%u)", (unsigned)subsong_1based);
        return 0;
    }

    if (lib->format->play_forever)
    {
        fsb_close_libvgmstream(lib, sf, memsf, sf_owns_memsf);
        if (want_error && !fsb_has_last_error())
            fsb_set_last_error("libvgmstream stream is play_forever (subsong=%u)", (unsigned)subsong_1based);
        return 0;
    }

    if (lib->format->sample_format != LIBVGMSTREAM_SFMT_PCM16)
    {
        fsb_close_libvgmstream(lib, sf, memsf, sf_owns_memsf);
        if (want_error && !fsb_has_last_error())
            fsb_set_last_error("libvgmstream output format not PCM16");
        return 0;
    }

    Uint8* pcm = NULL;
    size_t pcm_len = 0;
    size_t pcm_cap = 0;

    for (;;)
    {
        if (pcm_len > (size_t)(256u * 1024u * 1024u))
        {
            SDL_free(pcm);
            fsb_close_libvgmstream(lib, sf, memsf, sf_owns_memsf);
            if (want_error && !fsb_has_last_error())
                fsb_set_last_error("decoded wav too large");
            return 0;
        }

        int rc = g_fsb_vgm_dll.render(lib);
        if (rc < 0)
        {
            SDL_free(pcm);
            fsb_close_libvgmstream(lib, sf, memsf, sf_owns_memsf);
            if (want_error && !fsb_has_last_error())
                fsb_set_last_error("libvgmstream_render failed");
            return 0;
        }

        int bytes = lib->decoder->buf_bytes;
        if (bytes > 0 && lib->decoder->buf)
        {
            size_t need = pcm_len + (size_t)bytes;
            if (need > pcm_cap)
            {
                size_t new_cap = pcm_cap ? pcm_cap : 64 * 1024;
                while (new_cap < need)
                {
                    size_t next = new_cap * 2;
                    if (next <= new_cap)
                    {
                        SDL_free(pcm);
                        fsb_close_libvgmstream(lib, sf, memsf, sf_owns_memsf);
                        if (want_error && !fsb_has_last_error())
                            fsb_set_last_error("pcm buffer overflow");
                        return 0;
                    }
                    new_cap = next;
                }
                void* p = SDL_realloc(pcm, new_cap);
                if (!p)
                {
                    SDL_free(pcm);
                    fsb_close_libvgmstream(lib, sf, memsf, sf_owns_memsf);
                    if (want_error && !fsb_has_last_error())
                        fsb_set_last_error("out of memory");
                    return 0;
                }
                pcm = (Uint8*)p;
                pcm_cap = new_cap;
            }

            memcpy(pcm + pcm_len, lib->decoder->buf, (size_t)bytes);
            pcm_len += (size_t)bytes;
        }

        if (lib->decoder->done)
            break;
    }

    int ok = fsb_lua_push_wav(L, 0x0001u, 16u, (Uint32)lib->format->channels, (Uint32)lib->format->sample_rate, pcm ? pcm : "", (Uint32)pcm_len);
    SDL_free(pcm);
    fsb_close_libvgmstream(lib, sf, memsf, sf_owns_memsf);

    if (!ok)
        return 0;
    return 1;
}

static int fsb_decode_vgmstream_to_wav(lua_State* L, const Uint8* fsb_data, Uint32 fsb_size, Uint32 subsong_1based)
{
    return fsb_decode_libvgmstream_to_wav(L, fsb_data, fsb_size, subsong_1based, 1);
}

#if defined(_WIN32)
static int fsb_write_win32_file(void* ctx, const void* data, Uint32 size)
{
    if (!ctx || !data)
        return 0;
    HANDLE h = (HANDLE)ctx;
    const Uint8* p = (const Uint8*)data;
    Uint32 remain = size;
    while (remain)
    {
        DWORD toWrite = remain;
        if (toWrite > 1u * 1024u * 1024u)
            toWrite = 1u * 1024u * 1024u;
        DWORD written = 0;
        if (!WriteFile(h, p, toWrite, &written, NULL))
            return 0;
        if (written == 0)
            return 0;
        p += (Uint32)written;
        remain -= (Uint32)written;
    }
    return 1;
}
#endif

static int FSB_Create_FSB4(lua_State* L, Uint8* data, size_t len)
{
    if (!L || !data)
        return 0;
    if (len < sizeof(FSOUND_FSB_HEADER_FSB4))
        return 0;

    FSOUND_FSB_HEADER_FSB4* head = (FSOUND_FSB_HEADER_FSB4*)data;
    if (head->id != 0x34425346u) /* '4BSF' */
        return 0;
    if (head->numsamples <= 0 || head->numsamples > 1000000)
    {
        fsb_set_last_error("fsb4 invalid numsamples=%d", (int)head->numsamples);
        return 0;
    }
    if (head->shdrsize <= 0 || head->datasize < 0)
    {
        fsb_set_last_error("fsb4 invalid header sizes (shdr=%d,data=%d)", (int)head->shdrsize, (int)head->datasize);
        return 0;
    }

    const Uint64 headerSize = (Uint64)sizeof(FSOUND_FSB_HEADER_FSB4);
    const Uint64 shdrSize = (Uint64)(Uint32)head->shdrsize;
    if (headerSize + shdrSize > (Uint64)len)
    {
        fsb_set_last_error("fsb4 header overflow");
        return 0;
    }

    const Uint64 minHdrBytes = (Uint64)head->numsamples * (Uint64)sizeof(FSOUND_FSB_SAMPLE_HEADER_3_1);
    if (shdrSize < minHdrBytes)
    {
        fsb_set_last_error("fsb4 sample headers too small");
        return 0;
    }

    int num = head->numsamples;

    FSB_UserData* ud = (FSB_UserData*)lua_newuserdata(L, sizeof(FSB_UserData));
    SDL_memset(ud, 0, sizeof(FSB_UserData));
    ud->num = head->numsamples;
    ud->mode = head->mode;
    ud->fsb_ver = 4;
    ud->data = (Uint8*)SDL_malloc(len);
    ud->len = (Uint32)len;
    if (!ud->data)
    {
        fsb_set_last_error("out of memory");
        return luaL_error(L, "out of memory");
    }
    SDL_memcpy(ud->data, data, len);
    data = ud->data;
    luaL_setmetatable(L, "xyq_fsb");

    data += sizeof(FSOUND_FSB_HEADER_FSB4);
    ud->info = (FSOUND_FSB_SAMPLE_HEADER_3_1*)data;
    ud->list = (Uint32*)SDL_malloc(num * sizeof(Uint32));
    if (!ud->list)
    {
        fsb_set_last_error("out of memory");
        return luaL_error(L, "out of memory");
    }
    lua_createtable(L, 0, num);
    lua_createtable(L, num, 0);

    Uint64 offset64 = headerSize + shdrSize;
    for (int i = 0; i < num; i++)
    {
        Uint32 comp = (Uint32)ud->info[i].lengthcompressedbytes;
        if (offset64 + (Uint64)comp > (Uint64)len)
        {
            fsb_set_last_error("fsb4 sample data overflow (i=%d)", i);
            return 0;
        }

        ud->list[i] = (Uint32)offset64;
        offset64 += (Uint64)comp;
        lua_pushinteger(L, i + 1);
        lua_setfield(L, -3, ud->info[i].name);
        lua_pushstring(L, ud->info[i].name);
        lua_rawseti(L, -2, i + 1);
    }
    return 3;
}

static int FSB_Create_FSB5(lua_State* L, Uint8* data, size_t len)
{
    if (len < 60)
        return 0;
    if (SDL_memcmp(data, "FSB5", 4) != 0)
        return 0;

    Uint32 version = fsb_read_u32le(data + 4);
    Uint32 numsamples = fsb_read_u32le(data + 8);
    Uint32 sampleHeadersSize = fsb_read_u32le(data + 12);
    Uint32 nameTableSize = fsb_read_u32le(data + 16);
    Uint32 dataSize = fsb_read_u32le(data + 20);
    Uint32 mode = fsb_read_u32le(data + 24);

    if (numsamples > 1000000u)
        return luaL_error(L, "fsb5 numsamples too large");

    Uint32 headerSize = 60;
    if (version == 0)
    {
        if (len < 64)
            return 0;
        headerSize = 64;
    }

    Uint64 headersEnd64 = (Uint64)headerSize + (Uint64)sampleHeadersSize + (Uint64)nameTableSize;
    Uint64 dataEnd64 = headersEnd64 + (Uint64)dataSize;
    if (headersEnd64 > len)
        return 0;
    if (dataEnd64 > len)
        dataEnd64 = (Uint64)len;

    FSB_UserData* ud = (FSB_UserData*)lua_newuserdata(L, sizeof(FSB_UserData));
    SDL_memset(ud, 0, sizeof(FSB_UserData));
    ud->num = numsamples;
    ud->mode = mode;
    ud->fsb_ver = 5;
    ud->data_offset_base = headerSize + sampleHeadersSize + nameTableSize;
    ud->data_size = dataSize;
    ud->data = (Uint8*)SDL_malloc(len);
    ud->len = (Uint32)len;
    if (!ud->data)
    {
        fsb_set_last_error("out of memory");
        return luaL_error(L, "out of memory");
    }
    SDL_memcpy(ud->data, data, len);
    data = ud->data;
    luaL_setmetatable(L, "xyq_fsb");

    if (numsamples == 0)
    {
        lua_createtable(L, 0, 0);
        return 2;
    }

    ud->samples5 = (FSB5_Sample*)SDL_calloc(numsamples, sizeof(FSB5_Sample));
    if (!ud->samples5)
        return luaL_error(L, "out of memory");

    Uint64 pos = headerSize;
    Uint64 sampleHeadersEnd = (Uint64)headerSize + (Uint64)sampleHeadersSize;
    for (Uint32 i = 0; i < numsamples; i++)
    {
        if (pos + 8 > sampleHeadersEnd)
            return luaL_error(L, "fsb5 sample header overflow");
        Uint64 raw = fsb_read_u64le(data + pos);
        pos += 8;

        Uint32 next_chunk = (Uint32)fsb_bits_u64(raw, 0, 1);
        Uint32 freq_code = (Uint32)fsb_bits_u64(raw, 1, 4);
        Uint32 channels = (Uint32)fsb_bits_u64(raw, 1 + 4, 1) + 1;
        Uint32 dataOffset = (Uint32)fsb_bits_u64(raw, 1 + 4 + 1, 28) * 16u;
        Uint32 samples = (Uint32)fsb_bits_u64(raw, 1 + 4 + 1 + 28, 30);

        Uint32 frequency = fsb5_frequency_from_code(freq_code);
        while (next_chunk)
        {
            if (pos + 4 > sampleHeadersEnd)
                return luaL_error(L, "fsb5 metadata overflow");
            Uint32 mraw = fsb_read_u32le(data + pos);
            pos += 4;
            next_chunk = fsb_bits_u32(mraw, 0, 1);
            Uint32 chunk_size = fsb_bits_u32(mraw, 1, 24);
            Uint32 chunk_type = fsb_bits_u32(mraw, 1 + 24, 7);
            if (pos + chunk_size > sampleHeadersEnd)
                return luaL_error(L, "fsb5 metadata size overflow");

            if (chunk_type == 1 && chunk_size == 1)
            {
                channels = data[pos];
            }
            else if (chunk_type == 2 && chunk_size == 4)
            {
                frequency = fsb_read_u32le(data + pos);
            }
            else if (chunk_type == 11 && chunk_size >= 4)
            {
                ud->samples5[i].vorbis_crc32 = fsb_read_u32le(data + pos);
            }
            pos += chunk_size;
        }

        ud->samples5[i].dataOffset = dataOffset;
        ud->samples5[i].channels = (Uint8)channels;
        ud->samples5[i].frequency = frequency;
        ud->samples5[i].samples = samples;
    }

    if (pos < sampleHeadersEnd)
        pos = sampleHeadersEnd;
    if (pos + nameTableSize > (Uint64)len)
        return luaL_error(L, "fsb5 name table overflow");

    Uint64 nameTableStart = sampleHeadersEnd;
    Uint64 nameTableEnd = nameTableStart + (Uint64)nameTableSize;

    Uint64 poolSize64 = 0;
    if (nameTableSize)
    {
        Uint64 offsetsSize = (Uint64)numsamples * 4ull;
        if (nameTableStart + offsetsSize > nameTableEnd)
            return luaL_error(L, "fsb5 name offsets overflow");

        for (Uint32 i = 0; i < numsamples; i++)
        {
            Uint32 off = fsb_read_u32le(data + nameTableStart + (Uint64)i * 4ull);
            Uint64 p = nameTableStart + (Uint64)off;
            Uint32 n = 0;
            if (p < nameTableEnd)
            {
                while (p + n < nameTableEnd && data[p + n] != 0)
                    n++;
            }
            poolSize64 += (Uint64)n + 1ull;
            if (poolSize64 > (Uint64)((size_t)-1))
                return luaL_error(L, "fsb5 name pool overflow");
        }
    }
    else
    {
        poolSize64 = (Uint64)numsamples * 5ull;
        if (poolSize64 > (Uint64)((size_t)-1))
            return luaL_error(L, "fsb5 name pool overflow");
    }

    size_t poolSize = (size_t)poolSize64;
    ud->names5 = (char*)SDL_malloc(poolSize ? poolSize : 1);
    if (!ud->names5)
        return luaL_error(L, "out of memory");

    Uint32 poolPos = 0;
    if (nameTableSize)
    {
        for (Uint32 i = 0; i < numsamples; i++)
        {
            Uint32 off = fsb_read_u32le(data + nameTableStart + (Uint64)i * 4ull);
            Uint64 p = nameTableStart + (Uint64)off;
            Uint32 n = 0;
            if (p < nameTableEnd)
            {
                while (p + n < nameTableEnd && data[p + n] != 0)
                    n++;
            }

            ud->samples5[i].name = ud->names5 + poolPos;
            if (n)
                SDL_memcpy(ud->samples5[i].name, data + p, n);
            ud->samples5[i].name[n] = '\0';
            poolPos += n + 1;
        }
    }
    else
    {
        for (Uint32 i = 0; i < numsamples; i++)
        {
            ud->samples5[i].name = ud->names5 + poolPos;
            SDL_snprintf(ud->samples5[i].name, 5, "%04u", (unsigned)i);
            poolPos += 5;
        }
    }

    Uint64 dataBase = (Uint64)ud->data_offset_base;
    Uint64 dataLimit = dataBase + (Uint64)dataSize;
    if (dataLimit > (Uint64)len)
        dataLimit = (Uint64)len;

    for (Uint32 i = 0; i < numsamples; i++)
    {
        Uint64 start = dataBase + (Uint64)ud->samples5[i].dataOffset;
        Uint64 end = dataLimit;
        if (i + 1 < numsamples)
            end = dataBase + (Uint64)ud->samples5[i + 1].dataOffset;
        if (end > dataLimit)
            end = dataLimit;
        if (start > end)
            start = end;
        ud->samples5[i].dataSize = (Uint32)(end - start);
    }

    lua_createtable(L, 0, (int)numsamples);
    lua_createtable(L, (int)numsamples, 0);
    for (Uint32 i = 0; i < numsamples; i++)
    {
        const char* name = ud->samples5[i].name ? ud->samples5[i].name : "";
        lua_pushinteger(L, (lua_Integer)i + 1);
        lua_setfield(L, -3, name);
        lua_pushstring(L, name);
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    return 3;
}

static int FSB_Create_Raw(lua_State* L, Uint8* data, size_t len)
{
    if (!L || !data || len == 0)
        return 0;

    FSB_UserData* ud = (FSB_UserData*)lua_newuserdata(L, sizeof(FSB_UserData));
    SDL_memset(ud, 0, sizeof(FSB_UserData));
    ud->num = 1;
    ud->mode = 0;
    ud->fsb_ver = 0;
    ud->data = (Uint8*)SDL_malloc(len);
    ud->len = (Uint32)len;
    if (!ud->data)
        return luaL_error(L, "out of memory");
    SDL_memcpy(ud->data, data, len);
    luaL_setmetatable(L, "xyq_fsb");

    lua_createtable(L, 0, 1);
    lua_createtable(L, 1, 0);
    lua_pushinteger(L, 1);
    lua_setfield(L, -3, "raw");
    lua_pushstring(L, "raw");
    lua_rawseti(L, -2, 1);
    return 3;
}

int FSB_Create(lua_State* L, Uint8* data, size_t len)
{
    if (len >= 4 && SDL_memcmp(data, "FSB5", 4) == 0)
        return FSB_Create_FSB5(L, data, len);
    return FSB_Create_FSB4(L, data, len);
}

static int FSB_Get(lua_State* L)
{
    FSB_UserData* ud = (FSB_UserData*)luaL_checkudata(L, 1, "xyq_fsb");
    Uint32 i = (Uint32)luaL_checkinteger(L, 2) - 1;
    if (i >= ud->num)
        return luaL_error(L, "id error!");

    fsb_set_last_error("");

    if (ud->fsb_ver == 0)
    {
        const Uint8* data = (const Uint8*)ud->data;
        Uint32 size = ud->len;
        const char* sniff_ext = fsb_guess_ext(data, size);
        if (sniff_ext[0] != 'b' || sniff_ext[1] != 'i' || sniff_ext[2] != 'n' || sniff_ext[3] != '\0')
        {
            lua_pushlstring(L, (const char*)data, size);
            lua_pushstring(L, sniff_ext);
            return 2;
        }

        lua_pushlstring(L, (const char*)data, size);
        lua_pushstring(L, "bin");
        return 2;
    }

    if (ud->fsb_ver == 5)
    {
        const Uint8* fsb_data = (const Uint8*)ud->data;
        Uint32 fsb_size = ud->len;

        if (fsb_decode_vgmstream_to_wav(L, fsb_data, fsb_size, i + 1u))
        {
            lua_pushstring(L, "wav");
            return 2;
        }

        FSB5_Sample* s = &ud->samples5[i];
        Uint32 dataStart = ud->data_offset_base + s->dataOffset;
        Uint32 size = s->dataSize;
        if ((Uint64)dataStart + (Uint64)size > ud->len)
        {
            if (dataStart >= ud->len)
                size = 0;
            else
                size = ud->len - dataStart;
        }
        const Uint8* data = (const Uint8*)ud->data + dataStart;

        const char* sniff_ext = fsb_guess_ext(data, size);
        if (sniff_ext[0] != 'b' || sniff_ext[1] != 'i' || sniff_ext[2] != 'n' || sniff_ext[3] != '\0')
        {
            lua_pushlstring(L, (const char*)data, size);
            lua_pushstring(L, sniff_ext);
            return 2;
        }

        Uint32 format = fsb5_format_from_mode(ud->mode);
        if (format == (Uint32)FMOD_SOUND_FORMAT_MPEG)
        {
            lua_pushlstring(L, (const char*)data, size);
            lua_pushstring(L, "mp3");
            return 2;
        }

        if (format == (Uint32)FMOD_SOUND_FORMAT_VORBIS)
        {
            lua_pushlstring(L, (const char*)data, size);
            lua_pushstring(L, "vorbis");
            return 2;
        }

        lua_pushlstring(L, (const char*)data, size);
        lua_pushstring(L, "bin");
        return 2;
    }

    const Uint8* fsb_data = (const Uint8*)ud->data;
    Uint32 fsb_size = ud->len;

    if (fsb_decode_vgmstream_to_wav(L, fsb_data, fsb_size, i + 1u))
    {
        lua_pushstring(L, "wav");
        return 2;
    }

    FSOUND_FSB_SAMPLE_HEADER_3_1* info = &ud->info[i];
    const Uint8* data = (const Uint8*)ud->data + ud->list[i];
    Uint32 size = (Uint32)info->lengthcompressedbytes;

    const char* sniff_ext = fsb_guess_ext(data, size);
    if (sniff_ext[0] != 'b' || sniff_ext[1] != 'i' || sniff_ext[2] != 'n' || sniff_ext[3] != '\0')
    {
        lua_pushlstring(L, (const char*)data, size);
        lua_pushstring(L, sniff_ext);
        return 2;
    }

    int mode = info->mode;
    int chans = info->numchannels;
    int freq = info->deffreq;
    int bits = (mode & FSOUND_8BITS) ? 8 : 16;
    if (mode & FSOUND_STEREO)
        chans = 2;

    if (mode & FSOUND_DELTA)
    {
        lua_pushlstring(L, (const char*)data, size);
        lua_pushstring(L, "mp3");
        return 2;
    }

    if (!fsb_lua_push_wav(L, 0x0001u, (Uint16)bits, (Uint32)chans, (Uint32)freq, data, size))
        return 0;
    lua_pushstring(L, "wav");
    return 2;
}

#if defined(_WIN32)
static int FSB_Save(lua_State* L)
{
    FSB_UserData* ud = (FSB_UserData*)luaL_checkudata(L, 1, "xyq_fsb");
    Uint32 i = (Uint32)luaL_checkinteger(L, 2) - 1;
    size_t path_len = 0;
    const char* out_path = luaL_checklstring(L, 3, &path_len);
    if (i >= ud->num)
        return luaL_error(L, "id error!");
    if (!out_path || path_len == 0 || path_len >= 4096)
        return luaL_error(L, "path error!");

    char path[4096];
    SDL_memcpy(path, out_path, path_len);
    path[path_len] = '\0';

    HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        lua_pushnil(L);
        lua_pushstring(L, "open failed");
        return 2;
    }

    const char* ext = "bin";
    char ext_buf[16];
    SDL_snprintf(ext_buf, (int)sizeof(ext_buf), "%s", ext);
    int ok = 0;

    lua_settop(L, 2);
    if (FSB_Get(L) != 2)
    {
        lua_pushnil(L);
        if (fsb_has_last_error())
            lua_pushstring(L, g_fsb_last_error);
        else
            lua_pushstring(L, "get failed");
        CloseHandle(h);
        return 2;
    }

    size_t data_len = 0;
    const char* out_data = lua_tolstring(L, -2, &data_len);
    const char* out_ext = lua_tostring(L, -1);
    if (!out_data || !out_ext)
    {
        lua_pop(L, 2);
        lua_pushnil(L);
        lua_pushstring(L, "get invalid");
        CloseHandle(h);
        return 2;
    }

    SDL_snprintf(ext_buf, (int)sizeof(ext_buf), "%s", out_ext);
    ext = ext_buf;
    ok = fsb_write_win32_file(h, out_data, (Uint32)data_len);
    lua_pop(L, 2);

    CloseHandle(h);
    if (!ok)
    {
        lua_pushnil(L);
        lua_pushstring(L, "write failed");
        return 2;
    }

    lua_pushboolean(L, 1);
    lua_pushstring(L, ext);
    return 2;
}
#endif

static int FSB_LastError(lua_State* L)
{
    (void)luaL_checkudata(L, 1, "xyq_fsb");
    lua_pushstring(L, g_fsb_last_error);
    return 1;
}

static int FSB_NEW(lua_State* L)
{
    size_t len;
    const char* s = luaL_checklstring(L, 1, &len);
    if (len >= 4 && (SDL_memcmp(s, "FSB4", 4) == 0 || SDL_memcmp(s, "FSB5", 4) == 0))
        return FSB_Create(L, (Uint8*)s, len);

    if (len > 0 && len < 4096)
    {
        const void* nul = memchr(s, 0, len);
        if (!nul)
        {
            char path[4096];
            SDL_memcpy(path, s, len);
            path[len] = '\0';
            FILE* fp = fopen(path, "rb");
            if (fp)
            {
                if (fseek(fp, 0, SEEK_END) == 0)
                {
                    long endPos = ftell(fp);
                    if (endPos > 0 && (size_t)endPos < (size_t)0x7FFFFFFF)
                    {
                        size_t fileSize = (size_t)endPos;
                        if (fseek(fp, 0, SEEK_SET) == 0)
                        {
                            void* buf = SDL_malloc(fileSize);
                            if (buf)
                            {
                                size_t readBytes = fread(buf, 1, fileSize, fp);
                                if (readBytes == fileSize)
                                {
                                    fclose(fp);
                                    int ret = FSB_Create(L, (Uint8*)buf, fileSize);
                                    if (ret == 0)
                                        ret = FSB_Create_Raw(L, (Uint8*)buf, fileSize);
                                    SDL_free(buf);
                                    return ret;
                                }
                                SDL_free(buf);
                            }
                        }
                    }
                }
                fclose(fp);
            }
        }
    }

    {
        int ret = FSB_Create(L, (Uint8*)s, len);
        if (ret == 0)
            ret = FSB_Create_Raw(L, (Uint8*)s, len);
        return ret;
    }
}

static int FSB_GC(lua_State* L)
{
    FSB_UserData* ud = (FSB_UserData*)luaL_checkudata(L, 1, "xyq_fsb");
    SDL_free(ud->data);
    SDL_free(ud->list);
    SDL_free(ud->samples5);
    SDL_free(ud->names5);
    return 0;
}

static const luaL_Reg tcp_funs[] = {
    {"__gc",FSB_GC},
    {"__close",FSB_GC},
    {"Get",FSB_Get},
    {"LastError",FSB_LastError},

#if defined(_WIN32)
    {"Save",FSB_Save},
#endif

    {NULL,NULL},
};

static int FSB_Open(lua_State* L)
{
    luaL_newmetatable(L, "xyq_fsb");
    luaL_setfuncs(L, tcp_funs, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    lua_pushcfunction(L, FSB_NEW);
    return 1;
}

MYGXY_API int luaopen_mygxy_fsb(lua_State* L)
{
    return FSB_Open(L);
}
