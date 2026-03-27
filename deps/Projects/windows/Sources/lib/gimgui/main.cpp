// main.cpp - gimgui module entry point
// This is loaded via: require 'gimgui'

#include "help.h"

extern "C" {

#ifdef _WIN32
__declspec(dllexport)
#endif
int luaopen_gimgui(lua_State* L) {
    lua_newtable(L);
    
    // Register ImGui bindings
    bind_lua_imgui(L);
    
    // Register DrawList bindings
    bind_lua_drawlist(L);
    
    return 1;
}

} // extern "C"
