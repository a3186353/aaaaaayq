#include "lua.h"
#include "lauxlib.h"

typedef struct {
	int w;
	int h;
	char* data;
} AS_UserData;

static int astar_CheckPoint(lua_State* L)
{
	AS_UserData* map = (AS_UserData*)luaL_checkudata(L, 1, "gge_astar");
	int x = luaL_checkinteger(L, 2);
	int y = luaL_checkinteger(L, 3);

	if (map->data && x >= 0 && x < map->w && y >= 0 && y < map->h)
	{
		int n = y * map->w + x;
		lua_pushboolean(L, map->data[n] == 0);
	}
	else
		lua_pushboolean(L, 0);

	return 1;
}

static int astar_GetPoint(lua_State* L)
{
	AS_UserData* map = (AS_UserData*)luaL_checkudata(L, 1, "gge_astar");
	int x = luaL_checkinteger(L, 2);
	int y = luaL_checkinteger(L, 3);

	if (map->data && x >= 0 && x < map->w && y >= 0 && y < map->h)
	{
		int n = y * map->w + x;
		lua_pushinteger(L, map->data[n]);
	}
	else
		lua_pushinteger(L, -1);

	return 1;
}

static int astar_SetPoint(lua_State* L)
{
	AS_UserData* map = (AS_UserData*)luaL_checkudata(L, 1, "gge_astar");
	int x = luaL_checkinteger(L, 2);
	int y = luaL_checkinteger(L, 3);
	int v = luaL_checkinteger(L, 4);

	if (map->data && x >= 0 && x < map->w && y >= 0 && y < map->h)
	{
		int n = y * map->w + x;
		map->data[n] = v;
	}
	return 0;
}

static int astar_GetPath(lua_State* L)
{
	AS_UserData* map = (AS_UserData*)luaL_checkudata(L, 1, "gge_astar");
	return 0;
}


static int astar_new(lua_State* L)
{
	int w = luaL_checkinteger(L, 1);
	int h = luaL_checkinteger(L, 2);


	return 1;
}

static int astar_gc(lua_State* L)
{
	AS_UserData* map = (AS_UserData*)luaL_checkudata(L, 1, "gge_astar");
	if (map->data)
	{
		free(map->data);
		map->data = NULL;
	}

	return 0;
}
LUALIB_API
int luaopen_gastar(lua_State* L)
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