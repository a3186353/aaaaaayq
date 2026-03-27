#include "gge.h"
#include "SDL_cpuinfo.h"

static int LUA_GetCPUCount(lua_State *L)
{
    lua_pushinteger(L, SDL_GetCPUCount());
    return 1;
}

static int LUA_GetCPUCacheLineSize(lua_State *L)
{
    lua_pushinteger(L, SDL_GetCPUCacheLineSize());
    return 1;
}

static int LUA_HasRDTSC(lua_State *L)
{
    lua_pushboolean(L, SDL_HasRDTSC());
    return 1;
}

static int LUA_HasAltiVec(lua_State *L)
{
    lua_pushboolean(L, SDL_HasAltiVec());
    return 1;
}

static int LUA_HasMMX(lua_State *L)
{
    lua_pushboolean(L, SDL_HasMMX());
    return 1;
}

static int LUA_Has3DNow(lua_State *L)
{
    lua_pushboolean(L, SDL_Has3DNow());
    return 1;
}

static int LUA_HasSSE(lua_State *L)
{
    lua_pushboolean(L, SDL_HasSSE());
    return 1;
}

static int LUA_HasSSE2(lua_State *L)
{
    lua_pushboolean(L, SDL_HasSSE2());
    return 1;
}

static int LUA_HasSSE3(lua_State *L)
{
    lua_pushboolean(L, SDL_HasSSE3());
    return 1;
}

static int LUA_HasSSE41(lua_State *L)
{
    lua_pushboolean(L, SDL_HasSSE41());
    return 1;
}

static int LUA_HasSSE42(lua_State *L)
{
    lua_pushboolean(L, SDL_HasSSE42());
    return 1;
}

static int LUA_HasAVX(lua_State *L)
{
    lua_pushboolean(L, SDL_HasAVX());
    return 1;
}

static int LUA_HasAVX2(lua_State *L)
{
    lua_pushboolean(L, SDL_HasAVX2());
    return 1;
}

static int LUA_HasAVX512F(lua_State *L)
{
    lua_pushboolean(L, SDL_HasAVX512F());
    return 1;
}

static int LUA_HasARMSIMD(lua_State *L)
{
    lua_pushboolean(L, SDL_HasARMSIMD());
    return 1;
}

static int LUA_HasNEON(lua_State *L)
{
    lua_pushboolean(L, SDL_HasNEON());
    return 1;
}

static int LUA_GetSystemRAM(lua_State *L)
{
    lua_pushinteger(L, SDL_GetSystemRAM());
    return 1;
}

static const luaL_Reg sdl_funcs[] = {

    {NULL, NULL}};

int bind_cpuinfo(lua_State *L)
{
    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}