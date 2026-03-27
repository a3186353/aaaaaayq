#include "gge.h"
#include "SDL_power.h"

static int LUA_GetPowerInfo(lua_State *L)
{
    int secs, pct;
    lua_pushinteger(L, SDL_GetPowerInfo(&secs, &pct));
    lua_pushinteger(L, secs);
    lua_pushinteger(L, pct);
    return 3;
}

static const luaL_Reg sdl_funcs[] = {
    {"GetPowerInfo", LUA_GetPowerInfo},
    {NULL, NULL}};

int bind_power(lua_State *L)
{
    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}