#include "gge.h"
#include "SDL_platform.h"

static int LUA_GetPlatform(lua_State *L)
{
    lua_pushstring(L, SDL_GetPlatform());
    return 1;
}

static const luaL_Reg sdl_funcs[] = {
    {"GetPlatform", LUA_GetPlatform},
    {NULL, NULL}};

int bind_platform(lua_State *L)
{
    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}