// help.cpp - Helper utilities for gimgui Lua bindings
#include "help.h"

// Utility: push ImVec2 as two return values
void lua_pushimvec2(lua_State* L, const ImVec2& v) {
    lua_pushnumber(L, v.x);
    lua_pushnumber(L, v.y);
}

// Utility: get ImVec2 from stack
ImVec2 lua_toimvec2(lua_State* L, int idx) {
    ImVec2 v;
    v.x = (float)luaL_checknumber(L, idx);
    v.y = (float)luaL_checknumber(L, idx + 1);
    return v;
}
