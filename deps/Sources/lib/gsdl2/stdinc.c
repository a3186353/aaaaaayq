#include "gge.h"

static int LUA_getenv(lua_State* L)
{
    lua_pushstring(L, SDL_getenv(luaL_checkstring(L, 1)));

    return 1;
}

static int LUA_setenv(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    const char* value = luaL_checkstring(L, 2);
    int overwrite = luaL_optint(L, 3, 1);
    lua_pushboolean(L, SDL_setenv(name, value, overwrite) == 0);

    return 1;
}

static int LUA_malloc(lua_State* L)
{
    size_t size = 0;

    GGE_Memory* ud = (GGE_Memory*)lua_newuserdata(L, sizeof(GGE_Memory));
    if (lua_type(L, 1) == LUA_TNUMBER) {
        size = (size_t)luaL_checkinteger(L, 1);
        ud->ptr = SDL_malloc(size);
    }
    else if (lua_type(L, 1) == LUA_TSTRING) {
        const char* data = lua_tolstring(L, 1, &size);
        ud->ptr = SDL_malloc(size);
        SDL_memcpy(ud->ptr, data, size);
    }
    else
        ud->ptr = NULL;

    ud->type = 8;
    ud->free = SDL_TRUE;
    ud->len = (int)size;
    luaL_setmetatable(L, "SDL_Memory");

    return 1;
}

static int LUA_calloc(lua_State* L)
{
    int nmemb = (int)luaL_checkinteger(L, 1);
    int size = (int)luaL_checkinteger(L, 2);
    if (nmemb > 0 && size > 0) {
        GGE_Memory* ud = (GGE_Memory*)lua_newuserdata(L, sizeof(GGE_Memory));
        ud->ptr = SDL_calloc((int)nmemb, (int)size);
        ud->type = 8;
        ud->free = SDL_TRUE;
        ud->len = nmemb * size;
        luaL_setmetatable(L, "SDL_Memory");
        return 1;
    }
    return 0;
}

static int LUA_realloc(lua_State* L)
{
    GGE_Memory* mem = (GGE_Memory*)luaL_checkudata(L, 1, "SDL_Memory");
    size_t size = (size_t)luaL_checkinteger(L, 2);
    if (!mem->ptr)
        return 0;

    void* ptr = mem->ptr;
    mem->ptr = SDL_realloc(mem->ptr, size);
    lua_pushboolean(L, ptr != mem->ptr);
    return 1;
}

static int LUA_memset(lua_State* L)
{
    GGE_Memory* mem = (GGE_Memory*)luaL_checkudata(L, 1, "SDL_Memory");
    if (mem->ptr) {
        int c = (int)luaL_checknumber(L, 2);
        size_t len = (size_t)luaL_checknumber(L, 3);
        SDL_memset(mem->ptr, c, len);
    }
    return 0;
}

static int LUA_memcpy(lua_State* L)
{
    GGE_Memory* mem = (GGE_Memory*)luaL_checkudata(L, 1, "SDL_Memory");
    if (mem->ptr) {
        const char* src;
        size_t len = 0;
        if (lua_type(L, 2) == LUA_TSTRING) {
            src = lua_tostring(L, 2);
            len = (size_t)luaL_optint(L, 3, luaL_len(L, 2));
        }
        else {
            GGE_Memory* smem = (GGE_Memory*)luaL_checkudata(L, 2, "SDL_Memory");
            src = smem->ptr;
            len = (size_t)luaL_optint(L, 3, smem->len);
        }

        if (len <= mem->len)
            SDL_memcpy(mem->ptr, src, len);

    }
    return 0;
}
//__gc
static int LUA_Memoryfree(lua_State* L)
{
    GGE_Memory* mem = (GGE_Memory*)luaL_checkudata(L, 1, "SDL_Memory");
    if (mem->ptr && mem->free) {
        SDL_free(mem->ptr);
        mem->ptr = NULL;
    }
    return 0;
}
//__len
static int LUA_MemoryGetLength(lua_State* L)
{
    GGE_Memory* mem = (GGE_Memory*)luaL_checkudata(L, 1, "SDL_Memory");

    lua_pushinteger(L, mem->len);
    return 1;
}

static int LUA_MemoryGetPointer(lua_State* L)
{
    GGE_Memory* mem = (GGE_Memory*)luaL_checkudata(L, 1, "SDL_Memory");

    lua_pushlightuserdata(L, mem->ptr);
    return 1;
}

static int LUA_MemoryGetRWops(lua_State* L)
{
    GGE_Memory* mem = (GGE_Memory*)luaL_checkudata(L, 1, "SDL_Memory");

    if (!mem->ptr)
        return 0;

    SDL_RWops* rw = SDL_RWFromMem(mem->ptr, mem->len);

    if (rw) {
        SDL_RWops** mem = (SDL_RWops**)lua_newuserdata(L, sizeof(SDL_RWops*));
        *mem = rw;
        luaL_setmetatable(L, "SDL_RWops");
        return 1;
    }
    return 0;
}

static int LUA_MemoryGetString(lua_State* L)
{
    GGE_Memory* mem = (GGE_Memory*)luaL_checkudata(L, 1, "SDL_Memory");

    if (!mem->ptr)
        return 0;
    int off = (int)luaL_optinteger(L, 2, 0);
    int len = (int)luaL_optinteger(L, 3, mem->len - off);
    if (off >= 0 && off + len <= mem->len) {
        const char* ptr = (const char*)mem->ptr;
        lua_pushlstring(L, ptr + off, len);
        return 1;
    }
    return 0;
}
//__tostring
static int LUA_MemoryToString(lua_State* L)
{
    GGE_Memory* mem = (GGE_Memory*)luaL_checkudata(L, 1, "SDL_Memory");

    if (!mem->ptr) {
        lua_pushstring(L, "");
        return 1;
    }

    lua_pushstring(L, (const char*)mem->ptr);
    return 1;

}

static int LUA_MemoryIndex(lua_State* L)
{
    GGE_Memory* mem = (GGE_Memory*)luaL_checkudata(L, 1, "SDL_Memory");

    if (lua_type(L, 2) == LUA_TNUMBER) {
        int i = (int)lua_tonumber(L, 2);
        //if (i >= mem->len || i < 0)
        //    return 0;

        switch (mem->type)
        {
        case 8:
        {
            Uint8* ptr = (Uint8*)mem->ptr;
            lua_pushinteger(L, ptr[i]);
            return 1;
        }
        case 16:
        {
            Uint16* ptr = (Uint16*)mem->ptr;
            lua_pushinteger(L, ptr[i]);
            return 1;
        }
        case 32:
        {
            Uint32* ptr = (Uint32*)mem->ptr;
            lua_pushinteger(L, ptr[i]);
            return 1;
        }
        case 64:
        {
            Uint64* ptr = (Uint64*)mem->ptr;
            lua_pushinteger(L, (lua_Integer)ptr[i]);
            return 1;
        }
        }

    }
    else {
        const char* key = luaL_checkstring(L, 2);
        luaL_getmetatable(L, "SDL_Memory");
        lua_getfield(L, -1, key);
        lua_remove(L, -2);
    }
    return 1;
}

static int LUA_MemoryNewIndex(lua_State* L)
{
    GGE_Memory* mem = (GGE_Memory*)luaL_checkudata(L, 1, "SDL_Memory");
    int i = (int)luaL_checkinteger(L, 2);
    Uint64 v = (Uint64)luaL_checkinteger(L, 3);

    switch (mem->type)
    {
    case 8:
    {
        Uint8* ptr = (Uint8*)mem->ptr;
        ptr[i] = (Uint8)v;
        break;
    }
    case 16:
    {
        Uint16* ptr = (Uint16*)mem->ptr;
        ptr[i] = (Uint16)v;
        break;
    }
    case 32:
    {
        Uint32* ptr = (Uint32*)mem->ptr;
        ptr[i] = (Uint32)v;
        break;
    }
    case 64:
    {
        Uint64* ptr = (Uint64*)mem->ptr;
        ptr[i] = v;
        break;
    }
    default:
        break;
    }
    return 0;
}

static int LUA_MemoryCast(lua_State* L)
{
    GGE_Memory* mem = (GGE_Memory*)luaL_checkudata(L, 1, "SDL_Memory");
    int type = (int)luaL_checkinteger(L, 2);

    GGE_Memory* ud = (GGE_Memory*)lua_newuserdata(L, sizeof(GGE_Memory));
    ud->free = SDL_FALSE;
    ud->type = type;
    ud->ptr = mem->ptr;
    ud->len = mem->len;

    luaL_setmetatable(L, "SDL_Memory");
    return 1;
}

static int LUA_MemoryAdd(lua_State* L)
{
    GGE_Memory* mem = (GGE_Memory*)luaL_checkudata(L, 1, "SDL_Memory");
    int off = (int)luaL_checkinteger(L, 2);

    GGE_Memory* ud = (GGE_Memory*)lua_newuserdata(L, sizeof(GGE_Memory));
    ud->free = SDL_FALSE;
    ud->type = mem->type;
    ud->len = mem->len;

    switch (mem->type)
    {
    case 8:
    {
        Uint8* ptr = (Uint8*)mem->ptr;
        ud->ptr = ptr + off;
        break;
    }
    case 16:
    {
        Uint16* ptr = (Uint16*)mem->ptr;
        ud->ptr = ptr + off;
        break;
    }
    case 32:
    {
        Uint32* ptr = (Uint32*)mem->ptr;
        ud->ptr = ptr + off;
        break;
    }
    case 64:
    {
        Uint64* ptr = (Uint64*)mem->ptr;
        ud->ptr = ptr + off;
        break;
    }
    default:
        break;
    }

    luaL_setmetatable(L, "SDL_Memory");
    return 1;
}

static int LUA_MemorySub(lua_State* L)
{
    GGE_Memory* mem = (GGE_Memory*)luaL_checkudata(L, 1, "SDL_Memory");
    int off = (int)luaL_checkinteger(L, 2);

    GGE_Memory* ud = (GGE_Memory*)lua_newuserdata(L, sizeof(GGE_Memory));
    ud->free = SDL_FALSE;
    ud->type = mem->type;
    ud->len = mem->len;

    switch (mem->type)
    {
    case 8:
    {
        Uint8* ptr = (Uint8*)mem->ptr;
        ud->ptr = ptr - off;
        break;
    }
    case 16:
    {
        Uint16* ptr = (Uint16*)mem->ptr;
        ud->ptr = ptr - off;
        break;
    }
    case 32:
    {
        Uint32* ptr = (Uint32*)mem->ptr;
        ud->ptr = ptr - off;
        break;
    }
    case 64:
    {
        Uint64* ptr = (Uint64*)mem->ptr;
        ud->ptr = ptr - off;
        break;
    }
    default:
        break;
    }

    luaL_setmetatable(L, "SDL_Memory");
    return 1;
}

static const luaL_Reg mem_funcs[] = {
    {"__index", LUA_MemoryIndex},
    {"__newindex", LUA_MemoryNewIndex},
    {"__add", LUA_MemoryAdd},
    {"__sub", LUA_MemorySub},
    {"__tostring", LUA_MemoryToString},
    {"__len", LUA_MemoryGetLength},
    {"__gc", LUA_Memoryfree},
    {"free", LUA_Memoryfree},
    {"cast", LUA_MemoryCast},
    {"realloc", LUA_realloc},
    {"memset", LUA_memset},
    {"memcpy", LUA_memcpy},

    {"getpointer", LUA_MemoryGetPointer},
    {"getrwops", LUA_MemoryGetRWops},
    {"getstring", LUA_MemoryGetString},
};

static const luaL_Reg sdl_funcs[] = {
    {"malloc", LUA_malloc},
    {"calloc", LUA_calloc},
    //{"getenv", LUA_getenv},
    //{"setenv", LUA_setenv},
    {NULL, NULL} };

int bind_stdinc(lua_State* L)
{
    luaL_newmetatable(L, "SDL_Memory");
    luaL_setfuncs(L, mem_funcs, 0);
    //lua_pushvalue(L, -1);
    //lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}