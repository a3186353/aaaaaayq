extern "C" {
    #include "../luaopen.h"
}
#include "nanoid/nanoid.h"

static int lua_generate(lua_State* L)
{
    std::string id;
    if (lua_gettop(L) > 0 && lua_isinteger(L, 1)) {
        size_t size = (size_t)lua_tointeger(L, 1);
        id = nanoid::generate(size);
    } else if (lua_gettop(L) > 0 && lua_isnumber(L, 1)) {
        size_t size = (size_t)lua_tonumber(L, 1);
        id = nanoid::generate(size);
    } else {
        id = nanoid::generate();
    }
    lua_pushstring(L, id.c_str());
    return 1;
}

static const luaL_Reg fun_list[] = {
    {"generate", lua_generate},
    {NULL, NULL},
};

extern "C"
int luaopen_nanoid(lua_State* L)
{
    luaL_newlib(L, fun_list);
    return 1;
}