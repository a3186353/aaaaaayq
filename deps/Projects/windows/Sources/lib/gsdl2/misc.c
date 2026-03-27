#include "gge.h"
#include "SDL_misc.h"

static int LUA_OpenURL(lua_State *L)
{
    const char *url = luaL_checkstring(L, 1);
    lua_pushboolean(L, SDL_OpenURL(url) == 0);
    return 1;
}

static const luaL_Reg sdl_funcs[] = {
    {"OpenURL", LUA_OpenURL},
    {NULL, NULL}};

int bind_misc(lua_State *L)
{
    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}