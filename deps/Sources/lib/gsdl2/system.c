#include "gge.h"
#include "SDL_system.h"

#ifdef __ANDROID__
//SDL_AndroidGetJNIEnv
//SDL_AndroidGetActivity

static int LUA_GetAndroidSDKVersion(lua_State *L)
{
    lua_pushinteger(L, SDL_GetAndroidSDKVersion());
    return 1;
}

static int LUA_IsAndroidTV(lua_State *L)
{
    lua_pushboolean(L, SDL_IsAndroidTV());
    return 1;
}

//static int LUA_IsChromebook(lua_State* L)
//{
//	lua_pushboolean(L, SDL_IsChromebook());
//	return 1;
//}

//static int LUA_IsDeXMode(lua_State* L)
//{
//	lua_pushboolean(L, SDL_IsDeXMode());
//	return 1;
//}

static int LUA_AndroidBackButton(lua_State *L)
{
    SDL_AndroidBackButton();
    return 0;
}

static int LUA_AndroidGetInternalStoragePath(lua_State *L)
{
    lua_pushstring(L, SDL_AndroidGetInternalStoragePath());
    return 1;
}

static int LUA_AndroidGetExternalStorageState(lua_State *L)
{
    lua_pushinteger(L, SDL_AndroidGetExternalStorageState());
    return 1;
}

static int LUA_AndroidGetExternalStoragePath(lua_State *L)
{
    lua_pushstring(L, SDL_AndroidGetExternalStoragePath());
    return 1;
}

static int LUA_AndroidRequestPermission(lua_State *L)
{
    const char *permission = luaL_checkstring(L, 1);
    lua_pushboolean(L, SDL_AndroidRequestPermission(permission));
    return 1;
}
#endif /* __ANDROID__ */

static const luaL_Reg sdl_funcs[] = {
#ifdef __ANDROID__
    {"GetAndroidSDKVersion", LUA_GetAndroidSDKVersion},
    {"IsAndroidTV", LUA_IsAndroidTV},
    {"AndroidBackButton", LUA_AndroidBackButton},
    {"AndroidGetInternalStoragePath", LUA_AndroidGetInternalStoragePath},
    {"AndroidGetExternalStorageState", LUA_AndroidGetExternalStorageState},
    {"AndroidGetExternalStoragePath", LUA_AndroidGetExternalStoragePath},
    {"AndroidRequestPermission", LUA_AndroidRequestPermission},
#endif /* __ANDROID__ */
    {NULL, NULL}};

int bind_system(lua_State *L)
{
    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}