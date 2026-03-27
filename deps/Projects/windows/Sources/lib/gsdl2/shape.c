#include "gge.h"
#include "SDL_shape.h"

static int LUA_CreateShapedWindow(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    int x = (int)luaL_optinteger(L, 2, SDL_WINDOWPOS_CENTERED);
    int y = (int)luaL_optinteger(L, 3, SDL_WINDOWPOS_CENTERED);
    int width = (int)luaL_checkinteger(L, 4);
    int height = (int)luaL_checkinteger(L, 5);
    int flags = (int)luaL_checkinteger(L, 6);

    SDL_Window* win = SDL_CreateShapedWindow(name, x, y, width, height, flags);
    if (win)
    {
        SDL_Window** ud = (SDL_Window**)lua_newuserdata(L, sizeof(SDL_Window*));
        *ud = win;
        lua_pushinteger(L, 1);
        lua_setuservalue(L, -2);
        luaL_setmetatable(L, "SDL_Window");
        return 1;
    }
    return 0;
}

static int LUA_IsShapedWindow(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");

    lua_pushboolean(L, SDL_IsShapedWindow(win) == SDL_TRUE);
    return 1;
}

static int LUA_SetWindowShape(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 2, "SDL_Surface");

    SDL_WindowShapeMode shape_mode;
    if (SDL_HasColorKey(sf))
    {
        Uint32 key;
        SDL_GetColorKey(sf, &key);
        SDL_Color* c = &shape_mode.parameters.colorKey;
        SDL_GetRGBA(key, sf->format, &c->r, &c->g, &c->b, &c->a);
        shape_mode.mode = ShapeModeColorKey;
    }
    else
    {
        shape_mode.mode = ShapeModeBinarizeAlpha;
        shape_mode.parameters.binarizationCutoff = 255;
    }
    lua_pushboolean(L, SDL_SetWindowShape(win, sf, &shape_mode) == 0);
    return 1;
}

static int LUA_GetShapedWindowMod(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    SDL_WindowShapeMode shape_mode;
    if (SDL_GetShapedWindowMode(win, &shape_mode) == 0)
    {
        lua_pushinteger(L, shape_mode.mode);
        lua_pushinteger(L, shape_mode.parameters.binarizationCutoff);
        return 2;
    }
    return 0;
}

int bind_shape(lua_State* L)
{

    static const luaL_Reg window_funcs[] = {
        {"IsShapedWindow", LUA_IsShapedWindow},
        {"SetWindowShape", LUA_SetWindowShape},
        {"GetShapedWindowMod", LUA_GetShapedWindowMod},
        {NULL, NULL}
    };

    static const luaL_Reg sdl_funcs[] = {
        {"CreateShapedWindow", LUA_CreateShapedWindow},
        {NULL, NULL}
    };

    luaL_getmetatable(L, "SDL_Window");
    luaL_setfuncs(L, window_funcs, 0);
    lua_pop(L, 1);

    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}