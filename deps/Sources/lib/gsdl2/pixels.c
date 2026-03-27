#include "gge.h"
#include "SDL_pixels.h"

static int LUA_GetPixelFormatName(lua_State* L)
{
    Uint32 format = (int)luaL_checkinteger(L, 1);

    lua_pushstring(L, SDL_GetPixelFormatName(format));
    return 1;
}

static int LUA_PixelFormatEnumToMasks(lua_State* L)
{
    Uint32 format = (int)luaL_checkinteger(L, 1);
    int bpp = 0;
    Uint32 r, g, b, a;
    SDL_PixelFormatEnumToMasks(format, &bpp, &r, &g, &b, &a);
    lua_pushinteger(L, bpp);
    lua_pushinteger(L, r);
    lua_pushinteger(L, g);
    lua_pushinteger(L, b);
    lua_pushinteger(L, a);
    return 5;
}

static int LUA_MasksToPixelFormatEnum(lua_State* L)
{
    Uint32 bpp = (int)luaL_checkinteger(L, 1);
    Uint8 r = (int)luaL_checkinteger(L, 2);
    Uint8 g = (int)luaL_checkinteger(L, 3);
    Uint8 b = (int)luaL_checkinteger(L, 4);
    Uint8 a = (int)luaL_checkinteger(L, 5);
    lua_pushinteger(L, SDL_MasksToPixelFormatEnum(bpp, r, g, b, a));
    return 1;
}

static int LUA_AllocFormat(lua_State* L)
{
    int format = (int)luaL_checkinteger(L, 1);
    SDL_PixelFormat* pf = SDL_AllocFormat(format);
    SDL_PixelFormat** ud = (SDL_PixelFormat**)lua_newuserdata(L, sizeof(SDL_PixelFormat*));
    *ud = pf;
    luaL_setmetatable(L, "SDL_PixelFormat");
    lua_pushinteger(L, 1);
    lua_setuservalue(L, -2);
    return 1;
}

static int LUA_FreeFormat(lua_State* L)
{
    SDL_PixelFormat** pf = (SDL_PixelFormat**)luaL_checkudata(L, 1, "SDL_PixelFormat");
    lua_getuservalue(L, 1);
    if (*pf && lua_tointeger(L, -1))
    {
        SDL_FreeFormat(*pf);
        *pf = NULL;
    }
    return 0;
}

static int LUA_AllocPalette(lua_State* L)
{
    int format = (int)luaL_checkinteger(L, 1);
    SDL_Palette* pal = SDL_AllocPalette(format);
    lua_pushlightuserdata(L, pal);
    return 1;
}

static int LUA_SetPixelFormatPalette(lua_State* L)
{
    //SDL_PixelFormat *pf = *(SDL_PixelFormat**)luaL_checkudata(L, 1, "SDL_PixelFormat");
    //lua_pushinteger(L,SDL_SetPixelFormatPalette(pf,));
    return 0;
}
//static int    LUA_SetPaletteColors(lua_State *L)
//{
//  //SDL_SetPaletteColors
//}
//

static int LUA_FreePalette(lua_State* L)
{
    //SDL_FreePalette();
    return 0;
}

static int LUA_MapRGB(lua_State* L)
{
    SDL_PixelFormat* pf = *(SDL_PixelFormat**)luaL_checkudata(L, 1, "SDL_PixelFormat");
    Uint8 r = (int)luaL_optinteger(L, 2, 255);
    Uint8 g = (int)luaL_optinteger(L, 3, 255);
    Uint8 b = (int)luaL_optinteger(L, 4, 255);

    lua_pushinteger(L, SDL_MapRGB(pf, r, g, b));
    return 1;
}

static int LUA_MapRGBA(lua_State* L)
{
    SDL_PixelFormat* pf = *(SDL_PixelFormat**)luaL_checkudata(L, 1, "SDL_PixelFormat");
    Uint8 r = (int)luaL_optinteger(L, 2, 255);
    Uint8 g = (int)luaL_optinteger(L, 3, 255);
    Uint8 b = (int)luaL_optinteger(L, 4, 255);
    Uint8 a = (int)luaL_optinteger(L, 5, 255);
    lua_pushinteger(L, SDL_MapRGBA(pf, r, g, b, a));
    return 1;
}

static int LUA_GetRGB(lua_State* L)
{
    SDL_PixelFormat* pf = *(SDL_PixelFormat**)luaL_checkudata(L, 1, "SDL_PixelFormat");
    Uint32 pixel = (int)luaL_checkinteger(L, 2);
    Uint8 r, g, b;
    SDL_GetRGB(pixel, pf, &r, &g, &b);
    lua_pushinteger(L, r);
    lua_pushinteger(L, g);
    lua_pushinteger(L, b);
    return 3;
}

static int LUA_GetRGBA(lua_State* L)
{
    SDL_PixelFormat* pf = *(SDL_PixelFormat**)luaL_checkudata(L, 1, "SDL_PixelFormat");
    Uint32 pixel = (int)luaL_checkinteger(L, 2);
    Uint8 r, g, b, a;
    SDL_GetRGBA(pixel, pf, &r, &g, &b, &a);
    lua_pushinteger(L, r);
    lua_pushinteger(L, g);
    lua_pushinteger(L, b);
    lua_pushinteger(L, a);
    return 4;
}

static int LUA_CalculateGammaRamp(lua_State* L)
{
    //float gamma = (float)luaL_checknumber(L,1);
    //Uint16 * ramp;
    //SDL_CalculateGammaRamp(gamma,ramp);
    return 0;
}

static int LUA_PixelFormatIndex(lua_State* L)
{
    SDL_PixelFormat* pf = *(SDL_PixelFormat**)luaL_checkudata(L, 1, "SDL_PixelFormat");
    const char* name = lua_tostring(L, 2);

    if (SDL_strcmp(name, "format") == 0)
    {
        lua_pushinteger(L, pf->format);
    }
    else if (SDL_strcmp(name, "name") == 0)
    {
        lua_pushstring(L, SDL_GetPixelFormatName(pf->format));
        //} else if (SDL_strcmp(name, "palette") == 0) {
    }
    else if (SDL_strcmp(name, "BitsPerPixel") == 0)
    {
        lua_pushinteger(L, pf->BitsPerPixel);
    }
    else if (SDL_strcmp(name, "BytesPerPixel") == 0)
    {
        lua_pushinteger(L, pf->BytesPerPixel);
        //} else if (SDL_strcmp(name, "padding") == 0) {
    }
    else if (SDL_strcmp(name, "Rmask") == 0)
    {
        lua_pushinteger(L, pf->Rmask);
    }
    else if (SDL_strcmp(name, "Gmask") == 0)
    {
        lua_pushinteger(L, pf->Gmask);
    }
    else if (SDL_strcmp(name, "Bmask") == 0)
    {
        lua_pushinteger(L, pf->Bmask);
    }
    else if (SDL_strcmp(name, "Amask") == 0)
    {
        lua_pushinteger(L, pf->Amask);
    }
    else if (SDL_strcmp(name, "Rloss") == 0)
    {
        lua_pushinteger(L, pf->Rloss);
    }
    else if (SDL_strcmp(name, "Gloss") == 0)
    {
        lua_pushinteger(L, pf->Gloss);
    }
    else if (SDL_strcmp(name, "Bloss") == 0)
    {
        lua_pushinteger(L, pf->Bloss);
    }
    else if (SDL_strcmp(name, "Aloss") == 0)
    {
        lua_pushinteger(L, pf->Aloss);
    }
    else if (SDL_strcmp(name, "Rshift") == 0)
    {
        lua_pushinteger(L, pf->Rshift);
    }
    else if (SDL_strcmp(name, "Gshift") == 0)
    {
        lua_pushinteger(L, pf->Gshift);
    }
    else if (SDL_strcmp(name, "Bshift") == 0)
    {
        lua_pushinteger(L, pf->Bshift);
    }
    else if (SDL_strcmp(name, "Ashift") == 0)
    {
        lua_pushinteger(L, pf->Ashift);
    }
    else if (SDL_strcmp(name, "refcount") == 0)
    {
        lua_pushinteger(L, pf->refcount);
    }
    else
    {
        luaL_getmetatable(L, "SDL_PixelFormat");
        lua_getfield(L, -1, name);
        lua_remove(L, -2);
    }
    return 1;
}

static const luaL_Reg pixels_funcs[] = {
    {"__gc", LUA_FreeFormat},
    {"__close", LUA_FreeFormat},
    {"__index", LUA_PixelFormatIndex},
    {"GetRGB", LUA_GetRGB},
    {"GetRGBA", LUA_GetRGBA},
    {"MapRGB", LUA_MapRGB},
    {"MapRGBA", LUA_MapRGBA},
    {NULL, NULL} };

static const luaL_Reg sdl_funcs[] = {
    {"AllocFormat", LUA_AllocFormat}, //new SDL_PixelFormat
    {"AllocPalette", LUA_AllocPalette},
    {"CalculateGammaRamp", LUA_CalculateGammaRamp},
    //{"FreePalette", LUA_FreePalette},
    {"GetPixelFormatName", LUA_GetPixelFormatName},

    {"MasksToPixelFormatEnum", LUA_MasksToPixelFormatEnum},
    {"PixelFormatEnumToMasks", LUA_PixelFormatEnumToMasks},
    //{"SetPaletteColors", LUA_SetPaletteColors},
    //{"SetPixelFormatPalette", LUA_SetPixelFormatPalette},
    {NULL, NULL} };

int bind_pixels(lua_State* L)
{
    luaL_newmetatable(L, "SDL_PixelFormat");
    luaL_setfuncs(L, pixels_funcs, 0);
    lua_pop(L, 1);

    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}