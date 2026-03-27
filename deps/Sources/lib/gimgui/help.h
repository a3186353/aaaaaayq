// help.h - Helper utilities for gimgui Lua bindings
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#ifdef __cplusplus
}
#endif

#include "imgui/imgui.h"
#include "SDL.h"

// Register ImGui Lua bindings
int bind_lua_imgui(lua_State* L);
// Register DrawList Lua bindings  
int bind_lua_drawlist(lua_State* L);
