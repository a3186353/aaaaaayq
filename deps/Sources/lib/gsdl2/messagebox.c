#include "gge.h"
#include "SDL_messagebox.h"

static int LUA_ShowMessageBox(lua_State *L)
{
    //SDL_MessageBoxData data;
    //SDL_MessageBoxButtonData button;

    //SDL_zero(data);
    //data.flags = flags;
    //data.title = title;
    //data.message = message;
    //data.numbuttons = 1;
    //data.buttons = &button;
    //data.window = window;

    //SDL_zero(button);
    //button.flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    //button.flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    //button.text = "OK";
    //SDL_ShowMessageBox();
    return 1;
}

static int LUA_ShowSimpleMessageBox(lua_State *L)
{
    SDL_Window *win = *(SDL_Window **)luaL_checkudata(L, 1, "SDL_Window");
    Uint32 flags = (int)luaL_optinteger(L, 2, 0);
    const char *title = luaL_optstring(L, 3, "");
    const char *message = luaL_optstring(L, 4, "");
    lua_pushboolean(L, SDL_ShowSimpleMessageBox(flags, title, message, win) == 0);
    return 1;
}

static const luaL_Reg window_funcs[] = {
    //{"ShowMessageBox", LUA_ShowMessageBox},//SDL_messagebox
    {"ShowSimpleMessageBox", LUA_ShowSimpleMessageBox}, //SDL_messagebox
    {NULL, NULL}};

int bind_messagebox(lua_State *L)
{
    luaL_getmetatable(L, "SDL_Window");
    luaL_setfuncs(L, window_funcs, 0);
    lua_pop(L, 1);

    return 0;
}
