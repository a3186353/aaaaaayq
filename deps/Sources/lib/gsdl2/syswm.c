#include "gge.h"
#include "SDL_syswm.h"

static int LUA_GetWindowWMInfo(lua_State *L)
{
    SDL_Window *win = *(SDL_Window **)luaL_checkudata(L, 1, "SDL_Window");
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(win, &info))
    {
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, info.subsystem);
        lua_setfield(L, -2, "subsystem");
        lua_createtable(L, 0, 3);
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
        lua_pushinteger(L, (lua_Integer)info.info.win.window);
        lua_setfield(L, -2, "window");
        lua_pushinteger(L, (lua_Integer)info.info.win.hdc);
        lua_setfield(L, -2, "hdc");
        lua_pushinteger(L, (lua_Integer)info.info.win.hinstance);
#endif
        lua_setfield(L, -2, "hinstance");
        lua_setfield(L, -2, "info");
        return 1;
    }
    return 0;
}

static const luaL_Reg syswm_funcs[] = {
    {"GetWindowWMInfo", LUA_GetWindowWMInfo},
    {NULL, NULL}};

int bind_syswm(lua_State *L)
{
    luaL_getmetatable(L, "SDL_Window");
    luaL_setfuncs(L, syswm_funcs, 0);
    lua_pop(L, 1);
    return 0;
}