#include "sdl_proxy.h"
#include "tcp.h"

#if defined(_WIN32)
#define MYGXY_API __declspec(dllexport)
#else
#define MYGXY_API LUAMOD_API
#endif

static void* TCP_TestUserData(lua_State* L, int idx, const char* tname)
{
    void* p = lua_touserdata(L, idx);
    if (!p)
        return NULL;
    if (!lua_getmetatable(L, idx))
        return NULL;
    luaL_getmetatable(L, tname);
    int eq = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    return eq ? p : NULL;
}

static TCP_UserData* TCP_Check(lua_State* L, int idx)
{
    void* p = TCP_TestUserData(L, idx, TCP_MT_XYQ);
    if (!p)
        p = TCP_TestUserData(L, idx, TCP_MT_XY2);
    if (!p)
        luaL_error(L, "invalid tcp userdata");
    return (TCP_UserData*)p;
}

static int TCP_GetFrame(lua_State* L);
static int TCP_SetPP(lua_State* L);
static int TCP_GetPal(lua_State* L);
static int TCP_SetPal(lua_State* L);
static int TCP_SetPalette(lua_State* L);
static int TCP_GC(lua_State* L);

static const luaL_Reg TCP_FUNCS[] = {
    {"__gc", TCP_GC},
    {"__close", TCP_GC},
    {"GetFrame", TCP_GetFrame},
    {"get_frame", TCP_GetFrame},
    {"SetPP", TCP_SetPP},
    {"GetPal", TCP_GetPal},
    {"get_palette", TCP_GetPal},
    {"SetPal", TCP_SetPal},
    {"set_palette", TCP_SetPalette},
    {NULL, NULL},
};

static int TCP_SetPalette(lua_State* L)
{
    int top = lua_gettop(L);
    if (top < 2)
        return 0;
    if (lua_type(L, 2) == LUA_TSTRING)
        return TCP_SetPal(L);
    if (top >= 12)
        return TCP_SetPP(L);
    return 0;
}

static void TCP_RegisterMetatable(lua_State* L, const char* tname, const luaL_Reg* funcs)
{
    luaL_getmetatable(L, tname);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        luaL_newmetatable(L, tname);
        luaL_setfuncs(L, funcs, 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
}

static void TCP_RegisterMetatableAlias(lua_State* L, const char* alias, const char* base, const luaL_Reg* funcs)
{
    TCP_RegisterMetatable(L, base, funcs);

    luaL_getmetatable(L, alias);
    if (!lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        return;
    }
    lua_pop(L, 1);

    luaL_getmetatable(L, base);
    lua_setfield(L, LUA_REGISTRYINDEX, alias);
}

static int TCP_LUA_FreeSurface(lua_State* L)
{
    SDL_Surface** sf = (SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    if (*sf)
    {
        (*sf)->refcount--;
        if ((*sf)->refcount == 0)
        {
            SDL_FreeSurface(*sf);
            *sf = NULL;
        }
    }
    return 0;
}

static void TCP_EnsureSDLSurfaceMetatable(lua_State* L)
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
        lua_pushcfunction(L, TCP_LUA_FreeSurface);
        lua_setfield(L, -2, "__gc");
    }
    else
    {
        lua_pop(L, 1);
    }

    lua_getfield(L, -1, "__close");
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_pushcfunction(L, TCP_LUA_FreeSurface);
        lua_setfield(L, -2, "__close");
    }
    else
    {
        lua_pop(L, 1);
    }

    lua_getfield(L, -1, "__index");
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }
    else
    {
        lua_pop(L, 1);
    }

    lua_pop(L, 1);
}

static Uint32 RGB565to888(Uint16 color, Uint8 Alpha)
{
    Uint32 r = (color >> 11) & 0x1f;
    Uint32 g = (color >> 5) & 0x3f;
    Uint32 b = (color) & 0x1f;

    Uint32 A = Alpha << 24;
    Uint32 R = ((r * 255 + 15) / 31) << 16;
    Uint32 G = ((g * 255 + 31) / 63) << 8;
    Uint32 B = ((b * 255 + 15) / 31);

    return A | R | G | B;
}

static Uint32 TCP_FindNextOffset(const Uint32* list, Uint32 count, Uint32 cur, Uint32 fallback)
{
    Uint32 next = fallback;
    for (Uint32 i = 0; i < count; i++)
    {
        Uint32 v = list[i];
        if (v > cur && v < next)
            next = v;
    }
    return next;
}

static int TCP_ClampRange(Uint32 start, Uint32 end, Uint32 limit, Uint32* outStart, Uint32* outEnd)
{
    if (start > limit)
        return 0;
    if (end > limit)
        end = limit;
    if (end < start)
        return 0;
    *outStart = start;
    *outEnd = end;
    return 1;
}

#define TCP_FRAME_HEADER_SIZE 20
#define TCP_LAYER_HEADER_SIZE 20
#define TCP_RLE_CMD_TRANSPARENT 192
#define TCP_RLE_CMD_COLOR_RUN 160
#define TCP_RLE_CMD_DELTA 128

#define TCP_ALIGN4(x) (((x) + 3) & ~3U)

static Uint32 TCP_Pal8(const TCP_UserData* ud, Uint8 idx)
{
    if (ud->pal_dyn && ud->pal_dyn != ud->pal && ud->pal_count && idx < ud->pal_count)
        return ud->pal_dyn[idx];
    return ud->pal[idx];
}

static Uint32 BlendOver_ARGB8888(Uint32 below, Uint32 over)
{
    Uint32 a2 = (over >> 24) & 0xFF;
    
    // 快速路径：完全不透明或完全透明
    if (a2 == 255) return over;
    if (a2 == 0) return below;

    Uint32 a1 = (below >> 24) & 0xFF;
    Uint32 r1 = (below >> 16) & 0xFF;
    Uint32 g1 = (below >> 8) & 0xFF;
    Uint32 b1 = (below) & 0xFF;

    Uint32 r2 = (over >> 16) & 0xFF;
    Uint32 g2 = (over >> 8) & 0xFF;
    Uint32 b2 = (over) & 0xFF;

    Uint32 A = a1 + a2 - (a1 * a2) / 255;
    if (A == 0)
        return 0;

    Uint32 B = (b1 * a1 + b2 * a2 - ((a1 * a2) / 255) * b1) / A;
    Uint32 G = (g1 * a1 + g2 * a2 - ((a1 * a2) / 255) * g1) / A;
    Uint32 R = (r1 * a1 + r2 * a2 - ((a1 * a2) / 255) * r1) / A;

    if (B > 255) B = 255;
    if (G > 255) G = 255;
    if (R > 255) R = 255;
    if (A > 255) A = 255;

    return (A << 24) | (R << 16) | (G << 8) | B;
}

typedef struct
{
    int premul;
    int outline;
    Uint32 outline_color;
    Uint8 threshold;
} TCP_FrameOpts;

static void TCP_DefaultFrameOpts(TCP_FrameOpts* opts)
{
    opts->premul = 0;
    opts->outline = 0;
    opts->outline_color = 0xFF000000u;
    opts->threshold = 0;
}

static void TCP_ParseFrameOpts(lua_State* L, TCP_FrameOpts* opts)
{
    TCP_DefaultFrameOpts(opts);

    int top = lua_gettop(L);
    if (top < 3 || lua_isnil(L, 3))
        return;

    if (lua_istable(L, 3))
    {
        lua_getfield(L, 3, "premul");
        if (!lua_isnil(L, -1))
            opts->premul = lua_toboolean(L, -1) ? 1 : 0;
        lua_pop(L, 1);

        lua_getfield(L, 3, "premultiply");
        if (!lua_isnil(L, -1))
            opts->premul = lua_toboolean(L, -1) ? 1 : 0;
        lua_pop(L, 1);

        lua_getfield(L, 3, "outline");
        if (!lua_isnil(L, -1))
            opts->outline = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 3, "stroke");
        if (!lua_isnil(L, -1))
            opts->outline = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 3, "outline_color");
        if (!lua_isnil(L, -1))
            opts->outline_color = (Uint32)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 3, "color");
        if (!lua_isnil(L, -1))
            opts->outline_color = (Uint32)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 3, "threshold");
        if (!lua_isnil(L, -1))
        {
            int t = (int)lua_tointeger(L, -1);
            if (t < 0)
                t = 0;
            if (t > 255)
                t = 255;
            opts->threshold = (Uint8)t;
        }
        lua_pop(L, 1);
    }
    else
    {
        opts->premul = lua_toboolean(L, 3) ? 1 : 0;

        if (top >= 4 && !lua_isnil(L, 4))
            opts->outline = (int)lua_tointeger(L, 4);
        if (top >= 5 && !lua_isnil(L, 5))
            opts->outline_color = (Uint32)lua_tointeger(L, 5);
        if (top >= 6 && !lua_isnil(L, 6))
        {
            int t = (int)lua_tointeger(L, 6);
            if (t < 0)
                t = 0;
            if (t > 255)
                t = 255;
            opts->threshold = (Uint8)t;
        }
    }

    if (opts->outline < 0)
        opts->outline = 0;
    if (opts->outline > 64)
        opts->outline = 64;
}

static SDL_Surface* TCP_EnsureARGB8888(SDL_Surface* sf)
{
    if (!sf)
        return NULL;
    if (sf->format && sf->format->format == SDL_PIXELFORMAT_ARGB8888)
        return sf;
    SDL_Surface* temp = SDL_ConvertSurfaceFormat(sf, SDL_PIXELFORMAT_ARGB8888, SDL_SWSURFACE);
    SDL_FreeSurface(sf);
    return temp;
}

static SDL_Surface* TCP_ApplyOutline_ARGB8888(SDL_Surface* src, int radius, Uint32 outlineColor, Uint8 threshold)
{
    if (!src || radius <= 0)
        return src;
    if (!src->format || src->format->format != SDL_PIXELFORMAT_ARGB8888)
        return NULL;

    int w = src->w;
    int h = src->h;
    int outW = w + radius * 2;
    int outH = h + radius * 2;
    if (outW <= 0 || outH <= 0)
        return NULL;

    SDL_Surface* dstSf = SDL_CreateRGBSurfaceWithFormat(SDL_SWSURFACE, outW, outH, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!dstSf)
        return NULL;
    SDL_FillRect(dstSf, NULL, 0);

    if (SDL_MUSTLOCK(src))
        SDL_LockSurface(src);
    if (SDL_MUSTLOCK(dstSf))
        SDL_LockSurface(dstSf);

    Uint32* srcPixels = (Uint32*)src->pixels;
    Uint32* dstPixels = (Uint32*)dstSf->pixels;
    int srcStride = src->pitch / 4;
    int dstStride = dstSf->pitch / 4;

    Uint8* mask = (Uint8*)SDL_calloc((size_t)w * (size_t)h, 1);
    if (!mask)
    {
        if (SDL_MUSTLOCK(dstSf))
            SDL_UnlockSurface(dstSf);
        if (SDL_MUSTLOCK(src))
            SDL_UnlockSurface(src);
        SDL_FreeSurface(dstSf);
        return NULL;
    }

    int rr = radius * radius;
    for (int y = 0; y < h; y++)
    {
        Uint32* row = srcPixels + y * srcStride;
        for (int x = 0; x < w; x++)
        {
            Uint8 a = (Uint8)((row[x] >> 24) & 0xFF);
            if (a <= threshold)
                continue;
            for (int dy = -radius; dy <= radius; dy++)
            {
                int ny = y + dy;
                if (ny < 0 || ny >= h)
                    continue;
                int ddy = dy * dy;
                for (int dx = -radius; dx <= radius; dx++)
                {
                    if (dx * dx + ddy > rr)
                        continue;
                    int nx = x + dx;
                    if (nx < 0 || nx >= w)
                        continue;
                    mask[(size_t)ny * (size_t)w + (size_t)nx] = 1;
                }
            }
        }
    }

    for (int y = 0; y < h; y++)
    {
        Uint32* srcRow = srcPixels + y * srcStride;
        Uint32* dstRow = dstPixels + (y + radius) * dstStride + radius;
        size_t base = (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++)
        {
            if (!mask[base + (size_t)x])
                continue;
            Uint8 a = (Uint8)((srcRow[x] >> 24) & 0xFF);
            if (a <= threshold)
                dstRow[x] = outlineColor;
        }
    }

    for (int y = 0; y < h; y++)
    {
        Uint32* srcRow = srcPixels + y * srcStride;
        Uint32* dstRow = dstPixels + (y + radius) * dstStride + radius;
        for (int x = 0; x < w; x++)
        {
            Uint32 p = srcRow[x];
            if (((p >> 24) & 0xFF) != 0)
                dstRow[x] = p;
        }
    }

    SDL_free(mask);
    if (SDL_MUSTLOCK(dstSf))
        SDL_UnlockSurface(dstSf);
    if (SDL_MUSTLOCK(src))
        SDL_UnlockSurface(src);
    SDL_FreeSurface(src);
    return dstSf;
}

static int TCP_PostProcessSurface(lua_State* L, SDL_Surface** ioSf, Uint32* ioW, Uint32* ioH, Sint32* ioX, Sint32* ioY, const TCP_FrameOpts* opts)
{
    if (!ioSf || !*ioSf || !opts)
        return 1;

    if (opts->outline > 0 || opts->premul)
    {
        SDL_Surface* sf = TCP_EnsureARGB8888(*ioSf);
        if (!sf)
        {
            *ioSf = NULL;
            return luaL_error(L, "%s", SDL_GetError());
        }
        *ioSf = sf;
    }

    if (opts->outline > 0)
    {
        SDL_Surface* out = TCP_ApplyOutline_ARGB8888(*ioSf, opts->outline, opts->outline_color, opts->threshold);
        if (!out)
        {
            SDL_FreeSurface(*ioSf);
            *ioSf = NULL;
            return luaL_error(L, "%s", SDL_GetError());
        }
        *ioSf = out;
        *ioW = (Uint32)out->w;
        *ioH = (Uint32)out->h;
        *ioX += opts->outline;
        *ioY += opts->outline;
    }

    if (opts->premul)
    {
        SDL_Surface* sf = *ioSf;
        if (!sf->format || sf->format->format != SDL_PIXELFORMAT_ARGB8888)
            return luaL_error(L, "%s", SDL_GetError());

        int r = SDL_PremultiplyAlpha(sf->w, sf->h,
            SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch,
            SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch);
        if (r < 0)
        {
            SDL_FreeSurface(*ioSf);
            *ioSf = NULL;
            return luaL_error(L, "%s", SDL_GetError());
        }
    }

    SDL_SetSurfaceBlendMode(*ioSf, SDL_BLENDMODE_BLEND);
    return 1;
}

static int TCP_PushFrame(lua_State* L, SDL_Surface* sf, Uint32 w, Uint32 h, Sint32 x, Sint32 y, const TCP_FrameOpts* opts)
{
    if (!sf)
        return 0;

    if (!TCP_PostProcessSurface(L, &sf, &w, &h, &x, &y, opts))
    {
        SDL_FreeSurface(sf);
        return 0;
    }

    SDL_Surface** sfud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
    *sfud = sf;
    luaL_setmetatable(L, "SDL_Surface");

    lua_createtable(L, 0, 4);
    lua_pushinteger(L, (lua_Integer)h);
    lua_setfield(L, -2, "height");
    lua_pushinteger(L, (lua_Integer)w);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, (lua_Integer)x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, (lua_Integer)y);
    lua_setfield(L, -2, "y");
    return 2;
}

static Uint32 RGB565to888_Pal(Uint16 color16, Uint32 R1, Uint32 G1, Uint32 B1, Uint32 R2, Uint32 G2, Uint32 B2, Uint32 R3, Uint32 G3, Uint32 B3)
{

    Uint32 r = (color16 >> 11) & 0x1f;
    Uint32 g = (color16 >> 5) & 0x3f;
    Uint32 b = (color16) & 0x1f;

    Uint32 r2 = r * R1 + g * R2 + b * R3;
    Uint32 g2 = r * G1 + g * G2 + b * G3;
    Uint32 b2 = r * B1 + g * B2 + b * B3;
    Uint32 A, R, G, B;
    r = r2 >> 8;
    g = g2 >> 8;
    b = b2 >> 8;

    r = r > 0x1f ? 0x1f : r;
    g = g > 0x3f ? 0x3f : g;
    b = b > 0x1f ? 0x1f : b;

    A = 255 << 24;
    R = ((r * 255 + 15) / 31) << 16;
    G = ((g * 255 + 31) / 63) << 8;
    B = ((b * 255 + 15) / 31);

    return A | R | G | B;
}

static int TCP_GetPS(lua_State* L, TCP_UserData* ud, Uint32 id, const TCP_FrameOpts* opts)
{
    Uint32 frameStart = ud->splist[id];
    Uint32 frameEnd = TCP_FindNextOffset(ud->splist, ud->number, frameStart, ud->len);
    Uint32 safeFrameStart, safeFrameEnd;
    if (!TCP_ClampRange(frameStart, frameEnd, ud->len, &safeFrameStart, &safeFrameEnd))
        return 0;

    Uint8* frame = ud->data + safeFrameStart;
    Uint32 frameSize = safeFrameEnd - safeFrameStart;
    SP_INFO* info = (SP_INFO*)frame;
    Uint32* line;

    Uint8* rdata;
    Uint32 Color, h;
    Uint8 style, alpha, Repeat;
    Uint32 type = 0;
    Uint32 maskSize = 0;

    if (frameSize < sizeof(SP_INFO) + 4)
        return 0;
    Uint32 maybeType = *(Uint32*)(frame + sizeof(SP_INFO));
    if (maybeType == 1 || maybeType == 2)
    {
        type = maybeType;
        if (frameSize < sizeof(SP_INFO) + 8)
            return 0;
        maskSize = *(Uint32*)(frame + sizeof(SP_INFO) + 4);
        if (maskSize > frameSize)
            return 0;
        line = (Uint32*)(frame + sizeof(SP_INFO) + 8);
    }
    else
    {
        line = (Uint32*)(frame + sizeof(SP_INFO));
    }

    {
        size_t lineOff = (size_t)((Uint8*)line - frame);
        size_t need = (size_t)info->height * sizeof(Uint32);
        if (lineOff > frameSize || need > frameSize - lineOff)
            return 0;
    }

    if (!info->width || !info->height) //如果宽高为0
        return 0;
    SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormat(SDL_SWSURFACE, (int)info->width, (int)info->height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!sf)
        return luaL_error(L, "%s", SDL_GetError());
    SDL_FillRect(sf, NULL, 0);
    SDL_SetSurfaceBlendMode(sf, SDL_BLENDMODE_BLEND);

    Uint32* wdata2 = (Uint32*)sf->pixels;
    Uint32 linelen = info->width;
    Uint32 stride = (Uint32)(sf->pitch / 4);

    for (h = 0; h < info->height; h++)
    {
        Uint32 lineStart = line[h];
        Uint32 lineEnd = (h + 1 < info->height) ? line[h + 1] : frameSize;
        if (lineStart >= frameSize)
            continue;
        if (lineEnd > frameSize)
            lineEnd = frameSize;
        if (lineEnd < lineStart)
            continue;

        Uint8* linePtr = frame + lineStart;
        Uint8* lineLimit = frame + lineEnd;
        Uint32* row = wdata2 + h * stride;
        rdata = linePtr;

        if (!*rdata)
        { //法术隔行处理
            if (h > 0)
            {
                Uint32 prevStart = line[h - 1];
                if (prevStart < frameSize && *(frame + prevStart))
                    SDL_memcpy(row, row - stride, linelen * 4);
            }
        }
        else
        {
            Uint32 pos = 0;
            while (rdata < lineLimit && *rdata && pos < linelen)
            {                                 // {00000000} 表示像素行结束，如有剩余像素用透明色代替
                Uint8* prev = rdata;
                style = (*rdata & 0xC0) >> 6; // 取字节的前两个比特
                switch (style)
                {
                case 0: // {00******}
                {
                    Uint8 idx;
                    Uint8 dd, cc;
                    Uint32 a2, c2;
                    Uint32 c1;

                    if (*rdata & 0x20)
                    {                                     // {001*****} 表示带有Alpha通道的单个像素
                        if (rdata + 2 > lineLimit)
                            break;
                        alpha = ((*rdata++) & 0x1F) << 3; // 0x1f=(11111) 获得Alpha通道的值
                        idx = *rdata++;
                        c1 = (TCP_Pal8(ud, idx) & 0xFFFFFF) | ((Uint32)alpha << 24);
                        if (rdata + 3 <= lineLimit && *rdata == 0xC0)
                        {
                            rdata++;
                            dd = *rdata++;
                            cc = *rdata++;
                            a2 = (dd & 0x1F) << 3;
                            c2 = (TCP_Pal8(ud, cc) & 0xFFFFFF) | (a2 << 24);
                            row[pos++] = BlendOver_ARGB8888(c1, c2);
                        }
                        else
                        {
                            row[pos++] = c1;
                        }
                    }
                    else
                    {                               // {000*****} 表示重复n次带有Alpha通道的像素
                        if (rdata + 3 > lineLimit)
                            break;
                        Repeat = (*rdata++) & 0x1F; // 获得重复的次数
                        alpha = (*rdata++) << 3;    // 获得Alpha通道值
                        idx = *rdata++;
                        c1 = (TCP_Pal8(ud, idx) & 0xFFFFFF) | ((Uint32)alpha << 24);
                        if (rdata + 3 <= lineLimit && *rdata == 0xC0)
                        {
                            rdata++;
                            dd = *rdata++;
                            cc = *rdata++;
                            a2 = (dd & 0x1F) << 3;
                            c2 = (TCP_Pal8(ud, cc) & 0xFFFFFF) | (a2 << 24);
                            Color = BlendOver_ARGB8888(c1, c2);
                        }
                        else
                        {
                            Color = c1;
                        }
                        while (Repeat)
                        {
                            if (pos < linelen)
                                row[pos] = Color;
                            pos++;
                            Repeat--;
                            if (pos >= linelen)
                                break;
                        }
                    }
                    break;
                }
                case 1:                         // {01******} 表示不带Alpha通道不重复的n个像素组成的数据段
                    Repeat = (*rdata++) & 0x3F; // 获得数据组中的长度
                    while (Repeat)
                    {
                        if (rdata >= lineLimit)
                            break;
                        if (pos < linelen)
                            row[pos] = TCP_Pal8(ud, *rdata);
                        pos++;
                        rdata++;
                        Repeat--;
                        if (pos >= linelen)
                            break;
                    }
                    break;
                case 2:                         // {10******} 表示重复n次像素
                    if (rdata + 2 > lineLimit)
                        break;
                    Repeat = (*rdata++) & 0x3F;
                    Color = TCP_Pal8(ud, *rdata++);
                    while (Repeat)
                    {
                        if (pos < linelen)
                            row[pos] = Color;
                        pos++;
                        Repeat--;
                        if (pos >= linelen)
                            break;
                    }
                    break;
                case 3:                         // {11******} 表示跳过n个像素，跳过的像素用透明色代替
                    Repeat = (*rdata++) & 0x3f; // 获得重复次数
                    if (!Repeat)
                    { //非常规处理
                        if (rdata + 2 > lineLimit)
                            rdata = lineLimit;
                        else
                            rdata += 2;
                        break;
                    }
                    pos += Repeat;
                    break;
                }

                if (rdata == prev)
                    break;
            }
        }
    }

    if ((type == 1 || type == 2) && maskSize > 0 && maskSize < frameSize)
    {
        Sint16 headx = *(Sint16*)(ud->data + 12);
        Sint16 heady = *(Sint16*)(ud->data + 14);
        Sint32 baseTopX = (Sint32)headx - info->x;
        Sint32 baseTopY = (Sint32)heady - info->y;

        Uint8* mframe = frame + maskSize;
        Uint32 mframeSize = frameSize - maskSize;
        SP_INFO* minfo = (SP_INFO*)mframe;
        if (minfo->width && minfo->height)
        {
            Sint32 maskTopX = (Sint32)headx - minfo->x;
            Sint32 maskTopY = (Sint32)heady - minfo->y;
            Sint32 dx = maskTopX - baseTopX;
            Sint32 dy = maskTopY - baseTopY;

            if (mframeSize < sizeof(SP_INFO) + 4)
                return TCP_PushFrame(L, sf, info->width, info->height, info->x, info->y, opts);

            Uint32* mline;
            Uint32 mMaybeType = *(Uint32*)(mframe + sizeof(SP_INFO));
            if (mMaybeType == 1 || mMaybeType == 2)
                mline = (Uint32*)(mframe + sizeof(SP_INFO) + 8);
            else
                mline = (Uint32*)(mframe + sizeof(SP_INFO));

            {
                size_t mLineOff = (size_t)((Uint8*)mline - mframe);
                size_t need = (size_t)minfo->height * sizeof(Uint32);
                if (mLineOff > mframeSize || need > mframeSize - mLineOff)
                    goto skip_mask;
            }

            Uint32 dstW = info->width;
            Uint32 dstH = info->height;
            Uint32 dstStride = (Uint32)(sf->pitch / 4);
            Uint32* dst = (Uint32*)sf->pixels;

            Uint32 linelenMask = minfo->width;
            Uint32* prevMaskRow = (Uint32*)SDL_calloc((size_t)linelenMask, sizeof(Uint32));
            Uint32* maskRow = (Uint32*)SDL_calloc((size_t)linelenMask, sizeof(Uint32));
            if (!prevMaskRow || !maskRow)
            {
                SDL_free(prevMaskRow);
                SDL_free(maskRow);
                goto skip_mask;
            }
            int prevValid = 0;

            for (Uint32 mh = 0; mh < minfo->height; mh++)
            {
                Sint32 dstY = (Sint32)mh + dy;
                if (dstY < 0 || dstY >= (Sint32)dstH)
                    continue;

                Uint32 mLineStart = mline[mh];
                Uint32 mLineEnd = (mh + 1 < minfo->height) ? mline[mh + 1] : mframeSize;
                if (mLineStart >= mframeSize)
                    continue;
                if (mLineEnd > mframeSize)
                    mLineEnd = mframeSize;
                if (mLineEnd < mLineStart)
                    continue;

                rdata = mframe + mLineStart;
                Uint8* mLineLimit = mframe + mLineEnd;
                if (rdata >= mLineLimit)
                    continue;

                if (!*rdata)
                {
                    if (prevValid)
                    {
                        for (Uint32 x = 0; x < linelenMask; x++)
                        {
                            Uint32 c = prevMaskRow[x];
                            if (!c)
                                continue;
                            Sint32 dstX = (Sint32)x + dx;
                            if (dstX < 0 || dstX >= (Sint32)dstW)
                                continue;
                            Uint32* pDst = dst + (Uint32)dstY * dstStride + (Uint32)dstX;
                            *pDst = BlendOver_ARGB8888(*pDst, c);
                        }
                    }
                    continue;
                }

                SDL_memset(maskRow, 0, (size_t)linelenMask * sizeof(Uint32));
                Uint32 pos = 0;
                while (rdata < mLineLimit && *rdata && pos < linelenMask)
                {
                    Uint8* prev = rdata;
                    style = (*rdata & 0xC0) >> 6;
                    switch (style)
                    {
                    case 0:
                    {
                        Uint8 idx;
                        Uint8 dd, cc;
                        Uint32 a2, c2;
                        Uint32 c1;

                        if (*rdata & 0x20)
                        {
                            if (rdata + 2 > mLineLimit)
                                break;
                            alpha = ((*rdata++) & 0x1F) << 3;
                            idx = *rdata++;
                            c1 = (TCP_Pal8(ud, idx) & 0xFFFFFF) | ((Uint32)alpha << 24);
                            if (rdata + 3 <= mLineLimit && *rdata == 0xC0)
                            {
                                rdata++;
                                dd = *rdata++;
                                cc = *rdata++;
                                a2 = (dd & 0x1F) << 3;
                                c2 = (TCP_Pal8(ud, cc) & 0xFFFFFF) | (a2 << 24);
                                if (pos < linelenMask)
                                    maskRow[pos++] = BlendOver_ARGB8888(c1, c2);
                            }
                            else
                            {
                                if (pos < linelenMask)
                                    maskRow[pos++] = c1;
                            }
                        }
                        else
                        {
                            if (rdata + 3 > mLineLimit)
                                break;
                            Repeat = (*rdata++) & 0x1F;
                            alpha = (*rdata++) << 3;
                            idx = *rdata++;
                            c1 = (TCP_Pal8(ud, idx) & 0xFFFFFF) | ((Uint32)alpha << 24);
                            if (rdata + 3 <= mLineLimit && *rdata == 0xC0)
                            {
                                rdata++;
                                dd = *rdata++;
                                cc = *rdata++;
                                a2 = (dd & 0x1F) << 3;
                                c2 = (TCP_Pal8(ud, cc) & 0xFFFFFF) | (a2 << 24);
                                Color = BlendOver_ARGB8888(c1, c2);
                            }
                            else
                            {
                                Color = c1;
                            }

                            while (Repeat)
                            {
                                if (pos >= linelenMask)
                                {
                                    pos = linelenMask;
                                    break;
                                }
                                maskRow[pos++] = Color;
                                Repeat--;
                            }
                        }
                        break;
                    }
                    case 1:
                        Repeat = (*rdata++) & 0x3F;
                        while (Repeat)
                        {
                            if (pos >= linelenMask)
                            {
                                pos = linelenMask;
                                break;
                            }
                            if (rdata >= mLineLimit)
                                break;
                            maskRow[pos++] = TCP_Pal8(ud, *rdata++);
                            Repeat--;
                        }
                        break;
                    case 2:
                        if (rdata + 2 > mLineLimit)
                            break;
                        Repeat = (*rdata++) & 0x3F;
                        Color = TCP_Pal8(ud, *rdata++);
                        while (Repeat)
                        {
                            if (pos >= linelenMask)
                            {
                                pos = linelenMask;
                                break;
                            }
                            maskRow[pos++] = Color;
                            Repeat--;
                        }
                        break;
                    default:
                        Repeat = (*rdata++) & 0x3F;
                        if (!Repeat)
                        {
                            if (rdata + 2 > mLineLimit)
                                rdata = mLineLimit;
                            else
                                rdata += 2;
                        }
                        else
                        {
                            Uint32 remain = linelenMask - pos;
                            if (Repeat >= remain)
                                pos = linelenMask;
                            else
                                pos += Repeat;
                        }
                        break;
                    }

                    if (rdata == prev)
                        break;
                }

                for (Uint32 x = 0; x < linelenMask; x++)
                {
                    Uint32 c = maskRow[x];
                    if (!c)
                        continue;
                    Sint32 dstX = (Sint32)x + dx;
                    if (dstX < 0 || dstX >= (Sint32)dstW)
                        continue;
                    Uint32* pDst = dst + (Uint32)dstY * dstStride + (Uint32)dstX;
                    *pDst = BlendOver_ARGB8888(*pDst, c);
                }

                Uint32* tmp = prevMaskRow;
                prevMaskRow = maskRow;
                maskRow = tmp;
                prevValid = 1;
            }

            SDL_free(prevMaskRow);
            SDL_free(maskRow);
        }
    }

skip_mask:

    return TCP_PushFrame(L, sf, info->width, info->height, info->x, info->y, opts);
}

// 解码单个图层的RLE数据到临时缓冲区
// 返回该图层消费的实际字节数（对齐后），如果失败返回0
static Uint32 TCP_DecodeTPLayer(
    Uint8* base,           // 文件数据基址
    Uint32 layerPos,       // 图层起始位置（图像信息头，不含帧头0x14）
    Uint32 dataEnd,        // 数据结束边界
    Uint32* pal,           // 调色板
    Uint32 palCount,       // 调色板颜色数
    Uint32* dstPixels,     // 目标像素缓冲区
    Uint32 dstW,           // 目标宽度
    Uint32 dstH,           // 目标高度
    Uint32 dstStride,      // 目标行步长（像素数）
    Sint32 offsetX,        // 图层X偏移（相对于目标左上角）
    Sint32 offsetY,        // 图层Y偏移（相对于目标左上角）
    int isFirstLayer       // 是否为第一图层（决定是否blend）
)
{
    // 检查图层信息头是否可读
    if (layerPos + TCP_LAYER_HEADER_SIZE > dataEnd)
        return 0;

    Uint16 layerW = *(Uint16*)(base + layerPos + 4);
    Uint16 layerH = *(Uint16*)(base + layerPos + 6);
    if (!layerW || !layerH)
        return 0;

    // 解析行偏移表长度标志
    Uint8 lenFlag = (base[layerPos + 19] >> 5);
    Uint32 len;
    if (lenFlag & 4)
        len = 4;
    else if (lenFlag & 2)
        len = 2;
    else
        len = 1;

    // 检查行偏移表是否可读
    if (layerPos + TCP_LAYER_HEADER_SIZE + len * (Uint32)layerH > dataEnd)
        return 0;

    // 栈内存优化 (Small Vector Optimization)
    #define HEXLIST_STACK_SIZE 1024
    Uint32 stack_hexlist[HEXLIST_STACK_SIZE];
    Uint32* hexlist = stack_hexlist;

    // 如果需要的内存超过栈缓冲区，则分配堆内存
    if (layerH + 1 > HEXLIST_STACK_SIZE)
    {
        hexlist = (Uint32*)SDL_malloc(sizeof(Uint32) * ((Uint32)layerH + 1));
        if (!hexlist)
            return 0;
    }

    // 读取行偏移表
    hexlist[0] = 0;
    for (Uint32 i = 0; i < layerH; i++)
    {
        Uint32 v = 0;
        if (len == 1)
            v = base[layerPos + TCP_LAYER_HEADER_SIZE + i];
        else if (len == 2)
            v = *(Uint16*)(base + layerPos + TCP_LAYER_HEADER_SIZE + i * 2);
        else
            v = *(Uint32*)(base + layerPos + TCP_LAYER_HEADER_SIZE + i * 4);
        hexlist[i + 1] = v;
    }

    // 计算像素数据起始位置（4字节对齐）
    Uint32 pixelStart = layerPos + TCP_LAYER_HEADER_SIZE + len * layerH;
    pixelStart = TCP_ALIGN4(pixelStart);
    
    if (pixelStart > dataEnd)
    {
        if (hexlist != stack_hexlist)
            SDL_free(hexlist);
        return 0;
    }

    // 找出最后一行的结束位置，计算图层消费的总字节数
    Uint32 maxRowEnd = 0;
    for (Uint32 i = 1; i <= layerH; i++)
    {
        if (hexlist[i] > maxRowEnd)
            maxRowEnd = hexlist[i];
    }

    // 解码每一行的RLE数据
    for (Uint32 h = 0; h < layerH; h++)
    {
        Uint32 rowStart = hexlist[h];
        Uint32 rowEnd = hexlist[h + 1];
        if (rowEnd < rowStart)
            continue;

        Uint32 Position = pixelStart + rowStart;
        Uint32 wlen = rowEnd - rowStart;
        if (Position >= dataEnd)
            continue;
        Uint32 rowLimit = dataEnd - Position;
        if (wlen > rowLimit)
            wlen = rowLimit;

        // 计算目标行在目标缓冲区中的位置
        Sint32 dstRow = offsetY + (Sint32)h;
        if (dstRow < 0 || dstRow >= (Sint32)dstH)
            continue;
        Uint32 rowBase = (Uint32)dstRow * dstStride;

        Uint32 x = 0;  // RLE数据读取位置
        Uint32 y = 0;  // 图层内列位置
        
        while (x < wlen)
        {
            Uint8 b = base[Position + x];
            x++;
            
            if (b >= TCP_RLE_CMD_TRANSPARENT)
            {
                // 透明跳过指令
                Uint32 count = b & 0x3F;
                if (b == 0xFF)
                {
                    if (x >= wlen) break;
                    count += base[Position + x];
                    x++;
                }
                y += count;
            }
            else if (b >= TCP_RLE_CMD_COLOR_RUN)
            {
                // 连续不同颜色像素
                Uint32 count;
                Uint32 index;
                if (palCount <= 512)
                {
                    count = (b & 0x0F) + 1;
                    index = 16 * (b & 0x10);
                }
                else
                {
                    count = (b & 0x07) + 1;
                    index = ((Uint32)(b - TCP_RLE_CMD_COLOR_RUN) / 8) * 256;
                }
                while (count--)
                {
                    if (x >= wlen) break;
                    Uint32 c = (Uint32)base[Position + x] + index;
                    x++;
                    Sint32 dstX = offsetX + (Sint32)y;
                    if (dstX >= 0 && dstX < (Sint32)dstW && c < palCount)
                    {
                        Uint32 color = pal[c];
                        if (isFirstLayer)
                            dstPixels[rowBase + (Uint32)dstX] = color;
                        else
                            dstPixels[rowBase + (Uint32)dstX] = BlendOver_ARGB8888(dstPixels[rowBase + (Uint32)dstX], color);
                    }
                    y++;
                }
            }
            else if (b >= TCP_RLE_CMD_DELTA)
            {
                // Delta编码像素 (此处代码较长，为保证正确性保留原有逻辑结构)
                Uint32 count;
                Uint32 bankBase;
                if (palCount <= 512)
                {
                    count = (b & 0x0F) + 4;
                    bankBase = 16 * (b & 0x10);
                }
                else
                {
                    count = (b & 0x07) + 4;
                    bankBase = ((Uint32)(b - TCP_RLE_CMD_DELTA) / 8) * 256;
                }
                
                if (x >= wlen) break;
                Uint8 cur8 = base[Position + x];
                x++;
                Uint32 curIndex = (Uint32)cur8 + bankBase;
                Sint32 dstX = offsetX + (Sint32)y;
                if (dstX >= 0 && dstX < (Sint32)dstW)
                {
                    Uint32 color = (curIndex < palCount) ? pal[curIndex] : 0;
                    if (isFirstLayer)
                        dstPixels[rowBase + (Uint32)dstX] = color;
                    else if (color)
                        dstPixels[rowBase + (Uint32)dstX] = BlendOver_ARGB8888(dstPixels[rowBase + (Uint32)dstX], color);
                }
                y++;
                
                while (count > 0)
                {
                    if (x >= wlen) break;
                    Uint8 d = base[Position + x];
                    x++;
                    Sint32 delta1 = (Sint32)(((d >> 4) & 0x0F)) - 7;
                    cur8 = (Uint8)(cur8 + (Sint8)delta1);
                    curIndex = (Uint32)cur8 + bankBase;
                    dstX = offsetX + (Sint32)y;
                    if (dstX >= 0 && dstX < (Sint32)dstW)
                    {
                        Uint32 color = (curIndex < palCount) ? pal[curIndex] : 0;
                        if (isFirstLayer)
                            dstPixels[rowBase + (Uint32)dstX] = color;
                        else if (color)
                            dstPixels[rowBase + (Uint32)dstX] = BlendOver_ARGB8888(dstPixels[rowBase + (Uint32)dstX], color);
                    }
                    y++;
                    count--;
                    if (count == 0) break;
                    
                    Sint32 delta2 = (Sint32)(d & 0x0F) - 7;
                    cur8 = (Uint8)(cur8 + (Sint8)delta2);
                    curIndex = (Uint32)cur8 + bankBase;
                    dstX = offsetX + (Sint32)y;
                    if (dstX >= 0 && dstX < (Sint32)dstW)
                    {
                        Uint32 color = (curIndex < palCount) ? pal[curIndex] : 0;
                        if (isFirstLayer)
                            dstPixels[rowBase + (Uint32)dstX] = color;
                        else if (color)
                            dstPixels[rowBase + (Uint32)dstX] = BlendOver_ARGB8888(dstPixels[rowBase + (Uint32)dstX], color);
                    }
                    y++;
                    count--;
                }
            }
            else
            {
                // 连续相同颜色像素（RLE）case 2
                Uint32 index;
                Uint32 count;
                if (palCount <= 512)
                {
                    index = 4 * (b & 0x40);
                    count = b & 0x3F;
                    if (count == 0x3F)
                    {
                        if (x >= wlen) break;
                        count += base[Position + x];
                        x++;
                    }
                }
                else
                {
                    index = ((Uint32)b / 32) * 256;
                    count = b & 0x1F;
                    if (count == 0x1F)
                    {
                        if (x >= wlen) break;
                        count += base[Position + x];
                        x++;
                    }
                }
                count += 4;
                if (x >= wlen) break;
                Uint32 d = (Uint32)base[Position + x] + index;
                x++;
                Uint32 color = (d < palCount) ? pal[d] : 0;
                while (count--)
                {
                    Sint32 dstX = offsetX + (Sint32)y;
                    if (dstX >= 0 && dstX < (Sint32)dstW && d < palCount)
                    {
                        if (isFirstLayer)
                            dstPixels[rowBase + (Uint32)dstX] = color;
                        else if (color)
                            dstPixels[rowBase + (Uint32)dstX] = BlendOver_ARGB8888(dstPixels[rowBase + (Uint32)dstX], color);
                    }
                    y++;
                }
            }
        }
    }

    if (hexlist != stack_hexlist)
        SDL_free(hexlist);

    // 计算该图层消费的字节数（4字节对齐）
    Uint32 layerEnd = pixelStart + maxRowEnd;
    layerEnd = TCP_ALIGN4(layerEnd);
    return layerEnd - layerPos;
}

static int TCP_GetPT(lua_State* L, TCP_UserData* ud, Uint32 id, const TCP_FrameOpts* opts)
{
    if (!ud->tplist)
        return 0;

    Uint32 ofs = ud->tplist[id];
    if (!ofs)
        return 0;

    // 查找下一帧的起始位置作为当前帧的结束边界
    Uint32 frameEnd = TCP_FindNextOffset(ud->tplist, ud->number, ofs, ud->len);
    Uint32 safeFrameStart, safeFrameEnd;
    if (!TCP_ClampRange(ofs, frameEnd, ud->len, &safeFrameStart, &safeFrameEnd))
        return 0;

    Uint32 palCount = ud->pal_count;
    Uint32* pal = ud->pal_dyn;
    if (!pal || !palCount)
        return 0;

    Uint8* base = ud->data;

    // 读取第一个图层的信息以确定输出Surface的尺寸
    Uint32 firstLayerPos = safeFrameStart + 0x14;  // 跳过帧头0x14字节
    if (firstLayerPos + 20 > safeFrameEnd)
        return 0;

    Sint16 keyX = *(Sint16*)(base + firstLayerPos);
    Sint16 keyY = *(Sint16*)(base + firstLayerPos + 2);
    Uint16 width = *(Uint16*)(base + firstLayerPos + 4);
    Uint16 height = *(Uint16*)(base + firstLayerPos + 6);
    if (!width || !height)
        return 0;

    // 创建输出Surface
    SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormat(SDL_SWSURFACE, (int)width, (int)height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!sf)
        return luaL_error(L, "%s", SDL_GetError());
    SDL_FillRect(sf, NULL, 0);
    SDL_SetSurfaceBlendMode(sf, SDL_BLENDMODE_BLEND);
    Uint32* dst = (Uint32*)sf->pixels;
    Uint32 dstStride = (Uint32)(sf->pitch / 4);

    // 循环解析所有图层
    Uint32 currentPos = firstLayerPos;
    int layerIndex = 0;
    const int maxLayers = 64;  // 安全限制，防止无限循环

    while (currentPos + 20 <= safeFrameEnd && layerIndex < maxLayers)
    {
        // 读取当前图层的信息
        Sint16 layerKeyX = *(Sint16*)(base + currentPos);
        Sint16 layerKeyY = *(Sint16*)(base + currentPos + 2);
        Uint16 layerW = *(Uint16*)(base + currentPos + 4);
        Uint16 layerH = *(Uint16*)(base + currentPos + 6);

        // 如果图层尺寸为0，说明没有更多图层
        if (!layerW || !layerH)
            break;

        // 计算图层相对于第一图层的偏移（用于叠加定位）
        // 修正：偏移量应该是 (KeyX - layerKeyX)，即当前图层原点在第一图层坐标系中的位置
        Sint32 offsetX = (Sint32)keyX - (Sint32)layerKeyX;
        Sint32 offsetY = (Sint32)keyY - (Sint32)layerKeyY;

        // 解码当前图层
        // 注意：使用ud->len作为数据边界，而不是safeFrameEnd
        // 因为帧边界可能不精确（下一帧的偏移可能不包括当前帧的对齐填充）
        Uint32 consumed = TCP_DecodeTPLayer(
            base, currentPos, ud->len,
            pal, palCount,
            dst, (Uint32)width, (Uint32)height, dstStride,
            offsetX, offsetY,
            (layerIndex == 0) ? 1 : 0
        );

        if (consumed == 0)
            break;  // 解码失败，停止处理

        // 移动到下一个图层的位置
        currentPos += consumed;
        layerIndex++;

        // 检查是否已到达帧结束位置
        if (currentPos >= safeFrameEnd)
            break;
    }

    return TCP_PushFrame(L, sf, (Uint32)width, (Uint32)height, (Sint32)keyX, (Sint32)keyY, opts);
}

static int TCP_GetPR(lua_State* L, TCP_UserData* ud, Uint32 id, const TCP_FrameOpts* opts)
{
    RP_INFO* info = &ud->rpinfo[id];
    Uint32 ofs = ud->rplist[id].offset;
    Uint32 blen = ud->rplist[id].len;
    if (ofs > ud->len || blen > ud->len - ofs)
        return 0;
    SDL_RWops* src = SDL_RWFromMem(ud->data + ofs, blen);

    SDL_Surface* sf = IMG_Load_RW(src, SDL_TRUE);
    if (!sf)
        return luaL_error(L, "Failed to load image: %s", SDL_GetError());

    return TCP_PushFrame(L, sf, (Uint32)info->width, (Uint32)info->height, (Sint32)info->x, (Sint32)info->y, opts);
}

static int TCP_GetFrame(lua_State* L)
{
    TCP_UserData* ud = TCP_Check(L, 1);
    lua_Integer idx = luaL_checkinteger(L, 2);
    if (idx < 0)
        return luaL_error(L, "Frame index out of range: %d.", (int)idx);
    Uint32 i = (Uint32)idx;

    if (i >= ud->number)
        return luaL_error(L, "Frame index out of range: %d.", (int)idx);

    TCP_FrameOpts opts;
    TCP_ParseFrameOpts(L, &opts);

    if (ud->fmt == TCP_FMT_PS)
    {
        if (ud->splist && ud->splist[i])
            return TCP_GetPS(L, ud, i, &opts);
        return 0;
    }
    if (ud->fmt == TCP_FMT_PT)
    {
        if (ud->tplist && ud->tplist[i])
            return TCP_GetPT(L, ud, i, &opts);
        return 0;
    }
    if (ud->fmt == TCP_FMT_PR)
    {
        if (ud->rplist && ud->rplist[i].len)
            return TCP_GetPR(L, ud, i, &opts);
        return 0;
    }
    return 0;
}

static int TCP_SetPP(lua_State* L)
{
    TCP_UserData* ud = TCP_Check(L, 1);
    if (ud->splist && ud->fmt == TCP_FMT_PS && ud->sp_rgb565)
    {
        int s = (int)luaL_checkinteger(L, 2);
        int e = (int)luaL_checkinteger(L, 3);
        Uint32 r1 = (Uint32)luaL_checkinteger(L, 4);
        Uint32 g1 = (Uint32)luaL_checkinteger(L, 5);
        Uint32 b1 = (Uint32)luaL_checkinteger(L, 6);
        Uint32 r2 = (Uint32)luaL_checkinteger(L, 7);
        Uint32 g2 = (Uint32)luaL_checkinteger(L, 8);
        Uint32 b2 = (Uint32)luaL_checkinteger(L, 9);
        Uint32 r3 = (Uint32)luaL_checkinteger(L, 10);
        Uint32 g3 = (Uint32)luaL_checkinteger(L, 11);
        Uint32 b3 = (Uint32)luaL_checkinteger(L, 12);
        Uint8* data;
        SP_HEAD* head;
        Uint16* pal16;
        int i;
        if (s < 0 || e > 256)
            return 0;
        data = ud->data;
        head = (SP_HEAD*)data;
        data += head->len + 4;
        pal16 = (Uint16*)data;

        for (i = s; i < e; i++)
        {
            ud->pal[i] = RGB565to888_Pal(pal16[i], r1, g1, b1, r2, g2, b2, r3, g3, b3);
        }
        if (ud->pal_dyn && ud->pal_dyn != ud->pal)
        {
            Uint32 max = ud->pal_count < 256 ? ud->pal_count : 256;
            for (Uint32 j = 0; j < max; j++)
                ud->pal_dyn[j] = ud->pal[j];
        }
        lua_pushboolean(L, 1);
        return 1;
    }
    return 0;
}

static int TCP_GetPal(lua_State* L)
{
    TCP_UserData* ud = TCP_Check(L, 1);
    lua_pushlstring(L, (char*)ud->pal, 1024);
    return 1;
}

static int TCP_SetPal(lua_State* L)
{
    TCP_UserData* ud = TCP_Check(L, 1);
    size_t len;
    const char* pal = luaL_checklstring(L, 2, &len);
    if (len == 1024)
    {
        SDL_memcpy(ud->pal, pal, len);
        if (ud->pal_dyn && ud->pal_dyn != ud->pal)
        {
            Uint32 max = ud->pal_count < 256 ? ud->pal_count : 256;
            SDL_memcpy(ud->pal_dyn, ud->pal, max * sizeof(Uint32));
        }
    }
    return 0;
}

static int TCP_NEW(lua_State* L)
{
    size_t len;
    Uint8* data = (Uint8*)luaL_checklstring(L, 1, &len);
    return TCP_Create(L, data, len);
}

static void TCP_Reset(TCP_UserData* ud);

static int TCP_GC(lua_State* L)
{
    void* p = TCP_TestUserData(L, 1, TCP_MT_XYQ);
    if (!p)
        p = TCP_TestUserData(L, 1, TCP_MT_XY2);
    if (p)
        TCP_Reset((TCP_UserData*)p);
    return 0;
}

static void TCP_Reset(TCP_UserData* ud)
{
    if (ud->pal_dyn && ud->pal_dyn != ud->pal)
        SDL_free(ud->pal_dyn);
    ud->pal_dyn = NULL;
    if (ud->data)
        SDL_free(ud->data);
    ud->data = NULL;
    ud->splist = NULL;
    ud->rpinfo = NULL;
    ud->rplist = NULL;
    ud->tplist = NULL;
    ud->len = 0;
    ud->number = 0;
}

int TCP_Create(lua_State* L, Uint8* data, size_t len)
{
    SP_HEAD* head = (SP_HEAD*)data;

    TCP_RegisterMetatableAlias(L, TCP_MT_XYQ, TCP_MT_XY2, TCP_FUNCS);

    if (len < 16)
        return 0;

    if (head->flag != TCP_FMT_PS && head->flag != TCP_FMT_PR && head->flag != TCP_FMT_PT)
        return luaL_error(L, "Invalid type: expected RP, SP, or TP.");
    if (len > 0xFFFFFFFFu)
        return 0;
    TCP_UserData* ud = (TCP_UserData*)lua_newuserdata(L, sizeof(TCP_UserData));
    SDL_memset(ud, 0, sizeof(TCP_UserData));
    ud->data = (Uint8*)SDL_malloc(len);
    if (!ud->data)
        return 0;
    ud->len = (Uint32)len;
    SDL_memcpy(ud->data, data, len);
    data = ud->data;
    head = (SP_HEAD*)data;
    ud->fmt = head->flag;
    ud->pal_dyn = ud->pal;
    ud->pal_count = 256;
    ud->sp_rgb565 = 1;
    luaL_setmetatable(L, TCP_MT_XY2);

    lua_createtable(L, 0, 8);
    lua_pushinteger(L, (lua_Integer)head->flag);
    lua_setfield(L, -2, "flag");
    {
        char type[3];
        type[0] = (char)(head->flag & 0xFF);
        type[1] = (char)((head->flag >> 8) & 0xFF);
        type[2] = '\0';
        lua_pushstring(L, type);
        lua_setfield(L, -2, "type");
    }
    if (head->flag == TCP_FMT_PS)
    {
        Uint64 number64 = (Uint64)head->group * (Uint64)head->frame;
        if (number64 > 0xFFFFFFFFu)
        {
            TCP_Reset(ud);
            return 0;
        }

        if (head->len == 0x800F)
        {
            Uint32 HLen = ud->data[2] + 4;
            if (HLen < 6 || HLen >= ud->len)
            {
                TCP_Reset(ud);
                return 0;
            }

            Uint8 colorlen = *(ud->data + HLen - 2);
            Uint32 palCount = colorlen ? (Uint32)colorlen : 256U;
            {
                Uint64 need = (Uint64)HLen + (Uint64)palCount * 3ULL + (Uint64)number64 * 4ULL;
                if (need > (Uint64)ud->len)
                {
                    TCP_Reset(ud);
                    return 0;
                }
            }
            Uint8* pal24 = ud->data + HLen;
            Uint8* fl = pal24 + palCount * 3;

            ud->pal_dyn = (Uint32*)SDL_malloc(sizeof(Uint32) * palCount);
            if (!ud->pal_dyn)
            {
                TCP_Reset(ud);
                return 0;
            }
            ud->pal_count = palCount;
            ud->sp_rgb565 = 0;

            for (Uint32 i = 0; i < palCount; i++)
            {
                Uint8 b = pal24[i * 3 + 0];
                Uint8 g = pal24[i * 3 + 1];
                Uint8 r = pal24[i * 3 + 2];
                ud->pal_dyn[i] = (255U << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
                if (i < 256)
                    ud->pal[i] = ud->pal_dyn[i];
            }
            for (Uint32 i = palCount; i < 256; i++)
                ud->pal[i] = 0;

            ud->splist = (Uint32*)fl;
            ud->number = (Uint32)number64;
            for (Uint32 i = 0; i < ud->number; i++)
            {
                if (ud->splist[i])
                    ud->splist[i] += HLen;
            }

            lua_createtable(L, 0, 0);
            lua_setfield(L, -2, "dts");
        }
        else
        {
            {
                Uint32 headerPlus = (Uint32)head->len + 4;
                if (headerPlus < 16)
                {
                    TCP_Reset(ud);
                    return 0;
                }
                Uint64 need = (Uint64)headerPlus + 512ULL + (Uint64)number64 * 4ULL;
                if (need > (Uint64)ud->len)
                {
                    TCP_Reset(ud);
                    return 0;
                }
            }

            {
                Uint32 headerPlus = (Uint32)head->len + 4;
                Uint32 dtsSize = headerPlus - 16;
                lua_createtable(L, 0, (int)dtsSize);
                data += 16;
                for (Uint32 i = 0; i < dtsSize; i++)
                {
                    lua_pushinteger(L, *data++);
                    lua_seti(L, -2, (int)i + 1);
                }
                lua_setfield(L, -2, "dts");
            }

            Uint16* pal16 = (Uint16*)data;
            for (Uint32 i = 0; i < 256; i++)
            {
                ud->pal[i] = RGB565to888(pal16[i], 255);
            }
            data += 512;

            ud->splist = (Uint32*)data;
            ud->number = (Uint32)number64;
            for (Uint32 i = 0; i < ud->number; i++)
            {
                if (ud->splist[i])                    //空帧
                    ud->splist[i] += (head->len + 4); //偏移
            }
        }

        lua_pushinteger(L, head->group);
        lua_setfield(L, -2, "group");
        lua_pushinteger(L, head->frame);
        lua_setfield(L, -2, "frame");
        lua_pushinteger(L, head->width);
        lua_setfield(L, -2, "width");
        lua_pushinteger(L, head->height);
        lua_setfield(L, -2, "height");
        lua_pushinteger(L, head->x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, head->y);
        lua_setfield(L, -2, "y");
    }
    else if (head->flag == TCP_FMT_PR)
    {
        if (ud->len < sizeof(RP_HEAD))
        {
            TCP_Reset(ud);
            return 0;
        }
        RP_HEAD* rphead = (RP_HEAD*)data;
        {
            Uint64 number = (Uint64)rphead->number;
            Uint64 need = (Uint64)sizeof(RP_HEAD) + number * (Uint64)sizeof(RP_INFO) + 4ULL + number * (Uint64)sizeof(RP_LIST);
            if (need > (Uint64)ud->len)
            {
                TCP_Reset(ud);
                return 0;
            }
        }
        data += sizeof(RP_HEAD);
        ud->rpinfo = (RP_INFO*)data;
        data += rphead->number * sizeof(RP_INFO) + 4; //4未知
        ud->rplist = (RP_LIST*)data;                 //偏移
        ud->number = rphead->number;

        lua_pushinteger(L, rphead->group);
        lua_setfield(L, -2, "group");
        lua_pushinteger(L, rphead->frame);
        lua_setfield(L, -2, "frame");
        lua_pushinteger(L, rphead->width);
        lua_setfield(L, -2, "width");
        lua_pushinteger(L, rphead->height);
        lua_setfield(L, -2, "height");
        lua_pushinteger(L, rphead->x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, rphead->y);
        lua_setfield(L, -2, "y");
    }
    else if (head->flag == TCP_FMT_PT)
    {
        if (ud->len < sizeof(TP_HEAD))
        {
            TCP_Reset(ud);
            return 0;
        }
        TP_HEAD* tphead = (TP_HEAD*)data;
        Uint64 number64 = (Uint64)tphead->group * (Uint64)tphead->frame;
        if (number64 > 0xFFFFFFFFu)
        {
            TCP_Reset(ud);
            return 0;
        }
        ud->number = (Uint32)number64;
        ud->fmt = tphead->flag;
        ud->sp_rgb565 = 0;

        Uint32 palCount = tphead->palette_len;
        if (!palCount)
            palCount = 256;
        {
            Uint64 need = (Uint64)sizeof(TP_HEAD) + (Uint64)palCount * 4ULL + (Uint64)ud->number * 4ULL;
            if (need > (Uint64)ud->len)
            {
                TCP_Reset(ud);
                return 0;
            }
        }
        ud->pal_dyn = (Uint32*)SDL_malloc(sizeof(Uint32) * palCount);
        if (!ud->pal_dyn)
        {
            TCP_Reset(ud);
            return 0;
        }
        ud->pal_count = palCount;

        Uint8* palbgra = data + sizeof(TP_HEAD);
        for (Uint32 i = 0; i < palCount; i++)
        {
            Uint8 b = palbgra[i * 4 + 0];
            Uint8 g = palbgra[i * 4 + 1];
            Uint8 r = palbgra[i * 4 + 2];
            Uint8 a = palbgra[i * 4 + 3];
            if (a && a < 255)
            {
                Uint32 ha = (Uint32)a / 2u;
                Uint32 rr = ((Uint32)r * 255u + ha) / (Uint32)a;
                Uint32 gg = ((Uint32)g * 255u + ha) / (Uint32)a;
                Uint32 bb = ((Uint32)b * 255u + ha) / (Uint32)a;
                if (rr > 255u) rr = 255u;
                if (gg > 255u) gg = 255u;
                if (bb > 255u) bb = 255u;
                r = (Uint8)rr;
                g = (Uint8)gg;
                b = (Uint8)bb;
            }
            ud->pal_dyn[i] = ((Uint32)a << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
            if (i < 256)
                ud->pal[i] = ud->pal_dyn[i];
        }
        for (Uint32 i = palCount; i < 256; i++)
            ud->pal[i] = 0;

        ud->tplist = (Uint32*)(data + sizeof(TP_HEAD) + palCount * 4);

        lua_pushinteger(L, tphead->group);
        lua_setfield(L, -2, "group");
        lua_pushinteger(L, tphead->frame);
        lua_setfield(L, -2, "frame");
        lua_pushinteger(L, tphead->width);
        lua_setfield(L, -2, "width");
        lua_pushinteger(L, tphead->height);
        lua_setfield(L, -2, "height");
        lua_pushinteger(L, tphead->x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, tphead->y);
        lua_setfield(L, -2, "y");

        lua_pushinteger(L, (lua_Integer)palCount);
        lua_setfield(L, -2, "pal_count");

        lua_createtable(L, 0, 0);
        lua_setfield(L, -2, "dts");
    }

    lua_pushinteger(L, (lua_Integer)ud->number);
    lua_setfield(L, -2, "number");
    return 2;
}

static int TCP_Open(lua_State* L)
{
    TCP_EnsureSDLSurfaceMetatable(L);
    TCP_RegisterMetatableAlias(L, TCP_MT_XYQ, TCP_MT_XY2, TCP_FUNCS);
    lua_pushcfunction(L, TCP_NEW);
    return 1;
}

MYGXY_API int luaopen_mygxy_tcp(lua_State* L)
{
    init_lua_proxy();
    init_sdl_proxy();
    return TCP_Open(L);
}
