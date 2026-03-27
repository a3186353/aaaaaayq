#include "gge.h"
#include "SDL_filesystem.h"

static int LUA_GetBasePath(lua_State *L)
{
    char *str = SDL_GetBasePath();

    if (str)
    {
        lua_pushstring(L, str);
        SDL_free(str);
        return 1;
    }
    return 0;
}

static int LUA_GetPrefPath(lua_State *L)
{
    const char *organization = luaL_checkstring(L, 1);
    const char *application = luaL_checkstring(L, 2);
    char *str = SDL_GetPrefPath(organization, application);

    if (str)
    {
        lua_pushstring(L, str);
        SDL_free(str);
        return 1;
    }

    return 0;
}

static const luaL_Reg sdl_funcs[] = {
    {"GetBasePath", LUA_GetBasePath},
    {"GetPrefPath", LUA_GetPrefPath},
    {NULL, NULL}};

int bind_filesystem(lua_State *L)
{
    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}