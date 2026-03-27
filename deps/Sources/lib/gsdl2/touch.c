#include "gge.h"
#include "SDL_touch.h"

static int LUA_GetNumTouchDevices(lua_State *L)
{
    lua_pushinteger(L, SDL_GetNumTouchDevices());
    return 1;
}

static int LUA_GetTouchDevice(lua_State *L)
{
    int index = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, SDL_GetTouchDevice(index));
    return 1;
}

static int LUA_GetTouchDeviceType(lua_State *L)
{
    SDL_TouchID touchID = (SDL_TouchID)luaL_checkinteger(L, 1);
    lua_pushinteger(L, SDL_GetTouchDeviceType(touchID));
    return 1;
}

static int LUA_GetNumTouchFingers(lua_State *L)
{
    SDL_TouchID touchID = (SDL_TouchID)luaL_checkinteger(L, 1);
    lua_pushinteger(L, SDL_GetNumTouchFingers(touchID));
    return 1;
}

static int LUA_GetTouchFinger(lua_State *L)
{
    SDL_TouchID touchID = (SDL_TouchID)luaL_checkinteger(L, 1);
    int index = (int)luaL_checkinteger(L, 2);
    SDL_Finger *f = SDL_GetTouchFinger(touchID, index);
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, f->id);
    lua_setfield(L, -2, "id");
    lua_pushnumber(L, f->x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, f->y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, f->pressure);
    lua_setfield(L, -2, "pressure");
    return 1;
}

static const luaL_Reg sdl_funcs[] = {
    {"GetNumTouchDevices", LUA_GetNumTouchDevices},
    {"GetTouchDevice", LUA_GetTouchDevice},
    {"GetTouchDeviceType", LUA_GetTouchDeviceType},
    {"GetNumTouchFingers", LUA_GetNumTouchFingers},
    {"GetTouchFinger", LUA_GetTouchFinger},

    {NULL, NULL}};

int bind_touch(lua_State *L)
{
    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}