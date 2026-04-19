/*
 * jy.h — 锦衣祥瑞解析模块 (JSON+PNG Atlas)
 * mygxy.jy 子模块，与 tcp 接口对齐
 */
#pragma once
#include "lua_proxy.h"
#include "sdl_proxy.h"

/* Threading: sdl_proxy doesn't cover thread APIs.
 * On non-Android, SDL.h is already included via sdl_proxy.h.
 * On Android, we include the thread headers directly (they're
 * in the NDK SDL include path and don't need proxy). */
#if defined(__ANDROID__)
#include "SDL_thread.h"
#include "SDL_mutex.h"
#endif

#define JY_MT "xyq_jy"

/* ─── 单帧在 Atlas 中的区域 ─── */
typedef struct
{
    Sint32 key_x, key_y;       /* 关键位 */
    Uint32 sx, sy, sw, sh;     /* Atlas 裁剪区域 (from JSON res) */
} JY_FrameInfo;

/* ─── 帧缓存条目 ─── */
typedef struct
{
    SDL_Surface* surface;      /* 已解码 ARGB8888，NULL 表示空槽 */
    Uint16*     depth;         /* 16-bit 深度值 (w*h)，NULL 表示无深度 */
    Uint32 frame_id;
    Uint32 pal_ver;            /* 解码时的调色板版本 */
    Uint32 lru_tick;
} JY_CacheEntry;

/* ─── 异步任务 ─── */
typedef struct
{
    Uint32 frame_id;
    Uint32 pal_ver;
    volatile int done;         /* 0=pending, 1=done, -1=canceled */
} JY_AsyncTask;

/* ─── 主 UserData ─── */
typedef struct
{
    /* Atlas 像素 */
    Uint8*  index_pixels;      /* RGB24 (R=pal idx, G/B=depth) */
    Uint8*  alpha_pixels;      /* Grayscale alpha (单通道) */
    Uint8*  depth_pixels;      /* RGB24 (G/B=depth) */
    Uint32  atlas_w, atlas_h;
    Uint32  depth_atlas_w, depth_atlas_h; /* 深度 atlas 尺寸（可能与主 atlas 不同） */
    Uint32  index_bpp;         /* 每像素字节数 (3=RGB, 4=RGBA) */
    Uint32  alpha_bpp;         /* 每像素字节数 (1=Gray, 3=RGB, 4=RGBA) */
    Uint32  depth_bpp;         /* 深度图像素字节数 (3=RGB) */

    /* 帧映射 */
    JY_FrameInfo* frames;
    Uint32 frame_count;

    /* 深度帧映射 (独立于主帧, 用于深度 atlas 坐标与主 atlas 不同的情况) */
    JY_FrameInfo* depth_frames;
    Uint32 depth_frame_count;
    Uint32 group;              /* 方向数 */
    Uint32 frame_per_group;    /* 每方向帧数 */

    /* 调色板 */
    Uint32 pal[256];           /* ARGB8888 */
    Uint32 pal_count;
    Uint32 pal_mod;            /* 有效调色板模数 64/128/256 (palette_mod) */
    Uint32 pal_version;        /* 每次 SetPal 递增 */

    /* 全局尺寸 */
    Uint16 width, height;
    Sint16 global_x, global_y;

    /* LRU 帧缓存 */
    JY_CacheEntry* cache;
    Uint32 cache_cap;          /* 默认 128 */
    Uint32 cache_tick;

    /* 工作线程 */
    SDL_Thread*  workers[2];
    SDL_mutex*   queue_mutex;
    SDL_cond*    queue_cond;
    JY_AsyncTask* task_queue;
    Uint32 task_count;
    Uint32 task_cap;
    volatile int shutdown;
} JY_UserData;

/* 公共入口 */
int JY_Create(lua_State* L);
