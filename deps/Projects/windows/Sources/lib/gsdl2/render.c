#include "gge.h"
#include "SDL_render.h"

static int LUA_GetNumRenderDrivers(lua_State* L)
{
    lua_pushinteger(L, SDL_GetNumRenderDrivers());
    return 1;
}

static int LUA_GetRenderDriverInfo(lua_State* L)
{
    int index = (int)luaL_checkinteger(L, 1);
    SDL_RendererInfo info;
    Uint32 i;
    if (SDL_GetRenderDriverInfo(index, &info) == 0)
    {
        lua_createtable(L, 0, 5);
        lua_pushstring(L, info.name);
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, info.flags);
        lua_setfield(L, -2, "flags");
        //lua_pushinteger(L,info.num_texture_formats);
        //lua_setfield(L,-2,"num_texture_formats");
        lua_createtable(L, info.num_texture_formats, 0);
        for (i = 0; i < info.num_texture_formats; i++)
        {
            lua_pushinteger(L, info.texture_formats[i]);
            lua_seti(L, -2, i + 1);
        }
        lua_setfield(L, -2, "texture_formats");
        lua_pushinteger(L, info.max_texture_width);
        lua_setfield(L, -2, "max_texture_width");
        lua_pushinteger(L, info.max_texture_height);
        lua_setfield(L, -2, "max_texture_height");
        return 1;
    }

    return 0;
}
//SDL_CreateWindowAndRenderer
static int LUA_CreateRenderer(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    int index = (int)luaL_checkinteger(L, 2);
    int flags = (int)luaL_checkinteger(L, 3);
    SDL_Renderer* rd = SDL_CreateRenderer(win, index, flags);
    if (rd)
    {
        SDL_Renderer** ud = (SDL_Renderer**)lua_newuserdata(L, sizeof(SDL_Renderer*));
        *ud = rd;
        luaL_setmetatable(L, "SDL_Renderer");
        return 1;
    }
    return 0;
}

static int LUA_CreateSoftwareRenderer(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    SDL_Renderer* rd = SDL_CreateSoftwareRenderer(sf);
    if (rd)
    {
        SDL_Renderer** ud = (SDL_Renderer**)lua_newuserdata(L, sizeof(SDL_Renderer*));
        *ud = rd;
        luaL_setmetatable(L, "SDL_Renderer");
        return 1;
    }
    return 0;
}

static int LUA_GetRenderer(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    SDL_Renderer* rd = SDL_GetRenderer(win);
    if (rd)
    {
        SDL_Renderer** ud = (SDL_Renderer**)lua_newuserdata(L, sizeof(SDL_Renderer*));
        *ud = rd;
        luaL_setmetatable(L, "SDL_Renderer");
        return 1;
    }
    return 0;
}

static int LUA_GetRendererInfo(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_RendererInfo info;
    Uint32 i;
    if (SDL_GetRendererInfo(rd, &info) == 0)
    {
        lua_createtable(L, 0, 5);
        lua_pushstring(L, info.name);
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, info.flags);
        lua_setfield(L, -2, "flags");
        //lua_pushinteger(L,info.num_texture_formats);
        //lua_setfield(L,-2,"num_texture_formats");
        lua_createtable(L, info.num_texture_formats, 0);
        for (i = 0; i < info.num_texture_formats; i++)
        {
            lua_pushinteger(L, info.texture_formats[i]);
            lua_seti(L, -2, i + 1);
        }
        lua_setfield(L, -2, "texture_formats");
        lua_pushinteger(L, info.max_texture_width);
        lua_setfield(L, -2, "max_texture_width");
        lua_pushinteger(L, info.max_texture_height);
        lua_setfield(L, -2, "max_texture_height");
        return 1;
    }
    return 0;
}

static int LUA_GetRendererOutputSize(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    int w, h;
    if (SDL_GetRendererOutputSize(rd, &w, &h) == 0)
    {
        lua_pushinteger(L, w);
        lua_pushinteger(L, h);
        return 2;
    }
    return 0;
}

static int LUA_CreateTexture(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    int w = (int)luaL_checkinteger(L, 2);
    int h = (int)luaL_checkinteger(L, 3);
    int format = (int)luaL_optinteger(L, 5, SDL_PIXELFORMAT_ARGB8888);
    int access = (int)luaL_optinteger(L, 4, SDL_TEXTUREACCESS_TARGET);
    SDL_Texture* tex = SDL_CreateTexture(rd, format, access, w, h);
    if (tex)
    {
        GGE_Texture* gt = (GGE_Texture*)SDL_calloc(1, sizeof(GGE_Texture));
        SDL_Texture** ud = (SDL_Texture**)lua_newuserdata(L, sizeof(SDL_Texture*));
        *ud = tex;
        gt->refcount = 1;
        SDL_SetTextureUserData(tex, gt);
        luaL_setmetatable(L, "SDL_Texture");
        return 1;
    }
    return 0;
}

static int LUA_CreateTextureFromSurface(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 2, "SDL_Surface");
    int access = (int)luaL_optinteger(L, 3, SDL_TEXTUREACCESS_STATIC);

    if (access == SDL_TEXTUREACCESS_STATIC)
    {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(rd, sf);
        if (tex)
        {
            GGE_Texture* gt = (GGE_Texture*)SDL_calloc(1, sizeof(GGE_Texture));
            SDL_Texture** ud = (SDL_Texture**)lua_newuserdata(L, sizeof(SDL_Texture*));
            *ud = tex;
            gt->refcount = 1;
            gt->sf = GGE_SurfaceAlphaToSurface(sf, 0);
            SDL_SetTextureUserData(tex, gt);
            luaL_setmetatable(L, "SDL_Texture");
            return 1;
        }
    }
    else if (access == SDL_TEXTUREACCESS_STREAMING)
    {
        SDL_Texture* tex = SDL_CreateTexture(rd, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, sf->w, sf->h);

        if (tex)
        {
            void* pixels = NULL;
            int pitch;
            if (SDL_LockTexture(tex, NULL, &pixels, &pitch) == 0)
            {
                SDL_ConvertPixels(sf->w, sf->h, sf->format->format, sf->pixels, sf->pitch,
                    SDL_PIXELFORMAT_ARGB8888, pixels, pitch);
                SDL_UnlockTexture(tex);
            }
            GGE_Texture* gt = (GGE_Texture*)SDL_calloc(1, sizeof(GGE_Texture));
            SDL_Texture** ud = (SDL_Texture**)lua_newuserdata(L, sizeof(SDL_Texture*));
            *ud = tex;
            gt->refcount = 1;
            sf->refcount++;
            gt->sf = sf;
            SDL_SetTextureUserData(tex, gt);

            Uint8 r, g, b, a;
            SDL_GetSurfaceColorMod(sf, &r, &g, &b);
            SDL_SetTextureColorMod(tex, r, g, b);

            SDL_GetSurfaceAlphaMod(sf, &a);
            SDL_SetTextureAlphaMod(tex, a);

            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            luaL_setmetatable(L, "SDL_Texture");
            return 1;
        }
    }
    return 0;
}

static int LUA_QueryTexture(lua_State* L)
{
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 1, "SDL_Texture");
    Uint32 format;
    int access, w, h;

    if (SDL_QueryTexture(tex, &format, &access, &w, &h) == 0)
    {
        lua_pushinteger(L, format);
        lua_pushinteger(L, access);
        lua_pushinteger(L, w);
        lua_pushinteger(L, h);
        return 4;
    }
    return 0;
}

static int LUA_SetTextureColorMod(lua_State* L)
{
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 1, "SDL_Texture");
    Uint8 r = (Uint8)luaL_optinteger(L, 2, 255);
    Uint8 g = (Uint8)luaL_optinteger(L, 3, 255);
    Uint8 b = (Uint8)luaL_optinteger(L, 4, 255);

    lua_pushboolean(L, SDL_SetTextureColorMod(tex, r, g, b) == 0);
    return 1;
}

static int LUA_GetTextureColorMod(lua_State* L)
{
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 1, "SDL_Texture");
    Uint8 r, g, b;
    if (SDL_GetTextureColorMod(tex, &r, &g, &b) == 0)
    {
        lua_pushinteger(L, r);
        lua_pushinteger(L, g);
        lua_pushinteger(L, b);
        return 3;
    }
    return 0;
}

static int LUA_SetTextureAlphaMod(lua_State* L)
{
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 1, "SDL_Texture");
    Uint8 alpha = (int)luaL_optinteger(L, 2, 255);

    lua_pushboolean(L, SDL_SetTextureAlphaMod(tex, alpha) == 0);
    return 1;
}

static int LUA_GetTextureAlphaMod(lua_State* L)
{
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 1, "SDL_Texture");
    Uint8 alpha = 0xFF;
    SDL_GetTextureAlphaMod(tex, &alpha);
    lua_pushinteger(L, alpha);
    return 1;
}

static int LUA_SetTextureBlendMode(lua_State* L)
{
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 1, "SDL_Texture");
    SDL_BlendMode mode = (SDL_BlendMode)luaL_optinteger(L, 2, SDL_BLENDMODE_BLEND);

    lua_pushboolean(L, SDL_SetTextureBlendMode(tex, mode) == 0);
    return 1;
}

static int LUA_GetTextureBlendMode(lua_State* L)
{
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 1, "SDL_Texture");
    SDL_BlendMode mode;

    if (SDL_GetTextureBlendMode(tex, &mode) == 0)
    {
        lua_pushinteger(L, mode);
        return 1;
    }
    return 0;
}

static int LUA_SetTextureScaleMode(lua_State* L)
{
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 1, "SDL_Texture");
    SDL_ScaleMode mode = (SDL_ScaleMode)luaL_optinteger(L, 2, SDL_ScaleModeNearest);

    lua_pushboolean(L, SDL_SetTextureScaleMode(tex, mode) == 0);
    return 1;
}

static int LUA_GetTextureScaleMode(lua_State* L)
{
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 1, "SDL_Texture");
    SDL_ScaleMode mode;

    if (SDL_GetTextureScaleMode(tex, &mode) == 0)
    {
        lua_pushinteger(L, mode);
        return 1;
    }
    return 0;
}

static int LUA_UpdateTexture(lua_State* L)
{
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 1, "SDL_Texture");
    SDL_Rect* rect = rect = (SDL_Rect*)luaL_testudata(L, 2, "SDL_Rect");
    const void* pixels = lua_topointer(L, 3);
    int pitch = (int)luaL_checkinteger(L, 4);

    lua_pushboolean(L, SDL_UpdateTexture(tex, rect, pixels, pitch) == 0);
    return 1;
}

static int LUA_UpdateYUVTexture(lua_State* L)
{
    //SDL_UpdateYUVTexture()
    return 1;
}

static int LUA_LockTexture(lua_State* L)
{
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 1, "SDL_Texture");
    SDL_Rect* rect = rect = (SDL_Rect*)luaL_testudata(L, 2, "SDL_Rect");

    void* pixels;
    int pitch;
    if (SDL_LockTexture(tex, rect, &pixels, &pitch) == 0)
    {
        lua_pushlightuserdata(L, pixels);
        lua_pushinteger(L, pitch);
        return 2;
    }
    return 0;
}

static int LUA_LockTextureToSurface(lua_State* L)
{
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 1, "SDL_Texture");
    SDL_Rect* rect = rect = (SDL_Rect*)luaL_testudata(L, 2, "SDL_Rect");
    SDL_Surface* sf;
    if (SDL_LockTextureToSurface(tex, rect, &sf) == 0)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_UnlockTexture(lua_State* L)
{
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 1, "SDL_Texture");

    SDL_UnlockTexture(tex);

    return 0;
}

static int LUA_GetTexturePointer(lua_State* L)
{
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 1, "SDL_Texture");

    lua_pushlightuserdata(L, tex);
    return 1;
}

static int LUA_RenderTargetSupported(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    lua_pushboolean(L, SDL_RenderTargetSupported(rd));
    return 1;
}

static int LUA_SetRenderTarget(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_Texture** tex = (SDL_Texture**)luaL_testudata(L, 2, "SDL_Texture");

    lua_pushboolean(L, SDL_SetRenderTarget(rd, tex ? *tex : NULL) == 0);
    return 1;
}

static int LUA_GetRenderTarget(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_Texture* tex = SDL_GetRenderTarget(rd);
    if (tex)
    {
        GGE_Texture* gt = (GGE_Texture*)SDL_GetTextureUserData(tex);
        SDL_Texture** ud = (SDL_Texture**)lua_newuserdata(L, sizeof(GGE_Texture));
        *ud = tex;
        if (gt)
            gt->refcount++;
        luaL_setmetatable(L, "SDL_Texture");
        return 1;
    }
    return 0;
}

static int LUA_RenderSetLogicalSize(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    int w = (int)lua_tointeger(L, 2);
    int h = (int)lua_tointeger(L, 3);

    lua_pushboolean(L, SDL_RenderSetLogicalSize(rd, w, h) == 0);
    return 1;
}

static int LUA_RenderGetLogicalSize(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    int w, h;

    SDL_RenderGetLogicalSize(rd, &w, &h);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 2;
}

static int LUA_RenderSetIntegerScale(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    int enabled = lua_toboolean(L, 2);

    lua_pushboolean(L, SDL_RenderSetIntegerScale(rd, enabled) == 0);
    return 1;
}

static int LUA_RenderGetIntegerScale(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    lua_pushboolean(L, SDL_RenderGetIntegerScale(rd));
    return 1;
}

static int LUA_RenderSetViewport(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_Rect* rect = (SDL_Rect*)luaL_testudata(L, 2, "SDL_Rect");

    lua_pushboolean(L, SDL_RenderSetViewport(rd, rect) == 0);
    return 1;
}

static int LUA_RenderGetViewport(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_Rect* rect = (SDL_Rect*)lua_newuserdata(L, sizeof(SDL_Rect));

    SDL_RenderGetViewport(rd, rect);
    luaL_setmetatable(L, "SDL_Rect");
    return 1;
}

static int LUA_RenderSetClipRect(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_Rect* rect = (SDL_Rect*)luaL_testudata(L, 2, "SDL_Rect");

    lua_pushboolean(L, SDL_RenderSetClipRect(rd, rect) == 0);
    return 1;
}

static int LUA_RenderGetClipRect(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_Rect* rect = (SDL_Rect*)lua_newuserdata(L, sizeof(SDL_Rect));

    SDL_RenderGetClipRect(rd, rect);

    luaL_setmetatable(L, "SDL_Rect");
    return 1;
}

static int LUA_RenderIsClipEnabled(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");

    lua_pushboolean(L, SDL_RenderIsClipEnabled(rd));
    return 1;
}

static int LUA_RenderSetScale(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    lua_pushboolean(L, SDL_RenderSetScale(rd, x, y) == 0);
    return 1;
}

static int LUA_RenderGetScale(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    float x, y;
    SDL_RenderGetScale(rd, &x, &y);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    return 2;
}

static int LUA_RenderWindowToLogical(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    int wx = (int)luaL_checkinteger(L, 2);
    int wy = (int)luaL_checkinteger(L, 3);
    float lx, ly;
    SDL_RenderWindowToLogical(rd, wx, wy, &lx, &ly);
    lua_pushnumber(L, lx);
    lua_pushnumber(L, ly);
    return 2;
}

static int LUA_RenderLogicalToWindow(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    float lx = (float)luaL_checkinteger(L, 2);
    float ly = (float)luaL_checkinteger(L, 3);
    int wx, wy;
    SDL_RenderLogicalToWindow(rd, lx, ly, &wx, &wy);
    lua_pushnumber(L, wx);
    lua_pushnumber(L, wy);
    return 2;
}

static int LUA_SetRenderDrawColor(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    int r = (int)luaL_optinteger(L, 2, 0);
    int g = (int)luaL_optinteger(L, 3, 0);
    int b = (int)luaL_optinteger(L, 4, 0);
    int a = (int)luaL_optinteger(L, 5, 0);

    lua_pushboolean(L, SDL_SetRenderDrawColor(rd, r, g, b, a) == 0);
    return 1;
}

static int LUA_GetRenderDrawColor(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    Uint8 r, g, b, a;

    if (SDL_GetRenderDrawColor(rd, &r, &g, &b, &a) == 0)
    {
        lua_pushinteger(L, r);
        lua_pushinteger(L, g);
        lua_pushinteger(L, b);
        lua_pushinteger(L, a);
        return 4;
    }
    return 0;
}

static int LUA_SetRenderDrawBlendMode(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_BlendMode mode = (SDL_BlendMode)luaL_checkinteger(L, 2);

    lua_pushboolean(L, SDL_SetRenderDrawBlendMode(rd, mode) == 0);
    return 1;
}

static int LUA_GetRenderDrawBlendMode(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_BlendMode mode;

    SDL_GetRenderDrawBlendMode(rd, &mode);
    lua_pushinteger(L, mode);
    return 1;
}

static int LUA_RenderClear(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");

    lua_pushboolean(L, SDL_RenderClear(rd) == 0);
    return 1;
}

static int LUA_RenderDrawPoint(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);

    lua_pushboolean(L, SDL_RenderDrawPoint(rd, x, y) == 0);
    return 1;
}
//SDL_RenderDrawPoints

static int LUA_RenderDrawLine(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    int x1 = (int)luaL_checkinteger(L, 2);
    int y1 = (int)luaL_checkinteger(L, 3);
    int x2 = (int)luaL_checkinteger(L, 4);
    int y2 = (int)luaL_checkinteger(L, 5);

    lua_pushboolean(L, SDL_RenderDrawLine(rd, x1, y1, x2, y2) == 0);
    return 1;
}
//SDL_RenderDrawLines

static int LUA_RenderDrawRect(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_Rect* rect = (SDL_Rect*)luaL_testudata(L, 2, "SDL_Rect");

    lua_pushboolean(L, SDL_RenderDrawRect(rd, rect) == 0);
    return 1;
}
//SDL_RenderDrawRects

static int LUA_RenderFillRect(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_Rect* rect = (SDL_Rect*)luaL_testudata(L, 2, "SDL_Rect");

    lua_pushboolean(L, SDL_RenderFillRect(rd, rect) == 0);
    return 1;
}
//SDL_RenderFillRects

static int LUA_RenderCopy(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 2, "SDL_Texture");
    SDL_Rect* srcrect = (SDL_Rect*)luaL_testudata(L, 3, "SDL_Rect");
    SDL_Rect* dstrect = (SDL_Rect*)luaL_testudata(L, 4, "SDL_Rect");

    lua_pushboolean(L, SDL_RenderCopy(rd, tex, srcrect, dstrect) == 0);
    return 1;
}

static int LUA_RenderCopyEx(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 2, "SDL_Texture");
    SDL_Rect* srcrect = (SDL_Rect*)luaL_testudata(L, 3, "SDL_Rect");
    SDL_Rect* dstrect = (SDL_Rect*)luaL_testudata(L, 4, "SDL_Rect");
    double angle = luaL_optnumber(L, 5, 0);
    SDL_RendererFlip flip = (SDL_RendererFlip)luaL_optinteger(L, 6, SDL_FLIP_NONE);
    SDL_Point center;
    if (!lua_isnoneornil(L, 7))
    {
        center.x = (int)luaL_optinteger(L, 7, 0);
        center.y = (int)luaL_optinteger(L, 8, 0);
        lua_pushboolean(L, SDL_RenderCopyEx(rd, tex, srcrect, dstrect, angle, &center, flip) == 0);
    }
    else
        lua_pushboolean(L, SDL_RenderCopyEx(rd, tex, srcrect, dstrect, angle, NULL, flip) == 0);

    return 1;
}

static int LUA_RenderDrawPointF(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);

    lua_pushboolean(L, SDL_RenderDrawPointF(rd, x, y) == 0);
    return 1;
}
//SDL_RenderDrawPointsF
static int LUA_RenderDrawLineF(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    float x1 = (float)luaL_checknumber(L, 2);
    float y1 = (float)luaL_checknumber(L, 3);
    float x2 = (float)luaL_checknumber(L, 4);
    float y2 = (float)luaL_checknumber(L, 5);

    lua_pushboolean(L, SDL_RenderDrawLineF(rd, x1, y1, x2, y2) == 0);
    return 1;
}
//SDL_RenderDrawLinesF

static int LUA_RenderDrawRectF(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_FRect* rect = (SDL_FRect*)luaL_testudata(L, 2, "SDL_Rect");

    lua_pushboolean(L, SDL_RenderDrawRectF(rd, rect) == 0);
    return 1;
}
//SDL_RenderDrawRectsF
static int LUA_RenderFillRectF(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_FRect* rect = (SDL_FRect*)luaL_testudata(L, 2, "SDL_Rect");

    lua_pushboolean(L, SDL_RenderFillRectF(rd, rect) == 0);
    return 1;
}
//SDL_RenderFillRectsF
static int LUA_RenderCopyF(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 2, "SDL_Texture");
    SDL_Rect* srcrect = (SDL_Rect*)luaL_testudata(L, 3, "SDL_Rect");
    SDL_FRect* dstrect = (SDL_FRect*)luaL_testudata(L, 4, "SDL_Rect");

    lua_pushboolean(L, SDL_RenderCopyF(rd, tex, srcrect, dstrect) == 0);
    return 1;
}

static int LUA_RenderCopyExF(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 2, "SDL_Texture");
    SDL_Rect* srcrect = (SDL_Rect*)luaL_testudata(L, 3, "SDL_Rect");
    SDL_FRect* dstrect = (SDL_FRect*)luaL_testudata(L, 4, "SDL_Rect");
    double angle = luaL_optnumber(L, 5, 0);
    SDL_RendererFlip flip = (SDL_RendererFlip)luaL_optinteger(L, 6, SDL_FLIP_NONE);
    SDL_FPoint center;
    if (!lua_isnoneornil(L, 7))
    {
        center.x = (float)luaL_optnumber(L, 7, 0);
        center.y = (float)luaL_optnumber(L, 8, 0);
        lua_pushboolean(L, SDL_RenderCopyExF(rd, tex, srcrect, dstrect, angle, &center, flip) == 0);
    }
    else
        lua_pushboolean(L, SDL_RenderCopyExF(rd, tex, srcrect, dstrect, angle, NULL, flip) == 0);

    return 1;
}

static int LUA_RenderReadPixels(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_Rect* rect = (SDL_Rect*)luaL_testudata(L, 2, "SDL_Rect");
    Uint32 format = (int)luaL_optinteger(L, 3, 0);
    void* pixels = lua_touserdata(L, 4);
    int pitch = (int)luaL_checkinteger(L, 5);

    lua_pushboolean(L, SDL_RenderReadPixels(rd, rect, format, pixels, pitch) == 0);
    return 1;
}

static int LUA_RenderPresent(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");

    SDL_RenderPresent(rd);
    return 0;
}

static int LUA_DestroyTexture(lua_State* L)
{
    SDL_Texture** tex = (SDL_Texture**)luaL_checkudata(L, 1, "SDL_Texture");

    if (*tex && SDL_GetTextureUserData(*tex))
    {
        GGE_Texture* gt = (GGE_Texture*)SDL_GetTextureUserData(*tex);
        gt->refcount--;
        if (gt->refcount == 0)
        {
            if (gt->sf)
                SDL_FreeSurface(gt->sf);

            SDL_DestroyTexture(*tex);
            SDL_free(gt);
            *tex = NULL;
        }
    }
    return 0;
}

static int LUA_DestroyRenderer(lua_State* L)
{
    SDL_Renderer** rd = (SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    if (*rd)
    {
        SDL_DestroyRenderer(*rd);
        *rd = NULL;
    }
    return 0;
}

static int LUA_RenderFlush(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    lua_pushboolean(L, SDL_RenderFlush(rd) == 0);
    return 1;
}
//TODO
//SDL_GL_BindTexture
//SDL_GL_UnbindTexture
//SDL_RenderGetMetalLayer
//SDL_RenderGetMetalCommandEncoder

static const luaL_Reg renderer_funcs[] = {
    {"__index", NULL},
    {"DestroyRenderer", LUA_DestroyRenderer},

    {"GetRendererInfo", LUA_GetRendererInfo},
    {"GetRendererOutputSize", LUA_GetRendererOutputSize},
    {"CreateTexture", LUA_CreateTexture},
    {"CreateTextureFromSurface", LUA_CreateTextureFromSurface},
    {"RenderTargetSupported", LUA_RenderTargetSupported},
    {"SetRenderTarget", LUA_SetRenderTarget},
    {"GetRenderTarget", LUA_GetRenderTarget},
    {"RenderSetLogicalSize", LUA_RenderSetLogicalSize},
    {"RenderGetLogicalSize", LUA_RenderGetLogicalSize},
    {"RenderSetIntegerScale", LUA_RenderSetIntegerScale},
    {"RenderGetIntegerScale", LUA_RenderGetIntegerScale},
    {"RenderSetViewport", LUA_RenderSetViewport},
    {"RenderGetViewport", LUA_RenderGetViewport},
    {"RenderSetClipRect", LUA_RenderSetClipRect},
    {"RenderGetClipRect", LUA_RenderGetClipRect},
    {"RenderIsClipEnabled", LUA_RenderIsClipEnabled},
    {"RenderSetScale", LUA_RenderSetScale},
    {"RenderGetScale", LUA_RenderGetScale},
    {"RenderWindowToLogical", LUA_RenderWindowToLogical},
    {"RenderLogicalToWindow", LUA_RenderLogicalToWindow},
    {"SetRenderDrawColor", LUA_SetRenderDrawColor},
    {"GetRenderDrawColor", LUA_GetRenderDrawColor},
    {"SetRenderDrawBlendMode", LUA_SetRenderDrawBlendMode},
    {"GetRenderDrawBlendMode", LUA_GetRenderDrawBlendMode},

    {"RenderClear", LUA_RenderClear},
    {"RenderDrawPoint", LUA_RenderDrawPoint},
    {"RenderDrawLine", LUA_RenderDrawLine},
    {"RenderDrawRect", LUA_RenderDrawRect},
    {"RenderFillRect", LUA_RenderFillRect},

    {"RenderCopy", LUA_RenderCopy},
    {"RenderCopyEx", LUA_RenderCopyEx},

    {"RenderReadPixels", LUA_RenderReadPixels},
    {"RenderPresent", LUA_RenderPresent},
    {"RenderFlush", LUA_RenderFlush},
    //RenderGetMetalLayer
    //RenderGetMetalCommandEncoder
    {NULL, NULL} };

static const luaL_Reg sdl_funcs[] = {
    {"GetNumRenderDrivers", LUA_GetNumRenderDrivers},
    {"GetRenderDriverInfo", LUA_GetRenderDriverInfo},
    //{"GL_BindTexture", LUA_GL_BindTexture},
    //{"GL_UnbindTexture", LUA_GL_UnbindTexture},

    {NULL, NULL} };

static const luaL_Reg texture_funcs[] = {
    {"__gc", LUA_DestroyTexture},
    {"__index", NULL},
    {"QueryTexture", LUA_QueryTexture},
    {"SetTextureColorMod", LUA_SetTextureColorMod},
    {"GetTextureColorMod", LUA_GetTextureColorMod},
    {"SetTextureAlphaMod", LUA_SetTextureAlphaMod},
    {"GetTextureAlphaMod", LUA_GetTextureAlphaMod},
    {"SetTextureBlendMode", LUA_SetTextureBlendMode},
    {"GetTextureBlendMode", LUA_GetTextureBlendMode},
    {"SetTextureScaleMode", LUA_SetTextureScaleMode},
    {"GetTextureScaleMode", LUA_GetTextureScaleMode},

    {"UpdateTexture", LUA_UpdateTexture},
    //int LUA_UpdateYUVTexture

    {"LockTexture", LUA_LockTexture},
    {"LockTextureToSurface", LUA_LockTextureToSurface},
    {"UnlockTexture", LUA_UnlockTexture},

    {"GetTexturePointer", LUA_GetTexturePointer},
    {NULL, NULL} };

static const luaL_Reg window_funcs[] = {
    {"CreateRenderer", LUA_CreateRenderer},
    {"GetRenderer", LUA_GetRenderer},
    {NULL, NULL} };

static const luaL_Reg surface_funcs[] = {
    {"CreateSoftwareRenderer", LUA_CreateSoftwareRenderer},
    {NULL, NULL} };

int bind_renderer(lua_State* L)
{
    luaL_getmetatable(L, "SDL_Texture");
    luaL_setfuncs(L, texture_funcs, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    luaL_getmetatable(L, "SDL_Renderer");
    luaL_setfuncs(L, renderer_funcs, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    luaL_getmetatable(L, "SDL_Window");
    luaL_setfuncs(L, window_funcs, 0);
    lua_pop(L, 1);

    luaL_getmetatable(L, "SDL_Surface");
    luaL_setfuncs(L, surface_funcs, 0);
    lua_pop(L, 1);

    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}
