#include "lua.hpp"
#include "Astart.h"
#include <string.h>

static int astar_CheckPoint(lua_State* L)
{
    Map* map = *(Map**)luaL_checkudata(L, 1, "gge_astar");
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);

    if (map->data && x >= 0 && x < map->width && y >= 0 && y < map->height)
    {
        int n = y * map->width + x;
        lua_pushboolean(L, map->data[n] == 0);
    }
    else
        lua_pushboolean(L, 0);

    return 1;
}

static int astar_GetPoint(lua_State* L)
{
    Map* map = *(Map**)luaL_checkudata(L, 1, "gge_astar");
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);

    if (map->data && x >= 0 && x < map->width && y >= 0 && y < map->height)
    {
        int n = y * map->width + x;
        lua_pushinteger(L, map->data[n]);
    }
    else
        lua_pushinteger(L, -1);

    return 1;
}

static int astar_SetPoint(lua_State* L)
{
    Map* map = *(Map**)luaL_checkudata(L, 1, "gge_astar");
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);
    int v = luaL_checkinteger(L, 4);

    if (map->data && x >= 0 && x < map->width && y >= 0 && y < map->height)
    {
        int n = y * map->width + x;
        map->data[n] = v;
    }
    return 0;
}

static int astar_GetPath(lua_State* L)
{
    Map* map = *(Map**)luaL_checkudata(L, 1, "gge_astar");
    POINT nodeStart, nodeEnd, nodeCur;
    nodeStart.x = luaL_checkinteger(L, 2);
    nodeStart.y = luaL_checkinteger(L, 3);
    nodeEnd.x = luaL_checkinteger(L, 4);
    nodeEnd.y = luaL_checkinteger(L, 5);
    bool mode = luaL_optinteger(L, 6, 0);
    nodeCur = nodeStart;

    lua_newtable(L);
    if (!map || !map->data || map->width == 0 || map->height == 0)
        return 1;
    if (nodeStart.x < 0 || nodeStart.x >= map->width || nodeStart.y < 0 || nodeStart.y >= map->height)
        return 1;
    if (nodeEnd.x < 0 || nodeEnd.x >= map->width || nodeEnd.y < 0 || nodeEnd.y >= map->height)
        return 1;
    if (FindPath(map, &nodeStart, &nodeEnd, mode))
    {
        int i = 1;
        unsigned int guard = map->size + 1;
        while (guard-- > 0)
        {
            if (NextPath(map, &nodeCur))
            {
                lua_newtable(L);
                lua_pushinteger(L, nodeCur.x);
                lua_setfield(L, -2, "x");
                lua_pushinteger(L, nodeCur.y);
                lua_setfield(L, -2, "y");
                lua_rawseti(L, -2, i++);
                if (nodeCur.x == nodeEnd.x && nodeCur.y == nodeEnd.y)
                    break;
            }
            else
                break;
        }
    }
    return 1;
}

static int astar_new(lua_State* L)
{
    int w = luaL_checkinteger(L, 1);
    int h = luaL_checkinteger(L, 2);
    if (w <= 0 || h <= 0)
        return luaL_error(L, "invalid map size");
    Map* map = MapCreate(w, h, NULL);
    if (!map)
        return luaL_error(L, "create map failed");
    Map** ud = (Map**)lua_newuserdata(L, sizeof(Map*));
    *ud = map;
    map->data = (unsigned char*)malloc(map->size);
    if (!map->data)
    {
        MapDestroy(map);
        return luaL_error(L, "alloc map data failed");
    }
    if (lua_isuserdata(L, 3) && lua_isnumber(L, 4))
    {
        const void* data = (char*)lua_topointer(L, 3);
        size_t len = luaL_checkinteger(L, 4);
        if (len == map->size)
            memcpy(map->data, data, len);
        else
        {
            free(map->data);
            map->data = NULL;
            MapDestroy(map);
            return luaL_error(L, "data error");
        }
    }
    else if (lua_isstring(L, 3))
    {
        size_t len;
        const void* data = (const void*)luaL_checklstring(L, 3, &len);
        if (len == map->size)
            memcpy(map->data, data, len);
        else
        {
            free(map->data);
            map->data = NULL;
            MapDestroy(map);
            return luaL_error(L, "data error");
        }
    }
    else
    {
        memset(map->data, 0, map->size);
    }
    luaL_getmetatable(L, "gge_astar");
    lua_setmetatable(L, -2);
    return 1;
}

static int astar_gc(lua_State* L)
{
    Map* map = *(Map**)luaL_checkudata(L, 1, "gge_astar");
    if (map)
    {
        if (map->data)
            free(map->data);
        MapDestroy(map);
        *(Map**)lua_touserdata(L, 1) = NULL;
    }

    return 0;
}

extern "C"
LUALIB_API int luaopen_gastar(lua_State * L)
{
    luaL_Reg methods[] = {
        {"__gc", astar_gc},
        {"GetPath", astar_GetPath},
        {"CheckPoint", astar_CheckPoint},
        {"GetPoint", astar_GetPoint},
        {"SetPoint", astar_SetPoint},
        {NULL, NULL},
    };
    luaL_newmetatable(L, "gge_astar");
    luaL_setfuncs(L, methods, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    lua_pushcfunction(L, astar_new);
    return 1;
}
