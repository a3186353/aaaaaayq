#include "gge.h"
#include "SDL_gamecontroller.h"
//TODO
static int LUA_GameControllerAddMappingsFromRW(lua_State *L)
{
    SDL_RWops *rw = *(SDL_RWops **)luaL_checkudata(L, 1, "SDL_RWops");
    int r = SDL_GameControllerAddMappingsFromRW(rw, SDL_FALSE);
    lua_pushinteger(L, r);
    return 1;
}

static int LUA_GameControllerAddMappingsFromFile(lua_State *L)
{
    const char *file = luaL_checkstring(L, 1);
    int r = SDL_GameControllerAddMappingsFromFile(file);
    lua_pushinteger(L, r);
    return 1;
}

static int LUA_GameControllerAddMapping(lua_State *L)
{
    const char *str = luaL_checkstring(L, 1);
    int r = SDL_GameControllerAddMapping(str);
    lua_pushinteger(L, r);
    return 1;
}

static int LUA_GameControllerNumMappings(lua_State *L)
{
    int r = SDL_GameControllerNumMappings();
    lua_pushinteger(L, r);
    return 1;
}

static int LUA_GameControllerMappingForIndex(lua_State *L)
{
    int index = (int)luaL_checkinteger(L, 1);
    char *r = SDL_GameControllerMappingForIndex(index);
    if (r)
    {
        lua_pushstring(L, r);
        SDL_free(r);
        return 1;
    }
    return 0;
}

static int LUA_GameControllerMappingForGUID(lua_State *L)
{
    //SDL_JoystickGUID id = (SDL_JoystickGUID)luaL_checkinteger(L, 1);
    //char* r = SDL_GameControllerMappingForGUID(id);
    //if (r) {
    //  lua_pushstring(L, r);
    //  SDL_free(r);
    //  return 1;
    //}
    return 0;
}

static int LUA_GameControllerMapping(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    char *r = SDL_GameControllerMapping(gc);
    if (r)
    {
        lua_pushstring(L, r);
        SDL_free(r);
        return 1;
    }
    return 0;
}

static int LUA_IsGameController(lua_State *L)
{
    int index = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, SDL_IsGameController(index));

    return 1;
}

static int LUA_GameControllerNameForIndex(lua_State *L)
{
    int index = (int)luaL_checkinteger(L, 1);
    const char *r = SDL_GameControllerNameForIndex(index);
    if (r)
    {
        lua_pushstring(L, r);
        return 1;
    }
    return 0;
}

static int LUA_GameControllerTypeForIndex(lua_State *L)
{
    int index = (int)luaL_checkinteger(L, 1);
    SDL_GameControllerType r = SDL_GameControllerTypeForIndex(index);
    lua_pushinteger(L, r);
    return 1;
}

static int LUA_GameControllerMappingForDeviceIndex(lua_State *L)
{
    int index = (int)luaL_checkinteger(L, 1);
    char *r = SDL_GameControllerMappingForDeviceIndex(index);
    if (r)
    {
        lua_pushstring(L, r);
        SDL_free(r);
        return 1;
    }
    return 0;
}

static int LUA_GameControllerOpen(lua_State *L)
{
    int index = (int)luaL_checkinteger(L, 1);
    SDL_GameController *gc = SDL_GameControllerOpen(index);
    if (gc)
    {
        SDL_GameController **ud = (SDL_GameController **)lua_newuserdata(L, sizeof(SDL_GameController *));
        *ud = gc;
        luaL_setmetatable(L, "SDL_GameController");
        return 1;
    }
    return 0;
}

static int LUA_GameControllerFromInstanceID(lua_State *L)
{
    SDL_JoystickID id = (SDL_JoystickID)luaL_checkinteger(L, 1);
    SDL_GameController *gc = SDL_GameControllerFromInstanceID(id);
    if (gc)
    {
        SDL_GameController **ud = (SDL_GameController **)lua_newuserdata(L, sizeof(SDL_GameController *));
        *ud = gc;
        luaL_setmetatable(L, "SDL_GameController");
        return 1;
    }
    return 0;
}

static int LUA_GameControllerFromPlayerIndex(lua_State *L)
{
    int index = (int)luaL_checkinteger(L, 1);
    SDL_GameController *gc = SDL_GameControllerFromPlayerIndex(index);
    if (gc)
    {
        SDL_GameController **ud = (SDL_GameController **)lua_newuserdata(L, sizeof(SDL_GameController *));
        *ud = gc;
        luaL_setmetatable(L, "SDL_GameController");
        return 1;
    }
    return 0;
}

static int LUA_GameControllerName(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    lua_pushstring(L, SDL_GameControllerName(gc));
    return 1;
}

static int LUA_GameControllerGetType(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    lua_pushinteger(L, SDL_GameControllerGetType(gc));
    return 1;
}

static int LUA_GameControllerGetPlayerIndex(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    lua_pushinteger(L, SDL_GameControllerGetPlayerIndex(gc));
    return 1;
}

static int LUA_GameControllerSetPlayerIndex(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    int index = (int)luaL_checkinteger(L, 2);
    SDL_GameControllerSetPlayerIndex(gc, index);
    return 1;
}

static int LUA_GameControllerGetVendor(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    lua_pushinteger(L, SDL_GameControllerGetVendor(gc));
    return 1;
}

static int LUA_GameControllerGetProduct(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    lua_pushinteger(L, SDL_GameControllerGetProduct(gc));
    return 1;
}

static int LUA_GameControllerGetProductVersion(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    lua_pushinteger(L, SDL_GameControllerGetProductVersion(gc));
    return 1;
}

static int LUA_GameControllerGetSerial(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    const char *r = SDL_GameControllerGetSerial(gc);
    if (r)
    {
        lua_pushstring(L, r);
        return 1;
    }
    return 0;
}

static int LUA_GameControllerGetAttached(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    lua_pushboolean(L, SDL_GameControllerGetAttached(gc));
    return 1;
}

static int LUA_GameControllerGetJoystick(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    SDL_Joystick *js = SDL_GameControllerGetJoystick(gc);
    if (js)
    {
        SDL_Joystick **ud = (SDL_Joystick **)lua_newuserdata(L, sizeof(SDL_Joystick *));
        *ud = js;
        luaL_setmetatable(L, "SDL_Joystick");
        return 1;
    }
    return 0;
}

static int LUA_GameControllerEventState(lua_State *L)
{
    int s = (int)luaL_checkinteger(L, 1); //SDL_QUERY
    lua_pushinteger(L, SDL_GameControllerEventState(s));
    return 1;
}

static int LUA_GameControllerUpdate(lua_State *L)
{
    SDL_GameControllerUpdate();
    return 1;
}

static int LUA_GameControllerGetAxisFromString(lua_State *L)
{
    const char *s = luaL_checkstring(L, 1);
    lua_pushinteger(L, SDL_GameControllerGetAxisFromString(s));
    return 1;
}

static int LUA_GameControllerGetStringForAxis(lua_State *L)
{
    SDL_GameControllerAxis axis = (SDL_GameControllerAxis)luaL_checkinteger(L, 1);
    const char *r = SDL_GameControllerGetStringForAxis(axis);
    lua_pushstring(L, r);
    return 1;
}

static int LUA_GameControllerGetBindForAxis(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    SDL_GameControllerAxis axis = (SDL_GameControllerAxis)luaL_checkinteger(L, 2);
    SDL_GameControllerButtonBind r = SDL_GameControllerGetBindForAxis(gc, axis);
    lua_pushlightuserdata(L, &r);
    return 1;
}

static int LUA_GameControllerHasAxis(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    SDL_GameControllerAxis axis = (SDL_GameControllerAxis)luaL_checkinteger(L, 2);
    lua_pushboolean(L, SDL_GameControllerHasAxis(gc, axis));
    return 1;
}

static int LUA_GameControllerGetAxis(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    SDL_GameControllerAxis axis = (SDL_GameControllerAxis)luaL_checkinteger(L, 2);
    lua_pushinteger(L, SDL_GameControllerGetAxis(gc, axis));
    return 1;
}

static int LUA_GameControllerGetButtonFromString(lua_State *L)
{
    const char *str = luaL_checkstring(L, 1);
    lua_pushinteger(L, SDL_GameControllerGetButtonFromString(str));
    return 1;
}

static int LUA_GameControllerGetStringForButton(lua_State *L)
{
    SDL_GameControllerButton b = (SDL_GameControllerButton)luaL_checkinteger(L, 1);
    const char *r = SDL_GameControllerGetStringForButton(b);
    lua_pushstring(L, r);
    return 1;
}

static int LUA_GameControllerGetBindForButton(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    SDL_GameControllerButton b = (SDL_GameControllerButton)luaL_checkinteger(L, 2);
    SDL_GameControllerButtonBind r =
        SDL_GameControllerGetBindForButton(gc, b);
    lua_pushlightuserdata(L, &r);
    return 1;
}

static int LUA_GameControllerHasButton(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    SDL_GameControllerButton b = (SDL_GameControllerButton)luaL_checkinteger(L, 2);
    lua_pushboolean(L, SDL_GameControllerHasButton(gc, b));
    return 1;
}

static int LUA_GameControllerGetButton(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    SDL_GameControllerButton b = (SDL_GameControllerButton)luaL_checkinteger(L, 2);
    lua_pushinteger(L, SDL_GameControllerGetButton(gc, b));
    return 1;
}

static int LUA_GameControllerGetNumTouchpads(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    lua_pushinteger(L, SDL_GameControllerGetNumTouchpads(gc));
    return 1;
}

static int LUA_GameControllerGetNumTouchpadFingers(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    int touchpad = (int)luaL_checkinteger(L, 2);
    lua_pushinteger(L, SDL_GameControllerGetNumTouchpadFingers(gc, touchpad));
    return 1;
}

static int LUA_GameControllerGetTouchpadFinger(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    int touchpad = (int)luaL_checkinteger(L, 2);
    int finger = (int)luaL_checkinteger(L, 3);
    Uint8 state;
    float x, y, pressure;
    lua_pushinteger(L, SDL_GameControllerGetTouchpadFinger(gc, touchpad, finger, &state, &x, &y, &pressure));
    lua_pushinteger(L, state);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, pressure);
    return 4;
}

static int LUA_GameControllerHasSensor(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    SDL_SensorType type = (SDL_SensorType)luaL_checkinteger(L, 2);
    lua_pushboolean(L, SDL_GameControllerHasSensor(gc, type));
    return 1;
}

static int LUA_GameControllerSetSensorEnabled(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    SDL_SensorType type = (SDL_SensorType)luaL_checkinteger(L, 2);
    SDL_bool enabled = (SDL_bool)lua_toboolean(L, 3);
    lua_pushboolean(L, SDL_GameControllerSetSensorEnabled(gc, type, enabled) == 0);
    return 1;
}

static int LUA_GameControllerIsSensorEnabled(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    SDL_SensorType type = (SDL_SensorType)luaL_checkinteger(L, 2);
    lua_pushboolean(L, SDL_GameControllerIsSensorEnabled(gc, type));
    return 1;
}

static int LUA_GameControllerGetSensorData(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    SDL_SensorType type = (SDL_SensorType)luaL_checkinteger(L, 2);
    int num = (int)luaL_checkinteger(L, 3);
    float *data = (float *)SDL_calloc(num, sizeof(float));
    if (SDL_GameControllerGetSensorData(gc, type, data, num) == 0)
    {
        lua_createtable(L, num, 0);
        for (int n = 0; n < num; n++)
        {
            lua_pushnumber(L, data[n]);
            lua_seti(L, -2, n + 1);
        }
        SDL_free(data);
        return 1;
    }

    SDL_free(data);
    return 0;
}

static int LUA_GameControllerRumble(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    Uint16 lf = (Uint16)luaL_checkinteger(L, 2);
    Uint16 hf = (Uint16)luaL_checkinteger(L, 3);
    Uint32 ms = (Uint32)luaL_checkinteger(L, 4);
    lua_pushboolean(L, SDL_GameControllerRumble(gc, lf, hf, ms) == 0);
    return 1;
}

static int LUA_GameControllerRumbleTriggers(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    Uint16 lf = (Uint16)luaL_checkinteger(L, 2);
    Uint16 hf = (Uint16)luaL_checkinteger(L, 3);
    Uint32 ms = (Uint32)luaL_checkinteger(L, 4);
    lua_pushboolean(L, SDL_GameControllerRumbleTriggers(gc, lf, hf, ms) == 0);
    return 1;
}

static int LUA_GameControllerHasLED(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    lua_pushboolean(L, SDL_GameControllerHasLED(gc));
    return 1;
}

static int LUA_GameControllerSetLED(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    Uint8 red = (Uint8)luaL_checkinteger(L, 2);
    Uint8 green = (Uint8)luaL_checkinteger(L, 3);
    Uint8 blue = (Uint8)luaL_checkinteger(L, 4);
    lua_pushboolean(L, SDL_GameControllerSetLED(gc, red, green, blue) == 0);
    return 1;
}

static int LUA_GameControllerClose(lua_State *L)
{
    SDL_GameController *gc = *(SDL_GameController **)luaL_checkudata(L, 1, "SDL_GameController");
    SDL_GameControllerClose(gc);
    return 0;
}

static const luaL_Reg sdl_funcs[] = {

    {NULL, NULL}};

int bind_gamecontroller(lua_State *L)
{

    //  luaL_setfuncs(L,sdl_funcs,0);
    return 0;
}