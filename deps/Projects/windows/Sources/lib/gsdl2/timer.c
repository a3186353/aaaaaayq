#include "gge.h"
#include "SDL_timer.h"

static int LUA_GetTicks(lua_State* L)
{
    lua_pushnumber(L, SDL_GetTicks());
    return 1;
}

static int LUA_GetTicks64(lua_State* L)
{
    lua_pushnumber(L, (lua_Number)SDL_GetTicks64());
    return 1;
}

static int LUA_GetPerformanceCounter(lua_State* L)
{
    lua_pushnumber(L, (lua_Number)SDL_GetPerformanceCounter());
    return 1;
}

static int LUA_GetPerformanceFrequency(lua_State* L)
{
    lua_pushnumber(L, (lua_Number)SDL_GetPerformanceFrequency());
    return 1;
}

static int LUA_Delay(lua_State* L)
{
    SDL_Delay((int)lua_tointeger(L, 1));
    return 0;
}

typedef struct _TimeInfo
{
    lua_State* L;
    int cb;
    SDL_TimerID id;
    //struct _TimeInfo* next;
} TimeInfo;

static Uint32 LUA_TimerCallback(Uint32 interval, void* param)
{
    TimeInfo* info = (TimeInfo*)param;
    SDL_mutex* mutex = *(SDL_mutex**)lua_getextraspace(info->L);
    Uint32 ret = 0;

    if (SDL_LockMutex(mutex) == 0)
    {
        lua_geti(info->L, LUA_REGISTRYINDEX, info->cb);
        lua_pushinteger(info->L, interval);
        if (lua_pcall(info->L, 1, 1, 0) != LUA_OK)
            printf("%s\n", lua_tostring(info->L, -1));
        else
            ret = (Uint32)lua_tointeger(info->L, -1);
        lua_pop(info->L, 1);
        SDL_UnlockMutex(mutex);
    }
    return ret;
}

static int LUA_AddTimer(lua_State* L)
{
    Uint32 interval = (Uint32)lua_tointeger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    TimeInfo* info = (TimeInfo*)SDL_malloc(sizeof(TimeInfo));
    info->L = L;
    info->cb = luaL_ref(L, LUA_REGISTRYINDEX);
    info->id = SDL_AddTimer(interval, LUA_TimerCallback, info);

    TimeInfo** ud = (TimeInfo**)lua_newuserdata(L, sizeof(TimeInfo*));
    *ud = info;
    luaL_setmetatable(L, "SDL_Timer");
    return 1;
}

static int LUA_RemoveTimer(lua_State* L)
{
    TimeInfo** ud = (TimeInfo**)luaL_checkudata(L, 1, "SDL_Timer");
    if (*ud)
    {
        lua_pushboolean(L, SDL_RemoveTimer((*ud)->id));
        luaL_unref(L, LUA_REGISTRYINDEX, (*ud)->cb);
        SDL_free(*ud);
        *ud = NULL;
        return 1;
    }
    return 0;
}

static const luaL_Reg sdl_funcs[] = {
    {"Delay", LUA_Delay},
    {"GetPerformanceCounter", LUA_GetPerformanceCounter},
    {"GetPerformanceFrequency", LUA_GetPerformanceFrequency},
    {"GetTicks", LUA_GetTicks},
    {"GetTicks64", LUA_GetTicks64},
    {"AddTimer", LUA_AddTimer},
    {"RemoveTimer", LUA_RemoveTimer},
    {NULL, NULL} };

int bind_timer(lua_State* L)
{
    luaL_newmetatable(L, "SDL_Timer");
    //lua_pushcfunction(L, LUA_RemoveTimer);
    //lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}