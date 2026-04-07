#include "gge.h"
#include "SDL_misc.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#endif

static int LUA_OpenURL(lua_State *L)
{
    const char *url = luaL_checkstring(L, 1);
#ifdef _WIN32
    const char *dir = luaL_optstring(L, 2, NULL);
    if (dir) {
        /* 直接调用 ShellExecuteW 指定工作目录 */
        int wurl_len = MultiByteToWideChar(CP_UTF8, 0, url, -1, NULL, 0);
        int wdir_len = MultiByteToWideChar(CP_UTF8, 0, dir, -1, NULL, 0);
        wchar_t *wurl = (wchar_t *)malloc(wurl_len * sizeof(wchar_t));
        wchar_t *wdir = (wchar_t *)malloc(wdir_len * sizeof(wchar_t));
        MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, wurl_len);
        MultiByteToWideChar(CP_UTF8, 0, dir, -1, wdir, wdir_len);
        HINSTANCE rc = ShellExecuteW(NULL, L"open", wurl, NULL, wdir, SW_SHOWNORMAL);
        free(wurl);
        free(wdir);
        lua_pushboolean(L, rc > (HINSTANCE)32);
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