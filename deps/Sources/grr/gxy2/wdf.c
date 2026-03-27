#include "wdf.h"
#include "tcp.h"
#include "fsb.h"

static int WDF_GetHead(lua_State* L)
{
    WDF_UserData* ud = (WDF_UserData*)luaL_checkudata(L, 1, WDF_NAME);
    Uint32 i = (Uint32)luaL_checkinteger(L, 2) - 1;
    Uint32 len = (Uint32)luaL_optinteger(L, 3, 4);
    luaL_Buffer b;

    if (i >= 0 && i < ud->number)
    {
        char* p = luaL_buffinitsize(L, &b, len);
        SDL_RWseek(ud->file, ud->list[i].offset, RW_SEEK_SET);
        SDL_RWread(ud->file, p, len, 1);
        luaL_pushresultsize(&b, len);
        return 1;
    }
    return 0;
}

static int WDF_GetData(lua_State* L)
{
    WDF_UserData* ud = (WDF_UserData*)luaL_checkudata(L, 1, WDF_NAME);
    Uint32 i = (Uint32)luaL_checkinteger(L, 2) - 1;
    luaL_Buffer b;

    if (i >= 0 && i < ud->number)
    {
        char* p = luaL_buffinitsize(L, &b, ud->list[i].size);
        SDL_RWseek(ud->file, ud->list[i].offset, RW_SEEK_SET);
        SDL_RWread(ud->file, p, ud->list[i].size, 1);
        luaL_pushresultsize(&b, ud->list[i].size);
        lua_pushinteger(L, ud->list[i].size);
        return 2;
    }
    return 0;
}

static int WDF_GetTcp(lua_State* L)
{
    //WDF_UserData* ud = (WDF_UserData*)luaL_checkudata(L, 1, WDF_NAME);

    if (WDF_GetData(L))
    {
        size_t len;
        Uint8* data = (Uint8*)lua_tolstring(L, -2, &len);
        return TCP_Create(L, data, len);
    }
    return 0;
}

static int WDF_Fsb(lua_State* L)
{
    return 0;
}

static int WDF_GetList(lua_State* L)
{
    WDF_UserData* ud = (WDF_UserData*)luaL_checkudata(L, 1, WDF_NAME);
    Uint32 i;
    if (!lua_istable(L, 2)) //如果提供table
        lua_newtable(L);

    for (i = 0; i < ud->number; i++)
    {
        lua_createtable(L, 0, 5);
        lua_pushinteger(L, i + 1);
        lua_setfield(L, -2, "id");
        lua_pushinteger(L, ud->list[i].size);
        lua_setfield(L, -2, "size");
        lua_pushinteger(L, ud->list[i].offset);
        lua_setfield(L, -2, "offset");
        lua_pushinteger(L, ud->list[i].hash);
        lua_setfield(L, -2, "hash");
        lua_pushvalue(L, 1);
        lua_setfield(L, -2, "wdf"); //udata
        lua_pushstring(L, ud->path);
        lua_setfield(L, -2, "path");

        lua_seti(L, -2, ud->list[i].hash);
    }
    return 1;
}

static int WDF_NEW(lua_State* L)
{
    const char* file = luaL_checkstring(L, 1);
    SDL_RWops* fp;
    WDF_HEAD head;

    if ((fp = SDL_RWFromFile(file, "rb")) == NULL)
        return 0;
    SDL_RWread(fp, (void*)&head, sizeof(WDF_HEAD), 1);
    if (head.flag != 'WDFP')
    {
        SDL_RWclose(fp);
        return 0;
    }
    WDF_UserData* ud = (WDF_UserData*)lua_newuserdata(L, sizeof(WDF_UserData));
    SDL_strlcpy(ud->path, file, 256);
    ud->number = head.number;
    ud->file = fp;
    ud->list = (WDF_FileInfo*)SDL_malloc(sizeof(WDF_FileInfo) * ud->number);

    SDL_RWseek(ud->file, head.offset, RW_SEEK_SET);
    SDL_RWread(ud->file, ud->list, sizeof(WDF_FileInfo), ud->number);

    luaL_setmetatable(L, WDF_NAME);
    lua_pushinteger(L, ud->number);
    return 2;
}

static int WDF_GC(lua_State* L)
{
    WDF_UserData* ud = (WDF_UserData*)luaL_checkudata(L, 1, WDF_NAME);
    SDL_RWclose(ud->file);
    SDL_free(ud->list);
    return 0;
}

LUAMOD_API int luaopen_gxy2_wdf(lua_State* L)
{
    const luaL_Reg funcs[] = {
        {"__gc", WDF_GC},
        {"__close", WDF_GC},
        {"GetData", WDF_GetData},
        {"GetTcp", WDF_GetTcp},
        // {"GetFsb", WDF_Fsb},
         {"GetList", WDF_GetList},
         {"GetHead", WDF_GetHead},

         {NULL, NULL},
    };

    luaL_newmetatable(L, WDF_NAME);
    luaL_setfuncs(L, funcs, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    lua_pushcfunction(L, WDF_NEW);
    return 1;
}
