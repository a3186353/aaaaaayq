#include "gge.h"
#include "SDL_log.h"
//TODO
static int LUA_LogSetAllPriority(lua_State *L)
{
    int priority = (int)luaL_checkinteger(L, 1);

    SDL_LogSetAllPriority(priority);
    return 0;
}

static int LUA_LogSetPriority(lua_State *L)
{
    int category = (int)luaL_checkinteger(L, 1);
    int priority = (int)luaL_checkinteger(L, 2);

    SDL_LogSetPriority(category, priority);
    return 0;
}

static int LUA_LogGetPriority(lua_State *L)
{
    int category = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, SDL_LogGetPriority(category));
    return 1;
}

static int LUA_LogResetPriorities(lua_State *L)
{
    SDL_LogResetPriorities();
    return 0;
}

static int LUA_Log(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TSTRING);
    int n = lua_gettop(L);
    if (n > 1)
    {
        lua_getfield(L, 1, "format");
        lua_insert(L, 1);
        lua_call(L, n, 1);
    }

    SDL_Log("%s", lua_tostring(L, 1));
    return 0;
}

static int LUA_LogVerbose(lua_State *L)
{
    luaL_checktype(L, 2, LUA_TSTRING);
    int n = lua_gettop(L) - 1;
    if (n > 1)
    {
        lua_getfield(L, 2, "format");
        lua_insert(L, 2);
        lua_call(L, n, 1);
    }
    SDL_LogVerbose((int)luaL_checkinteger(L, 1), "%s", luaL_checkstring(L, 2));
    return 0;
}

static int LUA_LogDebug(lua_State *L)
{
    luaL_checktype(L, 2, LUA_TSTRING);
    int n = lua_gettop(L) - 1;
    if (n > 1)
    {
        lua_getfield(L, 2, "format");
        lua_insert(L, 2);
        lua_call(L, n, 1);
    }
    SDL_LogDebug((int)luaL_checkinteger(L, 1), "%s", luaL_checkstring(L, 2));
    return 0;
}

static int LUA_LogInfo(lua_State *L)
{
    luaL_checktype(L, 2, LUA_TSTRING);
    int n = lua_gettop(L) - 1;
    if (n > 1)
    {
        lua_getfield(L, 2, "format");
        lua_insert(L, 2);
        lua_call(L, n, 1);
    }
    SDL_LogInfo((int)luaL_checkinteger(L, 1), "%s", luaL_checkstring(L, 2));
    return 0;
}

static int LUA_LogWarn(lua_State *L)
{
    luaL_checktype(L, 2, LUA_TSTRING);
    int n = lua_gettop(L) - 1;
    if (n > 1)
    {
        lua_getfield(L, 2, "format");
        lua_insert(L, 2);
        lua_call(L, n, 1);
    }
    SDL_LogWarn((int)luaL_checkinteger(L, 1), "%s", luaL_checkstring(L, 2));
    return 0;
}

static int LUA_LogError(lua_State *L)
{
    luaL_checktype(L, 2, LUA_TSTRING);
    int n = lua_gettop(L) - 1;
    if (n > 1)
    {
        lua_getfield(L, 2, "format");
        lua_insert(L, 2);
        lua_call(L, n, 1);
    }
    SDL_LogError((int)luaL_checkinteger(L, 1), "%s", luaL_checkstring(L, 2));
    return 0;
}

static int LUA_LogCritical(lua_State *L)
{
    luaL_checktype(L, 2, LUA_TSTRING);
    int n = lua_gettop(L) - 1;
    if (n > 1)
    {
        lua_getfield(L, 2, "format");
        lua_insert(L, 2);
        lua_call(L, n, 1);
    }
    SDL_LogCritical((int)luaL_checkinteger(L, 1), "%s", luaL_checkstring(L, 2));
    return 0;
}

static int LUA_LogMessage(lua_State *L)
{
    int category = (int)luaL_checkinteger(L, 1);
    int priority = (int)luaL_checkinteger(L, 2);
    luaL_checktype(L, 3, LUA_TSTRING);
    int n = lua_gettop(L) - 2;
    if (n > 1)
    {
        lua_getfield(L, 3, "format");
        lua_insert(L, 3);
        lua_call(L, n, 1);
    }
    const char *msg = luaL_checkstring(L, 3);

    SDL_LogMessage(category, priority, "%s", msg);
    return 0;
}
//SDL_LogMessageV
static lua_State *outL;
static int outR = LUA_REFNIL;
static void LogOutputFunction(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
}

int LUA_LogSetOutputFunction(lua_State *L)
{
    //  luaL_checktype(L, 1, LUA_TFUNCTION);
    //
    //  /* Remove old one if needed */
    //  if (loggingOutputFunc != LUA_REFNIL)
    //      luaL_unref(L, LUA_REGISTRYINDEX, loggingOutputFunc);
    //
    //  lua_pushvalue(L, 1);
    //  loggingOutputFunc = luaL_ref(L, LUA_REGISTRYINDEX);
    //
    //  SDL_LogSetOutputFunction((SDL_LogOutputFunction)loggingCustomOutput, L);
    //
    return 0;
}

//SDL_LogGetOutputFunction

static const luaL_Reg sdl_funcs[] = {
    {"LogSetAllPriority", LUA_LogSetAllPriority},
    {"LogSetPriority", LUA_LogSetPriority},
    {"LogGetPriority", LUA_LogGetPriority},
    {"LogResetPriorities", LUA_LogResetPriorities},
    {"Log", LUA_Log},
    {"LogVerbose", LUA_LogVerbose},
    {"LogDebug", LUA_LogDebug},
    {"LogInfo", LUA_LogInfo},
    {"LogWarn", LUA_LogWarn},
    {"LogError", LUA_LogError},
    {"LogCritical", LUA_LogCritical},
    {"LogMessage", LUA_LogMessage},
    {"LogSetOutputFunction", LUA_LogSetOutputFunction},
    //{"LogGetOutputFunction", LUA_LogGetOutputFunction},
    //{"LogMessageV", LUA_LogMessageV},

    {NULL, NULL}};

int bind_log(lua_State *L)
{

    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}
