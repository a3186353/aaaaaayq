#include "gge.h"
#include "SDL_keyboard.h"


static int LUA_GetKeyboardFocus(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    lua_pushboolean(L, win == SDL_GetKeyboardFocus());
    return 1;
}

static int LUA_GetKeyboardState(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    Uint8 i = (Uint8)luaL_checkinteger(L, 2);
    if (win == SDL_GetKeyboardFocus())
    {
        int numkeys;
        const Uint8* keys = SDL_GetKeyboardState(&numkeys);

        if (i < 0 || i >= numkeys)
            lua_pushboolean(L, 0);
        else
            lua_pushboolean(L, keys[i]);
    }
    else
        lua_pushboolean(L, 0);

    return 1;
}

static int LUA_GetModState(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    if (win == SDL_GetKeyboardFocus())
    {
        int i = (int)luaL_checkinteger(L, 2);
        int state = (int)SDL_GetModState();
        lua_pushboolean(L, state & i);
    }
    else
        lua_pushboolean(L, 0);
    return 1;
}

static int LUA_SetModState(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    if (win == SDL_GetKeyboardFocus())
    {
        SDL_Keymod state = (SDL_Keymod)luaL_checkinteger(L, 2);
        SDL_SetModState(state);
    }
    return 0;
}

static int LUA_GetKeyFromScancode(lua_State* L)
{
    SDL_Scancode sc = (SDL_Scancode)luaL_checkinteger(L, 1);
    lua_pushinteger(L, SDL_GetKeyFromScancode(sc));
    return 1;
}

static int LUA_GetScancodeFromKey(lua_State* L)
{
    SDL_Keycode kc = (SDL_Keycode)luaL_checkinteger(L, 1);
    lua_pushinteger(L, SDL_GetScancodeFromKey(kc));
    return 1;
}

static int LUA_GetScancodeName(lua_State* L)
{
    SDL_Scancode code = (SDL_Scancode)luaL_checkinteger(L, 1);
    lua_pushstring(L, SDL_GetScancodeName(code));
    return 1;
}

static int LUA_GetScancodeFromName(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    lua_pushinteger(L, SDL_GetScancodeFromName(name));
    return 1;
}

static int LUA_GetKeyName(lua_State* L)
{
    SDL_Keycode kc = (SDL_Keycode)luaL_checkinteger(L, 1);
    lua_pushstring(L, SDL_GetKeyName(kc));
    return 1;
}

static int LUA_GetKeyFromName(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    lua_pushinteger(L, SDL_GetKeyFromName(name));
    return 1;
}

static int LUA_StartTextInput(lua_State* L)
{
    SDL_StartTextInput();
    return 0;
}

static int LUA_IsTextInputActive(lua_State* L)
{
    lua_pushboolean(L, SDL_IsTextInputActive());
    return 1;
}

static int LUA_StopTextInput(lua_State* L)
{
    SDL_StopTextInput();
    return 0;
}

static int LUA_ClearComposition(lua_State* L)
{
    SDL_ClearComposition();
    return 0;
}

static int LUA_IsTextInputShown(lua_State* L)
{
    lua_pushboolean(L, SDL_IsTextInputShown());
    return 1;
}

static int LUA_SetTextInputRect(lua_State* L)
{
    SDL_Rect rect;
    rect.x = (int)luaL_checkinteger(L, 1);
    rect.y = (int)luaL_checkinteger(L, 2);
    rect.w = (int)luaL_optinteger(L, 3, 0);
    rect.h = (int)luaL_optinteger(L, 4, 0);

    SDL_SetTextInputRect(&rect);
    return 0;
}

static int LUA_HasScreenKeyboardSupport(lua_State* L)
{
    lua_pushboolean(L, SDL_HasScreenKeyboardSupport());
    return 1;
}

static int LUA_IsScreenKeyboardShown(lua_State* L)
{
    SDL_Window* win = (SDL_Window*)luaL_checkudata(L, 1, "SDL_Window");
    lua_pushboolean(L, SDL_IsScreenKeyboardShown(win));
    return 1;
}

int bind_keyboard(lua_State* L)
{

    static const luaL_Reg sdl_funcs[] = {
        {"GetKeyFromScancode", LUA_GetKeyFromScancode},
        {"GetScancodeFromKey", LUA_GetScancodeFromKey},
        {"GetScancodeName", LUA_GetScancodeName},
        {"GetScancodeFromName", LUA_GetScancodeFromName},
        {"GetKeyName", LUA_GetKeyName},
        {"GetKeyFromName", LUA_GetKeyFromName},
        {"StartTextInput", LUA_StartTextInput},
        {"IsTextInputActive", LUA_IsTextInputActive},
        {"StopTextInput", LUA_StopTextInput},
        {"ClearComposition", LUA_ClearComposition},
        {"IsTextInputShown", LUA_IsTextInputShown},
        {"SetTextInputRect", LUA_SetTextInputRect},
        {"HasScreenKeyboardSupport", LUA_HasScreenKeyboardSupport},
        {NULL, NULL}
    };

    static const luaL_Reg window_funcs[] = {
        {"GetKeyboardFocus", LUA_GetKeyboardFocus},
        {"GetKeyboardState", LUA_GetKeyboardState},
        {"GetModState", LUA_GetModState},
        {"SetModState", LUA_SetModState},

        {"IsScreenKeyboardShown", LUA_IsScreenKeyboardShown},
        {NULL, NULL}
    };

    luaL_getmetatable(L, "SDL_Window");
    luaL_setfuncs(L, window_funcs, 0);
    lua_pop(L, 1);

    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}
