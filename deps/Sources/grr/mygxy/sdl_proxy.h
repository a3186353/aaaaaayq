#ifndef SDL_PROXY_H
#define SDL_PROXY_H

#if defined(__ANDROID__) || defined(__APPLE__)

#include "SDL.h"
#include "SDL_image.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#undef SDL_malloc
#define SDL_malloc malloc
#undef SDL_calloc
#define SDL_calloc calloc
#undef SDL_realloc
#define SDL_realloc realloc
#undef SDL_free
#define SDL_free free
#undef SDL_memset
#define SDL_memset memset
#undef SDL_memcpy
#define SDL_memcpy memcpy
#undef SDL_memcmp
#define SDL_memcmp memcmp
#undef SDL_strlen
#define SDL_strlen strlen
#undef SDL_strlcpy
#define SDL_strlcpy strlcpy
#undef SDL_strcasecmp
#define SDL_strcasecmp strcasecmp
#undef SDL_snprintf
#define SDL_snprintf snprintf
#undef SDL_ceil
#define SDL_ceil ceil

typedef const char * (*PFN_SDL_GetError)(void);
extern PFN_SDL_GetError proxy_SDL_GetError;
#define SDL_GetError proxy_SDL_GetError

typedef int (*PFN_SDL_Error)(SDL_errorcode code);
extern PFN_SDL_Error proxy_SDL_Error;
#define SDL_Error proxy_SDL_Error

typedef SDL_mutex * (*PFN_SDL_CreateMutex)(void);
extern PFN_SDL_CreateMutex proxy_SDL_CreateMutex;
#define SDL_CreateMutex proxy_SDL_CreateMutex

typedef int (*PFN_SDL_LockMutex)(SDL_mutex * mutex);
extern PFN_SDL_LockMutex proxy_SDL_LockMutex;
#define SDL_LockMutex proxy_SDL_LockMutex

typedef int (*PFN_SDL_UnlockMutex)(SDL_mutex * mutex);
extern PFN_SDL_UnlockMutex proxy_SDL_UnlockMutex;
#define SDL_UnlockMutex proxy_SDL_UnlockMutex

typedef void (*PFN_SDL_DestroyMutex)(SDL_mutex * mutex);
extern PFN_SDL_DestroyMutex proxy_SDL_DestroyMutex;
#define SDL_DestroyMutex proxy_SDL_DestroyMutex

typedef SDL_RWops * (*PFN_SDL_RWFromFile)(const char *file, const char *mode);
extern PFN_SDL_RWFromFile proxy_SDL_RWFromFile;
#define SDL_RWFromFile proxy_SDL_RWFromFile

typedef SDL_RWops * (*PFN_SDL_RWFromMem)(void *mem, int size);
extern PFN_SDL_RWFromMem proxy_SDL_RWFromMem;
#define SDL_RWFromMem proxy_SDL_RWFromMem

typedef Sint64 (*PFN_SDL_RWseek)(SDL_RWops *context, Sint64 offset, int whence);
extern PFN_SDL_RWseek proxy_SDL_RWseek;
#define SDL_RWseek proxy_SDL_RWseek

typedef Sint64 (*PFN_SDL_RWtell)(SDL_RWops *context);
extern PFN_SDL_RWtell proxy_SDL_RWtell;
#define SDL_RWtell proxy_SDL_RWtell

typedef size_t (*PFN_SDL_RWread)(SDL_RWops *context, void *ptr, size_t size, size_t maxnum);
extern PFN_SDL_RWread proxy_SDL_RWread;
#define SDL_RWread proxy_SDL_RWread

typedef size_t (*PFN_SDL_RWwrite)(SDL_RWops *context, const void *ptr, size_t size, size_t num);
extern PFN_SDL_RWwrite proxy_SDL_RWwrite;
#define SDL_RWwrite proxy_SDL_RWwrite

typedef int (*PFN_SDL_RWclose)(SDL_RWops *context);
extern PFN_SDL_RWclose proxy_SDL_RWclose;
#define SDL_RWclose proxy_SDL_RWclose

typedef SDL_Surface * (*PFN_SDL_CreateRGBSurfaceWithFormat)(Uint32 flags, int width, int height, int depth, Uint32 format);
extern PFN_SDL_CreateRGBSurfaceWithFormat proxy_SDL_CreateRGBSurfaceWithFormat;
#define SDL_CreateRGBSurfaceWithFormat proxy_SDL_CreateRGBSurfaceWithFormat

typedef void (*PFN_SDL_FreeSurface)(SDL_Surface * surface);
extern PFN_SDL_FreeSurface proxy_SDL_FreeSurface;
#define SDL_FreeSurface proxy_SDL_FreeSurface

typedef int (*PFN_SDL_LockSurface)(SDL_Surface * surface);
extern PFN_SDL_LockSurface proxy_SDL_LockSurface;
#define SDL_LockSurface proxy_SDL_LockSurface

typedef void (*PFN_SDL_UnlockSurface)(SDL_Surface * surface);
extern PFN_SDL_UnlockSurface proxy_SDL_UnlockSurface;
#define SDL_UnlockSurface proxy_SDL_UnlockSurface

typedef int (*PFN_SDL_SetSurfaceBlendMode)(SDL_Surface * surface, SDL_BlendMode blendMode);
extern PFN_SDL_SetSurfaceBlendMode proxy_SDL_SetSurfaceBlendMode;
#define SDL_SetSurfaceBlendMode proxy_SDL_SetSurfaceBlendMode

typedef SDL_Surface * (*PFN_SDL_DuplicateSurface)(SDL_Surface * surface);
extern PFN_SDL_DuplicateSurface proxy_SDL_DuplicateSurface;
#define SDL_DuplicateSurface proxy_SDL_DuplicateSurface

typedef SDL_Surface * (*PFN_SDL_ConvertSurfaceFormat)(SDL_Surface * src, Uint32 pixel_format, Uint32 flags);
extern PFN_SDL_ConvertSurfaceFormat proxy_SDL_ConvertSurfaceFormat;
#define SDL_ConvertSurfaceFormat proxy_SDL_ConvertSurfaceFormat

typedef int (*PFN_SDL_PremultiplyAlpha)(int width, int height, Uint32 src_format, const void * src, int src_pitch, Uint32 dst_format, void * dst, int dst_pitch);
extern PFN_SDL_PremultiplyAlpha proxy_SDL_PremultiplyAlpha;
#define SDL_PremultiplyAlpha proxy_SDL_PremultiplyAlpha

typedef int (*PFN_SDL_FillRect)(SDL_Surface * dst, const SDL_Rect * rect, Uint32 color);
extern PFN_SDL_FillRect proxy_SDL_FillRect;
#define SDL_FillRect proxy_SDL_FillRect

typedef int (*PFN_SDL_UpperBlit)(SDL_Surface * src, const SDL_Rect * srcrect, SDL_Surface * dst, SDL_Rect * dstrect);
extern PFN_SDL_UpperBlit proxy_SDL_UpperBlit;
#define SDL_UpperBlit proxy_SDL_UpperBlit

typedef void (*PFN_SDL_Delay)(Uint32 ms);
extern PFN_SDL_Delay proxy_SDL_Delay;
#define SDL_Delay proxy_SDL_Delay

typedef SDL_TimerID (*PFN_SDL_AddTimer)(Uint32 interval, SDL_TimerCallback callback, void *param);
extern PFN_SDL_AddTimer proxy_SDL_AddTimer;
#define SDL_AddTimer proxy_SDL_AddTimer

typedef SDL_Surface * (*PFN_IMG_Load_RW)(SDL_RWops *src, int freesrc);
extern PFN_IMG_Load_RW proxy_IMG_Load_RW;
#define IMG_Load_RW proxy_IMG_Load_RW

void init_sdl_proxy(void);

#else

#include "SDL.h"
#include "SDL_image.h"
#define init_sdl_proxy() 

#endif // iOS/Android Proxy Guards

#endif // SDL_PROXY_H
