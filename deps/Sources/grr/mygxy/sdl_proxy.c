#include "sdl_proxy.h"
#if defined(MYGXY_USE_DYNAMIC_PROXY)
#include <dlfcn.h>
#include <stdio.h>
#if defined(__ANDROID__)
#include <android/log.h>
#define LOG_TAG "MYGXY_SDL_PROXY"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGE(...) printf(__VA_ARGS__)
#endif

PFN_SDL_GetError proxy_SDL_GetError = NULL;
PFN_SDL_Error proxy_SDL_Error = NULL;
PFN_SDL_CreateMutex proxy_SDL_CreateMutex = NULL;
PFN_SDL_LockMutex proxy_SDL_LockMutex = NULL;
PFN_SDL_UnlockMutex proxy_SDL_UnlockMutex = NULL;
PFN_SDL_DestroyMutex proxy_SDL_DestroyMutex = NULL;
PFN_SDL_RWFromFile proxy_SDL_RWFromFile = NULL;
PFN_SDL_RWFromMem proxy_SDL_RWFromMem = NULL;
PFN_SDL_RWseek proxy_SDL_RWseek = NULL;
PFN_SDL_RWtell proxy_SDL_RWtell = NULL;
PFN_SDL_RWread proxy_SDL_RWread = NULL;
PFN_SDL_RWwrite proxy_SDL_RWwrite = NULL;
PFN_SDL_RWclose proxy_SDL_RWclose = NULL;
PFN_SDL_CreateRGBSurfaceWithFormat proxy_SDL_CreateRGBSurfaceWithFormat = NULL;
PFN_SDL_FreeSurface proxy_SDL_FreeSurface = NULL;
PFN_SDL_LockSurface proxy_SDL_LockSurface = NULL;
PFN_SDL_UnlockSurface proxy_SDL_UnlockSurface = NULL;
PFN_SDL_SetSurfaceBlendMode proxy_SDL_SetSurfaceBlendMode = NULL;
PFN_SDL_DuplicateSurface proxy_SDL_DuplicateSurface = NULL;
PFN_SDL_ConvertSurfaceFormat proxy_SDL_ConvertSurfaceFormat = NULL;
PFN_SDL_PremultiplyAlpha proxy_SDL_PremultiplyAlpha = NULL;
PFN_SDL_FillRect proxy_SDL_FillRect = NULL;
PFN_SDL_UpperBlit proxy_SDL_UpperBlit = NULL;
PFN_SDL_Delay proxy_SDL_Delay = NULL;
PFN_SDL_AddTimer proxy_SDL_AddTimer = NULL;
PFN_IMG_Load_RW proxy_IMG_Load_RW = NULL;

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
    
    proxy_SDL_GetError = (PFN_SDL_GetError)dlsym(handle, "SDL_GetError");
    if (!proxy_SDL_GetError && gsdl_handle) proxy_SDL_GetError = (PFN_SDL_GetError)dlsym(gsdl_handle, "SDL_GetError");
    if (!proxy_SDL_GetError && sdl_handle) proxy_SDL_GetError = (PFN_SDL_GetError)dlsym(sdl_handle, "SDL_GetError");
    if (!proxy_SDL_GetError) LOGE("Failed to resolve SDL symbol: SDL_GetError\n");
    proxy_SDL_Error = (PFN_SDL_Error)dlsym(handle, "SDL_Error");
    if (!proxy_SDL_Error && gsdl_handle) proxy_SDL_Error = (PFN_SDL_Error)dlsym(gsdl_handle, "SDL_Error");
    if (!proxy_SDL_Error && sdl_handle) proxy_SDL_Error = (PFN_SDL_Error)dlsym(sdl_handle, "SDL_Error");
    if (!proxy_SDL_Error) LOGE("Failed to resolve SDL symbol: SDL_Error\n");
    proxy_SDL_CreateMutex = (PFN_SDL_CreateMutex)dlsym(handle, "SDL_CreateMutex");
    if (!proxy_SDL_CreateMutex && gsdl_handle) proxy_SDL_CreateMutex = (PFN_SDL_CreateMutex)dlsym(gsdl_handle, "SDL_CreateMutex");
    if (!proxy_SDL_CreateMutex && sdl_handle) proxy_SDL_CreateMutex = (PFN_SDL_CreateMutex)dlsym(sdl_handle, "SDL_CreateMutex");
    if (!proxy_SDL_CreateMutex) LOGE("Failed to resolve SDL symbol: SDL_CreateMutex\n");
    proxy_SDL_LockMutex = (PFN_SDL_LockMutex)dlsym(handle, "SDL_LockMutex");
    if (!proxy_SDL_LockMutex && gsdl_handle) proxy_SDL_LockMutex = (PFN_SDL_LockMutex)dlsym(gsdl_handle, "SDL_LockMutex");
    if (!proxy_SDL_LockMutex && sdl_handle) proxy_SDL_LockMutex = (PFN_SDL_LockMutex)dlsym(sdl_handle, "SDL_LockMutex");
    if (!proxy_SDL_LockMutex) LOGE("Failed to resolve SDL symbol: SDL_LockMutex\n");
    proxy_SDL_UnlockMutex = (PFN_SDL_UnlockMutex)dlsym(handle, "SDL_UnlockMutex");
    if (!proxy_SDL_UnlockMutex && gsdl_handle) proxy_SDL_UnlockMutex = (PFN_SDL_UnlockMutex)dlsym(gsdl_handle, "SDL_UnlockMutex");
    if (!proxy_SDL_UnlockMutex && sdl_handle) proxy_SDL_UnlockMutex = (PFN_SDL_UnlockMutex)dlsym(sdl_handle, "SDL_UnlockMutex");
    if (!proxy_SDL_UnlockMutex) LOGE("Failed to resolve SDL symbol: SDL_UnlockMutex\n");
    proxy_SDL_DestroyMutex = (PFN_SDL_DestroyMutex)dlsym(handle, "SDL_DestroyMutex");
    if (!proxy_SDL_DestroyMutex && gsdl_handle) proxy_SDL_DestroyMutex = (PFN_SDL_DestroyMutex)dlsym(gsdl_handle, "SDL_DestroyMutex");
    if (!proxy_SDL_DestroyMutex && sdl_handle) proxy_SDL_DestroyMutex = (PFN_SDL_DestroyMutex)dlsym(sdl_handle, "SDL_DestroyMutex");
    if (!proxy_SDL_DestroyMutex) LOGE("Failed to resolve SDL symbol: SDL_DestroyMutex\n");
    proxy_SDL_RWFromFile = (PFN_SDL_RWFromFile)dlsym(handle, "SDL_RWFromFile");
    if (!proxy_SDL_RWFromFile && gsdl_handle) proxy_SDL_RWFromFile = (PFN_SDL_RWFromFile)dlsym(gsdl_handle, "SDL_RWFromFile");
    if (!proxy_SDL_RWFromFile && sdl_handle) proxy_SDL_RWFromFile = (PFN_SDL_RWFromFile)dlsym(sdl_handle, "SDL_RWFromFile");
    if (!proxy_SDL_RWFromFile) LOGE("Failed to resolve SDL symbol: SDL_RWFromFile\n");
    proxy_SDL_RWFromMem = (PFN_SDL_RWFromMem)dlsym(handle, "SDL_RWFromMem");
    if (!proxy_SDL_RWFromMem && gsdl_handle) proxy_SDL_RWFromMem = (PFN_SDL_RWFromMem)dlsym(gsdl_handle, "SDL_RWFromMem");
    if (!proxy_SDL_RWFromMem && sdl_handle) proxy_SDL_RWFromMem = (PFN_SDL_RWFromMem)dlsym(sdl_handle, "SDL_RWFromMem");
    if (!proxy_SDL_RWFromMem) LOGE("Failed to resolve SDL symbol: SDL_RWFromMem\n");
    proxy_SDL_RWseek = (PFN_SDL_RWseek)dlsym(handle, "SDL_RWseek");
    if (!proxy_SDL_RWseek && gsdl_handle) proxy_SDL_RWseek = (PFN_SDL_RWseek)dlsym(gsdl_handle, "SDL_RWseek");
    if (!proxy_SDL_RWseek && sdl_handle) proxy_SDL_RWseek = (PFN_SDL_RWseek)dlsym(sdl_handle, "SDL_RWseek");
    if (!proxy_SDL_RWseek) LOGE("Failed to resolve SDL symbol: SDL_RWseek\n");
    proxy_SDL_RWtell = (PFN_SDL_RWtell)dlsym(handle, "SDL_RWtell");
    if (!proxy_SDL_RWtell && gsdl_handle) proxy_SDL_RWtell = (PFN_SDL_RWtell)dlsym(gsdl_handle, "SDL_RWtell");
    if (!proxy_SDL_RWtell && sdl_handle) proxy_SDL_RWtell = (PFN_SDL_RWtell)dlsym(sdl_handle, "SDL_RWtell");
    if (!proxy_SDL_RWtell) LOGE("Failed to resolve SDL symbol: SDL_RWtell\n");
    proxy_SDL_RWread = (PFN_SDL_RWread)dlsym(handle, "SDL_RWread");
    if (!proxy_SDL_RWread && gsdl_handle) proxy_SDL_RWread = (PFN_SDL_RWread)dlsym(gsdl_handle, "SDL_RWread");
    if (!proxy_SDL_RWread && sdl_handle) proxy_SDL_RWread = (PFN_SDL_RWread)dlsym(sdl_handle, "SDL_RWread");
    if (!proxy_SDL_RWread) LOGE("Failed to resolve SDL symbol: SDL_RWread\n");
    proxy_SDL_RWwrite = (PFN_SDL_RWwrite)dlsym(handle, "SDL_RWwrite");
    if (!proxy_SDL_RWwrite && gsdl_handle) proxy_SDL_RWwrite = (PFN_SDL_RWwrite)dlsym(gsdl_handle, "SDL_RWwrite");
    if (!proxy_SDL_RWwrite && sdl_handle) proxy_SDL_RWwrite = (PFN_SDL_RWwrite)dlsym(sdl_handle, "SDL_RWwrite");
    if (!proxy_SDL_RWwrite) LOGE("Failed to resolve SDL symbol: SDL_RWwrite\n");
    proxy_SDL_RWclose = (PFN_SDL_RWclose)dlsym(handle, "SDL_RWclose");
    if (!proxy_SDL_RWclose && gsdl_handle) proxy_SDL_RWclose = (PFN_SDL_RWclose)dlsym(gsdl_handle, "SDL_RWclose");
    if (!proxy_SDL_RWclose && sdl_handle) proxy_SDL_RWclose = (PFN_SDL_RWclose)dlsym(sdl_handle, "SDL_RWclose");
    if (!proxy_SDL_RWclose) LOGE("Failed to resolve SDL symbol: SDL_RWclose\n");
    proxy_SDL_CreateRGBSurfaceWithFormat = (PFN_SDL_CreateRGBSurfaceWithFormat)dlsym(handle, "SDL_CreateRGBSurfaceWithFormat");
    if (!proxy_SDL_CreateRGBSurfaceWithFormat && gsdl_handle) proxy_SDL_CreateRGBSurfaceWithFormat = (PFN_SDL_CreateRGBSurfaceWithFormat)dlsym(gsdl_handle, "SDL_CreateRGBSurfaceWithFormat");
    if (!proxy_SDL_CreateRGBSurfaceWithFormat && sdl_handle) proxy_SDL_CreateRGBSurfaceWithFormat = (PFN_SDL_CreateRGBSurfaceWithFormat)dlsym(sdl_handle, "SDL_CreateRGBSurfaceWithFormat");
    if (!proxy_SDL_CreateRGBSurfaceWithFormat) LOGE("Failed to resolve SDL symbol: SDL_CreateRGBSurfaceWithFormat\n");
    proxy_SDL_FreeSurface = (PFN_SDL_FreeSurface)dlsym(handle, "SDL_FreeSurface");
    if (!proxy_SDL_FreeSurface && gsdl_handle) proxy_SDL_FreeSurface = (PFN_SDL_FreeSurface)dlsym(gsdl_handle, "SDL_FreeSurface");
    if (!proxy_SDL_FreeSurface && sdl_handle) proxy_SDL_FreeSurface = (PFN_SDL_FreeSurface)dlsym(sdl_handle, "SDL_FreeSurface");
    if (!proxy_SDL_FreeSurface) LOGE("Failed to resolve SDL symbol: SDL_FreeSurface\n");
    proxy_SDL_LockSurface = (PFN_SDL_LockSurface)dlsym(handle, "SDL_LockSurface");
    if (!proxy_SDL_LockSurface && gsdl_handle) proxy_SDL_LockSurface = (PFN_SDL_LockSurface)dlsym(gsdl_handle, "SDL_LockSurface");
    if (!proxy_SDL_LockSurface && sdl_handle) proxy_SDL_LockSurface = (PFN_SDL_LockSurface)dlsym(sdl_handle, "SDL_LockSurface");
    if (!proxy_SDL_LockSurface) LOGE("Failed to resolve SDL symbol: SDL_LockSurface\n");
    proxy_SDL_UnlockSurface = (PFN_SDL_UnlockSurface)dlsym(handle, "SDL_UnlockSurface");
    if (!proxy_SDL_UnlockSurface && gsdl_handle) proxy_SDL_UnlockSurface = (PFN_SDL_UnlockSurface)dlsym(gsdl_handle, "SDL_UnlockSurface");
    if (!proxy_SDL_UnlockSurface && sdl_handle) proxy_SDL_UnlockSurface = (PFN_SDL_UnlockSurface)dlsym(sdl_handle, "SDL_UnlockSurface");
    if (!proxy_SDL_UnlockSurface) LOGE("Failed to resolve SDL symbol: SDL_UnlockSurface\n");
    proxy_SDL_SetSurfaceBlendMode = (PFN_SDL_SetSurfaceBlendMode)dlsym(handle, "SDL_SetSurfaceBlendMode");
    if (!proxy_SDL_SetSurfaceBlendMode && gsdl_handle) proxy_SDL_SetSurfaceBlendMode = (PFN_SDL_SetSurfaceBlendMode)dlsym(gsdl_handle, "SDL_SetSurfaceBlendMode");
    if (!proxy_SDL_SetSurfaceBlendMode && sdl_handle) proxy_SDL_SetSurfaceBlendMode = (PFN_SDL_SetSurfaceBlendMode)dlsym(sdl_handle, "SDL_SetSurfaceBlendMode");
    if (!proxy_SDL_SetSurfaceBlendMode) LOGE("Failed to resolve SDL symbol: SDL_SetSurfaceBlendMode\n");
    proxy_SDL_DuplicateSurface = (PFN_SDL_DuplicateSurface)dlsym(handle, "SDL_DuplicateSurface");
    if (!proxy_SDL_DuplicateSurface && gsdl_handle) proxy_SDL_DuplicateSurface = (PFN_SDL_DuplicateSurface)dlsym(gsdl_handle, "SDL_DuplicateSurface");
    if (!proxy_SDL_DuplicateSurface && sdl_handle) proxy_SDL_DuplicateSurface = (PFN_SDL_DuplicateSurface)dlsym(sdl_handle, "SDL_DuplicateSurface");
    if (!proxy_SDL_DuplicateSurface) LOGE("Failed to resolve SDL symbol: SDL_DuplicateSurface\n");
    proxy_SDL_ConvertSurfaceFormat = (PFN_SDL_ConvertSurfaceFormat)dlsym(handle, "SDL_ConvertSurfaceFormat");
    if (!proxy_SDL_ConvertSurfaceFormat && gsdl_handle) proxy_SDL_ConvertSurfaceFormat = (PFN_SDL_ConvertSurfaceFormat)dlsym(gsdl_handle, "SDL_ConvertSurfaceFormat");
    if (!proxy_SDL_ConvertSurfaceFormat && sdl_handle) proxy_SDL_ConvertSurfaceFormat = (PFN_SDL_ConvertSurfaceFormat)dlsym(sdl_handle, "SDL_ConvertSurfaceFormat");
    if (!proxy_SDL_ConvertSurfaceFormat) LOGE("Failed to resolve SDL symbol: SDL_ConvertSurfaceFormat\n");
    proxy_SDL_PremultiplyAlpha = (PFN_SDL_PremultiplyAlpha)dlsym(handle, "SDL_PremultiplyAlpha");
    if (!proxy_SDL_PremultiplyAlpha && gsdl_handle) proxy_SDL_PremultiplyAlpha = (PFN_SDL_PremultiplyAlpha)dlsym(gsdl_handle, "SDL_PremultiplyAlpha");
    if (!proxy_SDL_PremultiplyAlpha && sdl_handle) proxy_SDL_PremultiplyAlpha = (PFN_SDL_PremultiplyAlpha)dlsym(sdl_handle, "SDL_PremultiplyAlpha");
    if (!proxy_SDL_PremultiplyAlpha) LOGE("Failed to resolve SDL symbol: SDL_PremultiplyAlpha\n");
    proxy_SDL_FillRect = (PFN_SDL_FillRect)dlsym(handle, "SDL_FillRect");
    if (!proxy_SDL_FillRect && gsdl_handle) proxy_SDL_FillRect = (PFN_SDL_FillRect)dlsym(gsdl_handle, "SDL_FillRect");
    if (!proxy_SDL_FillRect && sdl_handle) proxy_SDL_FillRect = (PFN_SDL_FillRect)dlsym(sdl_handle, "SDL_FillRect");
    if (!proxy_SDL_FillRect) LOGE("Failed to resolve SDL symbol: SDL_FillRect\n");
    proxy_SDL_UpperBlit = (PFN_SDL_UpperBlit)dlsym(handle, "SDL_UpperBlit");
    if (!proxy_SDL_UpperBlit && gsdl_handle) proxy_SDL_UpperBlit = (PFN_SDL_UpperBlit)dlsym(gsdl_handle, "SDL_UpperBlit");
    if (!proxy_SDL_UpperBlit && sdl_handle) proxy_SDL_UpperBlit = (PFN_SDL_UpperBlit)dlsym(sdl_handle, "SDL_UpperBlit");
    if (!proxy_SDL_UpperBlit) LOGE("Failed to resolve SDL symbol: SDL_UpperBlit\n");
    proxy_SDL_Delay = (PFN_SDL_Delay)dlsym(handle, "SDL_Delay");
    if (!proxy_SDL_Delay && gsdl_handle) proxy_SDL_Delay = (PFN_SDL_Delay)dlsym(gsdl_handle, "SDL_Delay");
    if (!proxy_SDL_Delay && sdl_handle) proxy_SDL_Delay = (PFN_SDL_Delay)dlsym(sdl_handle, "SDL_Delay");
    if (!proxy_SDL_Delay) LOGE("Failed to resolve SDL symbol: SDL_Delay\n");
    proxy_SDL_AddTimer = (PFN_SDL_AddTimer)dlsym(handle, "SDL_AddTimer");
    if (!proxy_SDL_AddTimer && gsdl_handle) proxy_SDL_AddTimer = (PFN_SDL_AddTimer)dlsym(gsdl_handle, "SDL_AddTimer");
    if (!proxy_SDL_AddTimer && sdl_handle) proxy_SDL_AddTimer = (PFN_SDL_AddTimer)dlsym(sdl_handle, "SDL_AddTimer");
    if (!proxy_SDL_AddTimer) LOGE("Failed to resolve SDL symbol: SDL_AddTimer\n");
    proxy_IMG_Load_RW = (PFN_IMG_Load_RW)dlsym(handle, "IMG_Load_RW");
    if (!proxy_IMG_Load_RW && gsdl_handle) proxy_IMG_Load_RW = (PFN_IMG_Load_RW)dlsym(gsdl_handle, "IMG_Load_RW");
    if (!proxy_IMG_Load_RW && sdl_handle) proxy_IMG_Load_RW = (PFN_IMG_Load_RW)dlsym(sdl_handle, "IMG_Load_RW");
    if (!proxy_IMG_Load_RW) LOGE("Failed to resolve SDL symbol: IMG_Load_RW\n");
}

#endif
