#include "gge.h"
#include "SDL_joystick.h"
//TODO
static int LUA_JoystickIsHaptic(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickIsHaptic
    return 1;
}

static int LUA_HapticOpenFromJoystick(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_HapticOpenFromJoystick
    return 1;
}

static int LUA_LockJoysticks(lua_State *L)
{
    //SDL_LockJoysticks
    return 1;
}

static int LUA_UnlockJoysticks(lua_State *L)
{
    //SDL_UnlockJoysticks
    return 1;
}

static int LUA_NumJoysticks(lua_State *L)
{
    //SDL_NumJoysticks
    return 1;
}

static int LUA_JoystickNameForIndex(lua_State *L)
{
    //SDL_JoystickNameForIndex
    return 1;
}

static int LUA_JoystickGetDevicePlayerIndex(lua_State *L)
{
    //SDL_JoystickGetDevicePlayerIndex
    return 1;
}

static int LUA_JoystickGetDeviceGUID(lua_State *L)
{
    //SDL_JoystickGetDeviceGUID
    return 1;
}

static int LUA_JoystickGetDeviceVendor(lua_State *L)
{
    //SDL_JoystickGetDeviceVendor
    return 1;
}

static int LUA_JoystickGetDeviceProduct(lua_State *L)
{
    //SDL_JoystickGetDeviceProduct
    return 1;
}

static int LUA_JoystickGetDeviceProductVersion(lua_State *L)
{
    //SDL_JoystickGetDeviceProductVersion
    return 1;
}

static int LUA_JoystickGetDeviceType(lua_State *L)
{
    //SDL_JoystickGetDeviceType
    return 1;
}

static int LUA_JoystickGetDeviceInstanceID(lua_State *L)
{
    //SDL_JoystickGetDeviceInstanceID
    return 1;
}

static int LUA_JoystickOpen(lua_State *L)
{
    //SDL_JoystickOpen
    return 1;
}

static int LUA_JoystickFromInstanceID(lua_State *L)
{
    //SDL_JoystickFromInstanceID
    return 1;
}

static int LUA_JoystickFromPlayerIndex(lua_State *L)
{
    //SDL_JoystickFromPlayerIndex
    return 1;
}

static int LUA_JoystickName(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickName
    return 1;
}

static int LUA_JoystickGetPlayerIndex(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickGetPlayerIndex
    return 1;
}

static int LUA_JoystickSetPlayerIndex(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickSetPlayerIndex
    return 1;
}

static int LUA_JoystickGetGUID(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickGetGUID
    return 1;
}

static int LUA_JoystickGetVendor(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickGetVendor
    return 1;
}

static int LUA_JoystickGetProduct(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickGetProduct
    return 1;
}

static int LUA_JoystickGetProductVersion(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickGetProductVersion
    return 1;
}

static int LUA_JoystickGetType(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickGetType
    return 1;
}

static int LUA_JoystickGetGUIDString(lua_State *L)
{
    //SDL_JoystickGetGUIDString
    return 1;
}

static int LUA_JoystickGetGUIDFromString(lua_State *L)
{
    //SDL_JoystickGetGUIDFromString
    return 1;
}

static int LUA_JoystickGetAttached(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickGetAttached
    return 1;
}

static int LUA_JoystickInstanceID(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickInstanceID
    return 1;
}

static int LUA_JoystickNumAxes(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickNumAxes
    return 1;
}

static int LUA_JoystickNumBalls(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickNumBalls
    return 1;
}

static int LUA_JoystickNumHats(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickNumHats
    return 1;
}

static int LUA_JoystickNumButtons(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickNumButtons
    return 1;
}

static int LUA_JoystickUpdate(lua_State *L)
{
    //SDL_JoystickUpdate
    return 1;
}

static int LUA_JoystickEventState(lua_State *L)
{
    //SDL_JoystickEventState
    return 1;
}

static int LUA_JoystickGetAxis(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickGetAxis
    return 1;
}

static int LUA_JoystickGetAxisInitialState(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickGetAxisInitialState
    return 1;
}

static int LUA_JoystickGetHat(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickGetHat
    return 1;
}

static int LUA_JoystickGetBall(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickGetBall
    return 1;
}

static int LUA_JoystickGetButton(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickGetButton
    return 1;
}

static int LUA_JoystickRumble(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickRumble
    return 1;
}

static int LUA_JoystickClose(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickClose
    return 1;
}

static int LUA_JoystickCurrentPowerLevel(lua_State *L)
{
    SDL_Joystick *ud = *(SDL_Joystick **)luaL_checkudata(L, 1, "SDL_Joystick");
    //SDL_JoystickCurrentPowerLevel
    return 1;
}

static const luaL_Reg sdl_funcs[] = {

    {NULL, NULL}};

int bind_joystick(lua_State *L)
{
    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}