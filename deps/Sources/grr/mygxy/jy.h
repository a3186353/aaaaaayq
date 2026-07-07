/*
 * jy.h — 锦衣祥瑞解析模块 (JSON+PNG Atlas)
 * mygxy.jy 子模块，与 tcp 接口对齐
 */
#pragma once
#include "lua_proxy.h"
#include "sdl_proxy.h"

#define JY_MT "xyq_jy"

#ifndef MYGXY_ASYNC_FRAME_STATUS
#define MYGXY_ASYNC_FRAME_STATUS
#define MYGXY_ASYNC_FRAME_QUEUED      0
#define MYGXY_ASYNC_FRAME_READY       1
#define MYGXY_ASYNC_FRAME_PENDING     2
#define MYGXY_ASYNC_FRAME_QUEUE_FULL -2
#define MYGXY_ASYNC_FRAME_ERROR      -1
#endif

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
/* ★ R10：增加 z 字段（atlas mc.animate.frames[i].z）
 *   用于 Composite 时与 pixel depth 叠加，对齐 JS sortSample(): dep = depth(px) + (-frame.z)
 *   SPR 模式下默认 0（SPR 14B 帧头无 z 字段，与 JS 行为一致：SPR 部件仅靠 pixel depth 排序） */
typedef struct
{
    Sint32 key_x, key_y;       /* 关键位 */
    Sint16 z;                  /* Atlas frame.z（per-frame 深度偏移）；SPR=0 */
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

typedef struct JY_AsyncJob
{
    Uint32 frame_id;
    Uint32 generation;
    Uint8* idx_pixels;
    Uint8* alpha_pixels;
    Uint16* depth;
    Uint16 w, h;
    int ok;
    struct JY_AsyncJob* next;
} JY_AsyncJob;

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

    /* ★ R9 紧致画布：单 act+dir 内所有 frame 的实际裁剪区最大值 */
    Uint16 max_frame_w, max_frame_h;
    Sint32 max_key_x, max_key_y;
    Sint32 max_frame_right, max_frame_bottom;

    /* LRU 帧缓存 */
    JY_CacheEntry* cache;
    Uint32 cache_cap;          /* 默认 JY_CACHE_CAP_DEFAULT (32)，运行时可按需调整 */
    Uint32 cache_tick;

    /* 后台 CPU 解码队列：worker 只读 atlas/frames 并产出 R8 三 buffer。
     * 主线程 PollAsync/GetFrame/IsFrameDecoded 吸收结果写入 cache。 */
    SDL_mutex* async_mutex;
    SDL_cond* async_cond;
    SDL_Thread* async_thread;
    int async_stop;
    Uint32 async_generation;
    JY_AsyncJob* async_queue_head;
    JY_AsyncJob* async_queue_tail;
    JY_AsyncJob* async_done_head;
    JY_AsyncJob* async_done_tail;
    Uint32 async_queued;
    Uint32 async_ready;
    Uint32 async_submitted;
    Uint32 async_decoded;
    Uint32 async_failed;
    Uint32 async_cancelled;
    int async_active;
    Uint32 async_active_frame;
    Uint32 async_active_generation;

    /* ★ R10：旧 zbuf_cached 已删除
     *   - 旧策略：单 z-buffer 仅保留最深像素 → 半透明部件被遮挡时颜色丢失
     *   - 新策略：per-pixel 8 槽插入排序 + 链式 over alpha（栈分配，无堆开销）
     *   - 与 JS sortSample shader 等价 */
} JY_UserData;

/* 公共入口 */
int JY_Create(lua_State* L);
int JY_NativeRequestFrame(JY_UserData* ud, Uint32 id, const char** status);
int JY_NativePollAsync(JY_UserData* ud, Uint32 limit);
int JY_NativeIsFrameDecoded(JY_UserData* ud, Uint32 id);
void JY_PushPerfStats(lua_State* L);
