/*
 * jy.c — 锦衣祥瑞解析模块 (JSON+PNG Atlas)
 * mygxy.jy 子模块，与 tcp 接口对齐
 *
 * 构造: xy_jy(index_png, alpha_png, pal_data, frames_table)
 *   index_png  : string (PNG 文件二进制, R=调色板索引, GB=depth)
 *   alpha_png  : string (PNG 文件二进制, 灰度=alpha)
 *   pal_data   : string (256*4=1024 字节 BGRA 调色板)
 *   frames_table: Lua table，每个条目 {sx,sy,sw,sh,key_x,key_y}
 *
 * 方法:  GetFrame / SetPal / GetPal / SetPP / Prefetch
 */
#include "jy.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>


#if defined(_WIN32)
#define MYGXY_API __declspec(dllexport)
#else
#define MYGXY_API LUAMOD_API
#endif

#define JY_PERF_SAMPLE_CAP 1024

typedef struct
{
    SDL_Surface* sf;
    int refcount;
} JY_GGETexture;

typedef struct
{
    Uint64 count;
    Uint64 total_us;
    Uint32 samples[JY_PERF_SAMPLE_CAP];
    int sample_pos;
    int sample_count;
} JY_TimeStats;

typedef struct
{
    SDL_mutex* mutex;
    JY_TimeStats decode_us;
    JY_TimeStats upload_us;
    Uint64 cache_bytes;
    Uint64 decoded_frames;
    Uint64 cache_hits;
    Uint64 cache_misses;
    Uint64 lru_evictions;
    Uint64 cache_clear_count;
    Uint64 cache_clear_freed_bytes;
} JY_PerfStats;

static JY_PerfStats g_jy_perf = {0};

static void JY_PerfEnsure(void)
{
    if (!g_jy_perf.mutex)
        g_jy_perf.mutex = SDL_CreateMutex();
}

static Uint64 JY_NowUS(void)
{
    Uint64 freq = SDL_GetPerformanceFrequency();
    if (!freq)
        return 0;
    return (SDL_GetPerformanceCounter() * 1000000ULL) / freq;
}

static void JY_TimeRecord(JY_TimeStats* s, Uint64 elapsed_us)
{
    if (!s)
        return;
    s->count++;
    s->total_us += elapsed_us;
    s->samples[s->sample_pos] = (Uint32)(elapsed_us > 0xFFFFFFFFULL ? 0xFFFFFFFFu : elapsed_us);
    s->sample_pos = (s->sample_pos + 1) % JY_PERF_SAMPLE_CAP;
    if (s->sample_count < JY_PERF_SAMPLE_CAP)
        s->sample_count++;
}

static void JY_RecordDecode(Uint64 elapsed_us)
{
    JY_PerfEnsure();
    if (g_jy_perf.mutex) SDL_LockMutex(g_jy_perf.mutex);
    JY_TimeRecord(&g_jy_perf.decode_us, elapsed_us);
    if (g_jy_perf.mutex) SDL_UnlockMutex(g_jy_perf.mutex);
}

static void JY_RecordUpload(Uint64 elapsed_us)
{
    JY_PerfEnsure();
    if (g_jy_perf.mutex) SDL_LockMutex(g_jy_perf.mutex);
    JY_TimeRecord(&g_jy_perf.upload_us, elapsed_us);
    if (g_jy_perf.mutex) SDL_UnlockMutex(g_jy_perf.mutex);
}

static Uint64 JY_CacheEntryBytes(const JY_CacheEntry* e)
{
    Uint64 npx;
    if (!e || !e->idx_pixels || e->w == 0 || e->h == 0)
        return 0;
    npx = (Uint64)e->w * (Uint64)e->h;
    return npx + (e->alpha_pixels ? npx : 0) + (e->depth ? npx * sizeof(Uint16) : 0);
}

static int JY_Uint32Compare(const void* a, const void* b)
{
    Uint32 av = *(const Uint32*)a;
    Uint32 bv = *(const Uint32*)b;
    return (av > bv) - (av < bv);
}

static Uint32 JY_Percentile(Uint32* values, int count, int pct)
{
    int idx;
    if (!values || count <= 0)
        return 0;
    qsort(values, (size_t)count, sizeof(Uint32), JY_Uint32Compare);
    idx = (count * pct + 99) / 100;
    if (idx < 1) idx = 1;
    if (idx > count) idx = count;
    return values[idx - 1];
}

static int JY_PushTexture(lua_State* L, SDL_Texture* tex)
{
    JY_GGETexture* gt;
    SDL_Texture** ud;
    if (!tex)
        return 0;
    gt = (JY_GGETexture*)SDL_calloc(1, sizeof(JY_GGETexture));
    if (!gt)
    {
        SDL_DestroyTexture(tex);
        return 0;
    }
    ud = (SDL_Texture**)lua_newuserdata(L, sizeof(SDL_Texture*));
    *ud = tex;
    gt->refcount = 1;
    SDL_SetTextureUserData(tex, gt);
    luaL_setmetatable(L, "SDL_Texture");
    return 1;
}

/* ═══════════════════════════════════════════
 *  Forward declarations
 * ═══════════════════════════════════════════ */
static int JY_GetFrame(lua_State* L);
static int JY_GetFrameReady(lua_State* L);
static int JY_GetFrameTexture(lua_State* L);
static int JY_GetFrameInfo(lua_State* L);
static int JY_SetPal(lua_State* L);
static int JY_GetPal(lua_State* L);
static int JY_SetPP(lua_State* L);
static int JY_SetPalette(lua_State* L);
static int JY_Prefetch(lua_State* L);
static int JY_RequestFrame(lua_State* L);
static int JY_PollAsync(lua_State* L);
static int JY_IsFrameDecoded(lua_State* L);
static int JY_AsyncStats(lua_State* L);
static int JY_LUA_CacheClear(lua_State* L);
static int JY_LUA_SetCacheCap(lua_State* L);
static int JY_Composite(lua_State* L);
static int JY_CompositeTo(lua_State* L);
static int JY_GC(lua_State* L);
static SDL_INLINE void JY_FreeR8Triple(void* idx, void* alpha, void* depth);

static int JY_LUA_FreeSurface(lua_State* L);

static const luaL_Reg JY_FUNCS[] = {
    {"__gc",        JY_GC},
    {"__close",     JY_GC},
    {"GetFrame",    JY_GetFrame},
    {"get_frame",   JY_GetFrame},
    {"GetFrameReady", JY_GetFrameReady},
    {"GetFrameTexture", JY_GetFrameTexture},
    {"GetFrameInfo", JY_GetFrameInfo},
    {"SetPal",      JY_SetPal},
    {"set_palette", JY_SetPalette},
    {"GetPal",      JY_GetPal},
    {"get_palette", JY_GetPal},
    {"SetPP",       JY_SetPP},
    {"Prefetch",    JY_Prefetch},
    {"RequestFrame", JY_RequestFrame},
    {"PollAsync",   JY_PollAsync},
    {"IsFrameDecoded", JY_IsFrameDecoded},
    {"AsyncStats",  JY_AsyncStats},
    {"CacheClear",  JY_LUA_CacheClear},
    {"SetCacheCap", JY_LUA_SetCacheCap},
    {"Composite",   JY_Composite},
    {"CompositeTo", JY_CompositeTo},
    {NULL, NULL},
};

/* ═══════════════════════════════════════════
 *  SDL_Surface metatable (共享 tcp 的)
 * ═══════════════════════════════════════════ */
static void JY_EnsureSDLSurfaceMetatable(lua_State* L)
{
    luaL_getmetatable(L, "SDL_Surface");
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        luaL_newmetatable(L, "SDL_Surface");
    }

    lua_getfield(L, -1, "__gc");
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_pushcfunction(L, JY_LUA_FreeSurface);
        lua_setfield(L, -2, "__gc");
    }
    else
        lua_pop(L, 1);

    lua_getfield(L, -1, "__close");
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_pushcfunction(L, JY_LUA_FreeSurface);
        lua_setfield(L, -2, "__close");
    }
    else
        lua_pop(L, 1);

    lua_getfield(L, -1, "__index");
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }
    else
        lua_pop(L, 1);

    lua_pop(L, 1);
}

/* Match tcp.c pattern: direct FreeSurface, no refcount */
static int JY_LUA_FreeSurface(lua_State* L)
{
    SDL_Surface** sf = (SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    if (*sf)
    {
        SDL_FreeSurface(*sf);
        *sf = NULL;
    }
    return 0;
}

/* ═══════════════════════════════════════════
 *  Metatable helpers
 * ═══════════════════════════════════════════ */
static void JY_RegisterMetatable(lua_State* L)
{
    luaL_getmetatable(L, JY_MT);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        luaL_newmetatable(L, JY_MT);
        luaL_setfuncs(L, JY_FUNCS, 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
}

static JY_UserData* JY_Check(lua_State* L, int idx)
{
    void* p = luaL_checkudata(L, idx, JY_MT);
    if (!p)
        luaL_error(L, "invalid jy userdata");
    return (JY_UserData*)p;
}

static void JY_UpdateFrameBounds(JY_UserData* ud, const JY_FrameInfo* f)
{
    if (!ud || !f || f->sw == 0 || f->sh == 0)
        return;

    if (f->sw > ud->max_frame_w) ud->max_frame_w = (Uint16)f->sw;
    if (f->sh > ud->max_frame_h) ud->max_frame_h = (Uint16)f->sh;

    if (f->key_x > ud->max_key_x) ud->max_key_x = f->key_x;
    if (f->key_y > ud->max_key_y) ud->max_key_y = f->key_y;

    Sint32 right = (Sint32)f->sw - f->key_x;
    Sint32 bottom = (Sint32)f->sh - f->key_y;
    if (right > ud->max_frame_right) ud->max_frame_right = right;
    if (bottom > ud->max_frame_bottom) ud->max_frame_bottom = bottom;
}

/* ═══════════════════════════════════════════
 *  palette_mod auto-detection (matching view.py _load_palette)
 *  Scans palette from idx 255 downward to find last non-zero
 *  RGB entry, then sets pal_mod to 64/128/256.
 * ═══════════════════════════════════════════ */
static void JY_CalcPalMod(JY_UserData* ud)
{
    Uint32 last_valid = 0;
    for (int i = 255; i >= 0; i--)
    {
        Uint32 c = ud->pal[i];
        Uint32 rgb = c & 0x00FFFFFF;
        if (rgb != 0)
        {
            last_valid = (Uint32)i;
            break;
        }
    }
    if (last_valid < 64)
        ud->pal_mod = 64;
    else if (last_valid < 128)
        ud->pal_mod = 128;
    else
        ud->pal_mod = 256;
}

/* ═══════════════════════════════════════════
 *  Frame decode (pure C, thread‑safe) — R8 优化版（路线 A）
 *
 *  解码到三 buffer（不应用调色板，与 pal_version 解耦）：
 *    out_idx     : malloc 的 Uint8[w*h]，每像素 1 字节调色板索引
 *    out_alpha   : malloc 的 Uint8[w*h]，每像素 1 字节 alpha；可能为 NULL（视为全 255）
 *    out_depth   : malloc 的 Uint16[w*h] 深度，可能为 NULL（无 depth 信息）
 *    out_w/out_h : 帧裁剪后尺寸
 *
 *  返回 1 = 成功（所有 out 已填充，调用方接管所有权）
 *       0 = 失败（所有 out_* 被置 NULL）
 * ═══════════════════════════════════════════ */
static int JY_DecodeFrame(
    JY_UserData* ud, Uint32 id,
    Uint8** out_idx, Uint8** out_alpha, Uint16** out_depth,
    Uint16* out_w, Uint16* out_h)
{
    /* 安全初始化 out 参数，失败路径统一返回 NULL */
    if (out_idx)   *out_idx = NULL;
    if (out_alpha) *out_alpha = NULL;
    if (out_depth) *out_depth = NULL;
    if (out_w)     *out_w = 0;
    if (out_h)     *out_h = 0;

    if (id >= ud->frame_count)
        return 0;

    JY_FrameInfo* f = &ud->frames[id];
    if (f->sw == 0 || f->sh == 0)
        return 0;

    Uint32 npx = f->sw * f->sh;

    /* 分配 R8 idx + R8 alpha + Uint16 depth 三 buffer
     * 任一分配失败即整体回滚（避免半完成状态污染 cache） */
    Uint8*  idx_buf   = (Uint8*)SDL_malloc(npx);
    Uint8*  alpha_buf = (Uint8*)SDL_malloc(npx);
    Uint16* depth_buf = (Uint16*)SDL_calloc(npx, sizeof(Uint16));
    if (!idx_buf || !alpha_buf || !depth_buf)
    {
        if (idx_buf)   SDL_free(idx_buf);
        if (alpha_buf) SDL_free(alpha_buf);
        if (depth_buf) SDL_free(depth_buf);
        return 0;
    }
    /* 透明像素的索引置 0（idx 0 通常是透明色，避免随机数据污染缓存） */
    SDL_memset(idx_buf, 0, npx);
    SDL_memset(alpha_buf, 0, npx);

    Uint32 aw   = ud->atlas_w;
    Uint32 ibpp = ud->index_bpp;
    Uint32 abpp = ud->alpha_bpp;

    /* 预计算 depth atlas 边界 */
    Uint32 depth_buf_pixels = 0;
    Uint32 depth_stride = 0;
    if (ud->depth_pixels)
    {
        Uint32 daw = ud->depth_atlas_w ? ud->depth_atlas_w : aw;
        Uint32 dah = ud->depth_atlas_h ? ud->depth_atlas_h : ud->atlas_h;
        depth_stride = daw;
        depth_buf_pixels = daw * dah;
    }

    for (Uint32 y = 0; y < f->sh; y++)
    {
        Uint32 src_y = f->sy + y;
        if (src_y >= ud->atlas_h)
            break;

        for (Uint32 x = 0; x < f->sw; x++)
        {
            Uint32 src_x = f->sx + x;
            if (src_x >= aw)
                continue;

            /* Index 像素：R 通道 = 调色板索引，G/B 备用作深度 */
            Uint32 idx_off = (src_y * aw + src_x) * ibpp;
            Uint8 pal_idx = ud->index_pixels[idx_off];
            Uint8 depth_hi = 0, depth_lo = 0;

            if (ud->depth_pixels)
            {
                if (ud->depth_frames && id < ud->depth_frame_count)
                {
                    /* 独立 depth atlas（与主 atlas 不同坐标），用最近邻映射对齐 */
                    JY_FrameInfo* df = &ud->depth_frames[id];
                    if (df->sw > 0 && df->sh > 0)
                    {
                        Uint32 dmx = ((x * 2 + 1) * df->sw) / (f->sw * 2);
                        Uint32 dmy = ((y * 2 + 1) * df->sh) / (f->sh * 2);
                        if (dmx < df->sw && dmy < df->sh)
                        {
                            Uint32 dax = df->sx + dmx;
                            Uint32 day = df->sy + dmy;
                            Uint32 d_pixel_off = day * depth_stride + dax;
                            if (d_pixel_off < depth_buf_pixels)
                            {
                                Uint32 d_byte_off = d_pixel_off * ud->depth_bpp;
                                depth_hi = ud->depth_pixels[d_byte_off + 1];
                                depth_lo = ud->depth_pixels[d_byte_off + 2];
                            }
                        }
                    }
                }
            }
            else if (ibpp >= 3)
            {
                /* 无独立 depth：从 index 像素 G/B 通道提取（等价 view.py） */
                depth_hi = ud->index_pixels[idx_off + 1];
                depth_lo = ud->index_pixels[idx_off + 2];
            }

            /* Alpha 像素 */
            Uint8 alpha = 255;
            if (ud->alpha_pixels)
            {
                Uint32 a_off = (src_y * aw + src_x) * abpp;
                alpha = ud->alpha_pixels[a_off];
            }

            /* 写入三 buffer：idx 不查表，调色延后到 RenderFrameToSurface / Composite */
            Uint32 dst_off = y * f->sw + x;
            idx_buf[dst_off]   = pal_idx;
            alpha_buf[dst_off] = alpha;
            Uint32 depth_val = (Uint32)depth_hi * 257u + depth_lo;
            depth_buf[dst_off] = (Uint16)(depth_val > 65535u ? 65535u : depth_val);
        }
    }

    *out_idx   = idx_buf;
    *out_alpha = alpha_buf;
    *out_depth = depth_buf;
    *out_w     = (Uint16)f->sw;
    *out_h     = (Uint16)f->sh;
    return 1;
}

static int JY_DecodeFrameProfiled(
    JY_UserData* ud, Uint32 id,
    Uint8** out_idx, Uint8** out_alpha, Uint16** out_depth,
    Uint16* out_w, Uint16* out_h)
{
    Uint64 start_us = JY_NowUS();
    int ok = JY_DecodeFrame(ud, id, out_idx, out_alpha, out_depth, out_w, out_h);
    JY_RecordDecode(JY_NowUS() - start_us);
    return ok;
}

/* ═══════════════════════════════════════════
 *  R8 → ARGB8888 反查渲染（路线 A 核心）
 *
 *  把 idx + alpha 双 buffer 按当前 ud->pal[] 反查生成 ARGB8888 surface。
 *  调用方接管返回 surface 所有权。失败返回 NULL。
 *
 *  热路径：每帧 GetFrame 与 Composite cache miss/染色后命中 时调用
 *  性能：~300×300 帧 ~1ms（含 SDL_CreateRGBSurfaceWithFormat 内部 memset）
 * ═══════════════════════════════════════════ */
static SDL_Surface* JY_RenderFrameToSurface(
    JY_UserData* ud,
    const Uint8* idx_pixels,
    const Uint8* alpha_pixels,
    Uint16 w, Uint16 h)
{
    if (!ud || !idx_pixels || w == 0 || h == 0)
        return NULL;

    SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormat(
        SDL_SWSURFACE, (int)w, (int)h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!sf)
        return NULL;

    if (SDL_MUSTLOCK(sf))
        SDL_LockSurface(sf);

    Uint32* dst = (Uint32*)sf->pixels;
    Uint32 stride = (Uint32)(sf->pitch / 4);
    /* P2: pal_mod 必为 64/128/256（2 的幂） → 用 (mod-1) mask 替代 % 除法 */
    Uint32 pmod = ud->pal_mod ? ud->pal_mod : 256;
    Uint32 pmask = pmod - 1;
    const Uint32* pal = ud->pal;

    /* 主循环：每像素 1 次查表 + 1 次按位组合
     * pmask / pal 在外提，避免循环内重复读 ud 字段 */
    for (Uint32 y = 0; y < h; y++)
    {
        Uint32* row = dst + (size_t)y * stride;
        const Uint8* irow = idx_pixels + (size_t)y * w;
        const Uint8* arow = alpha_pixels ? (alpha_pixels + (size_t)y * w) : NULL;
        for (Uint32 x = 0; x < w; x++)
        {
            Uint32 alpha = arow ? arow[x] : 255;
            Uint32 color = pal[irow[x] & pmask];
            row[x] = (color & 0x00FFFFFF) | (alpha << 24);
        }
    }

    if (SDL_MUSTLOCK(sf))
        SDL_UnlockSurface(sf);

    SDL_SetSurfaceBlendMode(sf, SDL_BLENDMODE_BLEND);
    return sf;
}

/* ═══════════════════════════════════════════
 *  LRU cache（R8 优化版：与 pal_version 解耦）
 * ═══════════════════════════════════════════ */

/* 释放单条 cache entry 的 R8 三 buffer（不清 frame_id/lru_tick，留给 Insert 覆盖） */
static Uint64 JY_CacheEntryFree(JY_CacheEntry* e)
{
    Uint64 bytes;
    if (!e) return 0;
    bytes = JY_CacheEntryBytes(e);
    if (e->idx_pixels)   { SDL_free(e->idx_pixels);   e->idx_pixels = NULL; }
    if (e->alpha_pixels) { SDL_free(e->alpha_pixels); e->alpha_pixels = NULL; }
    if (e->depth)        { SDL_free(e->depth);        e->depth = NULL; }
    e->w = 0;
    e->h = 0;
    if (bytes)
    {
        JY_PerfEnsure();
        if (g_jy_perf.mutex) SDL_LockMutex(g_jy_perf.mutex);
        if (g_jy_perf.cache_bytes >= bytes)
            g_jy_perf.cache_bytes -= bytes;
        else
            g_jy_perf.cache_bytes = 0;
        if (g_jy_perf.mutex) SDL_UnlockMutex(g_jy_perf.mutex);
    }
    return bytes;
}

static JY_CacheEntry* JY_CacheLookup(JY_UserData* ud, Uint32 frame_id)
{
    if (!ud->cache)
        return NULL;
    for (Uint32 i = 0; i < ud->cache_cap; i++)
    {
        JY_CacheEntry* e = &ud->cache[i];
        /* 命中条件：idx_pixels 非空 + frame_id 匹配（不再比 pal_ver） */
        if (e->idx_pixels && e->frame_id == frame_id)
        {
            e->lru_tick = ++ud->cache_tick;
            JY_PerfEnsure();
            if (g_jy_perf.mutex) SDL_LockMutex(g_jy_perf.mutex);
            g_jy_perf.cache_hits++;
            if (g_jy_perf.mutex) SDL_UnlockMutex(g_jy_perf.mutex);
            return e;
        }
    }
    JY_PerfEnsure();
    if (g_jy_perf.mutex) SDL_LockMutex(g_jy_perf.mutex);
    g_jy_perf.cache_misses++;
    if (g_jy_perf.mutex) SDL_UnlockMutex(g_jy_perf.mutex);
    return NULL;
}

/* Cache 接管 idx + alpha + depth 三 buffer 所有权 — 调用方不再 free
 * idx 必须非 NULL；alpha / depth 可为 NULL */
static void JY_CacheInsert(
    JY_UserData* ud, Uint32 frame_id,
    Uint8* idx, Uint8* alpha, Uint16* depth,
    Uint16 w, Uint16 h)
{
    if (!ud->cache || !idx)
        return;

    /* 找空槽或 LRU 牺牲者 */
    Uint32 victim = 0;
    Uint32 min_tick = 0xFFFFFFFFu;
    for (Uint32 i = 0; i < ud->cache_cap; i++)
    {
        if (!ud->cache[i].idx_pixels)
        {
            victim = i;
            break;
        }
        if (ud->cache[i].lru_tick < min_tick)
        {
            min_tick = ud->cache[i].lru_tick;
            victim = i;
        }
    }

    JY_CacheEntry* e = &ud->cache[victim];
    if (e->idx_pixels)
    {
        JY_PerfEnsure();
        if (g_jy_perf.mutex) SDL_LockMutex(g_jy_perf.mutex);
        g_jy_perf.lru_evictions++;
        if (g_jy_perf.mutex) SDL_UnlockMutex(g_jy_perf.mutex);
    }
    JY_CacheEntryFree(e);

    e->idx_pixels   = idx;
    e->alpha_pixels = alpha;
    e->depth        = depth;
    e->w            = w;
    e->h            = h;
    e->frame_id     = frame_id;
    e->lru_tick     = ++ud->cache_tick;
    JY_PerfEnsure();
    if (g_jy_perf.mutex) SDL_LockMutex(g_jy_perf.mutex);
    g_jy_perf.cache_bytes += (Uint64)w * (Uint64)h
        + (alpha ? (Uint64)w * (Uint64)h : 0)
        + (depth ? (Uint64)w * (Uint64)h * sizeof(Uint16) : 0);
    g_jy_perf.decoded_frames++;
    if (g_jy_perf.mutex) SDL_UnlockMutex(g_jy_perf.mutex);
}

static void JY_CacheClear(JY_UserData* ud)
{
    Uint64 freed = 0;
    if (!ud->cache)
        return;
    for (Uint32 i = 0; i < ud->cache_cap; i++)
        freed += JY_CacheEntryFree(&ud->cache[i]);
    JY_PerfEnsure();
    if (g_jy_perf.mutex) SDL_LockMutex(g_jy_perf.mutex);
    g_jy_perf.cache_clear_count++;
    g_jy_perf.cache_clear_freed_bytes += freed;
    if (g_jy_perf.mutex) SDL_UnlockMutex(g_jy_perf.mutex);
}

static void JY_AsyncJobFree(JY_AsyncJob* job)
{
    if (!job) return;
    JY_FreeR8Triple(job->idx_pixels, job->alpha_pixels, job->depth);
    SDL_free(job);
}

static void JY_AsyncJobListFree(JY_AsyncJob* job)
{
    while (job)
    {
        JY_AsyncJob* next = job->next;
        job->next = NULL;
        JY_AsyncJobFree(job);
        job = next;
    }
}

static int JY_AsyncQueueHasFrame(JY_UserData* ud, Uint32 frame_id)
{
    JY_AsyncJob* p;
    if (ud->async_active && ud->async_active_frame == frame_id
        && ud->async_active_generation == ud->async_generation)
        return 1;
    for (p = ud->async_queue_head; p; p = p->next)
    {
        if (p->frame_id == frame_id && p->generation == ud->async_generation)
            return 1;
    }
    for (p = ud->async_done_head; p; p = p->next)
    {
        if (p->frame_id == frame_id && p->generation == ud->async_generation)
            return 1;
    }
    return 0;
}

static int JY_AsyncWorker(void* ptr)
{
    JY_UserData* ud = (JY_UserData*)ptr;
    for (;;)
    {
        JY_AsyncJob* job = NULL;
        SDL_LockMutex(ud->async_mutex);
        while (!ud->async_stop && !ud->async_queue_head)
            SDL_CondWait(ud->async_cond, ud->async_mutex);

        if (ud->async_stop)
        {
            JY_AsyncJob* discard = ud->async_queue_head;
            ud->async_queue_head = ud->async_queue_tail = NULL;
            ud->async_cancelled += ud->async_queued;
            ud->async_queued = 0;
            SDL_UnlockMutex(ud->async_mutex);
            JY_AsyncJobListFree(discard);
            break;
        }

        job = ud->async_queue_head;
        if (job)
        {
            ud->async_queue_head = job->next;
            if (!ud->async_queue_head)
                ud->async_queue_tail = NULL;
            job->next = NULL;
            ud->async_active = 1;
            ud->async_active_frame = job->frame_id;
            ud->async_active_generation = job->generation;
            if (ud->async_queued > 0)
                ud->async_queued--;
        }
        SDL_UnlockMutex(ud->async_mutex);

        if (!job)
            continue;

        job->ok = JY_DecodeFrameProfiled(ud, job->frame_id,
                                 &job->idx_pixels, &job->alpha_pixels, &job->depth,
                                 &job->w, &job->h);

        SDL_LockMutex(ud->async_mutex);
        ud->async_active = 0;
        ud->async_active_frame = 0;
        ud->async_active_generation = 0;
        if (ud->async_stop || job->generation != ud->async_generation)
        {
            ud->async_cancelled++;
            SDL_UnlockMutex(ud->async_mutex);
            JY_AsyncJobFree(job);
            continue;
        }

        if (job->ok)
            ud->async_decoded++;
        else
            ud->async_failed++;
        if (ud->async_done_tail)
            ud->async_done_tail->next = job;
        else
            ud->async_done_head = job;
        ud->async_done_tail = job;
        ud->async_ready++;
        SDL_UnlockMutex(ud->async_mutex);
    }
    return 0;
}

static int JY_AsyncEnsure(JY_UserData* ud)
{
    if (!ud)
        return 0;
    if (!ud->async_mutex)
    {
        ud->async_mutex = SDL_CreateMutex();
        if (!ud->async_mutex)
            return 0;
    }
    if (!ud->async_cond)
    {
        ud->async_cond = SDL_CreateCond();
        if (!ud->async_cond)
        {
            if (ud->async_mutex)
            {
                SDL_DestroyMutex(ud->async_mutex);
                ud->async_mutex = NULL;
            }
            return 0;
        }
    }
    if (!ud->async_thread)
    {
        ud->async_stop = 0;
        ud->async_thread = SDL_CreateThread(JY_AsyncWorker, "jy_decode", ud);
        if (!ud->async_thread)
        {
            if (ud->async_cond)
            {
                SDL_DestroyCond(ud->async_cond);
                ud->async_cond = NULL;
            }
            if (ud->async_mutex)
            {
                SDL_DestroyMutex(ud->async_mutex);
                ud->async_mutex = NULL;
            }
            return 0;
        }
    }
    return 1;
}

static void JY_AsyncCancelPending(JY_UserData* ud, int bump_generation)
{
    JY_AsyncJob *queue = NULL, *done = NULL;
    if (!ud || !ud->async_mutex)
        return;
    SDL_LockMutex(ud->async_mutex);
    if (bump_generation)
        ud->async_generation++;
    queue = ud->async_queue_head;
    done = ud->async_done_head;
    ud->async_queue_head = ud->async_queue_tail = NULL;
    ud->async_done_head = ud->async_done_tail = NULL;
    if (!ud->async_active)
    {
        ud->async_active_frame = 0;
        ud->async_active_generation = 0;
    }
    ud->async_cancelled += ud->async_queued + ud->async_ready;
    ud->async_queued = 0;
    ud->async_ready = 0;
    SDL_UnlockMutex(ud->async_mutex);
    JY_AsyncJobListFree(queue);
    JY_AsyncJobListFree(done);
}

static void JY_AsyncShutdown(JY_UserData* ud)
{
    if (!ud)
        return;
    if (ud->async_mutex)
    {
        SDL_LockMutex(ud->async_mutex);
        ud->async_stop = 1;
        if (ud->async_cond)
            SDL_CondSignal(ud->async_cond);
        SDL_UnlockMutex(ud->async_mutex);
    }
    if (ud->async_thread)
    {
        SDL_WaitThread(ud->async_thread, NULL);
        ud->async_thread = NULL;
    }
    JY_AsyncCancelPending(ud, 1);
    if (ud->async_cond)
    {
        SDL_DestroyCond(ud->async_cond);
        ud->async_cond = NULL;
    }
    if (ud->async_mutex)
    {
        SDL_DestroyMutex(ud->async_mutex);
        ud->async_mutex = NULL;
    }
}

static int JY_AsyncPollDecoded(JY_UserData* ud, Uint32 limit)
{
    Uint32 processed = 0;
    if (!ud || !ud->async_mutex)
        return 0;

    while (limit == 0 || processed < limit)
    {
        JY_AsyncJob* job = NULL;
        SDL_LockMutex(ud->async_mutex);
        job = ud->async_done_head;
        if (job)
        {
            ud->async_done_head = job->next;
            if (!ud->async_done_head)
                ud->async_done_tail = NULL;
            job->next = NULL;
            if (ud->async_ready > 0)
                ud->async_ready--;
        }
        SDL_UnlockMutex(ud->async_mutex);

        if (!job)
            break;

        if (job->ok && job->generation == ud->async_generation && ud->cache)
        {
            if (!JY_CacheLookup(ud, job->frame_id))
            {
                JY_CacheInsert(ud, job->frame_id,
                               job->idx_pixels, job->alpha_pixels, job->depth,
                               job->w, job->h);
                job->idx_pixels = NULL;
                job->alpha_pixels = NULL;
                job->depth = NULL;
            }
        }
        JY_AsyncJobFree(job);
        processed++;
    }
    return (int)processed;
}

/* ═══════════════════════════════════════════
 *  R8 cache 辅助（GetFrame / Composite 共享）
 * ═══════════════════════════════════════════ */

/* 释放 R8 三 buffer（任意指针可为 NULL） */
static SDL_INLINE void JY_FreeR8Triple(void* idx, void* alpha, void* depth)
{
    if (idx)   SDL_free(idx);
    if (alpha) SDL_free(alpha);
    if (depth) SDL_free(depth);
}

/* ═══════════════════════════════════════════
 *  R10/R12 Composite 内核辅助：全 layer 插入排序 + 链式 over alpha
 *  对齐 JS jinyi.min.js depthVs shader (sortSample + compare_color)
 * ═══════════════════════════════════════════ */

/* 单 layer 的 Composite 工作集（外层准备一次，内层热路径复用） */
typedef struct
{
    JY_UserData* ud;          /* 仅用于 owned 释放回调判断 */
    const Uint8*  idx;
    const Uint8*  alpha;
    const Uint16* depth;
    Uint16 lw, lh;
    int    base_x, base_y;    /* off_x - key_x, off_y - key_y */
    int    start_x, start_y;  /* 画布上有效像素起点（含） */
    int    end_x, end_y;      /* 画布上有效像素终点（不含） */
    Sint32 z_total;           /* -frame.z + z_bias，每像素累加 */
    int    index_offset;
    int    transparent;       /* 0 / 1 透明遮罩标志 */
    int    owned;             /* 1 = 调用方 free，0 = borrow */
    Uint32 frame_id;
    int    cache_after;
    const Uint32* pal;
    Uint32 pmask;
} JY_CompLayer;

/* ═══════════════════════════════════════════
 *  Lua API: GetFrame(id) → SDL_Surface*, {x,y,width,height}
 * ═══════════════════════════════════════════ */
static int JY_PushFrame(lua_State* L, SDL_Surface* sf, JY_FrameInfo* f)
{
    if (!sf)
        return 0;

    /* Push surface userdata */
    SDL_Surface** sfud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
    *sfud = sf;
    luaL_setmetatable(L, "SDL_Surface");

    /* Push info table */
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, (lua_Integer)f->sh);
    lua_setfield(L, -2, "height");
    lua_pushinteger(L, (lua_Integer)f->sw);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, (lua_Integer)f->key_x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, (lua_Integer)f->key_y);
    lua_setfield(L, -2, "y");

    return 2;
}

static int JY_GetFrame(lua_State* L)
{
    JY_UserData* ud = JY_Check(L, 1);
    lua_Integer idx = luaL_checkinteger(L, 2);
    if (idx < 0 || (Uint32)idx >= ud->frame_count)
        return luaL_error(L, "JY frame index out of range: %d (max %d)", (int)idx, (int)ud->frame_count);

    Uint32 id = (Uint32)idx;
    JY_FrameInfo* f = &ud->frames[id];
    JY_AsyncPollDecoded(ud, 4);

    /* ─── Cache 命中：按当前 pal[] 实时反查生成 ARGB8888 surface ───
     * R8 优化的核心：cache 内只存 idx + alpha + depth，不存 ARGB
     * 染色变化（pal_version++）后 cache 仍命中，省一次完整 DecodeFrame */
    if (ud->cache)
    {
        JY_CacheEntry* hit = JY_CacheLookup(ud, id);
        if (hit && hit->idx_pixels)
        {
            SDL_Surface* sf = JY_RenderFrameToSurface(
                ud, hit->idx_pixels, hit->alpha_pixels, hit->w, hit->h);
            if (sf)
                return JY_PushFrame(L, sf, f);
        }
    }

    /* ─── Cache miss：同步解码 R8 三 buffer + 渲染 + 入 cache ─── */
    Uint8 *idx_buf = NULL, *alpha_buf = NULL;
    Uint16* depth_buf = NULL;
    Uint16 w = 0, h = 0;
    if (!JY_DecodeFrameProfiled(ud, id, &idx_buf, &alpha_buf, &depth_buf, &w, &h))
        return 0;

    SDL_Surface* sf = JY_RenderFrameToSurface(ud, idx_buf, alpha_buf, w, h);
    if (!sf)
    {
        JY_FreeR8Triple(idx_buf, alpha_buf, depth_buf);
        return 0;
    }

    /* cache 接管 R8 三 buffer 所有权；无 cache 时直接释放 */
    if (ud->cache)
    {
        JY_CacheInsert(ud, id, idx_buf, alpha_buf, depth_buf, w, h);
    }
    else
    {
        JY_FreeR8Triple(idx_buf, alpha_buf, depth_buf);
    }

    return JY_PushFrame(L, sf, f);
}

static int JY_GetFrameTexture(lua_State* L)
{
    JY_UserData* ud = JY_Check(L, 1);
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 2, "SDL_Renderer");
    lua_Integer idx = luaL_checkinteger(L, 3);
    Uint32 id;
    JY_FrameInfo* f;
    JY_CacheEntry* hit;
    SDL_Surface* sf;
    SDL_Texture* tex;
    Uint64 start_us;

    if (idx < 0 || (Uint32)idx >= ud->frame_count)
        return luaL_error(L, "JY frame index out of range: %d (max %d)", (int)idx, (int)ud->frame_count);

    id = (Uint32)idx;
    f = &ud->frames[id];
    JY_AsyncPollDecoded(ud, 4);
    hit = JY_CacheLookup(ud, id);
    if (!hit || !hit->idx_pixels)
        return 0;

    sf = JY_RenderFrameToSurface(ud, hit->idx_pixels, hit->alpha_pixels, hit->w, hit->h);
    if (!sf)
        return 0;

    start_us = JY_NowUS();
    tex = SDL_CreateTextureFromSurface(rd, sf);
    SDL_FreeSurface(sf);
    if (!tex)
        return 0;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    JY_RecordUpload(JY_NowUS() - start_us);

    if (!JY_PushTexture(L, tex))
        return 0;

    lua_createtable(L, 0, 4);
    lua_pushinteger(L, (lua_Integer)f->sh);
    lua_setfield(L, -2, "height");
    lua_pushinteger(L, (lua_Integer)f->sw);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, (lua_Integer)f->key_x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, (lua_Integer)f->key_y);
    lua_setfield(L, -2, "y");
    return 2;
}

static int JY_GetFrameReady(lua_State* L)
{
    JY_UserData* ud = JY_Check(L, 1);
    lua_Integer idx = luaL_checkinteger(L, 2);
    if (idx < 0 || (Uint32)idx >= ud->frame_count)
        return luaL_error(L, "JY frame index out of range: %d (max %d)", (int)idx, (int)ud->frame_count);

    Uint32 id = (Uint32)idx;
    JY_FrameInfo* f = &ud->frames[id];
    JY_AsyncPollDecoded(ud, 4);

    if (ud->cache)
    {
        JY_CacheEntry* hit = JY_CacheLookup(ud, id);
        if (hit && hit->idx_pixels)
        {
            SDL_Surface* sf = JY_RenderFrameToSurface(
                ud, hit->idx_pixels, hit->alpha_pixels, hit->w, hit->h);
            if (sf)
                return JY_PushFrame(L, sf, f);
        }
    }
    return 0;
}

static int JY_GetFrameInfo(lua_State* L)
{
    JY_UserData* ud = JY_Check(L, 1);
    lua_Integer idx = luaL_checkinteger(L, 2);
    if (idx < 0 || (Uint32)idx >= ud->frame_count)
        return luaL_error(L, "JY frame index out of range: %d (max %d)", (int)idx, (int)ud->frame_count);

    JY_FrameInfo* f = &ud->frames[(Uint32)idx];
    lua_createtable(L, 0, 5);
    lua_pushinteger(L, (lua_Integer)f->sw);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, (lua_Integer)f->sh);
    lua_setfield(L, -2, "height");
    lua_pushinteger(L, (lua_Integer)f->key_x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, (lua_Integer)f->key_y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, (lua_Integer)f->z);
    lua_setfield(L, -2, "z");
    return 1;
}

/* ═══════════════════════════════════════════
 *  Lua API: SetPal(pal_string)  — 1024 bytes BGRA
 * ═══════════════════════════════════════════ */
static int JY_SetPal(lua_State* L)
{
    JY_UserData* ud = JY_Check(L, 1);
    size_t len;
    const char* pal = luaL_checklstring(L, 2, &len);
    if (len >= 1024)
    {
        /* BGRA → ARGB8888 conversion (matching view.py BMP palette) */
        const Uint8* pb = (const Uint8*)pal;
        for (Uint32 i = 0; i < 256; i++)
        {
            Uint8 b = pb[i * 4 + 0];
            Uint8 g = pb[i * 4 + 1];
            Uint8 r = pb[i * 4 + 2];
            Uint8 a = pb[i * 4 + 3];
            ud->pal[i] = ((Uint32)a << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
        }
        JY_CalcPalMod(ud);
        ud->pal_version++;
    }
    else if (len >= 768)
    {
        /* BMP pixel data: BGR format (256*3 bytes) */
        const Uint8* pb = (const Uint8*)pal;
        for (Uint32 i = 0; i < 256; i++)
        {
            Uint8 b = pb[i * 3 + 0];
            Uint8 g = pb[i * 3 + 1];
            Uint8 r = pb[i * 3 + 2];
            ud->pal[i] = (255u << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
        }
        JY_CalcPalMod(ud);
        ud->pal_version++;
    }
    return 0;
}

/* ═══════════════════════════════════════════
 *  Lua API: GetPal() → pal_string (1024 bytes, BGRA format for Lua compat)
 * ═══════════════════════════════════════════ */
static int JY_GetPal(lua_State* L)
{
    JY_UserData* ud = JY_Check(L, 1);
    /* Convert internal ARGB8888 → BGRA for Lua compatibility */
    Uint8 buf[1024];
    for (Uint32 i = 0; i < 256; i++)
    {
        Uint32 c = ud->pal[i];
        buf[i * 4 + 0] = (Uint8)(c & 0xFF);         /* B */
        buf[i * 4 + 1] = (Uint8)((c >> 8) & 0xFF);  /* G */
        buf[i * 4 + 2] = (Uint8)((c >> 16) & 0xFF); /* R */
        buf[i * 4 + 3] = (Uint8)((c >> 24) & 0xFF); /* A */
    }
    lua_pushlstring(L, (const char*)buf, 1024);
    return 1;
}

/* ═══════════════════════════════════════════
 *  Lua API: SetPP(R1..R3, G1..G3, B1..B3, H, S, L)
 *  12‑param color matrix, matching tcp's SetPP
 * ═══════════════════════════════════════════ */
static int JY_SetPP(lua_State* L)
{
    JY_UserData* ud = JY_Check(L, 1);
    int top = lua_gettop(L);
    if (top < 13) /* self + 12 params */
        return 0;

    /* Read 9 matrix params + 3 HSL shifts */
    int params[12];
    for (int i = 0; i < 12; i++)
        params[i] = (int)luaL_checkinteger(L, i + 2);

    /* Apply color matrix to palette entries */
    for (Uint32 i = 0; i < ud->pal_count; i++)
    {
        Uint32 c = ud->pal[i];
        Uint32 r = (c >> 16) & 0xFF;
        Uint32 g = (c >> 8) & 0xFF;
        Uint32 b = c & 0xFF;
        Uint32 a = (c >> 24) & 0xFF;

        Uint32 rr = (r * params[0] + g * params[1] + b * params[2]) >> 8;
        Uint32 gg = (r * params[3] + g * params[4] + b * params[5]) >> 8;
        Uint32 bb = (r * params[6] + g * params[7] + b * params[8]) >> 8;

        if (rr > 255) rr = 255;
        if (gg > 255) gg = 255;
        if (bb > 255) bb = 255;

        ud->pal[i] = (a << 24) | (rr << 16) | (gg << 8) | bb;
    }

    JY_CalcPalMod(ud);
    ud->pal_version++;
    return 0;
}

/* ═══════════════════════════════════════════
 *  Lua API: set_palette — combined handler (tcp compat)
 *  If arg2 is string → SetPal; if top >= 13 → SetPP
 * ═══════════════════════════════════════════ */
static int JY_SetPalette(lua_State* L)
{
    int top = lua_gettop(L);
    if (top < 2)
        return 0;
    if (lua_type(L, 2) == LUA_TSTRING)
        return JY_SetPal(L);
    if (top >= 13)
        return JY_SetPP(L);
    return 0;
}

/* ═══════════════════════════════════════════
 *  Lua API: Prefetch(from, to) — sync fill cache
 * ═══════════════════════════════════════════ */
static int JY_Prefetch(lua_State* L)
{
    JY_UserData* ud = JY_Check(L, 1);
    Uint32 from = (Uint32)luaL_checkinteger(L, 2);
    Uint32 to   = (Uint32)luaL_checkinteger(L, 3);

    if (from > to || to >= ud->frame_count)
        return 0;

    for (Uint32 id = from; id <= to; id++)
    {
        /* 已缓存则跳过（R8 cache 与调色板版本无关）*/
        JY_CacheEntry* hit = JY_CacheLookup(ud, id);
        if (hit)
            continue;

        Uint8 *idx = NULL, *alpha = NULL;
        Uint16* depth = NULL;
        Uint16 w = 0, h = 0;
        if (JY_DecodeFrameProfiled(ud, id, &idx, &alpha, &depth, &w, &h))
        {
            if (ud->cache)
                JY_CacheInsert(ud, id, idx, alpha, depth, w, h);
            else
                JY_FreeR8Triple(idx, alpha, depth);
        }
    }

    return 0;
}

static int JY_RequestFrame(lua_State* L)
{
    JY_UserData* ud = JY_Check(L, 1);
    lua_Integer idx = luaL_checkinteger(L, 2);
    if (idx < 0 || (Uint32)idx >= ud->frame_count)
    {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "frame index out of range");
        return 2;
    }

    Uint32 id = (Uint32)idx;
    JY_AsyncPollDecoded(ud, 4);
    if (JY_CacheLookup(ud, id))
    {
        lua_pushboolean(L, 1);
        lua_pushstring(L, "ready");
        return 2;
    }

    if (!JY_AsyncEnsure(ud))
    {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "async init failed");
        return 2;
    }

    SDL_LockMutex(ud->async_mutex);
    if (JY_AsyncQueueHasFrame(ud, id))
    {
        SDL_UnlockMutex(ud->async_mutex);
        lua_pushboolean(L, 0);
        lua_pushstring(L, "pending");
        return 2;
    }
    if (ud->async_queued >= 128)
    {
        SDL_UnlockMutex(ud->async_mutex);
        lua_pushboolean(L, 0);
        lua_pushstring(L, "queue full");
        return 2;
    }

    JY_AsyncJob* job = (JY_AsyncJob*)SDL_calloc(1, sizeof(JY_AsyncJob));
    if (!job)
    {
        SDL_UnlockMutex(ud->async_mutex);
        lua_pushboolean(L, 0);
        lua_pushstring(L, "out of memory");
        return 2;
    }
    job->frame_id = id;
    job->generation = ud->async_generation;
    if (ud->async_queue_tail)
        ud->async_queue_tail->next = job;
    else
        ud->async_queue_head = job;
    ud->async_queue_tail = job;
    ud->async_queued++;
    ud->async_submitted++;
    SDL_CondSignal(ud->async_cond);
    SDL_UnlockMutex(ud->async_mutex);

    lua_pushboolean(L, 0);
    lua_pushstring(L, "queued");
    return 2;
}

int JY_NativePollAsync(JY_UserData* ud, Uint32 limit)
{
    return JY_AsyncPollDecoded(ud, limit);
}

int JY_NativeIsFrameDecoded(JY_UserData* ud, Uint32 id)
{
    if (!ud || id >= ud->frame_count)
        return 0;
    JY_AsyncPollDecoded(ud, 4);
    return JY_CacheLookup(ud, id) != NULL;
}

int JY_NativeRequestFrame(JY_UserData* ud, Uint32 id, const char** status)
{
    JY_AsyncJob* job;

    if (status) *status = "error";
    if (!ud || id >= ud->frame_count)
    {
        if (status) *status = "frame index out of range";
        return MYGXY_ASYNC_FRAME_ERROR;
    }

    JY_AsyncPollDecoded(ud, 4);
    if (JY_CacheLookup(ud, id))
    {
        if (status) *status = "ready";
        return MYGXY_ASYNC_FRAME_READY;
    }

    if (!JY_AsyncEnsure(ud))
    {
        if (status) *status = "async init failed";
        return MYGXY_ASYNC_FRAME_ERROR;
    }

    SDL_LockMutex(ud->async_mutex);
    if (JY_AsyncQueueHasFrame(ud, id))
    {
        SDL_UnlockMutex(ud->async_mutex);
        if (status) *status = "pending";
        return MYGXY_ASYNC_FRAME_PENDING;
    }
    if (ud->async_queued >= 128)
    {
        SDL_UnlockMutex(ud->async_mutex);
        if (status) *status = "queue full";
        return MYGXY_ASYNC_FRAME_QUEUE_FULL;
    }

    job = (JY_AsyncJob*)SDL_calloc(1, sizeof(JY_AsyncJob));
    if (!job)
    {
        SDL_UnlockMutex(ud->async_mutex);
        if (status) *status = "out of memory";
        return MYGXY_ASYNC_FRAME_ERROR;
    }
    job->frame_id = id;
    job->generation = ud->async_generation;
    if (ud->async_queue_tail)
        ud->async_queue_tail->next = job;
    else
        ud->async_queue_head = job;
    ud->async_queue_tail = job;
    ud->async_queued++;
    ud->async_submitted++;
    SDL_CondSignal(ud->async_cond);
    SDL_UnlockMutex(ud->async_mutex);

    if (status) *status = "queued";
    return MYGXY_ASYNC_FRAME_QUEUED;
}

static int JY_PollAsync(lua_State* L)
{
    JY_UserData* ud = JY_Check(L, 1);
    Uint32 limit = (Uint32)luaL_optinteger(L, 2, 8);
    lua_pushinteger(L, (lua_Integer)JY_AsyncPollDecoded(ud, limit));
    return 1;
}

static int JY_IsFrameDecoded(lua_State* L)
{
    JY_UserData* ud = JY_Check(L, 1);
    lua_Integer idx = luaL_checkinteger(L, 2);
    if (idx < 0 || (Uint32)idx >= ud->frame_count)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    JY_AsyncPollDecoded(ud, 4);
    lua_pushboolean(L, JY_CacheLookup(ud, (Uint32)idx) != NULL);
    return 1;
}

static int JY_AsyncStats(lua_State* L)
{
    JY_UserData* ud = JY_Check(L, 1);
    if (ud)
        JY_AsyncPollDecoded(ud, 0);
    lua_createtable(L, 0, 8);
    lua_pushinteger(L, (lua_Integer)(ud ? ud->async_queued : 0));
    lua_setfield(L, -2, "queued");
    lua_pushinteger(L, (lua_Integer)(ud ? ud->async_ready : 0));
    lua_setfield(L, -2, "ready");
    lua_pushinteger(L, (lua_Integer)(ud ? ud->async_submitted : 0));
    lua_setfield(L, -2, "submitted");
    lua_pushinteger(L, (lua_Integer)(ud ? ud->async_decoded : 0));
    lua_setfield(L, -2, "decoded");
    lua_pushinteger(L, (lua_Integer)(ud ? ud->async_failed : 0));
    lua_setfield(L, -2, "failed");
    lua_pushinteger(L, (lua_Integer)(ud ? ud->async_cancelled : 0));
    lua_setfield(L, -2, "cancelled");
    lua_pushboolean(L, ud && ud->async_thread != NULL);
    lua_setfield(L, -2, "worker");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "native_decode");
    return 1;
}

/* ═══════════════════════════════════════════
 *  Lua API: CacheClear() — manually free LRU SDL_Surface cache
 *  ★ 用于场景切换 / 战斗结束时主动释放 C 层 SDL_Surface 缓存（每个 jy 最多 ~44MB），
 *     不需等待 Lua GC 触发 __gc → JY_Reset。Lua 层 jy:清理缓存() 调用。
 *     调用后 cache 数组保留（容量不变），仅 surface/depth 内存释放，
 *     下次访问时按需重新 GetFrame 解码并填充。
 * ═══════════════════════════════════════════ */
static int JY_LUA_CacheClear(lua_State* L)
{
    JY_UserData* ud = JY_Check(L, 1);
    if (ud)
    {
        JY_AsyncCancelPending(ud, 1);
        JY_CacheClear(ud);
    }
    return 0;
}

/* ══════════════════════════════════════════
 *  Lua API: SetCacheCap(cap)
 *  ★ 动态调整帧缓存容量，供 Lua 层按场景优化内存：
 *     - 大世界/战斗：默认 32 帧足够
 *     - 霓裳宝阁拖动预览：可临时调高到 64~128 减少重解码
 *     - 内存压力微高：可调低到 16 进一步压缩
 *  边界：1 ≤ cap ≤ 256（其它越界值被限位到此区间）
 * 线程安全：当前 jy cache 仅在主 Lua 线程访问，不再启动后台解码线程
 *  返回值：boolean（true=调整成功，false=参数无效 / realloc 失败）
 * ══════════════════════════════════════════ */
static int JY_LUA_SetCacheCap(lua_State* L)
{
    JY_UserData* ud = JY_Check(L, 1);
    if (!ud || !ud->cache)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    /* 参数边界保护：1 ≤ cap ≤ 256 */
    int arg = (int)luaL_checkinteger(L, 2);
    if (arg < 1) arg = 1;
    if (arg > 256) arg = 256;
    Uint32 new_cap = (Uint32)arg;

    if (new_cap == ud->cache_cap)
    {
        lua_pushboolean(L, 1);
        return 1;
    }

    int ok = 1;

    if (new_cap < ud->cache_cap)
    {
        /* ── 缩容：按 LRU 淘汰 → 压缩 → realloc ──
         * R8 优化：判存活以 idx_pixels 为准（surface 字段已移除） */

        /* 1. 统计存活条目数 */
        Uint32 alive = 0;
        for (Uint32 i = 0; i < ud->cache_cap; i++)
            if (ud->cache[i].idx_pixels) alive++;

        /* 2. 超出 new_cap 部分按 lru_tick 升序逐个释放 */
        while (alive > new_cap)
        {
            Uint32 victim = 0;
            Uint32 min_tick = 0xFFFFFFFFu;
            int found = 0;
            for (Uint32 i = 0; i < ud->cache_cap; i++)
            {
                if (ud->cache[i].idx_pixels && ud->cache[i].lru_tick < min_tick)
                {
                    min_tick = ud->cache[i].lru_tick;
                    victim = i;
                    found = 1;
                }
            }
            if (!found) break;
            /* JY_CacheEntryFree 统一释放 idx + alpha + depth 三 buffer */
            JY_CacheEntryFree(&ud->cache[victim]);
            alive--;
        }

        /* 3. 压缩：将存活条目搬到数组前 new_cap 个槽位 */
        Uint32 dst = 0;
        for (Uint32 i = 0; i < ud->cache_cap && dst < new_cap; i++)
        {
            if (ud->cache[i].idx_pixels)
            {
                if (i != dst)
                {
                    ud->cache[dst] = ud->cache[i];
                    SDL_memset(&ud->cache[i], 0, sizeof(JY_CacheEntry));
                }
                dst++;
            }
        }

        /* 4. realloc 缩容 */
        JY_CacheEntry* nc = (JY_CacheEntry*)SDL_realloc(ud->cache, new_cap * sizeof(JY_CacheEntry));
        if (nc)
        {
            ud->cache = nc;
            ud->cache_cap = new_cap;
        }
        else
            ok = 0;  /* realloc 失败，保留原 cache 不变 */
    }
    else
    {
        /* ── 扩容：realloc + 清零新增槽位 ── */
        JY_CacheEntry* nc = (JY_CacheEntry*)SDL_realloc(ud->cache, new_cap * sizeof(JY_CacheEntry));
        if (nc)
        {
            SDL_memset(&nc[ud->cache_cap], 0, (new_cap - ud->cache_cap) * sizeof(JY_CacheEntry));
            ud->cache = nc;
            ud->cache_cap = new_cap;
        }
        else
            ok = 0;
    }

    lua_pushboolean(L, ok);
    return 1;
}

/* ═══════════════════════════════════════════
 *  Lua API: Composite / CompositeTo  ★ R11：抽出三阶段辅助函数,支持 surface 复用
 *
 *    Composite(canvas_w, canvas_h, layers_table)
 *      → 每帧新建 ARGB8888 surface,返回该 userdata(GC 自动释放)
 *
 *    CompositeTo(target_surface, canvas_w, canvas_h, layers_table)
 *      → 写入调用方持有的 ARGB8888 surface,消除每帧 SDL_CreateRGBSurfaceWithFormat
 *        ~230KB 分配/释放(密集场景手机端 heap 锁竞争 + 分页抖动主因)
 *      → target 必须 ARGB8888 且 w>=canvas_w,h>=canvas_h;返回 bool
 *
 *  layers_table = { {ud, frame_id, z_bias, x, y, index_offset, transparent}, ... }
 *
 *  算法（与 JS jinyi.min.js depthVs shader 等价）:
 *    1) 每像素维护实际 layer 数插入排序数组（dep 升序，远→近）
 *    2) 逐层 over alpha 链式混合（compare_color 等价）
 *    3) 调色板 indexOffset 偏移（对齐 JS shader 中 r.indexOffset 公式）
 *    4) transparent=1 时清空累积色（layer mask 路径）
 *
 *  effective_d = pixel_depth(G/B 通道) - frame.z + z_bias
 * ═══════════════════════════════════════════ */

/* ─── 阶段 1 辅助：构建 layer 工作集（解码 buffer + 几何裁剪）
 *      返回:实际有效 layer 数(>=0);-1 表示 SDL_calloc 失败,调用方需 luaL_error */
static int JY_BuildLayerSet(lua_State* L, int layers_idx, int canvas_w, int canvas_h,
                            JY_CompLayer** out_layers)
{
    *out_layers = NULL;
    int layer_argc = (int)lua_rawlen(L, layers_idx);
    if (layer_argc <= 0)
        return 0;

    JY_CompLayer* layers = (JY_CompLayer*)SDL_calloc(layer_argc, sizeof(JY_CompLayer));
    if (!layers)
        return -1;

    int layer_n = 0;
    for (int i = 1; i <= layer_argc; i++)
    {
        lua_rawgeti(L, layers_idx, i);
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            continue;
        }

        /* 7 字段 layer entry: {ud, frame_id, z_bias, off_x, off_y, index_offset, transparent} */
        lua_rawgeti(L, -1, 1);
        JY_UserData* layer_ud = (JY_UserData*)luaL_testudata(L, -1, JY_MT);
        lua_pop(L, 1);
        if (!layer_ud) { lua_pop(L, 1); continue; }

        lua_rawgeti(L, -1, 2); Uint32 frame_id = (Uint32)lua_tointeger(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, -1, 3); int z_bias       = (int)lua_tointeger(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, -1, 4); int off_x        = (int)lua_tointeger(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, -1, 5); int off_y        = (int)lua_tointeger(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, -1, 6); int index_offset = (int)lua_tointeger(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, -1, 7); int transparent  = (int)lua_tointeger(L, -1); lua_pop(L, 1);

        lua_pop(L, 1); /* pop layer entry table */

        if (frame_id >= layer_ud->frame_count)
            continue;

        /* 取 layer 三 buffer：单线程缓存路径，直接 borrow cache 内 buffer */
        const Uint8*  l_idx   = NULL;
        const Uint8*  l_alpha = NULL;
        const Uint16* l_depth = NULL;
        Uint16 lw16 = 0, lh16 = 0;
        int    owned = 0;
        int    cache_after = 0;

        if (layer_ud->cache)
        {
            JY_CacheEntry* hit = JY_CacheLookup(layer_ud, frame_id);
            if (hit && hit->idx_pixels)
            {
                l_idx   = hit->idx_pixels;
                l_alpha = hit->alpha_pixels;
                l_depth = hit->depth;
                lw16 = hit->w; lh16 = hit->h;
            }
        }

        /* Cache miss → 同步解码 R8 三 buffer */
        if (!l_idx)
        {
            Uint8 *decode_idx = NULL, *decode_alpha = NULL;
            Uint16* decode_depth = NULL;
            Uint16 dw = 0, dh = 0;
            if (!JY_DecodeFrameProfiled(layer_ud, frame_id,
                                &decode_idx, &decode_alpha, &decode_depth, &dw, &dh))
                continue;

            l_idx = decode_idx; l_alpha = decode_alpha; l_depth = decode_depth;
            lw16 = dw; lh16 = dh;
            owned = 1;
            if (layer_ud->cache)
                cache_after = 1;
        }

        /* 几何：base = off - frame.key（每个 layer 自己的锚点对齐到外部传入 off）
         *   等价 JS shader 中 keyX = I.x - F.x，画布上像素 px 对应 layer 局部 lx = px - base_x */
        int anchor_x = 0, anchor_y = 0;
        Sint16 frame_z = 0;
        if (layer_ud->frames && frame_id < layer_ud->frame_count)
        {
            anchor_x = (int)layer_ud->frames[frame_id].key_x;
            anchor_y = (int)layer_ud->frames[frame_id].key_y;
            frame_z  = layer_ud->frames[frame_id].z;
        }
        int base_x = off_x - anchor_x;
        int base_y = off_y - anchor_y;
        int lw = (int)lw16;
        int lh = (int)lh16;

        /* 画布裁剪边界（avoid per-pixel branch in inner loop） */
        int sx = base_x < 0 ? 0 : base_x;
        int sy = base_y < 0 ? 0 : base_y;
        int ex = base_x + lw > canvas_w ? canvas_w : base_x + lw;
        int ey = base_y + lh > canvas_h ? canvas_h : base_y + lh;
        if (sx >= ex || sy >= ey)
        {
            if (owned) JY_FreeR8Triple((void*)l_idx, (void*)l_alpha, (void*)l_depth);
            continue;
        }

        JY_CompLayer* L_slot = &layers[layer_n++];
        L_slot->ud           = layer_ud;
        L_slot->idx          = l_idx;
        L_slot->alpha        = l_alpha;
        L_slot->depth        = l_depth;
        L_slot->lw           = lw16;
        L_slot->lh           = lh16;
        L_slot->base_x       = base_x;
        L_slot->base_y       = base_y;
        L_slot->start_x      = sx;
        L_slot->start_y      = sy;
        L_slot->end_x        = ex;
        L_slot->end_y        = ey;
        L_slot->z_total      = z_bias - (Sint32)frame_z;
        L_slot->index_offset = index_offset;
        L_slot->transparent  = transparent;
        L_slot->owned        = owned;
        L_slot->frame_id     = frame_id;
        L_slot->cache_after  = cache_after;
        L_slot->pal          = layer_ud->pal;
        {
            Uint32 pmod = layer_ud->pal_mod ? layer_ud->pal_mod : 256;
            L_slot->pmask = pmod - 1;
        }
    }

    *out_layers = layers;
    return layer_n;
}

/* ─── 阶段 2 辅助：内核（外层 pixel × 内层 layer，写入 dst 左上 canvas 区域）
 *      要求:dst 已锁定且为 ARGB8888;返回 0 表示工作数组分配失败 */
static int JY_RunCompositeKernel(SDL_Surface* dst, const JY_CompLayer* layers, int layer_n,
                                 int canvas_w, int canvas_h)
{
    if (layer_n <= 0)
        return 1; /* 没有 layer,内核什么也不做(target 已被调用方清零) */

    int* active_layers = (int*)SDL_malloc((size_t)layer_n * sizeof(int));
    if (!active_layers)
        return 0;

    Uint32* dst_pixels = (Uint32*)dst->pixels;
    Uint32 dst_stride = (Uint32)(dst->pitch / 4);

    Sint32* dep_arr = (Sint32*)SDL_malloc((size_t)layer_n * sizeof(Sint32));
    Uint32* col_arr = (Uint32*)SDL_malloc((size_t)layer_n * sizeof(Uint32));
    Uint8*  alf_arr = (Uint8*) SDL_malloc((size_t)layer_n * sizeof(Uint8));
    Uint8*  trs_arr = (Uint8*) SDL_malloc((size_t)layer_n * sizeof(Uint8));
    if (!dep_arr || !col_arr || !alf_arr || !trs_arr)
    {
        if (dep_arr) SDL_free(dep_arr);
        if (col_arr) SDL_free(col_arr);
        if (alf_arr) SDL_free(alf_arr);
        if (trs_arr) SDL_free(trs_arr);
        SDL_free(active_layers);
        return 0;
    }

    /* per-pixel 动态槽数组，CPU 路径直接保留全部 layer。
     * 官方 WebGL shader 受 sampler 数限制以 8 层分批 + depthtex 合并；
     * 这里不受该限制，展开后更接近完整的跨层深度排序，避免复杂锦衣把 horse mask 挤掉。 */
    for (int py = 0; py < canvas_h; py++)
    {
        Uint32* dst_row = dst_pixels + (size_t)py * dst_stride;
        int active_n = 0;
        for (int li = 0; li < layer_n; li++)
        {
            const JY_CompLayer* L_p = &layers[li];
            if (py >= L_p->start_y && py < L_p->end_y)
                active_layers[active_n++] = li;
        }

        for (int px = 0; px < canvas_w; px++)
        {
            int    n_slot = 0;

            for (int ai = 0; ai < active_n; ai++)
            {
                const JY_CompLayer* L_p = &layers[active_layers[ai]];
                if (px < L_p->start_x || px >= L_p->end_x)
                    continue;

                int lx = px - L_p->base_x;
                int ly = py - L_p->base_y;
                size_t loff = (size_t)ly * L_p->lw + lx;

                Uint8 sa = L_p->alpha ? L_p->alpha[loff] : 255;
                if (sa == 0)
                    continue;

                Uint16 d = (sa < 77) ? 0 : (L_p->depth ? L_p->depth[loff] : 0);
                Sint32 dep = (Sint32)d + L_p->z_total;

                /* 调色板查表（带 indexOffset，对齐 JS shader）*/
                int pal_idx = (int)L_p->idx[loff] - L_p->index_offset;
                if (pal_idx < 0) pal_idx = 0;
                Uint32 col = L_p->pal[(Uint32)pal_idx & L_p->pmask];

                /* sortSample：与 JS shader 完全等价
                 *   dep_arr 维持升序：[0]=最小dep(最远)，[n-1]=最大dep(最近)
                 *   后续 compare_color 从 i=0(远) over 到 i=n-1(近)，物理意义：远的在底、近的在顶
                 *   同深度时后来的 layer 插到前面，使先来的 layer 保持在视觉上层（对齐 JS dep<=depArr+0.1） */
                int slot = 0;
                while (slot < n_slot && dep > dep_arr[slot]) slot++;
                /* 现在 dep_arr[0..slot-1] < dep <= dep_arr[slot..n_slot-1] */
                for (int i = n_slot; i > slot; i--)
                {
                    dep_arr[i] = dep_arr[i-1];
                    col_arr[i] = col_arr[i-1];
                    alf_arr[i] = alf_arr[i-1];
                    trs_arr[i] = trs_arr[i-1];
                }
                dep_arr[slot] = dep;
                col_arr[slot] = col;
                alf_arr[slot] = sa;
                trs_arr[slot] = (Uint8)(L_p->transparent ? 1 : 0);
                n_slot++;
            }

            if (n_slot == 0)
                continue;

            /* compare_color：从 i=0（最远）→ n-1（最近）顺序 over alpha 混合
             *   over 算子 acc = acc*(1-sa) + new：最后参与循环的(最近)在视觉最上层
             *   累积值在 premultiplied alpha 空间内运算，输出时反预乘
             *   trans=1 时清空 cur_color（layer mask 行为，对齐 JS shader） */
            Uint32 acc_a = 0, acc_r = 0, acc_g = 0, acc_b = 0;
            for (int i = 0; i < n_slot; i++)
            {
                if (trs_arr[i])
                {
                    acc_a = acc_r = acc_g = acc_b = 0;
                    continue;
                }
                Uint32 sa = alf_arr[i];
                if (sa == 0) continue;

                Uint32 c = col_arr[i];
                Uint32 cr = (c >> 16) & 0xFF;
                Uint32 cg = (c >> 8)  & 0xFF;
                Uint32 cb =  c        & 0xFF;
                /* 预乘：color.rgb *= sa */
                Uint32 pr = (cr * sa + 127u) / 255u;
                Uint32 pg = (cg * sa + 127u) / 255u;
                Uint32 pb = (cb * sa + 127u) / 255u;
                /* over: acc = acc*(1-sa/255) + (sa, pr, pg, pb) */
                Uint32 inv = 255u - sa;
                acc_a = (acc_a * inv + 127u) / 255u + sa;
                acc_r = (acc_r * inv + 127u) / 255u + pr;
                acc_g = (acc_g * inv + 127u) / 255u + pg;
                acc_b = (acc_b * inv + 127u) / 255u + pb;
                if (acc_a > 255u) acc_a = 255u;
                if (acc_r > 255u) acc_r = 255u;
                if (acc_g > 255u) acc_g = 255u;
                if (acc_b > 255u) acc_b = 255u;
            }
            if (acc_a == 0)
                continue;

            /* 反预乘：ARGB8888 输出（PixiJS 默认管线非预乘）*/
            Uint32 final_r = (acc_r * 255u + acc_a / 2u) / acc_a;
            Uint32 final_g = (acc_g * 255u + acc_a / 2u) / acc_a;
            Uint32 final_b = (acc_b * 255u + acc_a / 2u) / acc_a;
            if (final_r > 255u) final_r = 255u;
            if (final_g > 255u) final_g = 255u;
            if (final_b > 255u) final_b = 255u;

            dst_row[px] = (acc_a << 24) | (final_r << 16) | (final_g << 8) | final_b;
        }
    }

    SDL_free(dep_arr);
    SDL_free(col_arr);
    SDL_free(alf_arr);
    SDL_free(trs_arr);
    SDL_free(active_layers);
    return 1;
}

/* ─── 阶段 3 辅助：释放 owned buffer + cache 回填 + layers 数组 */
static void JY_ReleaseLayerSet(JY_CompLayer* layers, int layer_n)
{
    if (!layers) return;
    for (int i = 0; i < layer_n; i++)
    {
        if (layers[i].owned)
        {
            if (layers[i].cache_after && layers[i].ud && layers[i].ud->cache)
            {
                JY_CacheInsert(layers[i].ud, layers[i].frame_id,
                               (Uint8*)layers[i].idx, (Uint8*)layers[i].alpha,
                               (Uint16*)layers[i].depth, layers[i].lw, layers[i].lh);
            }
            else
            {
                JY_FreeR8Triple((void*)layers[i].idx, (void*)layers[i].alpha, (void*)layers[i].depth);
            }
        }
    }
    SDL_free(layers);
}

/* ─── Lua API: Composite — 每帧新建 surface 路径(向后兼容) ─── */
static int JY_Composite(lua_State* L)
{
    /* arg 1: self (jy ud — used as anchor / surface 工厂宿主) */
    JY_UserData* ud = JY_Check(L, 1);
    (void)ud;
    int canvas_w = (int)luaL_checkinteger(L, 2);
    int canvas_h = (int)luaL_checkinteger(L, 3);
    luaL_checktype(L, 4, LUA_TTABLE);

    if (canvas_w <= 0 || canvas_h <= 0)
        return 0;

    JY_CompLayer* layers = NULL;
    int layer_n = JY_BuildLayerSet(L, 4, canvas_w, canvas_h, &layers);
    if (layer_n < 0)
        return luaL_error(L, "JY_Composite: failed to alloc layer set");
    if (layer_n == 0)
    {
        JY_ReleaseLayerSet(layers, 0);
        return 0;
    }

    /* SDL_CreateRGBSurfaceWithFormat 已通过 SDL_memset 清零像素(SDL_surface.c:164),
     * 旧版 SDL_FillRect(result,NULL,0) 是 100% 冗余 — Plan C 收益点,直接删除 */
    SDL_Surface* result = SDL_CreateRGBSurfaceWithFormat(
        SDL_SWSURFACE, canvas_w, canvas_h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!result)
    {
        JY_ReleaseLayerSet(layers, layer_n);
        return luaL_error(L, "JY_Composite: failed to create result surface");
    }

    if (SDL_MUSTLOCK(result))
        SDL_LockSurface(result);

    int ok = JY_RunCompositeKernel(result, layers, layer_n, canvas_w, canvas_h);

    if (SDL_MUSTLOCK(result))
        SDL_UnlockSurface(result);

    if (!ok)
    {
        SDL_FreeSurface(result);
        JY_ReleaseLayerSet(layers, layer_n);
        return luaL_error(L, "JY_Composite: failed to alloc active layer list");
    }

    SDL_SetSurfaceBlendMode(result, SDL_BLENDMODE_BLEND);
    JY_ReleaseLayerSet(layers, layer_n);

    /* Push result as SDL_Surface userdata */
    SDL_Surface** sfud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
    *sfud = result;
    luaL_setmetatable(L, "SDL_Surface");

    return 1;
}

/* ─── Lua API: CompositeTo — surface 复用路径(降发热的核心新增) ───
 *   Lua 侧每个 wrapper 缓存一张 ARGB8888 surface,每帧调用 CompositeTo 写入,
 *   消除 ~230KB 的 SDL_CreateRGBSurfaceWithFormat 分配/释放循环。
 *
 *   target.w/h 可大于 canvas,内核只写左上 canvas_w×canvas_h 子区域,
 *   清屏也只清这个子区域（保留 target 其余像素，便于 Lua 侧自管纹理上传时机）。
 *
 *   失败语义:格式/尺寸不符时 luaL_error(便于调用方修业务逻辑,不静默吞掉) */
static int JY_CompositeTo(lua_State* L)
{
    JY_UserData* ud = JY_Check(L, 1);
    (void)ud;
    SDL_Surface* dst = *(SDL_Surface**)luaL_checkudata(L, 2, "SDL_Surface");
    int canvas_w = (int)luaL_checkinteger(L, 3);
    int canvas_h = (int)luaL_checkinteger(L, 4);
    luaL_checktype(L, 5, LUA_TTABLE);

    if (canvas_w <= 0 || canvas_h <= 0)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (!dst || !dst->format || dst->format->format != SDL_PIXELFORMAT_ARGB8888)
        return luaL_error(L, "JY_CompositeTo: target surface must be ARGB8888");
    if (dst->w < canvas_w || dst->h < canvas_h)
        return luaL_error(L, "JY_CompositeTo: target surface (%dx%d) smaller than canvas (%dx%d)",
                          dst->w, dst->h, canvas_w, canvas_h);

    JY_CompLayer* layers = NULL;
    int layer_n = JY_BuildLayerSet(L, 5, canvas_w, canvas_h, &layers);
    if (layer_n < 0)
        return luaL_error(L, "JY_CompositeTo: failed to alloc layer set");

    if (SDL_MUSTLOCK(dst))
        SDL_LockSurface(dst);

    /* 清掉 canvas 区域(target 可能比 canvas 大,只清需要写入的部分);
     * 比 SDL_FillRect 更直接,无 rect 边缘逻辑 */
    if (dst->w == canvas_w)
    {
        /* 行连续:单次大 memset 最快,绕过 per-row 调用开销 */
        SDL_memset(dst->pixels, 0, (size_t)canvas_h * (size_t)dst->pitch);
    }
    else
    {
        Uint8* row0 = (Uint8*)dst->pixels;
        size_t row_bytes = (size_t)canvas_w * 4u;
        for (int y = 0; y < canvas_h; y++)
            SDL_memset(row0 + (size_t)y * (size_t)dst->pitch, 0, row_bytes);
    }

    int ok = JY_RunCompositeKernel(dst, layers, layer_n, canvas_w, canvas_h);

    if (SDL_MUSTLOCK(dst))
        SDL_UnlockSurface(dst);

    JY_ReleaseLayerSet(layers, layer_n);

    if (!ok)
        return luaL_error(L, "JY_CompositeTo: failed to alloc active layer list");

    /* target 的 BlendMode 由 Lua 侧自管(创建 surface 时一次性设好,不每帧重设) */
    lua_pushboolean(L, 1);
    return 1;
}

/* ═══════════════════════════════════════════
 *  GC / cleanup
 * ═══════════════════════════════════════════ */
static void JY_Reset(JY_UserData* ud)
{
    JY_AsyncShutdown(ud);
    JY_CacheClear(ud);

    if (ud->cache)
    {
        SDL_free(ud->cache);
        ud->cache = NULL;
    }
    if (ud->frames)
    {
        SDL_free(ud->frames);
        ud->frames = NULL;
    }
    if (ud->index_pixels)
    {
        SDL_free(ud->index_pixels);
        ud->index_pixels = NULL;
    }
    if (ud->alpha_pixels)
    {
        SDL_free(ud->alpha_pixels);
        ud->alpha_pixels = NULL;
    }
    if (ud->depth_pixels)
    {
        SDL_free(ud->depth_pixels);
        ud->depth_pixels = NULL;
    }
    if (ud->depth_frames)
    {
        SDL_free(ud->depth_frames);
        ud->depth_frames = NULL;
    }
    /* ★ R10/R12: zbuf_cached 字段已删除（改为 per-pixel 全 layer 深度排序） */
}

static int JY_GC(lua_State* L)
{
    JY_UserData* ud = (JY_UserData*)luaL_checkudata(L, 1, JY_MT);
    if (ud)
        JY_Reset(ud);
    return 0;
}

/* ═══════════════════════════════════════════
 *  Helper: load PNG from Lua string → raw pixels
 * ═══════════════════════════════════════════ */
static SDL_Surface* JY_LoadPNG(const char* data, size_t len)
{
    /* SDL_RWFromMem takes void* — cast away const; we only read. */
    SDL_RWops* rw = SDL_RWFromMem((void*)data, (int)len);
    if (!rw)
        return NULL;
    SDL_Surface* sf = IMG_Load_RW(rw, 1); /* 1 = close RW */
    return sf;
}

/* Extract raw pixel data from SDL_Surface.
 * For surfaces with 3+ bytes per pixel (RGB/BGR/RGBA/BGRA), we first
 * convert to SDL_PIXELFORMAT_RGB24 so byte layout is always R,G,B.
 * This avoids platform-dependent BGR byte order issues on Windows. */
static Uint8* JY_ExtractPixels(SDL_Surface* sf, Uint32* out_w, Uint32* out_h, Uint32* out_bpp)
{
    if (!sf)
        return NULL;

    /* For 3+ bpp surfaces, normalize to RGB24 to guarantee R,G,B byte order */
    SDL_Surface* src = sf;
    SDL_Surface* conv = NULL;
    if (sf->format->BytesPerPixel >= 3 &&
        sf->format->format != SDL_PIXELFORMAT_RGB24)
    {
        /* Disable self-blending. For data-carrying images, we need the raw
         * memory layout exactly equivalent to Python's convert("RGB").
         * This absolutely prevents semi-transparent anti-aliasing edges from 
         * reducing RGB values against a black background. */
        SDL_SetSurfaceBlendMode(sf, SDL_BLENDMODE_NONE);
        conv = SDL_ConvertSurfaceFormat(sf, SDL_PIXELFORMAT_RGB24, 0);
        if (conv)
            src = conv;
        /* If conversion fails, fall through to use original surface */
    }

    *out_w = (Uint32)src->w;
    *out_h = (Uint32)src->h;
    *out_bpp = (Uint32)src->format->BytesPerPixel;

    size_t total = (size_t)src->w * (size_t)src->h * (size_t)src->format->BytesPerPixel;
    Uint8* pixels = (Uint8*)SDL_malloc(total);
    if (!pixels)
    {
        if (conv) SDL_FreeSurface(conv);
        return NULL;
    }

    if (SDL_MUSTLOCK(src))
        SDL_LockSurface(src);

    /* Copy row by row to handle pitch */
    Uint32 row_bytes = (Uint32)src->w * src->format->BytesPerPixel;
    for (int y = 0; y < src->h; y++)
    {
        SDL_memcpy(pixels + y * row_bytes,
                   (Uint8*)src->pixels + y * src->pitch,
                   row_bytes);
    }

    if (SDL_MUSTLOCK(src))
        SDL_UnlockSurface(src);

    if (conv)
        SDL_FreeSurface(conv);

    return pixels;
}

/* ═══════════════════════════════════════════
 *  SPR/FTEN constructor: xy_jy(spr_data [, pal_data])
 *
 *  spr_data : string (FTEN binary with embedded PNGs)
 *  pal_data : string (1024 bytes BGRA) — optional
 *
 *  Internally builds a virtual atlas by stacking embedded PNGs,
 *  extracting R→index_pixels and (A%32)*8→alpha_pixels,
 *  then delegates to the same JY_DecodeFrame pipeline.
 * ═══════════════════════════════════════════ */
static int JY_CreateFromSPR(lua_State* L, const Uint8* data, size_t len)
{
    /* ─── FTEN header: tag(4) + version(4) + filesize(4) + hash(4) = 16B ─── */
    Uint32 extra = 16;
    const Uint8* inner = data + extra;
    Uint32 innerLen = (Uint32)(len - extra);
    if (innerLen < 16)
        return luaL_error(L, "JY-SPR: inner data too short");

    /* ─── SPR inner header (16B) ─── */
    Uint32 off = 0;
    off += 2; /* sid */
    Uint16 dir_cnt = *(const Uint16*)(inner + off); off += 2;
    Uint16 frame_cnt = *(const Uint16*)(inner + off); off += 2;
    Uint16 spr_width = *(const Uint16*)(inner + off); off += 2;
    Uint16 spr_height = *(const Uint16*)(inner + off); off += 2;
    Sint16 kx = *(const Sint16*)(inner + off); off += 2;
    Sint16 ky = *(const Sint16*)(inner + off); off += 2;
    Uint16 image_res_nums = *(const Uint16*)(inner + off); off += 2;

    Uint32 total_frames = (Uint32)dir_cnt * (Uint32)frame_cnt;
    Uint32 frames_size = total_frames * 14;
    if (off + frames_size + 4 > innerLen)
        return luaL_error(L, "JY-SPR: frame table overflow");

    /* ─── Parse frame entries (14B each) ─── */
    typedef struct { Sint16 image_idx, pos_x, pos_y; Uint16 w, h; Sint16 key_x, key_y; } SPR_Frame;
    SPR_Frame* spr_frames = (SPR_Frame*)SDL_calloc(total_frames, sizeof(SPR_Frame));
    if (!spr_frames)
        return luaL_error(L, "JY-SPR: out of memory");

    for (Uint32 i = 0; i < total_frames; i++)
    {
        const Uint8* fp = inner + off;
        spr_frames[i].image_idx = *(const Sint16*)(fp + 0);
        spr_frames[i].pos_x    = *(const Sint16*)(fp + 2);
        spr_frames[i].pos_y    = *(const Sint16*)(fp + 4);
        spr_frames[i].w        = *(const Uint16*)(fp + 6);
        spr_frames[i].h        = *(const Uint16*)(fp + 8);
        spr_frames[i].key_x    = *(const Sint16*)(fp + 10);
        spr_frames[i].key_y    = *(const Sint16*)(fp + 12);
        off += 14;
    }

    off += 4; /* sprtype */

    /* ─── Image offset table (8B each) ─── */
    if (off + (Uint32)image_res_nums * 8 > innerLen)
    {
        SDL_free(spr_frames);
        return luaL_error(L, "JY-SPR: offset table overflow");
    }

    typedef struct { Uint32 offset, length; } IMG_Entry;
    IMG_Entry* img_entries = (IMG_Entry*)SDL_calloc(image_res_nums, sizeof(IMG_Entry));
    if (!img_entries)
    {
        SDL_free(spr_frames);
        return luaL_error(L, "JY-SPR: out of memory");
    }

    for (Uint32 i = 0; i < image_res_nums; i++)
    {
        const Uint8* op = inner + off;
        Sint32 img_off = *(const Sint32*)(op + 0);
        Sint32 img_len = *(const Sint32*)(op + 4);
        img_entries[i].offset = (Uint32)(img_off + (Sint32)extra);
        img_entries[i].length = (Uint32)img_len;
        off += 8;
    }

    /* ─── Load all embedded PNGs ─── */
    SDL_Surface** png_surfaces = (SDL_Surface**)SDL_calloc(image_res_nums, sizeof(SDL_Surface*));
    if (!png_surfaces)
    {
        SDL_free(img_entries);
        SDL_free(spr_frames);
        return luaL_error(L, "JY-SPR: out of memory");
    }

    Uint32 max_w = 0;
    Uint32 total_h = 0;
    Uint32* y_offsets = (Uint32*)SDL_calloc(image_res_nums, sizeof(Uint32));

    for (Uint32 i = 0; i < image_res_nums; i++)
    {
        Uint32 ofs = img_entries[i].offset;
        Uint32 blen = img_entries[i].length;

        if (ofs >= (Uint32)len || blen == 0 || ofs + blen > (Uint32)len)
            continue;

        /* Search for PNG signature (89 50 4E 47) or WebP/RIFF signature (52 49 46 46) */
        const Uint8* raw = data + ofs;
        const Uint8* img_start = NULL;
        Uint32 img_blen = blen;
        for (Uint32 j = 0; j + 4 <= blen; j++)
        {
            if (raw[j] == 0x89 && raw[j+1] == 0x50 && raw[j+2] == 0x4E && raw[j+3] == 0x47)
            {
                /* PNG signature found */
                img_start = raw + j;
                img_blen = blen - j;
                break;
            }
            if (raw[j] == 0x52 && raw[j+1] == 0x49 && raw[j+2] == 0x46 && raw[j+3] == 0x46)
            {
                /* RIFF (WebP) signature found — verify WEBP tag at offset +8 */
                if (j + 12 <= blen &&
                    raw[j+8] == 0x57 && raw[j+9] == 0x45 && raw[j+10] == 0x42 && raw[j+11] == 0x50)
                {
                    img_start = raw + j;
                    img_blen = blen - j;
                    break;
                }
            }
        }
        if (!img_start)
            continue;

        SDL_RWops* rw = SDL_RWFromMem((void*)img_start, (int)img_blen);
        if (rw)
        {
            png_surfaces[i] = IMG_Load_RW(rw, 1);
        }

        if (png_surfaces[i])
        {
            y_offsets[i] = total_h;
            if ((Uint32)png_surfaces[i]->w > max_w)
                max_w = (Uint32)png_surfaces[i]->w;
            total_h += (Uint32)png_surfaces[i]->h;
        }
    }

    if (max_w == 0 || total_h == 0)
    {
        for (Uint32 i = 0; i < image_res_nums; i++)
            if (png_surfaces[i]) SDL_FreeSurface(png_surfaces[i]);
        SDL_free(png_surfaces);
        SDL_free(y_offsets);
        SDL_free(img_entries);
        SDL_free(spr_frames);
        return luaL_error(L, "JY-SPR: no valid PNG images found");
    }

    /* ─── Build virtual atlas: index_pixels (R) + alpha_pixels ((A%32)*8) ─── */
    Uint32 atlas_w = max_w;
    Uint32 atlas_h = total_h;
    size_t pixel_count = (size_t)atlas_w * atlas_h;

    Uint8* index_pixels = (Uint8*)SDL_calloc(pixel_count, 3); /* RGB format */
    Uint8* alpha_pixels = (Uint8*)SDL_calloc(pixel_count, 1); /* Grayscale */
    if (!index_pixels || !alpha_pixels)
    {
        if (index_pixels) SDL_free(index_pixels);
        if (alpha_pixels) SDL_free(alpha_pixels);
        for (Uint32 i = 0; i < image_res_nums; i++)
            if (png_surfaces[i]) SDL_FreeSurface(png_surfaces[i]);
        SDL_free(png_surfaces);
        SDL_free(y_offsets);
        SDL_free(img_entries);
        SDL_free(spr_frames);
        return luaL_error(L, "JY-SPR: out of memory for atlas");
    }

    for (Uint32 i = 0; i < image_res_nums; i++)
    {
        SDL_Surface* sf = png_surfaces[i];
        if (!sf) continue;

        /* Convert to RGBA32 for uniform access */
        SDL_Surface* conv = SDL_ConvertSurfaceFormat(sf, SDL_PIXELFORMAT_ARGB8888, 0);
        if (!conv) continue;

        if (SDL_MUSTLOCK(conv)) SDL_LockSurface(conv);

        Uint32 pw = (Uint32)conv->w;
        Uint32 ph = (Uint32)conv->h;
        Uint32 pitch4 = (Uint32)(conv->pitch / 4);
        Uint32* pixels = (Uint32*)conv->pixels;

        for (Uint32 row = 0; row < ph; row++)
        {
            Uint32 dst_y = y_offsets[i] + row;
            if (dst_y >= atlas_h) break;

            for (Uint32 col = 0; col < pw; col++)
            {
                if (col >= atlas_w) break;

                Uint32 pixel = pixels[row * pitch4 + col];
                /* ARGB8888: A[31:24] R[23:16] G[15:8] B[7:0] */
                Uint8 pa = (Uint8)((pixel >> 24) & 0xFF);
                Uint8 pr = (Uint8)((pixel >> 16) & 0xFF);
                Uint8 pg = (Uint8)((pixel >> 8) & 0xFF);
                Uint8 pb = (Uint8)(pixel & 0xFF);

                size_t dst_idx = (size_t)dst_y * atlas_w + col;

                /* index_pixels: R=pal_idx, G=depth_hi, B=depth_lo */
                index_pixels[dst_idx * 3 + 0] = pr;
                index_pixels[dst_idx * 3 + 1] = pg;
                index_pixels[dst_idx * 3 + 2] = pb;

                /* alpha: 5-bit decode (matching view.py) */
                Uint32 alpha = (Uint32)(pa % 32) * 8;
                if (alpha > 255) alpha = 255;
                alpha_pixels[dst_idx] = (Uint8)alpha;
            }
        }

        if (SDL_MUSTLOCK(conv)) SDL_UnlockSurface(conv);
        SDL_FreeSurface(conv);
    }

    /* Free PNG surfaces */
    for (Uint32 i = 0; i < image_res_nums; i++)
        if (png_surfaces[i]) SDL_FreeSurface(png_surfaces[i]);
    SDL_free(png_surfaces);

    /* ─── Create userdata ─── */
    JY_UserData* ud = (JY_UserData*)lua_newuserdata(L, sizeof(JY_UserData));
    SDL_memset(ud, 0, sizeof(JY_UserData));
    luaL_setmetatable(L, JY_MT);

    ud->index_pixels = index_pixels;
    ud->alpha_pixels = alpha_pixels;
    ud->atlas_w = atlas_w;
    ud->atlas_h = atlas_h;
    ud->index_bpp = 3; /* RGB */
    ud->alpha_bpp = 1; /* Grayscale */

    /* ─── Load palette ─── */
    ud->pal_count = 256;
    if (lua_type(L, 2) == LUA_TSTRING)
    {
        size_t pal_len;
        const char* pal_data = lua_tolstring(L, 2, &pal_len);
        if (pal_len >= 1024)
        {
            /* BGRA format (BMP 32-bit) */
            const Uint8* ppb = (const Uint8*)pal_data;
            for (Uint32 i = 0; i < 256; i++)
            {
                Uint8 b = ppb[i * 4 + 0];
                Uint8 g = ppb[i * 4 + 1];
                Uint8 r = ppb[i * 4 + 2];
                Uint8 a = ppb[i * 4 + 3];
                ud->pal[i] = ((Uint32)a << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
            }
        }
        else if (pal_len >= 768)
        {
            /* BMP pixel data: BGR format (256*3 bytes) */
            const Uint8* ppb = (const Uint8*)pal_data;
            for (Uint32 i = 0; i < 256; i++)
            {
                Uint8 b = ppb[i * 3 + 0];
                Uint8 g = ppb[i * 3 + 1];
                Uint8 r = ppb[i * 3 + 2];
                ud->pal[i] = (255u << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
            }
        }
        else
        {
            /* Fallback: grayscale */
            for (Uint32 i = 0; i < 256; i++)
                ud->pal[i] = (255u << 24) | (i << 16) | (i << 8) | i;
        }
    }
    else
    {
        /* No palette: grayscale */
        for (Uint32 i = 0; i < 256; i++)
            ud->pal[i] = (255u << 24) | (i << 16) | (i << 8) | i;
    }
    JY_CalcPalMod(ud);
    ud->pal_version = 0;

    /* ─── Build frames (map into virtual atlas) ─── */
    ud->group = dir_cnt;
    ud->frame_per_group = frame_cnt;
    ud->width = spr_width;
    ud->height = spr_height;
    ud->global_x = kx;
    ud->global_y = ky;
    ud->frame_count = total_frames;

    ud->frames = (JY_FrameInfo*)SDL_calloc(total_frames, sizeof(JY_FrameInfo));
    if (!ud->frames)
    {
        SDL_free(y_offsets);
        SDL_free(img_entries);
        SDL_free(spr_frames);
        return luaL_error(L, "JY-SPR: out of memory for frames");
    }

    for (Uint32 i = 0; i < total_frames; i++)
    {
        JY_FrameInfo* jf = &ud->frames[i];
        SPR_Frame* sf_info = &spr_frames[i];

        Sint16 img_idx = sf_info->image_idx;
        if (img_idx >= 0 && img_idx < (Sint16)image_res_nums)
        {
            /* Map pos into virtual atlas:
               sx = pos_x within the PNG image
               sy = y_offsets[img_idx] + pos_y */
            jf->sx = (Uint32)sf_info->pos_x;
            jf->sy = y_offsets[img_idx] + (Uint32)sf_info->pos_y;
            jf->sw = (Uint32)sf_info->w;
            jf->sh = (Uint32)sf_info->h;
        }
        jf->key_x = (Sint32)sf_info->key_x;
        jf->key_y = (Sint32)sf_info->key_y;
        jf->z     = 0; /* SPR 14B 帧头无 z 字段，默认 0（与 JS 行为一致）*/
        JY_UpdateFrameBounds(ud, jf);
    }

    SDL_free(y_offsets);
    SDL_free(img_entries);
    SDL_free(spr_frames);

    /* ─── Init cache ─── */
    ud->cache_cap = JY_CACHE_CAP_DEFAULT;
    ud->cache = (JY_CacheEntry*)SDL_calloc(ud->cache_cap, sizeof(JY_CacheEntry));
    ud->cache_tick = 0;

    /* cache 按需同步填充，Prefetch 仅提前解码到同一 LRU */

    /* ─── Build info return table (tcp compatible) ─── */
    lua_createtable(L, 0, 16);

    lua_pushinteger(L, (lua_Integer)ud->group);
    lua_setfield(L, -2, "group");
    lua_pushinteger(L, (lua_Integer)ud->frame_per_group);
    lua_setfield(L, -2, "frame");
    lua_pushinteger(L, (lua_Integer)ud->width);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, (lua_Integer)ud->height);
    lua_setfield(L, -2, "height");
    lua_pushinteger(L, (lua_Integer)ud->global_x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, (lua_Integer)ud->global_y);
    lua_setfield(L, -2, "y");
    lua_pushstring(L, "FT");
    lua_setfield(L, -2, "type");
    lua_pushinteger(L, (lua_Integer)ud->frame_count);
    lua_setfield(L, -2, "total");

    /* R9 紧致画布：单 act+dir 内所有帧的实际裁剪区最大值
     *   Lua 层 合成动画源.新建 用此替代 width/height 计算画布尺寸 */
    lua_pushinteger(L, (lua_Integer)ud->max_frame_w);
    lua_setfield(L, -2, "max_frame_w");
    lua_pushinteger(L, (lua_Integer)ud->max_frame_h);
    lua_setfield(L, -2, "max_frame_h");
    lua_pushinteger(L, (lua_Integer)ud->max_key_x);
    lua_setfield(L, -2, "max_key_x");
    lua_pushinteger(L, (lua_Integer)ud->max_key_y);
    lua_setfield(L, -2, "max_key_y");
    lua_pushinteger(L, (lua_Integer)ud->max_frame_right);
    lua_setfield(L, -2, "max_frame_right");
    lua_pushinteger(L, (lua_Integer)ud->max_frame_bottom);
    lua_setfield(L, -2, "max_frame_bottom");

    return 2;
}

static int JY_PushCJsonDecoded(lua_State* L, const char* data, size_t len)
{
    int top = lua_gettop(L);

    lua_getglobal(L, "require");
    lua_pushstring(L, "cjson");
    if (lua_pcall(L, 1, 1, 0) != LUA_OK)
    {
        lua_settop(L, top);
        return 0;
    }

    lua_getfield(L, -1, "decode");
    if (!lua_isfunction(L, -1))
    {
        lua_settop(L, top);
        return 0;
    }

    lua_pushlstring(L, data, len);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK)
    {
        lua_settop(L, top);
        return 0;
    }

    lua_copy(L, top + 2, top + 1);
    lua_settop(L, top + 1);
    return 1;
}

static int JY_GetNumberField(lua_State* L, int table_idx, const char* key, lua_Number* out)
{
    int ok = 0;
    table_idx = lua_absindex(L, table_idx);
    lua_getfield(L, table_idx, key);
    if (lua_isnumber(L, -1))
    {
        *out = lua_tonumber(L, -1);
        ok = 1;
    }
    lua_pop(L, 1);
    return ok;
}

static lua_Number JY_GetNumberFieldOr(lua_State* L, int table_idx, const char* key, lua_Number fallback)
{
    lua_Number value = fallback;
    table_idx = lua_absindex(L, table_idx);
    lua_getfield(L, table_idx, key);
    if (lua_isnumber(L, -1))
        value = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return value;
}

static void JY_SetInfoInteger(lua_State* L, int idx, const char* key, lua_Integer value)
{
    idx = lua_absindex(L, idx);
    lua_pushinteger(L, value);
    lua_setfield(L, idx, key);
}

static void JY_SetInfoNumber(lua_State* L, int idx, const char* key, lua_Number value)
{
    idx = lua_absindex(L, idx);
    lua_pushnumber(L, value);
    lua_setfield(L, idx, key);
}

static int JY_PushAtlasFramesFromJson(lua_State* L, const char* json, size_t json_len)
{
    int top = lua_gettop(L);
    int decoded_idx, mc_idx, animate_idx, frames_idx, res_idx, result_idx;
    lua_Unsigned frame_count, i;
    lua_Number frame_rate;
    int max_w = 0;
    int max_h = 0;

    if (!JY_PushCJsonDecoded(L, json, json_len) || !lua_istable(L, -1))
    {
        lua_settop(L, top);
        return 0;
    }
    decoded_idx = lua_absindex(L, -1);

    lua_getfield(L, decoded_idx, "mc");
    if (!lua_istable(L, -1))
    {
        lua_settop(L, top);
        return 0;
    }
    mc_idx = lua_absindex(L, -1);

    lua_getfield(L, mc_idx, "animate");
    if (!lua_istable(L, -1))
    {
        lua_settop(L, top);
        return 0;
    }
    animate_idx = lua_absindex(L, -1);

    lua_getfield(L, animate_idx, "frames");
    if (!lua_istable(L, -1))
    {
        lua_settop(L, top);
        return 0;
    }
    frames_idx = lua_absindex(L, -1);

    lua_getfield(L, decoded_idx, "res");
    if (!lua_istable(L, -1))
    {
        lua_settop(L, top);
        return 0;
    }
    res_idx = lua_absindex(L, -1);

    frame_count = lua_rawlen(L, frames_idx);
    if (!JY_GetNumberField(L, decoded_idx, "frameRate", &frame_rate)
        && !JY_GetNumberField(L, animate_idx, "frameRate", &frame_rate))
    {
        frame_rate = 8.0;
    }

    lua_createtable(L, (int)frame_count, 8);
    result_idx = lua_absindex(L, -1);
    JY_SetInfoInteger(L, result_idx, "group", 1);
    JY_SetInfoInteger(L, result_idx, "frame", (lua_Integer)frame_count);
    JY_SetInfoNumber(L, result_idx, "frameRate", frame_rate);
    JY_SetInfoInteger(L, result_idx, "x", 0);
    JY_SetInfoInteger(L, result_idx, "y", 0);

    for (i = 1; i <= frame_count; i++)
    {
        lua_geti(L, frames_idx, (lua_Integer)i);
        if (lua_istable(L, -1))
        {
            int f_idx = lua_absindex(L, -1);
            lua_getfield(L, f_idx, "res");
            lua_gettable(L, res_idx);
            if (lua_istable(L, -1))
            {
                int res_info_idx = lua_absindex(L, -1);
                lua_Number sx = JY_GetNumberFieldOr(L, res_info_idx, "x", 0.0);
                lua_Number sy = JY_GetNumberFieldOr(L, res_info_idx, "y", 0.0);
                lua_Number sw = JY_GetNumberFieldOr(L, res_info_idx, "w", 0.0);
                lua_Number sh = JY_GetNumberFieldOr(L, res_info_idx, "h", 0.0);
                lua_Number fx = JY_GetNumberFieldOr(L, f_idx, "x", 0.0);
                lua_Number fy = JY_GetNumberFieldOr(L, f_idx, "y", 0.0);
                lua_Number z = JY_GetNumberFieldOr(L, f_idx, "z", 0.0);

                lua_createtable(L, 0, 7);
                JY_SetInfoNumber(L, -1, "sx", sx);
                JY_SetInfoNumber(L, -1, "sy", sy);
                JY_SetInfoNumber(L, -1, "sw", sw);
                JY_SetInfoNumber(L, -1, "sh", sh);
                JY_SetInfoNumber(L, -1, "key_x", -fx);
                JY_SetInfoNumber(L, -1, "key_y", -fy);
                JY_SetInfoNumber(L, -1, "z", z);
                lua_seti(L, result_idx, (lua_Integer)i);

                if ((int)sw > max_w) max_w = (int)sw;
                if ((int)sh > max_h) max_h = (int)sh;
            }
            lua_pop(L, 1); /* res_info */
        }
        lua_pop(L, 1); /* frame entry */
    }

    JY_SetInfoInteger(L, result_idx, "width", max_w);
    JY_SetInfoInteger(L, result_idx, "height", max_h);

    lua_copy(L, result_idx, top + 1);
    lua_settop(L, top + 1);
    return 1;
}

/* ═══════════════════════════════════════════
 *  Constructor: xy_jy(idx_png, alpha_png, pal_data, frames_table/json_string)
 *           OR: xy_jy(spr_data [, pal_data])  — auto-detect FTEN
 *
 *  Returns: userdata, info_table
 * ═══════════════════════════════════════════ */
static int JY_NEW(lua_State* L)
{
    /* ─── Auto-detect FTEN/SPR format ─── */
    size_t arg1_len;
    const char* arg1_data = luaL_checklstring(L, 1, &arg1_len);
    if (arg1_len >= 32 &&
        arg1_data[0] == 'F' && arg1_data[1] == 'T' &&
        arg1_data[2] == 'E' && arg1_data[3] == 'N')
    {
        return JY_CreateFromSPR(L, (const Uint8*)arg1_data, arg1_len);
    }

    /* ─── Original atlas path ─── */
    /* arg1 is already idx_data/idx_len */
    size_t idx_len = arg1_len;
    const char* idx_data = arg1_data;

    /* Arg 2: alpha PNG binary (optional if idx_png has alpha) */
    size_t alpha_len = 0;
    const char* alpha_data = NULL;
    if (lua_type(L, 2) == LUA_TSTRING)
        alpha_data = lua_tolstring(L, 2, &alpha_len);

    /* Arg 3: palette data (1024 bytes BGRA or 768 bytes BGR) */
    size_t pal_len = 0;
    const char* pal_data = NULL;
    if (lua_type(L, 3) == LUA_TSTRING)
        pal_data = lua_tolstring(L, 3, &pal_len);
    if (pal_data && pal_len == 0)
        pal_data = NULL;
    if (pal_data && pal_len < 768)
        return luaL_error(L, "JY: palette must be 1024 BGRA or 768 BGR bytes, got %d", (int)pal_len);

    /* Arg 4: frames table or atlas JSON string */
    int frames_arg_idx = 4;
    if (lua_type(L, 4) == LUA_TSTRING)
    {
        size_t frames_json_len = 0;
        const char* frames_json = lua_tolstring(L, 4, &frames_json_len);
        if (!JY_PushAtlasFramesFromJson(L, frames_json, frames_json_len))
            return luaL_error(L, "JY: failed to parse atlas JSON frames");
        frames_arg_idx = lua_absindex(L, -1);
    }
    else
    {
        luaL_checktype(L, 4, LUA_TTABLE);
        frames_arg_idx = lua_absindex(L, 4);
    }

    /* Arg 5: optional depth PNG */
    size_t depth_len = 0;
    const char* depth_data = NULL;
    if (lua_type(L, 5) == LUA_TSTRING)
        depth_data = lua_tolstring(L, 5, &depth_len);

    /* ─── Decode index PNG ─── */
    SDL_Surface* idx_sf = JY_LoadPNG(idx_data, idx_len);
    if (!idx_sf)
        return luaL_error(L, "JY: failed to decode index PNG");

    /* ─── Create userdata ─── */
    JY_UserData* ud = (JY_UserData*)lua_newuserdata(L, sizeof(JY_UserData));
    SDL_memset(ud, 0, sizeof(JY_UserData));
    luaL_setmetatable(L, JY_MT);

    Uint32 aw2 = (Uint32)idx_sf->w, ah2 = (Uint32)idx_sf->h;

    /* ─── Decode or Extract alpha PNG ─── */
    if (alpha_data)
    {
        SDL_Surface* alpha_sf = JY_LoadPNG(alpha_data, alpha_len);
        if (!alpha_sf)
        {
            SDL_FreeSurface(idx_sf);
            return luaL_error(L, "JY: failed to decode alpha PNG");
        }
        ud->alpha_pixels = JY_ExtractPixels(alpha_sf, &aw2, &ah2, &ud->alpha_bpp);
        SDL_FreeSurface(alpha_sf);
    }
    else
    {
        /* No separate alpha PNG provided.
         * Match Python MountLoader.load line 137:
         *   alpha_img = Image.new("L", index_img.size, 255)
         * → create a fully opaque alpha buffer. */
        Uint32 aw = (Uint32)idx_sf->w;
        Uint32 ah = (Uint32)idx_sf->h;
        ud->alpha_pixels = (Uint8*)SDL_malloc(aw * ah);
        ud->alpha_bpp = 1;
        aw2 = aw; ah2 = ah;
        if (ud->alpha_pixels)
            SDL_memset(ud->alpha_pixels, 255, aw * ah);
    }

    /* Extract pixels */
    ud->index_pixels = JY_ExtractPixels(idx_sf, &ud->atlas_w, &ud->atlas_h, &ud->index_bpp);
    SDL_FreeSurface(idx_sf);

    if (!ud->index_pixels)
        return luaL_error(L, "JY: failed to extract index pixels");

    if (depth_data && depth_len > 0)
    {
        SDL_Surface* depth_sf = JY_LoadPNG(depth_data, depth_len);
        if (depth_sf)
        {
            /* Force depth surface to RGB24 to safely extract G and B channels.
             * This prevents 8-bit indexed PNGs (from size optimization) from being
             * misread as byte-misaligned coordinates when `depth_bpp` == 1. */
            if (depth_sf->format->format != SDL_PIXELFORMAT_RGB24)
            {
                SDL_SetSurfaceBlendMode(depth_sf, SDL_BLENDMODE_NONE);
                SDL_Surface* conv = SDL_ConvertSurfaceFormat(depth_sf, SDL_PIXELFORMAT_RGB24, 0);
                if (conv)
                {
                    SDL_FreeSurface(depth_sf);
                    depth_sf = conv;
                }
            }

            Uint32 dw, dh;
            ud->depth_pixels = JY_ExtractPixels(depth_sf, &dw, &dh, &ud->depth_bpp);
            ud->depth_atlas_w = dw;
            ud->depth_atlas_h = dh;
            SDL_FreeSurface(depth_sf);
        }
    }

    /* ─── Parse global info from frames table (arg 4) ─── */
    lua_getfield(L, frames_arg_idx, "group");
    ud->group = lua_isnil(L, -1) ? 1 : (Uint32)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, frames_arg_idx, "frame");
    ud->frame_per_group = lua_isnil(L, -1) ? 1 : (Uint32)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, frames_arg_idx, "width");
    ud->width = lua_isnil(L, -1) ? 0 : (Uint16)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, frames_arg_idx, "height");
    ud->height = lua_isnil(L, -1) ? 0 : (Uint16)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, frames_arg_idx, "x");
    ud->global_x = lua_isnil(L, -1) ? 0 : (Sint16)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, frames_arg_idx, "y");
    ud->global_y = lua_isnil(L, -1) ? 0 : (Sint16)lua_tointeger(L, -1);
    lua_pop(L, 1);

    Uint32 total_expected_frames = ud->group * ud->frame_per_group;

    /* Arg 6: optional depth frames table (independent depth atlas coordinates) */
    if (lua_type(L, 6) == LUA_TTABLE)
    {
        /* Use total_expected_frames instead of lua_len, which fails on tables with holes */
        Uint32 dn = total_expected_frames;

        if (dn > 0)
        {
            ud->depth_frame_count = dn;
            ud->depth_frames = (JY_FrameInfo*)SDL_calloc(dn, sizeof(JY_FrameInfo));
            if (ud->depth_frames)
            {
                for (Uint32 i = 0; i < dn; i++)
                {
                    lua_geti(L, 6, (lua_Integer)(i + 1));
                    if (lua_istable(L, -1))
                    {
                        JY_FrameInfo* df = &ud->depth_frames[i];

                        lua_getfield(L, -1, "sx");
                        df->sx = lua_isnil(L, -1) ? 0 : (Uint32)lua_tointeger(L, -1);
                        lua_pop(L, 1);

                        lua_getfield(L, -1, "sy");
                        df->sy = lua_isnil(L, -1) ? 0 : (Uint32)lua_tointeger(L, -1);
                        lua_pop(L, 1);

                        lua_getfield(L, -1, "sw");
                        df->sw = lua_isnil(L, -1) ? 0 : (Uint32)lua_tointeger(L, -1);
                        lua_pop(L, 1);

                        lua_getfield(L, -1, "sh");
                        df->sh = lua_isnil(L, -1) ? 0 : (Uint32)lua_tointeger(L, -1);
                        lua_pop(L, 1);

                        lua_getfield(L, -1, "key_x");
                        df->key_x = lua_isnil(L, -1) ? 0 : (Sint32)lua_tointeger(L, -1);
                        lua_pop(L, 1);

                        lua_getfield(L, -1, "key_y");
                        df->key_y = lua_isnil(L, -1) ? 0 : (Sint32)lua_tointeger(L, -1);
                        lua_pop(L, 1);
                    }
                    lua_pop(L, 1);
                }
            }
        }
    }

    /* alpha atlas dimensions should match index */
    if (aw2 != ud->atlas_w || ah2 != ud->atlas_h)
    {
        /* Tolerate mismatch but use min dimensions */
        if (aw2 < ud->atlas_w) ud->atlas_w = aw2;
        if (ah2 < ud->atlas_h) ud->atlas_h = ah2;
    }

    /* ─── Load palette (BGRA → ARGB8888) ─── */
    ud->pal_count = 256;
    if (pal_data && pal_len >= 1024)
    {
        const Uint8* pb = (const Uint8*)pal_data;
        for (Uint32 i = 0; i < 256; i++)
        {
            Uint8 b = pb[i * 4 + 0];
            Uint8 g = pb[i * 4 + 1];
            Uint8 r = pb[i * 4 + 2];
            Uint8 a = pb[i * 4 + 3];
            ud->pal[i] = ((Uint32)a << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
        }
    }
    else if (pal_data && pal_len >= 768)
    {
        const Uint8* pb = (const Uint8*)pal_data;
        for (Uint32 i = 0; i < 256; i++)
        {
            Uint8 b = pb[i * 3 + 0];
            Uint8 g = pb[i * 3 + 1];
            Uint8 r = pb[i * 3 + 2];
            ud->pal[i] = (255u << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
        }
    }
    else
    {
        /* Fallback if pal_data is completely missing/optional */
        for (Uint32 i = 0; i < 256; i++)
            ud->pal[i] = (255u << 24) | (i << 16) | (i << 8) | i;
    }
    JY_CalcPalMod(ud);
    ud->pal_version = 0;

    /* ─── Parse frames table ─── */
    Uint32 n = total_expected_frames;

    ud->frame_count = n;
    ud->frames = (JY_FrameInfo*)SDL_calloc(n, sizeof(JY_FrameInfo));
    if (!ud->frames)
        return luaL_error(L, "JY: out of memory for %d frames", (int)n);

    for (Uint32 i = 0; i < n; i++)
    {
        lua_geti(L, frames_arg_idx, (lua_Integer)(i + 1));
        if (lua_istable(L, -1))
        {
            JY_FrameInfo* f = &ud->frames[i];

            lua_getfield(L, -1, "sx");
            f->sx = lua_isnil(L, -1) ? 0 : (Uint32)lua_tointeger(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "sy");
            f->sy = lua_isnil(L, -1) ? 0 : (Uint32)lua_tointeger(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "sw");
            f->sw = lua_isnil(L, -1) ? 0 : (Uint32)lua_tointeger(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "sh");
            f->sh = lua_isnil(L, -1) ? 0 : (Uint32)lua_tointeger(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "key_x");
            f->key_x = lua_isnil(L, -1) ? 0 : (Sint32)lua_tointeger(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "key_y");
            f->key_y = lua_isnil(L, -1) ? 0 : (Sint32)lua_tointeger(L, -1);
            lua_pop(L, 1);

            /* ★ R10: atlas mc.animate.frames[i].z（逐帧深度偏移）——与 JS shader Rect.zOffset = -I.z 对齐
             *   Lua 端解析Atlas帧 应试试传 z = tonumber(f.z) or 0，该字段为集成到 JY_FrameInfo */
            lua_getfield(L, -1, "z");
            f->z = lua_isnil(L, -1) ? 0 : (Sint16)lua_tointeger(L, -1);
            lua_pop(L, 1);

            JY_UpdateFrameBounds(ud, f);
        }
        lua_pop(L, 1);
    }

    /* ─── Init cache ─── */
    ud->cache_cap = JY_CACHE_CAP_DEFAULT;
    ud->cache = (JY_CacheEntry*)SDL_calloc(ud->cache_cap, sizeof(JY_CacheEntry));
    ud->cache_tick = 0;

    /* cache 按需同步填充，Prefetch 仅提前解码到同一 LRU */

    /* ─── Build info return table (tcp compatible) ─── */
    lua_createtable(L, 0, 16);

    lua_pushinteger(L, (lua_Integer)ud->group);
    lua_setfield(L, -2, "group");

    lua_pushinteger(L, (lua_Integer)ud->frame_per_group);
    lua_setfield(L, -2, "frame");

    lua_pushinteger(L, (lua_Integer)ud->width);
    lua_setfield(L, -2, "width");

    lua_pushinteger(L, (lua_Integer)ud->height);
    lua_setfield(L, -2, "height");

    lua_pushinteger(L, (lua_Integer)ud->global_x);
    lua_setfield(L, -2, "x");

    lua_pushinteger(L, (lua_Integer)ud->global_y);
    lua_setfield(L, -2, "y");

    lua_pushstring(L, "JY");
    lua_setfield(L, -2, "type");

    lua_pushinteger(L, (lua_Integer)ud->frame_count);
    lua_setfield(L, -2, "total");

    /* R9 紧致画布：单 act+dir 内所有帧的实际裁剪区最大值
     *   Lua 层 合成动画源.新建 用此替代 width/height 计算画布尺寸 */
    lua_pushinteger(L, (lua_Integer)ud->max_frame_w);
    lua_setfield(L, -2, "max_frame_w");
    lua_pushinteger(L, (lua_Integer)ud->max_frame_h);
    lua_setfield(L, -2, "max_frame_h");
    lua_pushinteger(L, (lua_Integer)ud->max_key_x);
    lua_setfield(L, -2, "max_key_x");
    lua_pushinteger(L, (lua_Integer)ud->max_key_y);
    lua_setfield(L, -2, "max_key_y");
    lua_pushinteger(L, (lua_Integer)ud->max_frame_right);
    lua_setfield(L, -2, "max_frame_right");
    lua_pushinteger(L, (lua_Integer)ud->max_frame_bottom);
    lua_setfield(L, -2, "max_frame_bottom");

    /* Return: userdata, info_table */
    return 2;
}

/* ═══════════════════════════════════════════
 *  Module open: require("mygxy.jy") → constructor
 * ═══════════════════════════════════════════ */
static int JY_Open(lua_State* L)
{
    JY_PerfEnsure();
    JY_EnsureSDLSurfaceMetatable(L);
    JY_RegisterMetatable(L);
    lua_pushcfunction(L, JY_NEW);
    return 1;
}

void JY_PushPerfStats(lua_State* L)
{
    JY_PerfStats snap;
    Uint32 decode_samples[JY_PERF_SAMPLE_CAP];
    Uint32 upload_samples[JY_PERF_SAMPLE_CAP];
    Uint32 decode_p95;
    Uint32 decode_p99;
    Uint32 upload_p95;
    Uint32 upload_p99;

    SDL_memset(&snap, 0, sizeof(snap));
    JY_PerfEnsure();
    if (g_jy_perf.mutex) SDL_LockMutex(g_jy_perf.mutex);
    snap = g_jy_perf;
    if (snap.decode_us.sample_count > 0)
        SDL_memcpy(decode_samples, g_jy_perf.decode_us.samples, sizeof(Uint32) * (size_t)snap.decode_us.sample_count);
    if (snap.upload_us.sample_count > 0)
        SDL_memcpy(upload_samples, g_jy_perf.upload_us.samples, sizeof(Uint32) * (size_t)snap.upload_us.sample_count);
    if (g_jy_perf.mutex) SDL_UnlockMutex(g_jy_perf.mutex);

    decode_p95 = JY_Percentile(decode_samples, snap.decode_us.sample_count, 95);
    decode_p99 = JY_Percentile(decode_samples, snap.decode_us.sample_count, 99);
    upload_p95 = JY_Percentile(upload_samples, snap.upload_us.sample_count, 95);
    upload_p99 = JY_Percentile(upload_samples, snap.upload_us.sample_count, 99);

    lua_createtable(L, 0, 3);

    lua_createtable(L, 0, 5);
    lua_pushinteger(L, (lua_Integer)snap.decode_us.count);
    lua_setfield(L, -2, "count");
    lua_pushinteger(L, (lua_Integer)snap.decode_us.total_us);
    lua_setfield(L, -2, "total");
    lua_pushinteger(L, (lua_Integer)decode_p95);
    lua_setfield(L, -2, "p95");
    lua_pushinteger(L, (lua_Integer)decode_p99);
    lua_setfield(L, -2, "p99");
    lua_pushinteger(L, (lua_Integer)snap.decode_us.sample_count);
    lua_setfield(L, -2, "samples");
    lua_setfield(L, -2, "decode_us");

    lua_createtable(L, 0, 5);
    lua_pushinteger(L, (lua_Integer)snap.upload_us.count);
    lua_setfield(L, -2, "count");
    lua_pushinteger(L, (lua_Integer)snap.upload_us.total_us);
    lua_setfield(L, -2, "total");
    lua_pushinteger(L, (lua_Integer)upload_p95);
    lua_setfield(L, -2, "p95");
    lua_pushinteger(L, (lua_Integer)upload_p99);
    lua_setfield(L, -2, "p99");
    lua_pushinteger(L, (lua_Integer)snap.upload_us.sample_count);
    lua_setfield(L, -2, "samples");
    lua_setfield(L, -2, "upload_us");

    lua_createtable(L, 0, 7);
    lua_pushinteger(L, (lua_Integer)snap.cache_bytes);
    lua_setfield(L, -2, "bytes");
    lua_pushinteger(L, (lua_Integer)snap.decoded_frames);
    lua_setfield(L, -2, "decoded_frames");
    lua_pushinteger(L, (lua_Integer)snap.cache_hits);
    lua_setfield(L, -2, "hits");
    lua_pushinteger(L, (lua_Integer)snap.cache_misses);
    lua_setfield(L, -2, "misses");
    lua_pushinteger(L, (lua_Integer)snap.lru_evictions);
    lua_setfield(L, -2, "lru_evictions");
    lua_pushinteger(L, (lua_Integer)snap.cache_clear_count);
    lua_setfield(L, -2, "clear_count");
    lua_pushinteger(L, (lua_Integer)snap.cache_clear_freed_bytes);
    lua_setfield(L, -2, "clear_freed_bytes");
    lua_setfield(L, -2, "cache");
}

MYGXY_API int luaopen_mygxy_jy(lua_State* L)
{
    return JY_Open(L);
}
