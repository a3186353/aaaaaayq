#include "gge.h"
#include "SDL_sensor.h"

static int LUA_LockSensors(lua_State *L)
{
    SDL_LockSensors();
    return 0;
}

static int LUA_UnlockSensors(lua_State *L)
{
    SDL_UnlockSensors();
    return 0;
}

static int LUA_NumSensors(lua_State *L)
{
    lua_pushinteger(L, SDL_NumSensors());
    return 1;
}

static int LUA_SensorGetDeviceName(lua_State *L)
{
    int index = (int)luaL_checkinteger(L, 1);
    lua_pushstring(L, SDL_SensorGetDeviceName(index));
    return 1;
}

static int LUA_SensorGetDeviceType(lua_State *L)
{
    int index = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, SDL_SensorGetDeviceType(index));
    return 1;
}

static int LUA_SensorGetDeviceNonPortableType(lua_State *L)
{
    int index = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, SDL_SensorGetDeviceNonPortableType(index));
    return 1;
}

static int LUA_SensorGetDeviceInstanceID(lua_State *L)
{
    int index = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, SDL_SensorGetDeviceInstanceID(index));
    return 1;
}

static int LUA_SensorOpen(lua_State *L)
{
    int index = (int)luaL_checkinteger(L, 1);
    SDL_Sensor *se = SDL_SensorOpen(index);
    if (se)
    {
        SDL_Sensor **ud = (SDL_Sensor **)lua_newuserdata(L, sizeof(SDL_Sensor *));
        *ud = se;
        luaL_setmetatable(L, "SDL_Sensor");
        return 1;
    }
    return 0;
}

static int LUA_SensorFromInstanceID(lua_State *L)
{
    SDL_SensorID id = (SDL_SensorID)luaL_checkinteger(L, 1);
    SDL_Sensor *se = SDL_SensorFromInstanceID(id);
    if (se)
    {
        SDL_Sensor **ud = (SDL_Sensor **)lua_newuserdata(L, sizeof(SDL_Sensor *));
        *ud = se;
        luaL_setmetatable(L, "SDL_Sensor");
        return 1;
    }
    return 0;
}

static int LUA_SensorGetName(lua_State *L)
{
    SDL_Sensor *se = *(SDL_Sensor **)luaL_checkudata(L, 1, "SDL_Sensor");
    lua_pushstring(L, SDL_SensorGetName(se));
    return 1;
}

static int LUA_SensorGetType(lua_State *L)
{
    SDL_Sensor *se = *(SDL_Sensor **)luaL_checkudata(L, 1, "SDL_Sensor");
    lua_pushinteger(L, SDL_SensorGetType(se));
    return 1;
}

static int LUA_SensorGetNonPortableType(lua_State *L)
{
    SDL_Sensor *se = *(SDL_Sensor **)luaL_checkudata(L, 1, "SDL_Sensor");
    lua_pushinteger(L, SDL_SensorGetNonPortableType(se));
    return 1;
}

static int LUA_SensorGetInstanceID(lua_State *L)
{
    SDL_Sensor *se = *(SDL_Sensor **)luaL_checkudata(L, 1, "SDL_Sensor");
    lua_pushinteger(L, SDL_SensorGetInstanceID(se));
    return 1;
}

static int LUA_SensorGetData(lua_State *L)
{
    SDL_Sensor *se = *(SDL_Sensor **)luaL_checkudata(L, 1, "SDL_Sensor");
    int n = (int)luaL_checkinteger(L, 2);
    float *data = (float *)SDL_calloc(n, sizeof(float));
    if (SDL_SensorGetData(se, data, n) == 0)
    {
        lua_createtable(L, n, 0);
        for (int i = 0; i < n; i++)
        {
            lua_pushnumber(L, data[i]);
            lua_seti(L, -2, i + 1);
        }
        SDL_free(data);
        return 1;
    }
    return 0;
}

static int LUA_SensorClose(lua_State *L)
{
    SDL_Sensor *se = *(SDL_Sensor **)luaL_checkudata(L, 1, "SDL_Sensor");
    SDL_SensorClose(se);
    return 0;
}

static int LUA_SensorUpdate(lua_State *L)
{
    SDL_SensorUpdate();
    return 0;
}

static const luaL_Reg sdl_funcs[] = {

    {NULL, NULL}};

int bind_sensor(lua_State *L)
{
    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}