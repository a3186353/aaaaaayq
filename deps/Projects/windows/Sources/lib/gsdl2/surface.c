#include "gge.h"
#include "SDL_surface.h"

static int LUA_CreateRGBSurface(lua_State* L)
{
    int width = (int)luaL_checkinteger(L, 1);
    int height = (int)luaL_checkinteger(L, 2);
    int depth = (int)luaL_checkinteger(L, 3);
    Uint32 rmask = (int)luaL_checkinteger(L, 4);
    Uint32 gmask = (int)luaL_checkinteger(L, 5);
    Uint32 bmask = (int)luaL_checkinteger(L, 6);
    Uint32 amask = (int)luaL_checkinteger(L, 7);
    SDL_Surface* sf = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, depth, rmask, gmask, bmask, amask);
    if (sf)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_CreateRGBSurfaceWithFormat(lua_State* L)
{
    int width = (int)luaL_checkinteger(L, 1);
    int height = (int)luaL_checkinteger(L, 2);
    int depth = (int)luaL_optinteger(L, 3, 32);
    int format = (int)luaL_optinteger(L, 4, SDL_PIXELFORMAT_ARGB8888);
    SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormat(SDL_SWSURFACE, width, height, depth, format);
    if (sf)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_CreateRGBSurfaceFrom(lua_State* L)
{
    void* pixels = lua_touserdata(L, 1);
    int width = (int)luaL_checkinteger(L, 2);
    int height = (int)luaL_checkinteger(L, 3);
    int depth = (int)luaL_checkinteger(L, 4);
    int pitch = (int)luaL_checkinteger(L, 5);
    int rmask = (int)luaL_checkinteger(L, 6);
    int gmask = (int)luaL_checkinteger(L, 7);
    int bmask = (int)luaL_checkinteger(L, 8);
    int amask = (int)luaL_checkinteger(L, 9);

    SDL_Surface* sf = SDL_CreateRGBSurfaceFrom(pixels, width, height, depth, pitch, rmask, gmask, bmask, amask);

    if (sf)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_CreateRGBSurfaceWithFormatFrom(lua_State* L)
{
    void* pixels = lua_touserdata(L, 1);
    int width = (int)luaL_checkinteger(L, 2);
    int height = (int)luaL_checkinteger(L, 3);
    int depth = (int)luaL_checkinteger(L, 4);
    int pitch = (int)luaL_checkinteger(L, 5);
    Uint32 format = (int)luaL_checkinteger(L, 6);

    SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormatFrom(pixels, width, height, depth, pitch, format);

    if (sf)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_FreeSurface(lua_State* L)
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

static int LUA_SetSurfacePalette(lua_State* L)
{
    //SDL_Surface * sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    //SDL_Palette palette;//?

    //if (SDL_SetSurfacePalette(sf, &palette)== 0){
    //  lua_pushboolean(L,1);
    //  return 1;
    //}

    return 0;
}

static int LUA_LockSurface(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    lua_pushlightuserdata(L, sf->pixels);
    lua_pushinteger(L, sf->pitch);
    return 2;
}

static int LUA_UnlockSurface(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");

    if (SDL_MUSTLOCK(sf))
        SDL_UnlockSurface(sf);
    return 0;
}

static int LUA_LoadBMP_RW(lua_State* L)
{
    SDL_RWops* ops = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    SDL_Surface* sf = SDL_LoadBMP_RW(ops, 0);
    if (sf)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_LoadBMP(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    SDL_Surface* sf = SDL_LoadBMP(path);
    if (sf)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_SaveBMP_RW(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    SDL_RWops* ops = *(SDL_RWops**)luaL_checkudata(L, 2, "SDL_RWops");

    lua_pushboolean(L, SDL_SaveBMP_RW(sf, ops, 0) == 0);
    return 1;
}

static int LUA_SaveBMP(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    const char* path = luaL_checkstring(L, 2);

    lua_pushboolean(L, SDL_SaveBMP(sf, path) == 0);
    return 1;
}

static int LUA_SetSurfaceRLE(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    int flag = lua_toboolean(L, 2);

    lua_pushboolean(L, SDL_SetSurfaceRLE(sf, flag) == 0);
    return 1;
}

static int LUA_HasSurfaceRLE(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    lua_pushboolean(L, SDL_HasSurfaceRLE(sf));
    return 1;
}

static int LUA_SetColorKey(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    Uint8 r = (Uint8)luaL_optinteger(L, 2, 255);
    Uint8 g = (Uint8)luaL_optinteger(L, 3, 255);
    Uint8 b = (Uint8)luaL_optinteger(L, 4, 255);
    int flag = (int)luaL_optinteger(L, 5, SDL_TRUE);

    lua_pushboolean(L, SDL_SetColorKey(sf, flag, SDL_MapRGB(sf->format, r, g, b)) == 0);
    return 1;
}

static int LUA_HasColorKey(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");

    lua_pushboolean(L, SDL_HasColorKey(sf));
    return 1;
}

static int LUA_GetColorKey(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    Uint32 value;

    if (SDL_GetColorKey(sf, &value) == 0)
    {
        Uint8 r, g, b;
        SDL_GetRGB(value, sf->format, &r, &g, &b);
        lua_pushinteger(L, r);
        lua_pushinteger(L, g);
        lua_pushinteger(L, b);
        return 3;
    }
    return 0;
}

static int LUA_SetSurfaceColorMod(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    Uint8 r = (Uint8)luaL_optinteger(L, 2, 255);
    Uint8 g = (Uint8)luaL_optinteger(L, 3, 255);
    Uint8 b = (Uint8)luaL_optinteger(L, 4, 255);

    lua_pushboolean(L, SDL_SetSurfaceColorMod(sf, r, g, b) == 0);
    return 1;
}

static int LUA_GetSurfaceColorMod(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    Uint8 r, g, b;
    if (SDL_GetSurfaceColorMod(sf, &r, &g, &b) == 0)
    {
        lua_pushinteger(L, r);
        lua_pushinteger(L, g);
        lua_pushinteger(L, b);
        return 3;
    }
    return 0;
}

static int LUA_SetSurfaceAlphaMod(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    Uint8 alpha = (Uint8)luaL_optinteger(L, 2, 255);

    lua_pushboolean(L, SDL_SetSurfaceAlphaMod(sf, alpha) == 0);
    return 1;
}

static int LUA_GetSurfaceAlphaMod(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    Uint8 value;

    if (SDL_GetSurfaceAlphaMod(sf, &value) == 0)
    {
        lua_pushinteger(L, value);
        return 1;
    }
    return 0;
}

static int LUA_SetSurfaceBlendMode(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    SDL_BlendMode mode = (SDL_BlendMode)luaL_checkinteger(L, 2);

    lua_pushboolean(L, SDL_SetSurfaceBlendMode(sf, mode) == 0);
    return 1;
}

static int LUA_GetSurfaceBlendMode(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    SDL_BlendMode value;

    if (SDL_GetSurfaceBlendMode(sf, &value) == 0)
    {
        lua_pushinteger(L, value);
        return 1;
    }
    return 0;
}

static int LUA_SetClipRect(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    SDL_Rect* rect = (SDL_Rect*)luaL_testudata(L, 2, "SDL_Rect");

    lua_pushboolean(L, SDL_SetClipRect(sf, rect) == SDL_TRUE);
    return 1;
}

static int LUA_GetClipRect(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");

    SDL_Rect* rect = (SDL_Rect*)lua_newuserdata(L, sizeof(SDL_Rect));
    SDL_GetClipRect(sf, rect);
    luaL_setmetatable(L, "SDL_Rect");
    return 1;
}

static int LUA_DuplicateSurface(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    SDL_Surface* rsf = SDL_DuplicateSurface(sf);
    if (sf)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = rsf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_ConvertSurface(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    SDL_PixelFormat* fmt = *(SDL_PixelFormat**)luaL_checkudata(L, 2, "SDL_PixelFormat");

    Uint32 flags = (int)luaL_optinteger(L, 3, SDL_SWSURFACE);
    SDL_Surface* ret = SDL_ConvertSurface(sf, fmt, flags);
    if (ret)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = ret;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_ConvertSurfaceFormat(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    Uint32 pixel_format = (int)luaL_checkinteger(L, 2);
    Uint32 flags = (int)luaL_optinteger(L, 3, SDL_SWSURFACE);
    SDL_Surface* ret = SDL_ConvertSurfaceFormat(sf, pixel_format, flags);
    if (ret)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = ret;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_ConvertPixels(lua_State* L)
{
    int width = (int)luaL_checkinteger(L, 1);
    int height = (int)luaL_checkinteger(L, 2);

    Uint32 src_format = (int)luaL_checkinteger(L, 3);
    const void* src = lua_topointer(L, 4);
    Uint32 src_pitch = (int)luaL_checkinteger(L, 5);

    Uint32 dst_format = (int)luaL_checkinteger(L, 6);
    void* dst = (void*)lua_topointer(L, 7);
    Uint32 dst_pitch = (int)luaL_checkinteger(L, 8);
    lua_pushboolean(L, SDL_ConvertPixels(width, height,
        src_format, src, src_pitch,
        dst_format, dst, dst_pitch) == 0);
    return 1;
}

static int LUA_FillRect(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    int r = (int)luaL_optinteger(L, 2, 255);
    int g = (int)luaL_optinteger(L, 3, 255);
    int b = (int)luaL_optinteger(L, 4, 255);
    int a = (int)luaL_optinteger(L, 5, 255);
    lua_pushboolean(L, SDL_FillRect(sf, NULL, SDL_MapRGBA(sf->format, r, g, b, a)) == 0);
    return 1;
}
//SDL_FillRects
static int LUA_UpperBlit(lua_State* L)
{
    SDL_Surface* src = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    SDL_Rect* srcrect = (SDL_Rect*)luaL_testudata(L, 2, "SDL_Rect");
    SDL_Surface* dst = *(SDL_Surface**)luaL_checkudata(L, 3, "SDL_Surface");
    SDL_Rect* dstrect = (SDL_Rect*)luaL_testudata(L, 4, "SDL_Rect");

    lua_pushboolean(L, SDL_UpperBlit(src, srcrect, dst, dstrect) == 0);
    return 1;
}

static int LUA_LowerBlit(lua_State* L)
{
    SDL_Surface* src = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    SDL_Rect* srcrect = (SDL_Rect*)luaL_testudata(L, 2, "SDL_Rect");
    SDL_Surface* dst = *(SDL_Surface**)luaL_checkudata(L, 3, "SDL_Surface");
    SDL_Rect* dstrect = (SDL_Rect*)luaL_testudata(L, 4, "SDL_Rect");

    lua_pushboolean(L, SDL_LowerBlit(src, srcrect, dst, dstrect) == 0);
    return 1;
}

static int LUA_SoftStretch(lua_State* L)
{
    SDL_Surface* src = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    SDL_Rect* srcrect = (SDL_Rect*)luaL_testudata(L, 2, "SDL_Rect");
    SDL_Surface* dst = *(SDL_Surface**)luaL_checkudata(L, 3, "SDL_Surface");
    SDL_Rect* dstrect = (SDL_Rect*)luaL_testudata(L, 4, "SDL_Rect");

    lua_pushboolean(L, SDL_SoftStretch(src, srcrect, dst, dstrect) == 0);
    return 1;
}

static int LUA_SoftStretchLinear(lua_State* L)
{
    SDL_Surface* src = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    SDL_Rect* srcrect = (SDL_Rect*)luaL_testudata(L, 2, "SDL_Rect");
    SDL_Surface* dst = *(SDL_Surface**)luaL_checkudata(L, 3, "SDL_Surface");
    SDL_Rect* dstrect = (SDL_Rect*)luaL_testudata(L, 4, "SDL_Rect");

    lua_pushboolean(L, SDL_SoftStretchLinear(src, srcrect, dst, dstrect) == 0);
    return 1;
}

static int LUA_UpperBlitScaled(lua_State* L)
{
    SDL_Surface* src = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    SDL_Rect* srcrect = (SDL_Rect*)luaL_testudata(L, 2, "SDL_Rect");
    SDL_Surface* dst = *(SDL_Surface**)luaL_checkudata(L, 3, "SDL_Surface");
    SDL_Rect* dstrect = (SDL_Rect*)luaL_testudata(L, 4, "SDL_Rect");

    lua_pushboolean(L, SDL_UpperBlitScaled(src, srcrect, dst, dstrect) == 0);
    return 1;
}

static int LUA_LowerBlitScaled(lua_State* L)
{
    SDL_Surface* src = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    SDL_Rect* srcrect = (SDL_Rect*)luaL_testudata(L, 2, "SDL_Rect");
    SDL_Surface* dst = *(SDL_Surface**)luaL_checkudata(L, 3, "SDL_Surface");
    SDL_Rect* dstrect = (SDL_Rect*)luaL_testudata(L, 4, "SDL_Rect");

    lua_pushboolean(L, SDL_LowerBlitScaled(src, srcrect, dst, dstrect) == 0);
    return 1;
}

static int LUA_SetYUVConversionMode(lua_State* L)
{
    SDL_YUV_CONVERSION_MODE mode = (SDL_YUV_CONVERSION_MODE)luaL_checkinteger(L, 1);
    SDL_SetYUVConversionMode(mode);
    return 0;
}

static int LUA_GetYUVConversionMode(lua_State* L)
{
    lua_pushinteger(L, SDL_GetYUVConversionMode());
    return 1;
}

static int LUA_GetYUVConversionModeForResolution(lua_State* L)
{
    int width = (int)luaL_checkinteger(L, 1);
    int height = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, SDL_GetYUVConversionModeForResolution(width, height));
    return 1;
}

//================================================================================
static int LUA_SetSurfaceRef(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    int ref = (int)luaL_checkinteger(L, 2);
    sf->refcount = ref;
    return 0;
}

static int LUA_SurfaceIndex(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    const char* key = lua_tostring(L, 2);
    if (SDL_strcmp(key, "flags") == 0) {
        lua_pushinteger(L, sf->flags);
    }
    else if (SDL_strcmp(key, "format") == 0) {
        SDL_PixelFormat** ud = (SDL_PixelFormat**)lua_newuserdata(L, sizeof(SDL_PixelFormat*));
        *ud = sf->format;
        luaL_setmetatable(L, "SDL_PixelFormat");
    }
    else if (SDL_strcmp(key, "formatname") == 0) {
        lua_pushstring(L, SDL_GetPixelFormatName(sf->format->format));
    }
    else if (SDL_strcmp(key, "w") == 0) {
        lua_pushinteger(L, sf->w);
    }
    else if (SDL_strcmp(key, "h") == 0) {
        lua_pushinteger(L, sf->h);
    }
    else if (SDL_strcmp(key, "pitch") == 0) {
        lua_pushinteger(L, sf->pitch);
    }
    else if (SDL_strcmp(key, "pixels") == 0) {
        lua_pushlightuserdata(L, sf->pixels);
    }
    else if (SDL_strcmp(key, "locked") == 0) {
        lua_pushboolean(L, sf->locked);
    }
    else if (SDL_strcmp(key, "refcount") == 0) {
        lua_pushinteger(L, sf->refcount);
    }
    else {
        luaL_getmetatable(L, "SDL_Surface");
        lua_getfield(L, -1, key);
        lua_remove(L, -2);
    }
    return 1;
}

static const luaL_Reg surface_funcs[] = {
    {"__gc", LUA_FreeSurface},
    {"__close", LUA_FreeSurface},
    {"__index", LUA_SurfaceIndex},
    //{"SetSurfacePalette", LUA_SetSurfacePalette},
    {"LockSurface", LUA_LockSurface},
    {"UnlockSurface", LUA_UnlockSurface},
    {"SaveBMP", LUA_SaveBMP},
    {"SaveBMP_RW", LUA_SaveBMP_RW},
    {"SetSurfaceRLE", LUA_SetSurfaceRLE},
    {"SetColorKey", LUA_SetColorKey},
    {"HasColorKey", LUA_HasColorKey},
    {"GetColorKey", LUA_GetColorKey},
    {"SetSurfaceColorMod", LUA_SetSurfaceColorMod},
    {"GetSurfaceColorMod", LUA_GetSurfaceColorMod},
    {"SetSurfaceAlphaMod", LUA_SetSurfaceAlphaMod},
    {"GetSurfaceAlphaMod", LUA_GetSurfaceAlphaMod},
    {"SetSurfaceBlendMode", LUA_SetSurfaceBlendMode},
    {"GetSurfaceBlendMode", LUA_GetSurfaceBlendMode},
    {"SetClipRect", LUA_SetClipRect},
    {"GetClipRect", LUA_GetClipRect},
    {"DuplicateSurface", LUA_DuplicateSurface},
    {"ConvertSurface", LUA_ConvertSurface},
    {"ConvertSurfaceFormat", LUA_ConvertSurfaceFormat},
    {"FillRect", LUA_FillRect},
    {"BlitSurface", LUA_UpperBlit},
    {"UpperBlit", LUA_UpperBlit},
    {"LowerBlit", LUA_LowerBlit},
    {"SoftStretch", LUA_SoftStretch},
    {"SoftStretchLinear", LUA_SoftStretchLinear},
    {"BlitScaled", LUA_UpperBlitScaled},
    {"UpperBlitScaled", LUA_UpperBlitScaled},
    {"LowerBlitScaled", LUA_LowerBlitScaled},
    {"SetYUVConversionMode", LUA_SetYUVConversionMode},
    {"GetYUVConversionMode", LUA_GetYUVConversionMode},
    {"GetYUVConversionModeForResolution", LUA_GetYUVConversionModeForResolution},

    {"SetSurfaceRef", LUA_SetSurfaceRef},
    {NULL, NULL} };

static const luaL_Reg sdl_funcs[] = {
    {"ConvertPixels", LUA_ConvertPixels},
    {"CreateRGBSurface", LUA_CreateRGBSurface},
    {"CreateRGBSurfaceWithFormat", LUA_CreateRGBSurfaceWithFormat},
    {"CreateRGBSurfaceFrom", LUA_CreateRGBSurfaceFrom},
    //LUA_CreateRGBSurfaceWithFormatFrom
    {"LoadBMP", LUA_LoadBMP},
    {"LoadBMP_RW", LUA_LoadBMP_RW},
    {NULL, NULL} };

int bind_surface(lua_State* L)
{
    luaL_getmetatable(L, "SDL_Surface");
    luaL_setfuncs(L, surface_funcs, 0);
    lua_pop(L, 1);

    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}