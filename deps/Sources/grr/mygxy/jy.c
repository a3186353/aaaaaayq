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
 *  Frame decode (pure C, thread‑safe)
 * ═══════════════════════════════════════════ */
static SDL_Surface* JY_DecodeFrame(JY_UserData* ud, Uint32 id)
{
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

    if (SDL_MUSTLOCK(sf))
        SDL_LockSurface(sf);

    Uint32* dst = (Uint32*)sf->pixels;
    Uint32 stride = (Uint32)(sf->pitch / 4);
    Uint32 aw = ud->atlas_w;
    Uint32 ibpp = ud->index_bpp;
    Uint32 abpp = ud->alpha_bpp;

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

            /* Index pixel → palette index from R channel */
            Uint32 idx_off = (src_y * aw + src_x) * ibpp;
            Uint8 pal_idx = ud->index_pixels[idx_off]; /* R channel */

            /* Alpha pixel */
            Uint8 alpha = 255;
            if (ud->alpha_pixels)
            {
                Uint32 a_off = (src_y * aw + src_x) * abpp;
                alpha = ud->alpha_pixels[a_off]; /* first channel */
            }

            Uint32 color = ud->pal[pal_idx % ud->pal_count];
            dst[y * stride + x] = (color & 0x00FFFFFF) | ((Uint32)alpha << 24);
        }
    }

    if (SDL_MUSTLOCK(sf))
        SDL_UnlockSurface(sf);

    SDL_SetSurfaceBlendMode(sf, SDL_BLENDMODE_BLEND);
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

/* Cache owns the surface — caller must NOT free sf after insert */
static void JY_CacheInsert(JY_UserData* ud, Uint32 frame_id, SDL_Surface* sf)
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

    e->surface = sf;
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

        /* Decode */
        SDL_Surface* sf = JY_DecodeFrame(ud, task.frame_id);
        if (sf)
        {
            SDL_LockMutex(ud->queue_mutex);
            /* CacheInsert takes ownership of sf */
            JY_CacheInsert(ud, task.frame_id, sf);
            SDL_UnlockMutex(ud->queue_mutex);
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
    SDL_Surface* sf = JY_DecodeFrame(ud, id);
    if (!sf)
        return 0;

    /* Insert a duplicate into cache; Lua owns the original */
    if (ud->cache && ud->queue_mutex)
    {
        SDL_Surface* cache_copy = SDL_DuplicateSurface(sf);
        if (cache_copy)
        {
            SDL_LockMutex(ud->queue_mutex);
            JY_CacheInsert(ud, id, cache_copy);
            SDL_UnlockMutex(ud->queue_mutex);
        }
    }

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
    if (len == 1024)
    {
        SDL_memcpy(ud->pal, pal, 1024);
        ud->pal_version++;
        /* Invalidate cache lazily: new pal_version won't match older entries */
    }
    return 0;
}

/* ═══════════════════════════════════════════
 *  Lua API: GetPal() → pal_string (1024 bytes)
 * ═══════════════════════════════════════════ */
static int JY_GetPal(lua_State* L)
{
    JY_UserData* ud = JY_Check(L, 1);
    lua_pushlstring(L, (const char*)ud->pal, 1024);
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

/* Extract raw pixel data from SDL_Surface */
static Uint8* JY_ExtractPixels(SDL_Surface* sf, Uint32* out_w, Uint32* out_h, Uint32* out_bpp)
{
    if (!sf)
        return NULL;

    *out_w = (Uint32)sf->w;
    *out_h = (Uint32)sf->h;
    *out_bpp = (Uint32)sf->format->BytesPerPixel;

    size_t total = (size_t)sf->w * (size_t)sf->h * (size_t)sf->format->BytesPerPixel;
    Uint8* pixels = (Uint8*)SDL_malloc(total);
    if (!pixels)
        return NULL;

    if (SDL_MUSTLOCK(sf))
        SDL_LockSurface(sf);

    /* Copy row by row to handle pitch */
    Uint32 row_bytes = (Uint32)sf->w * sf->format->BytesPerPixel;
    for (int y = 0; y < sf->h; y++)
    {
        SDL_memcpy(pixels + y * row_bytes,
                   (Uint8*)sf->pixels + y * sf->pitch,
                   row_bytes);
    }

    if (SDL_MUSTLOCK(sf))
        SDL_UnlockSurface(sf);

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

        /* Search for PNG signature (matching view.py) */
        const Uint8* raw = data + ofs;
        const Uint8* png_start = NULL;
        for (Uint32 j = 0; j + 4 <= blen; j++)
        {
            if (raw[j] == 0x89 && raw[j+1] == 0x50 && raw[j+2] == 0x4E && raw[j+3] == 0x47)
            {
                png_start = raw + j;
                blen -= j;
                break;
            }
        }
        if (!png_start)
            continue;

        SDL_RWops* rw = SDL_RWFromMem((void*)png_start, (int)blen);
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
            /* RGB format (256*3 bytes) */
            const Uint8* ppb = (const Uint8*)pal_data;
            for (Uint32 i = 0; i < 256; i++)
            {
                Uint8 r = ppb[i * 3 + 0];
                Uint8 g = ppb[i * 3 + 1];
                Uint8 b = ppb[i * 3 + 2];
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

    /* ─── Start worker threads ─── */
    JY_StartWorkers(ud);

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

    /* Arg 2: alpha PNG binary */
    size_t alpha_len;
    const char* alpha_data = luaL_checklstring(L, 2, &alpha_len);

    /* Arg 3: palette data (1024 bytes BGRA) */
    size_t pal_len;
    const char* pal_data = luaL_checklstring(L, 3, &pal_len);
    if (pal_len < 1024)
        return luaL_error(L, "JY: palette must be 1024 bytes, got %d", (int)pal_len);

    /* Arg 4: frames table */
    luaL_checktype(L, 4, LUA_TTABLE);

    /* ─── Decode index PNG ─── */
    SDL_Surface* idx_sf = JY_LoadPNG(idx_data, idx_len);
    if (!idx_sf)
        return luaL_error(L, "JY: failed to decode index PNG");

    /* ─── Decode alpha PNG ─── */
    SDL_Surface* alpha_sf = JY_LoadPNG(alpha_data, alpha_len);
    if (!alpha_sf)
    {
        SDL_FreeSurface(idx_sf);
        return luaL_error(L, "JY: failed to decode alpha PNG");
    }

    /* ─── Create userdata ─── */
    JY_UserData* ud = (JY_UserData*)lua_newuserdata(L, sizeof(JY_UserData));
    SDL_memset(ud, 0, sizeof(JY_UserData));
    luaL_setmetatable(L, JY_MT);

    /* Extract pixels */
    ud->index_pixels = JY_ExtractPixels(idx_sf, &ud->atlas_w, &ud->atlas_h, &ud->index_bpp);
    SDL_FreeSurface(idx_sf);

    if (!ud->index_pixels)
        return luaL_error(L, "JY: failed to extract index pixels");

    Uint32 aw2, ah2;
    ud->alpha_pixels = JY_ExtractPixels(alpha_sf, &aw2, &ah2, &ud->alpha_bpp);
    SDL_FreeSurface(alpha_sf);

    /* alpha atlas dimensions should match index */
    if (aw2 != ud->atlas_w || ah2 != ud->atlas_h)
    {
        /* Tolerate mismatch but use min dimensions */
        if (aw2 < ud->atlas_w) ud->atlas_w = aw2;
        if (ah2 < ud->atlas_h) ud->atlas_h = ah2;
    }

    /* ─── Load palette (BGRA → ARGB8888) ─── */
    const Uint8* pb = (const Uint8*)pal_data;
    ud->pal_count = 256;
    for (Uint32 i = 0; i < 256; i++)
    {
        Uint8 b = pb[i * 4 + 0];
        Uint8 g = pb[i * 4 + 1];
        Uint8 r = pb[i * 4 + 2];
        Uint8 a = pb[i * 4 + 3];
        ud->pal[i] = ((Uint32)a << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
    }
    ud->pal_version = 0;

    /* ─── Parse frames table ─── */
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

    /* Count array entries in frames table */
    lua_len(L, 4);
    Uint32 n = (Uint32)lua_tointeger(L, -1);
    lua_pop(L, 1);

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

    /* ─── Start worker threads ─── */
    JY_StartWorkers(ud);

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
