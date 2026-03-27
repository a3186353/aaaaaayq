#include "gge.h"
#include "SDL_error.h"

static int LUA_SetError(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TSTRING);
    int n = lua_gettop(L);
    if (n > 1)
    {
        lua_getfield(L, 1, "format");
        lua_insert(L, 1);
        lua_call(L, n, 1);
    }

    lua_pushinteger(L, SDL_SetError("%s", lua_tostring(L, 1)));
    return 1;
}

static int LUA_GetError(lua_State *L)
{
    lua_pushstring(L, SDL_GetError());
    return 1;
}
//SDL_GetErrorMsg
static int LUA_ClearError(lua_State *L)
{
    SDL_ClearError();
    return 0;
}

//SDL_Error
static const luaL_Reg sdl_funcs[] = {
    {"ClearError", LUA_ClearError},
    {"GetError", LUA_GetError},
    {"SetError", LUA_SetError},
    {NULL, NULL}};

int bind_error(lua_State *L)
{
    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}