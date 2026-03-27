#include "gge.h"
#include "SDL_hints.h"

static int LUA_SetHintWithPriority(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    const char *value = luaL_checkstring(L, 2);
    const SDL_HintPriority priority = (SDL_HintPriority)luaL_optinteger(L, 3, SDL_HINT_DEFAULT);
    lua_pushboolean(L, SDL_SetHintWithPriority(name, value, priority));
    return 1;
}

static int LUA_SetHint(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    const char *value = luaL_checkstring(L, 2);
    lua_pushboolean(L, SDL_SetHint(name, value));
    return 1;
}

static int LUA_GetHint(lua_State *L)
{
    lua_pushstring(L, SDL_GetHint(luaL_checkstring(L, 1)));
    return 1;
}

static int LUA_GetHintBoolean(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    int value = lua_toboolean(L, 2);
    lua_pushboolean(L, SDL_GetHintBoolean(name, value));
    return 1;
}
//LUA_AddHintCallback
//LUA_DelHintCallback
static int LUA_ClearHints(lua_State *L)
{
    SDL_ClearHints();
    return 0;
}

static const luaL_Reg sdl_funcs[] = {
    //{"AddHintCallback", LUA_AddHintCallback},
    {"ClearHints", LUA_ClearHints},
    //{"DelHintCallback", LUA_DelHintCallback},
    {"GetHint", LUA_GetHint},
    {"GetHintBoolean", LUA_GetHintBoolean},
    {"SetHintWithPriority", LUA_SetHintWithPriority},
    {"SetHint", LUA_SetHint},
    {NULL, NULL}};

int bind_hints(lua_State *L)
{
    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}