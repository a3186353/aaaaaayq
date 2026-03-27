#include "../luaopen.h"
#include "uuid4.h"
static int lua_generate(lua_State* L)
{
    UUID4_STATE_T state;
    UUID4_T uuid;

    uuid4_seed(&state);
    uuid4_gen(&state, &uuid);

    char buffer[UUID4_STR_BUFFER_SIZE]; //luaL_Buffer
    if (!uuid4_to_s(uuid, buffer, sizeof(buffer)))
        return 0;

    lua_pushstring(L, buffer);

    return 1;
}

static const luaL_Reg fun_list[] = {
    {"generate", lua_generate},
    {NULL, NULL},
};

int luaopen_uuid(lua_State* L)
{
    luaL_newlib(L, fun_list);
    return 1;
}