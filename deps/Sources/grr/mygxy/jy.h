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

/* ─── LRU 缓存容量默认值 ───
 * ★ 从 128 调低到 32：
 *   - 单个 JY_UserData 的 key = pid|cdn_eid|act|dir，仅承载单动作单方向
 *   - 实际帧数：stand 8〜walk 12〜attack 16〜cast 12〜die 24，最大 24 帧
 *   - 32 帧提供 ~33% 余量，足够覆盖全部常规动作重复播放不会重解码
 *   - R8 优化后帧大小：~160KB/帧（200×200 idx+alpha+depth），32 帧约 5MB/jy_obj
 *     (R8 之前 SDL_Surface ARGB+depth 约 240KB/帧 → R8 约省 33%)
 *   - 代价：跨 32+ 帧的滚动场景（霓裳宝阁拖动预览）需重解码，单帧 ~1ms 肉眼无感
 *   - cache_cap 字段仍保留运行时可调，宏仅为默认初始值 */
#define JY_CACHE_CAP_DEFAULT 32

/* ─── 单帧在 Atlas 中的区域 ─── */
typedef struct
{
    Sint32 key_x, key_y;       /* 关键位 */
    Uint32 sx, sy, sw, sh;     /* Atlas 裁剪区域 (from JSON res) */
} JY_FrameInfo;

/* ─── 帧缓存条目 ─── */
/* ★ R8 优化（路线 A）：cache 从 SDL_Surface(ARGB8888 32bpp) 改为 R8 三 buffer
 *   - idx_pixels / alpha_pixels：调色板索引 + alpha 蒙版（各 1 字节/像素）
 *   - depth：保持 16-bit
 *   - 不存调色后的 ARGB → 调色板版本变化时缓存仍然有效（零失效）
 *   - 视觉一致性：GetFrame 返回时按当前 pal[] 实时反查生成 ARGB8888 surface
 *   - 内存收益：单帧 4 字节 → 2 字节（不含 depth），约 -50% */
typedef struct
{
    Uint8*  idx_pixels;        /* R8 调色板索引 (sw*sh 字节)，NULL 表示空槽 */
    Uint8*  alpha_pixels;      /* R8 alpha 蒙版 (sw*sh 字节)，可为 NULL（视为全 255） */
    Uint16* depth;             /* 16-bit 深度值 (w*h)，NULL 表示无深度 */
    Uint16  w, h;              /* 帧裁剪后尺寸（反查用） */
    Uint32  frame_id;
    /* pal_ver 字段移除：cache 内容与调色板解耦，染色变化零失效 */
    Uint32  lru_tick;
} JY_CacheEntry;

/* ─── 异步任务 ─── */
typedef struct
{
    Uint32 frame_id;
    /* pal_ver 字段移除：worker 解码 R8，与调色板版本无关 */
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
    Uint32 cache_cap;          /* 默认 JY_CACHE_CAP_DEFAULT (32)，运行时可按需调整 */
    Uint32 cache_tick;

    /* 工作线程（仅在 :Prefetch 调用时延迟启动；客户端不调用时全程 NULL） */
    SDL_Thread*  workers[2];
    SDL_mutex*   queue_mutex;
    SDL_cond*    queue_cond;
    JY_AsyncTask* task_queue;
    Uint32 task_count;
    Uint32 task_cap;
    volatile int shutdown;

    /* M3: Composite z-buffer 复用（避免每帧 230KB malloc/free + memset 抖动）
     * 按需扩容；JY_Reset 时统一释放。 */
    Sint32* zbuf_cached;
    Uint32  zbuf_cached_size;  /* zbuf_cached 元素数（pixels），非字节数 */
} JY_UserData;

/* 公共入口 */
int JY_Create(lua_State* L);
