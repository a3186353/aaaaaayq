#include "gge.h"
#include "SDL_haptic.h"
//TODO
static int LUA_NumHaptics(lua_State *L)
{

    //SDL_NumHaptics
    return 1;
}

static int LUA_HapticName(lua_State *L)
{

    //SDL_HapticName
    return 1;
}

static int LUA_HapticOpen(lua_State *L)
{

    //SDL_HapticOpen
    return 1;
}

static int LUA_HapticOpened(lua_State *L)
{

    //SDL_HapticOpened
    return 1;
}

static int LUA_HapticIndex(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticIndex
    return 1;
}

static int LUA_MouseIsHaptic(lua_State *L)
{

    //SDL_MouseIsHaptic
    return 1;
}

static int LUA_HapticOpenFromMouse(lua_State *L)
{

    //SDL_HapticOpenFromMouse
    return 1;
}

static int LUA_HapticClose(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticClose
    return 1;
}

static int LUA_HapticNumEffects(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticNumEffects
    return 1;
}

static int LUA_HapticNumEffectsPlaying(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticNumEffectsPlaying
    return 1;
}

static int LUA_HapticQuery(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticQuery
    return 1;
}

static int LUA_HapticNumAxes(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticNumAxes
    return 1;
}

static int LUA_HapticEffectSupported(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticEffectSupported
    return 1;
}

static int LUA_HapticNewEffect(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticNewEffect
    return 1;
}

static int LUA_HapticUpdateEffect(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticUpdateEffect
    return 1;
}

static int LUA_HapticRunEffect(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticRunEffect
    return 1;
}

static int LUA_HapticStopEffect(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticStopEffect
    return 1;
}

static int LUA_HapticDestroyEffect(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticDestroyEffect
    return 1;
}

static int LUA_HapticGetEffectStatus(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticGetEffectStatus
    return 1;
}

static int LUA_HapticSetGain(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticSetGain
    return 1;
}

static int LUA_HapticSetAutocenter(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticSetAutocenter
    return 1;
}

static int LUA_HapticPause(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticPause
    return 1;
}

static int LUA_HapticUnpause(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticUnpause
    return 1;
}

static int LUA_HapticStopAll(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticStopAll
    return 1;
}

static int LUA_HapticRumbleSupported(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticRumbleSupported
    return 1;
}

static int LUA_HapticRumbleInit(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticRumbleInit
    return 1;
}

static int LUA_HapticRumblePlay(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticRumblePlay
    return 1;
}

static int LUA_HapticRumbleStop(lua_State *L)
{
    //SDL_Haptic * ud = *(SDL_Haptic**)luaL_checkudata(L, 1, "SDL_Haptic");
    //SDL_HapticRumbleStop
    return 1;
}

static const luaL_Reg sdl_funcs[] = {

    {NULL, NULL}};

int bind_haptic(lua_State *L)
{
    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}