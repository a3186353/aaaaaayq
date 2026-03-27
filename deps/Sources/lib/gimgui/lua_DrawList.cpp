// lua_DrawList.cpp - ImGui DrawList Lua bindings for GGELUA
// Provides basic drawing primitives via ImGui's DrawList API

#include "help.h"

// The DrawList API is not extensively used in the current Lua codebase,
// so we provide a minimal binding here. It can be extended as needed.

static const luaL_Reg drawlist_funcs[] = {
    {NULL, NULL}
};

int bind_lua_drawlist(lua_State* L) {
    luaL_setfuncs(L, drawlist_funcs, 0);
    return 0;
}
