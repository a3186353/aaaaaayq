#include "gge.h"
#include "SDL_rect.h"

static int LUA_CreateRect(lua_State* L)
{
    int x = (int)luaL_optinteger(L, 1, 0);
    int y = (int)luaL_optinteger(L, 2, 0);
    int w = (int)luaL_optinteger(L, 3, 0);
    int h = (int)luaL_optinteger(L, 4, 0);
    SDL_Rect* rect = (SDL_Rect*)lua_newuserdata(L, sizeof(SDL_Rect));
    rect->x = x;
    rect->y = y;
    rect->w = w;
    rect->h = h;
    luaL_setmetatable(L, "SDL_Rect");
    return 1;
}

static int LUA_SetRect(lua_State* L)
{
    SDL_Rect* rect = (SDL_Rect*)luaL_checkudata(L, 1, "SDL_Rect");

    rect->x = (int)luaL_optinteger(L, 2, rect->x);
    rect->y = (int)luaL_optinteger(L, 3, rect->y);
    rect->w = (int)luaL_optinteger(L, 4, rect->w);
    rect->h = (int)luaL_optinteger(L, 5, rect->h);
    return 0;
}

static int LUA_GetRect(lua_State* L)
{
    SDL_Rect* rect = (SDL_Rect*)luaL_checkudata(L, 1, "SDL_Rect");
    lua_pushinteger(L, rect->x);
    lua_pushinteger(L, rect->y);
    lua_pushinteger(L, rect->w);
    lua_pushinteger(L, rect->h);
    return 4;
}

static int LUA_SetRectXY(lua_State* L)
{
    SDL_Rect* rect = (SDL_Rect*)luaL_checkudata(L, 1, "SDL_Rect");
    rect->x = (int)luaL_optinteger(L, 2, rect->x);
    rect->y = (int)luaL_optinteger(L, 3, rect->y);
    return 0;
}

static int LUA_GetRectXY(lua_State* L)
{
    SDL_Rect* rect = (SDL_Rect*)luaL_checkudata(L, 1, "SDL_Rect");
    lua_pushinteger(L, rect->x);
    lua_pushinteger(L, rect->y);
    return 2;
}

static int LUA_SetRectWH(lua_State* L)
{
    SDL_Rect* rect = (SDL_Rect*)luaL_checkudata(L, 1, "SDL_Rect");
    rect->w = (int)luaL_optinteger(L, 2, rect->w);
    rect->h = (int)luaL_optinteger(L, 3, rect->h);
    return 0;
}

static int LUA_GetRectWH(lua_State* L)
{
    SDL_Rect* rect = (SDL_Rect*)luaL_checkudata(L, 1, "SDL_Rect");
    lua_pushinteger(L, rect->w);
    lua_pushinteger(L, rect->h);
    return 2;
}

static int LUA_PointInRect(lua_State* L)
{
    SDL_Rect* rect = (SDL_Rect*)luaL_checkudata(L, 1, "SDL_Rect");
    SDL_Point p;
    p.x = (int)luaL_checkinteger(L, 2);
    p.y = (int)luaL_checkinteger(L, 3);
    lua_pushboolean(L, SDL_PointInRect(&p, rect));
    return 1;
}

static int LUA_RectEmpty(lua_State* L)
{
    SDL_Rect* rect = (SDL_Rect*)luaL_checkudata(L, 1, "SDL_Rect");
    lua_pushboolean(L, SDL_RectEmpty(rect));
    return 1;
}

static int LUA_RectEquals(lua_State* L)
{
    SDL_Rect* A = (SDL_Rect*)luaL_checkudata(L, 1, "SDL_Rect");
    SDL_Rect* B = (SDL_Rect*)luaL_checkudata(L, 2, "SDL_Rect");
    lua_pushboolean(L, SDL_RectEquals(A, B));
    return 1;
}

static int LUA_HasIntersection(lua_State* L)
{
    SDL_Rect* A = (SDL_Rect*)luaL_checkudata(L, 1, "SDL_Rect");
    SDL_Rect* B = (SDL_Rect*)luaL_checkudata(L, 2, "SDL_Rect");
    lua_pushboolean(L, SDL_HasIntersection(A, B));
    return 1;
}

static int LUA_IntersectRect(lua_State* L)
{
    SDL_Rect* A = (SDL_Rect*)luaL_checkudata(L, 1, "SDL_Rect");
    SDL_Rect* B = (SDL_Rect*)luaL_checkudata(L, 2, "SDL_Rect");

    SDL_Rect* result = (SDL_Rect*)lua_newuserdata(L, sizeof(SDL_Rect));
    SDL_memset(result, 0, sizeof(SDL_Rect));
    luaL_setmetatable(L, "SDL_Rect");

    SDL_IntersectRect(A, B, result);
    return 1;
}

static int LUA_UnionRect(lua_State* L)
{
    SDL_Rect* A = (SDL_Rect*)luaL_checkudata(L, 1, "SDL_Rect");
    SDL_Rect* B = (SDL_Rect*)luaL_checkudata(L, 2, "SDL_Rect");
    SDL_Rect* result = (SDL_Rect*)lua_newuserdata(L, sizeof(SDL_Rect));

    SDL_UnionRect(A, B, result);
    luaL_setmetatable(L, "SDL_Rect");
    return 1;
}

static int LUA_EnclosePoints(lua_State* L)
{
    SDL_Point* points = (SDL_Point*)luaL_checkudata(L, 1, "SDL_Points");
    int count = (int)luaL_checkinteger(L, 2);
    SDL_Rect* clip = (SDL_Rect*)luaL_checkudata(L, 3, "SDL_Rect");
    SDL_Rect* result = (SDL_Rect*)lua_newuserdata(L, sizeof(SDL_Rect));

    SDL_EnclosePoints(points, count, clip, result);
    luaL_setmetatable(L, "SDL_Rect");
    return 1;
}

static int LUA_IntersectRectAndLine(lua_State* L)
{
    SDL_Rect* rect = (SDL_Rect*)luaL_checkudata(L, 1, "SDL_Rect");
    int x1, y1, x2, y2;

    x1 = (int)luaL_checkinteger(L, 2);
    y1 = (int)luaL_checkinteger(L, 3);
    x2 = (int)luaL_checkinteger(L, 4);
    y2 = (int)luaL_checkinteger(L, 5);

    if (SDL_IntersectRectAndLine(rect, &x1, &y1, &x2, &y2) == SDL_TRUE)
    {
        lua_pushinteger(L, x1);
        lua_pushinteger(L, y1);
        lua_pushinteger(L, x2);
        lua_pushinteger(L, y2);
        return 4;
    }
    return 0;
}

static int LUA_RectIndex(lua_State* L)
{
    SDL_Rect* rect = (SDL_Rect*)luaL_checkudata(L, 1, "SDL_Rect");
    const char* key = lua_tostring(L, 2);
    switch (key[0])
    {
    case 'x':
        lua_pushinteger(L, rect->x);
        break;
    case 'y':
        lua_pushinteger(L, rect->y);
        break;
    case 'w':
        lua_pushinteger(L, rect->w);
        break;
    case 'h':
        lua_pushinteger(L, rect->h);
        break;
    default:
        luaL_getmetatable(L, "SDL_Rect");
        lua_getfield(L, -1, key);
        lua_remove(L, -2);
    }
    return 1;
}

static int LUA_RectNewIndex(lua_State* L)
{
    SDL_Rect* rect = (SDL_Rect*)luaL_checkudata(L, 1, "SDL_Rect");
    const char* key = lua_tostring(L, 2);
    int value = (int)luaL_checkinteger(L, 3);

    switch (key[0])
    {
    case 'x':
        rect->x = value;
        break;
    case 'y':
        rect->y = value;
        break;
    case 'w':
        rect->w = value;
        break;
    case 'h':
        rect->h = value;
        break;
    }
    return 0;
}


static int LUA_CreateFRect(lua_State* L)
{
    float x = (float)luaL_optnumber(L, 1, 0);
    float y = (float)luaL_optnumber(L, 2, 0);
    float w = (float)luaL_optnumber(L, 3, 0);
    float h = (float)luaL_optnumber(L, 4, 0);
    SDL_FRect* rect = (SDL_FRect*)lua_newuserdata(L, sizeof(SDL_FRect));
    rect->x = x;
    rect->y = y;
    rect->w = w;
    rect->h = h;
    luaL_setmetatable(L, "SDL_FRect");
    return 1;
}

static int LUA_SetFRect(lua_State* L)
{
    SDL_FRect* rect = (SDL_FRect*)luaL_checkudata(L, 1, "SDL_FRect");

    rect->x = (float)luaL_optnumber(L, 2, rect->x);
    rect->y = (float)luaL_optnumber(L, 3, rect->y);
    rect->w = (float)luaL_optnumber(L, 4, rect->w);
    rect->h = (float)luaL_optnumber(L, 5, rect->h);
    return 0;
}

static int LUA_GetFRect(lua_State* L)
{
    SDL_FRect* rect = (SDL_FRect*)luaL_checkudata(L, 1, "SDL_FRect");
    lua_pushnumber(L, rect->x);
    lua_pushnumber(L, rect->y);
    lua_pushnumber(L, rect->w);
    lua_pushnumber(L, rect->h);
    return 4;
}

static int LUA_SetFRectXY(lua_State* L)
{
    SDL_FRect* rect = (SDL_FRect*)luaL_checkudata(L, 1, "SDL_FRect");
    rect->x = (float)luaL_optnumber(L, 2, rect->x);
    rect->y = (float)luaL_optnumber(L, 3, rect->y);
    return 0;
}

static int LUA_GetFRectXY(lua_State* L)
{
    SDL_FRect* rect = (SDL_FRect*)luaL_checkudata(L, 1, "SDL_FRect");
    lua_pushnumber(L, rect->x);
    lua_pushnumber(L, rect->y);
    return 2;
}

static int LUA_SetFRectWH(lua_State* L)
{
    SDL_FRect* rect = (SDL_FRect*)luaL_checkudata(L, 1, "SDL_FRect");
    rect->w = (float)luaL_optnumber(L, 2, rect->w);
    rect->h = (float)luaL_optnumber(L, 3, rect->h);
    return 0;
}

static int LUA_GetFRectWH(lua_State* L)
{
    SDL_FRect* rect = (SDL_FRect*)luaL_checkudata(L, 1, "SDL_FRect");
    lua_pushnumber(L, rect->w);
    lua_pushnumber(L, rect->h);
    return 2;
}

static int LUA_PointInFRect(lua_State* L)
{
    SDL_FRect* rect = (SDL_FRect*)luaL_checkudata(L, 1, "SDL_FRect");
    SDL_FPoint p;
    p.x = (float)luaL_checknumber(L, 2);
    p.y = (float)luaL_checknumber(L, 3);
    lua_pushboolean(L, SDL_PointInFRect(&p, rect));
    return 1;
}

static int LUA_FRectEmpty(lua_State* L)
{
    SDL_FRect* rect = (SDL_FRect*)luaL_checkudata(L, 1, "SDL_FRect");
    lua_pushboolean(L, SDL_FRectEmpty(rect));
    return 1;
}

static int LUA_FRectEquals(lua_State* L)
{
    SDL_FRect* A = (SDL_FRect*)luaL_checkudata(L, 1, "SDL_FRect");
    SDL_FRect* B = (SDL_FRect*)luaL_checkudata(L, 2, "SDL_FRect");
    lua_pushboolean(L, SDL_FRectEquals(A, B));
    return 1;
}

static int LUA_HasIntersectionF(lua_State* L)
{
    SDL_FRect* A = (SDL_FRect*)luaL_checkudata(L, 1, "SDL_FRect");
    SDL_FRect* B = (SDL_FRect*)luaL_checkudata(L, 2, "SDL_FRect");
    lua_pushboolean(L, SDL_HasIntersectionF(A, B));
    return 1;
}

static int LUA_IntersectFRect(lua_State* L)
{
    SDL_FRect* A = (SDL_FRect*)luaL_checkudata(L, 1, "SDL_FRect");
    SDL_FRect* B = (SDL_FRect*)luaL_checkudata(L, 2, "SDL_FRect");

    SDL_FRect* result = (SDL_FRect*)lua_newuserdata(L, sizeof(SDL_FRect));
    luaL_setmetatable(L, "SDL_FRect");
    SDL_IntersectFRect(A, B, result);
    return 1;
}

static int LUA_UnionFRect(lua_State* L)
{
    SDL_FRect* A = (SDL_FRect*)luaL_checkudata(L, 1, "SDL_FRect");
    SDL_FRect* B = (SDL_FRect*)luaL_checkudata(L, 2, "SDL_FRect");
    SDL_FRect* result = (SDL_FRect*)lua_newuserdata(L, sizeof(SDL_FRect));

    SDL_UnionFRect(A, B, result);
    luaL_setmetatable(L, "SDL_FRect");
    return 1;
}

static int LUA_EncloseFPoints(lua_State* L)
{
    SDL_FPoint* points = (SDL_FPoint*)luaL_checkudata(L, 1, "SDL_FPoints");
    int count = (int)luaL_checkinteger(L, 2);
    SDL_FRect* clip = (SDL_FRect*)luaL_checkudata(L, 3, "SDL_FRect");
    SDL_FRect* result = (SDL_FRect*)lua_newuserdata(L, sizeof(SDL_FRect));

    SDL_EncloseFPoints(points, count, clip, result);
    luaL_setmetatable(L, "SDL_FRect");
    return 1;
}

static int LUA_IntersectFRectAndLine(lua_State* L)
{
    SDL_FRect* rect = (SDL_FRect*)luaL_checkudata(L, 1, "SDL_FRect");
    float x1, y1, x2, y2;

    x1 = (float)luaL_checknumber(L, 2);
    y1 = (float)luaL_checknumber(L, 3);
    x2 = (float)luaL_checknumber(L, 4);
    y2 = (float)luaL_checknumber(L, 5);

    if (SDL_IntersectFRectAndLine(rect, &x1, &y1, &x2, &y2) == SDL_TRUE)
    {
        lua_pushnumber(L, x1);
        lua_pushnumber(L, y1);
        lua_pushnumber(L, x2);
        lua_pushnumber(L, y2);
        return 4;
    }
    return 0;
}

static int LUA_FRectIndex(lua_State* L)
{
    SDL_FRect* rect = (SDL_FRect*)luaL_checkudata(L, 1, "SDL_FRect");
    const char* key = lua_tostring(L, 2);
    switch (key[0])
    {
    case 'x':
        lua_pushnumber(L, rect->x);
        break;
    case 'y':
        lua_pushnumber(L, rect->y);
        break;
    case 'w':
        lua_pushnumber(L, rect->w);
        break;
    case 'h':
        lua_pushnumber(L, rect->h);
        break;
    default:
        luaL_getmetatable(L, "SDL_FRect");
        lua_getfield(L, -1, key);
        lua_remove(L, -2);
    }
    return 1;
}

static int LUA_FRectNewIndex(lua_State* L)
{
    SDL_FRect* rect = (SDL_FRect*)luaL_checkudata(L, 1, "SDL_FRect");
    const char* key = lua_tostring(L, 2);
    float value = (float)luaL_checknumber(L, 3);

    switch (key[0])
    {
    case 'x':
        rect->x = value;
        break;
    case 'y':
        rect->y = value;
        break;
    case 'w':
        rect->w = value;
        break;
    case 'h':
        rect->h = value;
        break;
    }
    return 0;
}

int bind_rect(lua_State* L)
{
    static const luaL_Reg rect_funcs[] = {
        {"__index", LUA_RectIndex},
        {"__newindex", LUA_RectNewIndex},
        {"SetRect", LUA_SetRect},
        {"GetRect", LUA_GetRect},
        {"SetRectXY", LUA_SetRectXY},
        {"SetRectWH", LUA_SetRectWH},
        {"GetRectXY", LUA_GetRectXY},
        {"GetRectWH", LUA_GetRectWH},

        {"PointInRect", LUA_PointInRect},
        {"RectEmpty", LUA_RectEmpty},
        {"RectEquals", LUA_RectEquals},
        {"HasIntersection", LUA_HasIntersection},
        {"IntersectRect", LUA_IntersectRect},
        {"UnionRect", LUA_UnionRect},
        {"EnclosePoints", LUA_EnclosePoints},
        {"IntersectRectAndLine", LUA_IntersectRectAndLine},

        {NULL, NULL}
    };

    luaL_newmetatable(L, "SDL_Rect");
    luaL_setfuncs(L, rect_funcs, 0);
    lua_pop(L, 1);


    static const luaL_Reg frect_funcs[] = {
        {"__index", LUA_FRectIndex},
        {"__newindex", LUA_FRectNewIndex},
        {"SetRect", LUA_SetFRect},
        {"GetRect", LUA_GetFRect},
        {"SetRectXY", LUA_SetFRectXY},
        {"SetRectWH", LUA_SetFRectWH},
        {"GetRectXY", LUA_GetFRectXY},
        {"GetRectWH", LUA_GetFRectWH},

        {"PointInRect", LUA_PointInFRect},
        {"RectEmpty", LUA_FRectEmpty},
        {"RectEquals", LUA_FRectEquals},
        {"HasIntersection", LUA_HasIntersectionF},
        {"IntersectRect", LUA_IntersectFRect},
        {"UnionRect", LUA_UnionFRect},
        {"EnclosePoints", LUA_EncloseFPoints},
        {"IntersectRectAndLine", LUA_IntersectFRectAndLine},

        {NULL, NULL}
    };

    luaL_newmetatable(L, "SDL_FRect");
    luaL_setfuncs(L, frect_funcs, 0);
    lua_pop(L, 1);


    static const luaL_Reg sdl_funcs[] = {
        {"CreateRect", LUA_CreateRect},
        {"CreateFRect", LUA_CreateFRect},
        {NULL, NULL}
    };

    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}