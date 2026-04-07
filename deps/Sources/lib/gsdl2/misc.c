#include "gge.h"
#include "SDL_misc.h"

#ifdef _WIN32
/* Windows: 支持可选的第二个参数指定工作目录 */
extern int SDL_SYS_OpenURL_Dir(const char *url, const char *dir);
#endif

static int LUA_OpenURL(lua_State *L)
{
    const char *url = luaL_checkstring(L, 1);
#ifdef _WIN32
    const char *dir = luaL_optstring(L, 2, NULL);
    if (dir) {
        lua_pushboolean(L, SDL_SYS_OpenURL_Dir(url, dir) == 0);
    } else {
        lua_pushboolean(L, SDL_OpenURL(url) == 0);
    }
#else
    lua_pushboolean(L, SDL_OpenURL(url) == 0);
#endif
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