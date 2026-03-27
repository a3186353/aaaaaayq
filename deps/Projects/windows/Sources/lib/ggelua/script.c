#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "SDL_rwops.h"
#include "luaopen.h"

typedef struct _LISTINFO
{
    char path[256]; //路径
    Uint32 hash;    //哈希
    Uint32 offset;  //偏移
    Uint32 len;     //长度
} LISTINFO;

typedef struct _PACKINFO
{
    LISTINFO *list; //脚本列表
    Uint32 num;     //数量
    SDL_RWops *rw;  //SDL_RWFromConstMem
} PACKINFO;

//读取脚本
static int LUA_GetScriptData(lua_State *L)
{
    const char *file = luaL_checkstring(L, 1);
    Uint32 hash = g_tohash(file);
    PACKINFO *info = (PACKINFO *)lua_touserdata(L, lua_upvalueindex(1));

    for (Uint32 i = 0; i < info->num; i++)
    {
        if (info->list[i].hash == hash)
        {
            if (SDL_RWseek(info->rw, info->list[i].offset, RW_SEEK_SET) == -1)
                return 0;
            luaL_Buffer b;
            char *ptr = luaL_buffinitsize(L, &b, info->list[i].len);
            if (SDL_RWread(info->rw, ptr, info->list[i].len, 1) != 0)
            {
                luaL_pushresultsize(&b, info->list[i].len);
                return 1;
            }
            else
                return 0;
        }
    }

    return 0;
}

//取所有路径，用于遍历
static int LUA_GetScriptList(lua_State *L)
{
    PACKINFO *info = (PACKINFO *)lua_touserdata(L, lua_upvalueindex(1));
    lua_createtable(L, info->num, 0);
    for (Uint32 i = 0; i < info->num; i++)
    {
        lua_pushstring(L, info->list[i].path);
        lua_seti(L, -2, i + 1);
    }
    return 1;
}

static int LUA_FreeScript(lua_State *L)
{
    PACKINFO *info = (PACKINFO *)lua_touserdata(L, lua_upvalueindex(1));
    SDL_RWclose(info->rw);
    SDL_free(info->list);
    return 0;
}

static const luaL_Reg fun_list[] = {
    {"getdata", LUA_GetScriptData},
    {"getlist", LUA_GetScriptList},
    {"__gc", LUA_FreeScript},
    {NULL, NULL},
};

int luaopen_ggescript(lua_State *L)
{
    if (lua_type(L, 1) != LUA_TSTRING)
        return 0;
    size_t size;
    const void *pack = (void *)lua_tolstring(L, 1, &size);
    SDL_RWops *rw = SDL_RWFromConstMem(pack, (int)size);
    int head;

    if (SDL_RWread(rw, &head, sizeof(int), 1) != 1 || head != 0x50454747) //GGEP
        return 0;

    luaL_newlibtable(L, fun_list);

    PACKINFO *info = (PACKINFO *)lua_newuserdata(L, sizeof(PACKINFO));
    info->rw = rw;
    if (SDL_RWread(rw, &info->num, sizeof(Uint32), 1) != 1 || SDL_RWseek(rw, 4, RW_SEEK_CUR) == -1)
        return 0;

    info->list = (LISTINFO *)SDL_malloc(info->num * sizeof(LISTINFO));
    if (SDL_RWread(rw, info->list, sizeof(LISTINFO), info->num) != info->num)
    {
        return 0;
    }

    luaL_setfuncs(L, fun_list, 1); //up = info
    lua_pushvalue(L, -1);
    lua_setmetatable(L, -2); //__gc
    return 1;
}
