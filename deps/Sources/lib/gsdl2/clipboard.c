#include "gge.h"
#include "SDL_clipboard.h"

static int LUA_SetClipboardText(lua_State *L)
{
    const char *text = luaL_checkstring(L, 1);

    lua_pushboolean(L, SDL_SetClipboardText(text) == 0);
    return 1;
}

static int LUA_GetClipboardText(lua_State *L)
{
    char *str = SDL_GetClipboardText();
    lua_pushstring(L, str);
    SDL_free(str);

    return 1;
}

static int LUA_HasClipboardText(lua_State *L)
{
    lua_pushboolean(L, SDL_HasClipboardText());
    return 1;
}

static const luaL_Reg sdl_funcs[] = {
    {"GetClipboardText", LUA_GetClipboardText},
    {"HasClipboardText", LUA_HasClipboardText},
    {"SetClipboardText", LUA_SetClipboardText},
    {NULL, NULL}};

int bind_clipboard(lua_State *L)
{

    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}