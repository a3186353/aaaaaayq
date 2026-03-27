#include "gge.h"

static int LUA_Init(lua_State* L)
{
    Uint32 flags = (int)luaL_optinteger(L, 1, SDL_INIT_EVERYTHING);
    lua_pushboolean(L, SDL_Init(flags) == 0);
    return 1;
}

static int LUA_InitSubSystem(lua_State* L)
{
    Uint32 flags = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, SDL_InitSubSystem(flags) == 0);
    return 1;
}

static int LUA_QuitSubSystem(lua_State* L)
{
    Uint32 flags = (int)luaL_checkinteger(L, 1);
    SDL_QuitSubSystem(flags);
    return 0;
}

static int LUA_WasInit(lua_State* L)
{
    Uint32 flags = (Uint32)luaL_optinteger(L, 1, 0);
    lua_pushinteger(L, SDL_WasInit(flags));
    return 1;
}

static int LUA_Quit(lua_State* L)
{
    SDL_Quit();
    return 0;
}

LUALIB_API int luaopen_gsdl2(lua_State* L)
{
    static const luaL_Reg sdl_funcs[] = {
    {"Init", LUA_Init},
    {"InitSubSystem", LUA_InitSubSystem},
    {"Quit", LUA_Quit},
    {"QuitSubSystem", LUA_QuitSubSystem},
    {"WasInit", LUA_WasInit},
    {NULL, NULL}
    };
#ifdef _DEBUG
    setvbuf(stdout, NULL, _IONBF, 0);
#endif // DEBUG
    luaL_newmetatable(L, "SDL_Window");
    lua_pop(L, 1);
    luaL_newmetatable(L, "SDL_Surface");
    lua_pop(L, 1);
    luaL_newmetatable(L, "SDL_Texture");
    lua_pop(L, 1);
    luaL_newmetatable(L, "SDL_Renderer");
    lua_pop(L, 1);

    luaL_newlib(L, sdl_funcs);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "SDL");

    bind_gge(L);

    bind_audio(L);
    bind_blendmode(L);
    bind_clipboard(L);
    bind_cpuinfo(L);
    bind_error(L);
    bind_events(L); //SDL_Event
    bind_filesystem(L);
    bind_gamecontroller(L);
    bind_gesture(L);
    bind_haptic(L);
    bind_hints(L);
    bind_joystick(L);
    bind_keyboard(L);
    //bind_locale(L);
    bind_log(L);
    bind_messagebox(L);
    bind_misc(L);
    bind_mouse(L);
    bind_pixels(L); //SDL_PixelFormat
    bind_platform(L);
    bind_power(L);
    bind_rect(L);     //SDL_Rect
    bind_renderer(L); //SDL_Renderer/SDL_Texture
    bind_rwops(L);
    bind_sensor(L);
    bind_shape(L);
    bind_stdinc(L);
    bind_surface(L); //SDL_Surface
    bind_system(L);
    bind_syswm(L);
    bind_thread(L);
    bind_timer(L);
    bind_touch(L);
    bind_version(L);
    bind_video(L); //SDL_Window
    return 1;
}