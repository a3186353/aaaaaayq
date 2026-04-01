#include "sdl_proxy.h"
#if defined(__ANDROID__)
#include <dlfcn.h>
#include <stdio.h>
#if defined(__ANDROID__)
#include <android/log.h>
#define LOG_TAG "MYGXY_SDL_PROXY"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGE(...) printf(__VA_ARGS__)
#endif

#define SDL_PROXY_SYMBOLS(X) \
    X(SDL_GetError, PFN_SDL_GetError) \
    X(SDL_Error, PFN_SDL_Error) \
    X(SDL_CreateMutex, PFN_SDL_CreateMutex) \
    X(SDL_LockMutex, PFN_SDL_LockMutex) \
    X(SDL_UnlockMutex, PFN_SDL_UnlockMutex) \
    X(SDL_DestroyMutex, PFN_SDL_DestroyMutex) \
    X(SDL_CreateCond, PFN_SDL_CreateCond) \
    X(SDL_DestroyCond, PFN_SDL_DestroyCond) \
    X(SDL_CondSignal, PFN_SDL_CondSignal) \
    X(SDL_CondWait, PFN_SDL_CondWait) \
    X(SDL_RWFromFile, PFN_SDL_RWFromFile) \
    X(SDL_RWFromMem, PFN_SDL_RWFromMem) \
    X(SDL_RWseek, PFN_SDL_RWseek) \
    X(SDL_RWtell, PFN_SDL_RWtell) \
    X(SDL_RWread, PFN_SDL_RWread) \
    X(SDL_RWwrite, PFN_SDL_RWwrite) \
    X(SDL_RWclose, PFN_SDL_RWclose) \
    X(SDL_CreateRGBSurfaceWithFormat, PFN_SDL_CreateRGBSurfaceWithFormat) \
    X(SDL_FreeSurface, PFN_SDL_FreeSurface) \
    X(SDL_LockSurface, PFN_SDL_LockSurface) \
    X(SDL_UnlockSurface, PFN_SDL_UnlockSurface) \
    X(SDL_SetSurfaceBlendMode, PFN_SDL_SetSurfaceBlendMode) \
    X(SDL_DuplicateSurface, PFN_SDL_DuplicateSurface) \
    X(SDL_ConvertSurfaceFormat, PFN_SDL_ConvertSurfaceFormat) \
    X(SDL_PremultiplyAlpha, PFN_SDL_PremultiplyAlpha) \
    X(SDL_FillRect, PFN_SDL_FillRect) \
    X(SDL_UpperBlit, PFN_SDL_UpperBlit) \
    X(SDL_Delay, PFN_SDL_Delay) \
    X(SDL_AddTimer, PFN_SDL_AddTimer) \
    X(IMG_Load_RW, PFN_IMG_Load_RW) \

#define DECLARE_SDL_PROXY(name, type) type proxy_##name = NULL;
SDL_PROXY_SYMBOLS(DECLARE_SDL_PROXY)


#if defined(_MSC_VER)
#pragma section(".CRT$XCU",read)
static void __cdecl _init_sdl_proxy(void);
__declspec(allocate(".CRT$XCU")) void (__cdecl *_init_sdl_proxy_) (void) = _init_sdl_proxy;
static void __cdecl _init_sdl_proxy(void) { init_sdl_proxy(); }
#else
__attribute__((constructor))
#endif
void init_sdl_proxy(void) {
    // Try RTLD_DEFAULT first to find symbols globally exported by the engine
    void* handle = RTLD_DEFAULT;
    
    // Fallback to specific shared libraries if global search fails
    void* gsdl_handle = NULL;
    void* sdl_handle = NULL;
#if defined(__ANDROID__)
    gsdl_handle = dlopen("libgsdl2.so", RTLD_LAZY | RTLD_GLOBAL);
    sdl_handle = dlopen("libSDL2.so", RTLD_LAZY | RTLD_GLOBAL);
#elif defined(__APPLE__)
    // iOS uses .framework bundles — names must match CMake OUTPUT_NAME exactly
    // gsdl2_fw → "libgsdl2" → libgsdl2.framework/libgsdl2
    // SDL2_fw  → "SDL2"     → SDL2.framework/SDL2
    gsdl_handle = dlopen("libgsdl2.framework/libgsdl2", RTLD_LAZY | RTLD_GLOBAL);
    if (!gsdl_handle) gsdl_handle = dlopen("libgsdl2", RTLD_LAZY | RTLD_GLOBAL);
    sdl_handle = dlopen("SDL2.framework/SDL2", RTLD_LAZY | RTLD_GLOBAL);
    if (!sdl_handle) sdl_handle = dlopen("SDL2", RTLD_LAZY | RTLD_GLOBAL);
#endif
    
    #define RESOLVE_SDL_PROXY(name, type) \
        proxy_##name = (type)dlsym(handle, #name); \
        if (!proxy_##name && gsdl_handle) proxy_##name = (type)dlsym(gsdl_handle, #name); \
        if (!proxy_##name && sdl_handle) proxy_##name = (type)dlsym(sdl_handle, #name); \
        if (!proxy_##name) LOGE("Failed to resolve SDL symbol: " #name "\n");

    SDL_PROXY_SYMBOLS(RESOLVE_SDL_PROXY)
}

#endif // Android Proxy Guards
