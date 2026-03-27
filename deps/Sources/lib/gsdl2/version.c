#include "gge.h"
#include "SDL_version.h"

static int LUA_GetVersion(lua_State *L)
{
    lua_pushfstring(L, "SDL %d.%d.%d", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
    return 1;
}

static int LUA_GetRevision(lua_State *L)
{
    lua_pushfstring(L, SDL_GetRevision());
    return 1;
}

static int LUA_GetRevisionNumber(lua_State *L)
{
    lua_pushinteger(L, SDL_GetRevisionNumber());
    return 1;
}

static const luaL_Reg sdl_funcs[] = {
    {"GetVersion", LUA_GetVersion},
    {"GetRevision", LUA_GetRevision},
    {"GetRevisionNumber", LUA_GetRevisionNumber},
    {NULL, NULL}};

int bind_version(lua_State *L)
{
    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}