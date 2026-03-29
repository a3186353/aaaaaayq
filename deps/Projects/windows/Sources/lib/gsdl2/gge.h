#ifndef _GGE_H_
#define _GGE_H_

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include <SDL.h>

typedef struct
{
    SDL_Surface* sf;
    int refcount;
} GGE_Texture;

typedef struct
{
    int type;
    void* ptr;
    int len;
    int free;
} GGE_Memory;

int bind_gge(lua_State* L);
int bind_video(lua_State* L);
int bind_renderer(lua_State* L);
int bind_surface(lua_State* L);

int bind_audio(lua_State* L);
int bind_blendmode(lua_State* L);
int bind_clipboard(lua_State* L);
int bind_cpuinfo(lua_State* L);
int bind_error(lua_State* L);
int bind_events(lua_State* L);
int bind_filesystem(lua_State* L);
int bind_gamecontroller(lua_State* L);
int bind_gesture(lua_State* L);
int bind_haptic(lua_State* L);
int bind_hints(lua_State* L);
int bind_joystick(lua_State* L);
int bind_keyboard(lua_State* L);
//int bind_locale(lua_State* L); // 已弃用: locale.c 未实现
int bind_log(lua_State* L);
int bind_messagebox(lua_State* L);
//int bind_metal(lua_State* L);
int bind_misc(lua_State* L);
int bind_mouse(lua_State* L);
int bind_pixels(lua_State* L);
int bind_platform(lua_State* L);
int bind_power(lua_State* L);
int bind_rect(lua_State* L);
int bind_rwops(lua_State* L);
int bind_sensor(lua_State* L);
int bind_shape(lua_State* L);
int bind_stdinc(lua_State* L);
int bind_system(lua_State* L);
int bind_syswm(lua_State* L);
int bind_thread(lua_State* L);
int bind_timer(lua_State* L);
int bind_touch(lua_State* L);
int bind_version(lua_State* L);

SDL_Surface* GGE_SurfaceAlphaToSurface(SDL_Surface* sf, int addpal);
#endif
