#include "gge.h"
#include "SDL_gesture.h"

static int LUA_RecordGesture(lua_State *L)
{
    SDL_TouchID touchId = (SDL_TouchID)luaL_checkinteger(L, 1);
    lua_pushinteger(L, SDL_RecordGesture(touchId));
    return 1;
}

static int LUA_SaveAllDollarTemplates(lua_State *L)
{
    SDL_RWops *rw = *(SDL_RWops **)luaL_checkudata(L, 1, "SDL_RWops");
    lua_pushinteger(L, SDL_SaveAllDollarTemplates(rw));
    return 1;
}

static int LUA_SaveDollarTemplate(lua_State *L)
{
    SDL_RWops *rw = *(SDL_RWops **)luaL_checkudata(L, 1, "SDL_RWops");
    SDL_GestureID gestureId = (SDL_GestureID)luaL_checkinteger(L, 2);
    lua_pushinteger(L, SDL_SaveDollarTemplate(gestureId, rw));
    return 1;
}

static int LUA_LoadDollarTemplates(lua_State *L)
{
    SDL_RWops *rw = *(SDL_RWops **)luaL_checkudata(L, 1, "SDL_RWops");
    SDL_TouchID touchId = (SDL_TouchID)luaL_checkinteger(L, 2);
    lua_pushinteger(L, SDL_LoadDollarTemplates(touchId, rw));
    return 1;
}

static const luaL_Reg sdl_funcs[] = {
    {"RecordGesture", LUA_RecordGesture},
    {"SaveAllDollarTemplates", LUA_SaveAllDollarTemplates},
    {"SaveDollarTemplate", LUA_SaveDollarTemplate},
    {"LoadDollarTemplates", LUA_LoadDollarTemplates},
    {NULL, NULL}};

int bind_gesture(lua_State *L)
{
    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}