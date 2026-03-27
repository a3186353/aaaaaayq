#include "gge.h"
#include "SDL_mouse.h"

static int LUA_GetMouseFocus(lua_State *L)
{
    SDL_Window *win = *(SDL_Window **)luaL_checkudata(L, 1, "SDL_Window");

    lua_pushboolean(L, win == SDL_GetMouseFocus());
    return 1;
}

static int LUA_GetMouseState(lua_State *L)
{
    int x, y;
    int r = SDL_GetMouseState(&x, &y);
    lua_pushinteger(L, r);
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);

    return 3;
}

static int LUA_GetGlobalMouseState(lua_State *L)
{
    int x, y;
    int r = SDL_GetGlobalMouseState(&x, &y);
    lua_pushinteger(L, r);
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);

    return 3;
}

static int LUA_GetRelativeMouseState(lua_State *L)
{
    int x, y;
    int r = SDL_GetRelativeMouseState(&x, &y);
    lua_pushinteger(L, r);
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);

    return 3;
}

static int LUA_WarpMouseInWindow(lua_State *L)
{
    SDL_Window *win = *(SDL_Window **)luaL_checkudata(L, 1, "SDL_Window");
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);

    SDL_WarpMouseInWindow(win, x, y);
    return 0;
}

static int LUA_WarpMouseGlobal(lua_State *L)
{
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);

    SDL_WarpMouseGlobal(x, y);
    return 0;
}

static int LUA_SetRelativeMouseMode(lua_State *L)
{
    SDL_bool enabled = (SDL_bool)lua_toboolean(L, 1);

    lua_pushboolean(L, SDL_SetRelativeMouseMode(enabled) == 0);
    return 1;
}

static int LUA_CaptureMouse(lua_State *L)
{
    SDL_bool enabled = (SDL_bool)lua_toboolean(L, 1);

    lua_pushboolean(L, SDL_CaptureMouse(enabled) == 0);
    return 1;
}

static int LUA_GetRelativeMouseMode(lua_State *L)
{
    lua_pushboolean(L, SDL_GetRelativeMouseMode() == 0);
    return 1;
}

static int LUA_CreateCursor(lua_State *L)
{
    const Uint8 *data = (const Uint8 *)luaL_checkstring(L, 1);
    const Uint8 *mask = (const Uint8 *)luaL_checkstring(L, 2);
    int w = (int)luaL_checkinteger(L, 3);
    int h = (int)luaL_checkinteger(L, 4);
    int hot_x = (int)luaL_checkinteger(L, 5);
    int hot_y = (int)luaL_checkinteger(L, 6);
    SDL_Cursor **ud;
    SDL_Cursor *c = SDL_CreateCursor(data, mask, w, h, hot_x, hot_y);

    ud = (SDL_Cursor **)lua_newuserdata(L, sizeof(SDL_Cursor *));
    *ud = c;
    luaL_setmetatable(L, "SDL_Cursor");
    return 1;
}

static int LUA_CreateColorCursor(lua_State *L)
{
    SDL_Surface *sf = *(SDL_Surface **)luaL_checkudata(L, 1, "SDL_Surface");
    int hot_x = (int)luaL_checkinteger(L, 2);
    int hot_y = (int)luaL_checkinteger(L, 3);
    SDL_Cursor *c = SDL_CreateColorCursor(sf, hot_x, hot_y);
    if (c)
    {
        SDL_Cursor **ud = (SDL_Cursor **)lua_newuserdata(L, sizeof(SDL_Cursor *));
        *ud = c;
        luaL_setmetatable(L, "SDL_Cursor");
        return 1;
    }
    return 0;
}

static int LUA_CreateSystemCursor(lua_State *L)
{
    int id = (int)luaL_checkinteger(L, 1);
    SDL_Cursor *c;
    SDL_Cursor **ud;
    c = SDL_CreateSystemCursor(id);

    ud = (SDL_Cursor **)lua_newuserdata(L, sizeof(SDL_Cursor *));
    *ud = c;
    luaL_setmetatable(L, "SDL_Cursor");
    return 1;
}

static int LUA_SetCursor(lua_State *L)
{
    SDL_Cursor *c = *(SDL_Cursor **)luaL_checkudata(L, 1, "SDL_Cursor");
    SDL_SetCursor(c);
    return 0;
}

static int LUA_GetCursor(lua_State *L)
{
    SDL_Cursor *c = SDL_GetCursor();
    SDL_Cursor **ud = (SDL_Cursor **)lua_newuserdata(L, sizeof(SDL_Cursor *));
    *ud = c;
    luaL_setmetatable(L, "SDL_Cursor");
    return 1;
}

static int LUA_GetDefaultCursor(lua_State *L)
{
    SDL_Cursor *c = SDL_GetDefaultCursor();
    SDL_Cursor **ud = (SDL_Cursor **)lua_newuserdata(L, sizeof(SDL_Cursor *));
    *ud = c;
    luaL_setmetatable(L, "SDL_Cursor");
    return 1;
}

static int LUA_FreeCursor(lua_State *L)
{
    SDL_Cursor *c = *(SDL_Cursor **)luaL_checkudata(L, 1, "SDL_Cursor");
    SDL_FreeCursor(c);
    return 0;
}

static int LUA_ShowCursor(lua_State *L)
{
    int toggle = (int)luaL_opt(L, lua_toboolean, 1, 1);
    lua_pushinteger(L, SDL_ShowCursor(toggle));
    return 1;
}

static const luaL_Reg sdl_funcs[] = {
    {"CaptureMouse", LUA_CaptureMouse},
    {"CreateCursor", LUA_CreateCursor},
    {"CreateSystemCursor", LUA_CreateSystemCursor},
    {"GetCursor", LUA_GetCursor},
    //{"GetDefaultCursor", LUA_GetDefaultCursor},
    {"GetGlobalMouseState", LUA_GetGlobalMouseState},

    {"GetRelativeMouseMode", LUA_GetRelativeMouseMode},
    {"GetRelativeMouseState", LUA_GetRelativeMouseState},
    {"SetRelativeMouseMode", LUA_SetRelativeMouseMode},
    {"ShowCursor", LUA_ShowCursor},
    {"WarpMouseGlobal", LUA_WarpMouseGlobal},
    {"GetMouseState", LUA_GetMouseState},
    //LUA_SetCursor
    {NULL, NULL}};

static const luaL_Reg surface_funcs[] = {
    {"CreateColorCursor", LUA_CreateColorCursor},
    {NULL, NULL}};

static const luaL_Reg window_funcs[] = {
    {"WarpMouseInWindow", LUA_WarpMouseInWindow},
    {"GetMouseFocus", LUA_GetMouseFocus},
    {NULL, NULL}};

int bind_mouse(lua_State *L)
{
    luaL_getmetatable(L, "SDL_Surface");
    luaL_setfuncs(L, surface_funcs, 0);
    lua_pop(L, 1);

    luaL_getmetatable(L, "SDL_Window");
    luaL_setfuncs(L, window_funcs, 0);
    lua_pop(L, 1);

    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}