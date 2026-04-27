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


#if defined(_WIN32)
#define MYGXY_API __declspec(dllexport)
#else
#define MYGXY_API LUAMOD_API
#endif

/* ═══════════════════════════════════════════
 *  Forward declarations
 * ═══════════════════════════════════════════ */
static int JY_GetFrame(lua_State* L);
static int JY_SetPal(lua_State* L);
static int JY_GetPal(lua_State* L);
static int JY_SetPP(lua_State* L);
static int JY_SetPalette(lua_State* L);
static int JY_Prefetch(lua_State* L);
static int JY_Composite(lua_State* L);
static int JY_GC(lua_State* L);

static int JY_LUA_FreeSurface(lua_State* L);

static const luaL_Reg JY_FUNCS[] = {
    {"__gc",        JY_GC},
    {"__close",     JY_GC},
    {"GetFrame",    JY_GetFrame},
    {"get_frame",   JY_GetFrame},
    {"SetPal",      JY_SetPal},
    {"set_palette", JY_SetPalette},
    {"GetPal",      JY_GetPal},
    {"get_palette", JY_GetPal},
    {"SetPP",       JY_SetPP},
    {"Prefetch",    JY_Prefetch},
    {"Composite",   JY_Composite},
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
 *  Frame decode (pure C, thread‑safe)
 *  out_depth: if non-NULL, receives malloc'd Uint16[w*h] depth buffer
 *             depth = (G << 8) | B  from index pixels, 0 if alpha < 77
 *             (matching view.py depth extraction)
 * ═══════════════════════════════════════════ */
static SDL_Surface* JY_DecodeFrame(JY_UserData* ud, Uint32 id, Uint16** out_depth)
{
    if (out_depth) *out_depth = NULL;

    if (id >= ud->frame_count)
        return NULL;

    JY_FrameInfo* f = &ud->frames[id];
    if (f->sw == 0 || f->sh == 0)
        return NULL;

    SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormat(
        SDL_SWSURFACE, (int)f->sw, (int)f->sh, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!sf)
        return NULL;
    SDL_FillRect(sf, NULL, 0);

    /* Allocate depth buffer */
    Uint16* depth_buf = (Uint16*)SDL_calloc(f->sw * f->sh, sizeof(Uint16));

    if (SDL_MUSTLOCK(sf))
        SDL_LockSurface(sf);

    Uint32* dst = (Uint32*)sf->pixels;
    Uint32 stride = (Uint32)(sf->pitch / 4);
    Uint32 aw = ud->atlas_w;
    Uint32 ibpp = ud->index_bpp;
    Uint32 abpp = ud->alpha_bpp;
    Uint32 pmod = ud->pal_mod;

    /* Pre-compute depth buffer limits for safe access */
    Uint32 depth_buf_pixels = 0; /* total pixel count in depth_pixels buffer */
    Uint32 depth_stride = 0;     /* row stride in pixels for depth atlas */
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

            /* Index pixel → palette index from R channel, depth from G/B */
            Uint32 idx_off = (src_y * aw + src_x) * ibpp;
            Uint8 pal_idx = ud->index_pixels[idx_off]; /* R channel */
            Uint8 depth_hi = 0, depth_lo = 0;
            if (ud->depth_pixels)
            {
                if (ud->depth_frames && id < ud->depth_frame_count)
                {
                    /* Separate depth atlas with independent frame coordinates.
                     * Map pixel (x, y) in main frame → depth frame via
                     * nearest-neighbor scaling (matching Python resize NEAREST). */
                    JY_FrameInfo* df = &ud->depth_frames[id];
                    if (df->sw > 0 && df->sh > 0)
                    {
                        /* Nearest-neighbor: map (x,y) in main frame to depth frame.
                         * PILLOW uses center-pixel mapping: dst_idx + 0.5
                         * Formula: src_idx = int((dst_idx + 0.5) * src_width / dst_width)
                         * Equivalent integer math: (dst_idx * 2 + 1) * src_width / (dst_width * 2) */
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
                                depth_hi = ud->depth_pixels[d_byte_off + 1]; /* G channel */
                                depth_lo = ud->depth_pixels[d_byte_off + 2]; /* B channel */
                            }
                        }
                    }
                    /* else: depth frame has zero size, keep depth = 0 */
                }
                /* If depth_frames is missing or id is out of range, 
                 * depth_hi/depth_lo remain 0, perfectly matching Python's np.zeros_like(). */
            }
            else
            {
                /* No separate depth image: extract depth from index pixel G/B channels.
                 * Original data format: R=palette index, G/B=depth (matching view.py). */
                if (ibpp >= 3)
                {
                    depth_hi = ud->index_pixels[idx_off + 1]; /* G channel */
                    depth_lo = ud->index_pixels[idx_off + 2]; /* B channel */
                }
            }

            /* Alpha pixel */
            Uint8 alpha = 255;
            if (ud->alpha_pixels)
            {
                Uint32 a_off = (src_y * aw + src_x) * abpp;
                alpha = ud->alpha_pixels[a_off]; /* first channel */
            }

            Uint32 color = ud->pal[pal_idx % pmod];
            dst[y * stride + x] = (color & 0x00FFFFFF) | ((Uint32)alpha << 24);

            /* Depth: (G << 8) | B — matching Python: depth = 0 when alpha < 77 */
            if (depth_buf)
            {
                Uint16 d = (alpha >= 77) ? (((Uint16)depth_hi << 8) | depth_lo) : 0;
                depth_buf[y * f->sw + x] = d;
            }
        }
    }

    if (SDL_MUSTLOCK(sf))
        SDL_UnlockSurface(sf);

    SDL_SetSurfaceBlendMode(sf, SDL_BLENDMODE_BLEND);

    if (out_depth)
        *out_depth = depth_buf;
    else
        SDL_free(depth_buf);

    return sf;
}

/* ═══════════════════════════════════════════
 *  LRU cache
 * ═══════════════════════════════════════════ */
static JY_CacheEntry* JY_CacheLookup(JY_UserData* ud, Uint32 frame_id, Uint32 pal_ver)
{
    if (!ud->cache)
        return NULL;
    for (Uint32 i = 0; i < ud->cache_cap; i++)
    {
        JY_CacheEntry* e = &ud->cache[i];
        if (e->surface && e->frame_id == frame_id && e->pal_ver == pal_ver)
        {
            e->lru_tick = ++ud->cache_tick;
            return e;
        }
    }
    return NULL;
}

/* Cache owns the surface + depth — caller must NOT free after insert */
static void JY_CacheInsert(JY_UserData* ud, Uint32 frame_id, SDL_Surface* sf, Uint16* depth)
{
    if (!ud->cache || !sf)
        return;

    /* Find empty slot or LRU victim */
    Uint32 victim = 0;
    Uint32 min_tick = 0xFFFFFFFFu;
    for (Uint32 i = 0; i < ud->cache_cap; i++)
    {
        if (!ud->cache[i].surface)
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
    if (e->surface)
    {
        SDL_FreeSurface(e->surface);
        e->surface = NULL;
    }
    if (e->depth)
    {
        SDL_free(e->depth);
        e->depth = NULL;
    }

    e->surface = sf;
    e->depth = depth;
    e->frame_id = frame_id;
    e->pal_ver = ud->pal_version;
    e->lru_tick = ++ud->cache_tick;
}

static void JY_CacheClear(JY_UserData* ud)
{
    if (!ud->cache)
        return;
    for (Uint32 i = 0; i < ud->cache_cap; i++)
    {
        if (ud->cache[i].surface)
        {
            SDL_FreeSurface(ud->cache[i].surface);
            ud->cache[i].surface = NULL;
        }
        if (ud->cache[i].depth)
        {
            SDL_free(ud->cache[i].depth);
            ud->cache[i].depth = NULL;
        }
    }
}

/* ═══════════════════════════════════════════
 *  Worker thread
 * ═══════════════════════════════════════════ */
static int JY_WorkerFunc(void* data)
{
    JY_UserData* ud = (JY_UserData*)data;

    while (!ud->shutdown)
    {
        SDL_LockMutex(ud->queue_mutex);

        /* Wait for work */
        while (ud->task_count == 0 && !ud->shutdown)
            SDL_CondWait(ud->queue_cond, ud->queue_mutex);

        if (ud->shutdown)
        {
            SDL_UnlockMutex(ud->queue_mutex);
            break;
        }

        /* Dequeue task */
        JY_AsyncTask task = {0};
        int has_task = 0;
        if (ud->task_count > 0)
        {
            task = ud->task_queue[0];
            ud->task_count--;
            if (ud->task_count > 0)
                SDL_memmove(&ud->task_queue[0], &ud->task_queue[1],
                            ud->task_count * sizeof(JY_AsyncTask));
            has_task = 1;
        }

        SDL_UnlockMutex(ud->queue_mutex);

        if (!has_task)
            continue;

        /* Skip if already cached */
        SDL_LockMutex(ud->queue_mutex);
        JY_CacheEntry* hit = JY_CacheLookup(ud, task.frame_id, task.pal_ver);
        SDL_UnlockMutex(ud->queue_mutex);

        if (hit)
            continue;

        /* Decode (with depth) */
        Uint16* depth = NULL;
        SDL_Surface* sf = JY_DecodeFrame(ud, task.frame_id, &depth);
        if (sf)
        {
            SDL_LockMutex(ud->queue_mutex);
            /* CacheInsert takes ownership of sf + depth */
            JY_CacheInsert(ud, task.frame_id, sf, depth);
            SDL_UnlockMutex(ud->queue_mutex);
        }
        else if (depth)
        {
            SDL_free(depth);
        }
    }
    return 0;
}

static void JY_StartWorkers(JY_UserData* ud)
{
    if (ud->queue_mutex)
        return; /* Already started */

    ud->queue_mutex = SDL_CreateMutex();
    ud->queue_cond = SDL_CreateCond();
    ud->task_cap = 64;
    ud->task_queue = (JY_AsyncTask*)SDL_calloc(ud->task_cap, sizeof(JY_AsyncTask));
    ud->task_count = 0;
    ud->shutdown = 0;

    for (int i = 0; i < 2; i++)
    {
        char name[32];
        SDL_snprintf(name, sizeof(name), "jy_worker_%d", i);
        ud->workers[i] = SDL_CreateThread(JY_WorkerFunc, name, ud);
    }
}

static void JY_StopWorkers(JY_UserData* ud)
{
    if (!ud->queue_mutex)
        return;

    ud->shutdown = 1;
    SDL_CondBroadcast(ud->queue_cond);

    for (int i = 0; i < 2; i++)
    {
        if (ud->workers[i])
        {
            SDL_WaitThread(ud->workers[i], NULL);
            ud->workers[i] = NULL;
        }
    }

    SDL_DestroyMutex(ud->queue_mutex);
    ud->queue_mutex = NULL;
    SDL_DestroyCond(ud->queue_cond);
    ud->queue_cond = NULL;

    if (ud->task_queue)
    {
        SDL_free(ud->task_queue);
        ud->task_queue = NULL;
    }
    ud->task_count = 0;
}

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

    /* Try cache first — return a duplicate so cache keeps its copy */
    if (ud->cache && ud->queue_mutex)
    {
        SDL_LockMutex(ud->queue_mutex);
        JY_CacheEntry* hit = JY_CacheLookup(ud, id, ud->pal_version);
        if (hit && hit->surface)
        {
            SDL_Surface* dup = SDL_DuplicateSurface(hit->surface);
            SDL_UnlockMutex(ud->queue_mutex);
            return JY_PushFrame(L, dup, f);
        }
        SDL_UnlockMutex(ud->queue_mutex);
    }

    /* Cache miss → synchronous decode */
    Uint16* depth = NULL;
    SDL_Surface* sf = JY_DecodeFrame(ud, id, &depth);
    if (!sf)
    {
        if (depth) SDL_free(depth);
        return 0;
    }

    /* Insert a duplicate into cache; Lua owns the original */
    if (ud->cache && ud->queue_mutex)
    {
        SDL_Surface* cache_copy = SDL_DuplicateSurface(sf);
        Uint16* depth_copy = NULL;
        if (depth && sf->w > 0 && sf->h > 0)
        {
            Uint32 dpx = (Uint32)(sf->w * sf->h);
            depth_copy = (Uint16*)SDL_malloc(dpx * sizeof(Uint16));
            if (depth_copy)
                SDL_memcpy(depth_copy, depth, dpx * sizeof(Uint16));
        }
        if (cache_copy)
        {
            SDL_LockMutex(ud->queue_mutex);
            JY_CacheInsert(ud, id, cache_copy, depth_copy);
            SDL_UnlockMutex(ud->queue_mutex);
        }
        else if (depth_copy)
        {
            SDL_free(depth_copy);
        }
    }
    if (depth) SDL_free(depth);

    return JY_PushFrame(L, sf, f);
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
 *  Lua API: Prefetch(from, to) — async preload
 * ═══════════════════════════════════════════ */
static int JY_Prefetch(lua_State* L)
{
    JY_UserData* ud = JY_Check(L, 1);
    Uint32 from = (Uint32)luaL_checkinteger(L, 2);
    Uint32 to   = (Uint32)luaL_checkinteger(L, 3);

    if (from > to || to >= ud->frame_count)
        return 0;

    /* Ensure workers are running */
    if (!ud->queue_mutex)
        JY_StartWorkers(ud);

    SDL_LockMutex(ud->queue_mutex);

    for (Uint32 id = from; id <= to; id++)
    {
        /* Skip if already cached */
        JY_CacheEntry* hit = JY_CacheLookup(ud, id, ud->pal_version);
        if (hit)
            continue;

        /* Grow queue if needed */
        if (ud->task_count >= ud->task_cap)
        {
            Uint32 new_cap = ud->task_cap * 2;
            JY_AsyncTask* new_q = (JY_AsyncTask*)SDL_realloc(
                ud->task_queue, new_cap * sizeof(JY_AsyncTask));
            if (!new_q)
                break;
            ud->task_queue = new_q;
            ud->task_cap = new_cap;
        }

        JY_AsyncTask* t = &ud->task_queue[ud->task_count++];
        t->frame_id = id;
        t->pal_ver = ud->pal_version;
        t->done = 0;
    }

    SDL_CondBroadcast(ud->queue_cond);
    SDL_UnlockMutex(ud->queue_mutex);

    return 0;
}

/* ═══════════════════════════════════════════
 *  Lua API: Composite(canvas_w, canvas_h, layers_table)
 *  layers_table = { {ud, frame_id, z_offset, x, y}, ... }
 *  Per-pixel Z-test depth compositing (matching view.py OutfitComposer)
 *  Returns: SDL_Surface* (composited ARGB8888)
 * ═══════════════════════════════════════════ */
static int JY_Composite(lua_State* L)
{
    /* arg 1: self (jy ud — used as anchor + zbuf_cached 宿主) */
    JY_UserData* ud = JY_Check(L, 1);
    int canvas_w = (int)luaL_checkinteger(L, 2);
    int canvas_h = (int)luaL_checkinteger(L, 3);
    luaL_checktype(L, 4, LUA_TTABLE);

    int n = (int)lua_rawlen(L, 4);
    if (n <= 0 || canvas_w <= 0 || canvas_h <= 0)
        return 0;

    /* Allocate result surface */
    SDL_Surface* result = SDL_CreateRGBSurfaceWithFormat(
        SDL_SWSURFACE, canvas_w, canvas_h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!result)
        return luaL_error(L, "JY_Composite: failed to create result surface");
    SDL_FillRect(result, NULL, 0);

    /* M3: 复用 zbuf_cached，避免每帧 malloc/free + 全量初始化抖动
     *      首次 / 画布变大时按需扩容；初始化用 SDL_memset(0xFF) 等价 INT32_MIN
     *      （0xFFFFFFFF = -1，与 INT32_MIN 不同——保留逐像素赋值以保证语义一致） */
    Uint32 canvas_px = (Uint32)(canvas_w * canvas_h);
    if (ud->zbuf_cached_size < canvas_px)
    {
        Sint32* nz = (Sint32*)SDL_realloc(ud->zbuf_cached, canvas_px * sizeof(Sint32));
        if (!nz)
        {
            SDL_FreeSurface(result);
            return luaL_error(L, "JY_Composite: failed to alloc z-buffer");
        }
        ud->zbuf_cached = nz;
        ud->zbuf_cached_size = canvas_px;
    }
    Sint32* zbuf = ud->zbuf_cached;
    for (Uint32 zi = 0; zi < canvas_px; zi++) zbuf[zi] = (-2147483647 - 1); /* INT32_MIN */

    if (SDL_MUSTLOCK(result))
        SDL_LockSurface(result);

    Uint32* dst_pixels = (Uint32*)result->pixels;
    Uint32 dst_stride = (Uint32)(result->pitch / 4);

    /* Process each layer */
    for (int i = 1; i <= n; i++)
    {
        lua_rawgeti(L, 4, i);
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            continue;
        }

        /* Extract: {ud, frame_id, z_offset, x, y} */
        lua_rawgeti(L, -1, 1); /* ud */
        JY_UserData* layer_ud = (JY_UserData*)luaL_testudata(L, -1, JY_MT);
        lua_pop(L, 1);
        if (!layer_ud) { lua_pop(L, 1); continue; }

        lua_rawgeti(L, -1, 2); /* frame_id */
        Uint32 frame_id = (Uint32)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_rawgeti(L, -1, 3); /* z_offset */
        int z_offset = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_rawgeti(L, -1, 4); /* off_x */
        int off_x = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_rawgeti(L, -1, 5); /* off_y */
        int off_y = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_pop(L, 1); /* pop layer entry table */

        if (frame_id >= layer_ud->frame_count)
            continue;

        /* H6: 双路径 cache hit
         *  Worker 已启动：保留 SDL_DuplicateSurface 防 UAF（worker 可能并发淘汰）
         *  Worker 未启动：直接引用 cache 内 surface/depth，跳过 ~3MB/帧 拷贝
         *                 （H1 关闭主动启动后，客户端不调 Prefetch 时常驻此分支） */
        SDL_Surface* layer_sf = NULL;
        Uint16* layer_depth = NULL;
        int layer_sf_owned = 0;     /* 是否拥有所有权（结尾负责 free） */
        int layer_depth_owned = 0;

        if (layer_ud->cache && layer_ud->queue_mutex)
        {
            /* Worker 启动路径：保持原行为，复制后释放锁 */
            SDL_LockMutex(layer_ud->queue_mutex);
            JY_CacheEntry* hit = JY_CacheLookup(layer_ud, frame_id, layer_ud->pal_version);
            if (hit && hit->surface)
            {
                layer_sf = SDL_DuplicateSurface(hit->surface);
                layer_sf_owned = 1;
                if (hit->depth && hit->surface->w > 0 && hit->surface->h > 0)
                {
                    Uint32 dpx = (Uint32)(hit->surface->w * hit->surface->h);
                    layer_depth = (Uint16*)SDL_malloc(dpx * sizeof(Uint16));
                    if (layer_depth)
                    {
                        SDL_memcpy(layer_depth, hit->depth, dpx * sizeof(Uint16));
                        layer_depth_owned = 1;
                    }
                }
            }
            SDL_UnlockMutex(layer_ud->queue_mutex);
        }
        else if (layer_ud->cache)
        {
            /* ★ H6 快路径：worker 未启动 → 无并发淘汰风险 → 零拷贝直接引用 */
            JY_CacheEntry* hit = JY_CacheLookup(layer_ud, frame_id, layer_ud->pal_version);
            if (hit && hit->surface)
            {
                layer_sf = hit->surface;       /* borrow，不拥有所有权 */
                layer_depth = hit->depth;
            }
        }

        /* Cache miss → decode */
        if (!layer_sf)
        {
            Uint16* decode_depth = NULL;
            layer_sf = JY_DecodeFrame(layer_ud, frame_id, &decode_depth);
            if (!layer_sf)
                continue;
            layer_depth = decode_depth;
            layer_sf_owned = 1;
            layer_depth_owned = (decode_depth != NULL) ? 1 : 0;

            /* Insert into cache for future use */
            if (layer_ud->cache && layer_ud->queue_mutex)
            {
                /* Worker 启动路径：复制后插入，原对象保留作本帧使用 */
                SDL_Surface* cc = SDL_DuplicateSurface(layer_sf);
                Uint16* dc = NULL;
                if (decode_depth && layer_sf->w > 0 && layer_sf->h > 0)
                {
                    Uint32 dpx = (Uint32)(layer_sf->w * layer_sf->h);
                    dc = (Uint16*)SDL_malloc(dpx * sizeof(Uint16));
                    if (dc)
                        SDL_memcpy(dc, decode_depth, dpx * sizeof(Uint16));
                }
                if (cc)
                {
                    SDL_LockMutex(layer_ud->queue_mutex);
                    JY_CacheInsert(layer_ud, frame_id, cc, dc);
                    SDL_UnlockMutex(layer_ud->queue_mutex);
                }
                else if (dc)
                    SDL_free(dc);
            }
            else if (layer_ud->cache)
            {
                /* ★ H6: worker 未启动 → 直接转移所有权到 cache（节省一次拷贝）
                 *      cache insert 不会淘汰本次插入的新条目，本帧引用安全 */
                JY_CacheInsert(layer_ud, frame_id, layer_sf, layer_depth);
                layer_sf_owned = 0;
                layer_depth_owned = 0;
            }
        }

        /* Apply frame anchor offset (key_x, key_y) so frames are centered correctly.
         * Without this, each frame is placed at its raw top-left corner, causing
         * jitter as different frames have different sizes and anchor positions. */
        int anchor_x = 0, anchor_y = 0;
        if (layer_ud->frames && frame_id < layer_ud->frame_count)
        {
            anchor_x = (int)layer_ud->frames[frame_id].key_x;
            anchor_y = (int)layer_ud->frames[frame_id].key_y;
        }
        int base_x = off_x - anchor_x;
        int base_y = off_y - anchor_y;

        /* Per-pixel Z-test composite */
        int lw = layer_sf->w;
        int lh = layer_sf->h;

        if (SDL_MUSTLOCK(layer_sf))
            SDL_LockSurface(layer_sf);

        /* Pre-calculate clipping bounds so we don't branch per pixel */
        int start_y = (base_y < 0) ? -base_y : 0;
        int start_x = (base_x < 0) ? -base_x : 0;
        int end_y = (base_y + lh > canvas_h) ? canvas_h - base_y : lh;
        int end_x = (base_x + lw > canvas_w) ? canvas_w - base_x : lw;

        for (int py = start_y; py < end_y; py++)
        {
            int dy = base_y + py;
            for (int px = start_x; px < end_x; px++)
            {
                int dx = base_x + px;

                Uint32 spixel = ((Uint32*)layer_sf->pixels)[py * (layer_sf->pitch / 4) + px];
                Uint32 sa = (spixel >> 24) & 0xFF;
                if (sa == 0)
                    continue;

                Uint16 d = layer_depth ? layer_depth[py * lw + px] : 0;

                Sint32 effective_d = (Sint32)d + z_offset;

                Sint32* zp = &zbuf[dy * canvas_w + dx];
                if (effective_d >= *zp)
                {
                    *zp = effective_d;

                    /* Alpha composite (Porter-Duff over) — matching Python */
                    Uint32 dpixel = dst_pixels[dy * dst_stride + dx];
                    Uint8 da = (Uint8)((dpixel >> 24) & 0xFF);
                    if (da == 0 || sa == 255)
                    {
                        dst_pixels[dy * dst_stride + dx] = spixel;
                    }
                    else
                    {
                        Uint32 inv_sa = 255u - sa;
                        /* out_a = sa + da*(1-sa/255) ≈ sa + da*inv_sa/255 */
                        Uint32 out_a = sa + (da * inv_sa / 255u);
                        if (out_a > 255u) out_a = 255u;
                        if (out_a == 0)
                        {
                            dst_pixels[dy * dst_stride + dx] = 0;
                        }
                        else
                        {
                            Uint32 sr = (spixel >> 16) & 0xFF;
                            Uint32 sg = (spixel >> 8)  & 0xFF;
                            Uint32 sb =  spixel        & 0xFF;
                            Uint32 dr = (dpixel >> 16) & 0xFF;
                            Uint32 dg = (dpixel >> 8)  & 0xFF;
                            Uint32 db =  dpixel        & 0xFF;
                            Uint32 or_ = (sr * sa + dr * da * inv_sa / 255u) / out_a;
                            Uint32 og  = (sg * sa + dg * da * inv_sa / 255u) / out_a;
                            Uint32 ob  = (sb * sa + db * da * inv_sa / 255u) / out_a;
                            if (or_ > 255u) or_ = 255u;
                            if (og  > 255u) og  = 255u;
                            if (ob  > 255u) ob  = 255u;
                            dst_pixels[dy * dst_stride + dx] =
                                (out_a << 24) | (or_ << 16) | (og << 8) | ob;
                        }
                    }
                }
            }
        }

        if (SDL_MUSTLOCK(layer_sf))
            SDL_UnlockSurface(layer_sf);


        /* H6: 仅在拥有所有权时释放（borrow 路径不释放，由 cache 持有） */
        if (layer_sf_owned)
            SDL_FreeSurface(layer_sf);
        if (layer_depth_owned && layer_depth)
            SDL_free(layer_depth);
    }

    if (SDL_MUSTLOCK(result))
        SDL_UnlockSurface(result);

    /* M3: zbuf 复用，不释放（JY_Reset 内统一释放） */
    SDL_SetSurfaceBlendMode(result, SDL_BLENDMODE_BLEND);

    /* Push result as SDL_Surface userdata */
    SDL_Surface** sfud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
    *sfud = result;
    luaL_setmetatable(L, "SDL_Surface");

    return 1;
}

/* ═══════════════════════════════════════════
 *  GC / cleanup
 * ═══════════════════════════════════════════ */
static void JY_Reset(JY_UserData* ud)
{
    JY_StopWorkers(ud);
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
    /* M3: 释放 zbuf 复用缓冲 */
    if (ud->zbuf_cached)
    {
        SDL_free(ud->zbuf_cached);
        ud->zbuf_cached = NULL;
        ud->zbuf_cached_size = 0;
    }
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
    }

    SDL_free(y_offsets);
    SDL_free(img_entries);
    SDL_free(spr_frames);

    /* ─── Init cache ─── */
    ud->cache_cap = 128;
    ud->cache = (JY_CacheEntry*)SDL_calloc(ud->cache_cap, sizeof(JY_CacheEntry));
    ud->cache_tick = 0;

    /* ★ H1: 不主动启动 worker，仅在 :Prefetch 调用时延迟启动。
     *      客户端不调用时零内核线程/锁对象，避免最多 512 个闲置线程。
     *      JY_StartWorkers 本身仍保留并在 Prefetch 内调用。 */

    /* ─── Build info return table (tcp compatible) ─── */
    lua_createtable(L, 0, 8);

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

    return 2;
}

/* ═══════════════════════════════════════════
 *  Constructor: xy_jy(idx_png, alpha_png, pal_data, frames_table)
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

    /* Arg 3: palette data (1024 bytes BGRA) */
    size_t pal_len = 0;
    const char* pal_data = NULL;
    if (lua_type(L, 3) == LUA_TSTRING)
        pal_data = lua_tolstring(L, 3, &pal_len);
    if (pal_data && pal_len < 1024)
        return luaL_error(L, "JY: palette must be 1024 bytes, got %d", (int)pal_len);

    /* Arg 4: frames table */
    luaL_checktype(L, 4, LUA_TTABLE);

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
    lua_getfield(L, 4, "group");
    ud->group = lua_isnil(L, -1) ? 1 : (Uint32)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 4, "frame");
    ud->frame_per_group = lua_isnil(L, -1) ? 1 : (Uint32)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 4, "width");
    ud->width = lua_isnil(L, -1) ? 0 : (Uint16)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 4, "height");
    ud->height = lua_isnil(L, -1) ? 0 : (Uint16)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 4, "x");
    ud->global_x = lua_isnil(L, -1) ? 0 : (Sint16)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 4, "y");
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
    if (pal_data)
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
        lua_geti(L, 4, (lua_Integer)(i + 1));
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
        }
        lua_pop(L, 1);
    }

    /* ─── Init cache ─── */
    ud->cache_cap = 128;
    ud->cache = (JY_CacheEntry*)SDL_calloc(ud->cache_cap, sizeof(JY_CacheEntry));
    ud->cache_tick = 0;

    /* ★ H1: 不主动启动 worker（同 SPR 路径，参见上方注释） */

    /* ─── Build info return table (tcp compatible) ─── */
    lua_createtable(L, 0, 8);

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

    /* Version marker for DLL verification */
    lua_pushinteger(L, 2);
    lua_setfield(L, -2, "depth_ver");

    /* Return: userdata, info_table */
    return 2;
}

/* ═══════════════════════════════════════════
 *  Module open: require("mygxy.jy") → constructor
 * ═══════════════════════════════════════════ */
static int JY_Open(lua_State* L)
{
    JY_EnsureSDLSurfaceMetatable(L);
    JY_RegisterMetatable(L);
    lua_pushcfunction(L, JY_NEW);
    return 1;
}

MYGXY_API int luaopen_mygxy_jy(lua_State* L)
{
    return JY_Open(L);
}
