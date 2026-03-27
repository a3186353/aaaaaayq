#include "gge.h"
#include "SDL_rwops.h"

static int LUA_RWFromFile(lua_State* L)
{
    const char* file = luaL_checkstring(L, 1);
    const char* mode = luaL_optstring(L, 2, "rb");
    SDL_RWops* rw = SDL_RWFromFile(file, mode);
    if (rw)
    {
        SDL_RWops** ud = (SDL_RWops**)lua_newuserdata(L, sizeof(SDL_RWops*));
        *ud = rw;
        luaL_setmetatable(L, "SDL_RWops");
        return 1;
    }
    return 0;
}

static int LUA_RWFromFP(lua_State* L)
{
    luaL_Stream* ls = (luaL_Stream*)luaL_checkudata(L, 1, LUA_FILEHANDLE);
    if (ls->f)
    {
        SDL_RWops* rw = SDL_RWFromFP(ls->f, SDL_FALSE);
        if (rw)
        {
            SDL_RWops** ud = (SDL_RWops**)lua_newuserdata(L, sizeof(SDL_RWops*));
            *ud = rw;
            luaL_setmetatable(L, "SDL_RWops");
            return 1;
        }
    }
    return 0;
}

static int LUA_RWFromMem(lua_State* L)
{
    void* mem = (void*)lua_topointer(L, 1);
    int size = (int)luaL_checkinteger(L, 2);
    SDL_RWops* rw = SDL_RWFromMem(mem, size);

    if (rw)
    {
        SDL_RWops** ud = (SDL_RWops**)lua_newuserdata(L, sizeof(SDL_RWops*));
        *ud = rw;
        luaL_setmetatable(L, "SDL_RWops");
        return 1;
    }

    return 0;
}

static int LUA_RWFromConstMem(lua_State* L)
{
    const void* mem = lua_topointer(L, 1);
    int size = (int)luaL_checkinteger(L, 2);
    SDL_RWops* rw = SDL_RWFromConstMem(mem, size);

    if (rw)
    {
        SDL_RWops** ud = (SDL_RWops**)lua_newuserdata(L, sizeof(SDL_RWops*));
        *ud = rw;
        luaL_setmetatable(L, "SDL_RWops");
        return 1;
    }
    return 0;
}

static int LUA_RWFromStr(lua_State* L)
{
    size_t size;
    const void* mem = (void*)luaL_checklstring(L, 1, &size);
    SDL_RWops* rw = SDL_RWFromConstMem(mem, (int)size);

    if (rw)
    {
        SDL_RWops** ud = (SDL_RWops**)lua_newuserdata(L, sizeof(SDL_RWops*));
        *ud = rw;
        luaL_setmetatable(L, "SDL_RWops");
        return 1;
    }

    return 0;
}

//static int LUA_AllocRW(lua_State* L)
//{
//	SDL_RWops* rw = SDL_AllocRW();
//
//	if (rw) {
//		SDL_RWops** ud = (SDL_RWops**)lua_newuserdata(L, sizeof(SDL_RWops*));
//		*ud = rw;
//		luaL_setmetatable(L, "SDL_RWops");
//		return 1;
//	}
//	return 0;
//}
//
//static int LUA_FreeRW(lua_State* L)
//{
//	SDL_RWops* rwf = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
//	SDL_FreeRW(rwf);
//	return 0;
//}

static int LUA_RWsize(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");

    lua_pushinteger(L, SDL_RWsize(rw));
    return 1;
}

static int LUA_RWseek(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    Sint64 offset = luaL_checkinteger(L, 2);
    int whence = (int)luaL_optinteger(L, 3, RW_SEEK_SET);

    lua_pushboolean(L, SDL_RWseek(rw, offset, whence) != -1);
    return 1;
}

static int LUA_RWtell(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");

    lua_pushinteger(L, SDL_RWtell(rw));
    return 1;
}

static int LUA_RWread(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    int tp = lua_type(L, 2);
    if (tp == LUA_TNUMBER) {
        size_t size = (size_t)luaL_checkinteger(L, 2);
        luaL_Buffer b;
        char* ptr = luaL_buffinitsize(L, &b, size);
        size_t nr = SDL_RWread(rw, ptr, sizeof(char), size);
        luaL_addsize(&b, nr);

        luaL_pushresult(&b);
        return 1;
    }
    else if (tp == LUA_TUSERDATA) {
        GGE_Memory* mem = (GGE_Memory*)luaL_testudata(L, 2, "SDL_Memory");
        if (mem) {
            size_t size = (size_t)luaL_optint(L, 3, mem->len);
            if (size > mem->len)
                return luaL_error(L, "not enough memory");
            size_t nr = SDL_RWread(rw, mem->ptr, sizeof(char), size);
            lua_pushnumber(L, (lua_Number)nr);
            return 1;
        }
    }
    return 0;
}

static int LUA_RWwrite(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    size_t size;
    const char* ptr = luaL_checklstring(L, 2, &size);

    lua_pushinteger(L, SDL_RWwrite(rw, ptr, size, 1));
    return 1;
}

static int LUA_RWclose(lua_State* L)
{
    SDL_RWops** rw = (SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    if (*rw)
    {
        SDL_RWclose(*rw);
        *rw = NULL;
    }
    return 0;
}

static int LUA_LoadFile_RW(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    size_t datasize;
    void* data = SDL_LoadFile_RW(rw, &datasize, 0);
    if (data)
    {
        lua_pushlstring(L, data, datasize);
        SDL_free(data);
        return 1;
    }
    return 0;
}

static int LUA_LoadFile(lua_State* L)
{
    const char* file = luaL_checkstring(L, 1);
    SDL_RWops* rw = SDL_RWFromFile(file, "r");
    size_t nr;
    luaL_Buffer b;
    if (rw)
    {
        luaL_buffinit(L, &b);
        do
        { /* read file in chunks of LUAL_BUFFERSIZE bytes */
            char* p = luaL_prepbuffer(&b);
            nr = SDL_RWread(rw, p, sizeof(char), LUAL_BUFFERSIZE);
            luaL_addsize(&b, nr);
        } while (nr == LUAL_BUFFERSIZE);
        luaL_pushresult(&b); /* close buffer */
        SDL_RWclose(rw);
        return 1;
    }
    return 0;
}
//Read
static int LUA_ReadU8(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");

    lua_pushinteger(L, SDL_ReadU8(rw));
    return 1;
}

static int LUA_ReadLE16(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");

    lua_pushinteger(L, SDL_ReadLE16(rw));
    return 1;
}

static int LUA_ReadBE16(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");

    lua_pushinteger(L, SDL_ReadBE16(rw));
    return 1;
}

static int LUA_ReadLE32(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");

    lua_pushinteger(L, SDL_ReadLE32(rw));
    return 1;
}

static int LUA_ReadBE32(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");

    lua_pushinteger(L, SDL_ReadBE32(rw));
    return 1;
}

static int LUA_ReadLE64(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");

    lua_pushinteger(L, SDL_ReadLE64(rw));
    return 1;
}

static int LUA_ReadBE64(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");

    lua_pushinteger(L, SDL_ReadBE64(rw));
    return 1;
}
//Write
static int LUA_WriteU8(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    Uint8 b = (Uint8)luaL_checkinteger(L, 2);

    lua_pushboolean(L, (int)SDL_WriteU8(rw, b));
    return 1;
}

static int LUA_WriteLE16(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    Uint16 b = (Uint16)luaL_checkinteger(L, 2);

    lua_pushboolean(L, (int)SDL_WriteLE16(rw, b));
    return 1;
}

static int LUA_WriteBE16(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    Uint16 b = (Uint16)luaL_checkinteger(L, 2);

    lua_pushboolean(L, (int)SDL_WriteBE16(rw, b));
    return 1;
}
static int LUA_WriteLE32(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    Uint32 b = (int)luaL_checkinteger(L, 2);

    lua_pushboolean(L, (int)SDL_WriteLE32(rw, b));
    return 1;
}
static int LUA_WriteBE32(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    Uint32 b = (int)luaL_checkinteger(L, 2);

    lua_pushboolean(L, (int)SDL_WriteBE32(rw, b));
    return 1;
}

static int LUA_WriteLE64(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    Uint32 b = (int)luaL_checkinteger(L, 2);

    lua_pushboolean(L, (int)SDL_WriteLE64(rw, b));
    return 1;
}

static int LUA_WriteBE64(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    Uint32 b = (int)luaL_checkinteger(L, 2);

    lua_pushboolean(L, (int)SDL_WriteBE64(rw, b));
    return 1;
}

static int LUA_RWtype(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");

    lua_pushinteger(L, rw->type);
    return 1;
}

static const luaL_Reg rwops_funcs[] = {
    {"__index", NULL},
    {"__gc", LUA_RWclose},
    {"__close", LUA_RWclose},

    {"RWclose", LUA_RWclose},
    {"RWsize", LUA_RWsize},
    {"RWseek", LUA_RWseek},
    {"RWtell", LUA_RWtell},
    {"RWread", LUA_RWread},
    {"RWwrite", LUA_RWwrite},
    {"LoadFile", LUA_LoadFile_RW},

    {"ReadU8", LUA_ReadU8},
    {"ReadBE16", LUA_ReadBE16},
    {"ReadLE16", LUA_ReadLE16},
    {"ReadBE32", LUA_ReadBE32},
    {"ReadLE32", LUA_ReadLE32},
    {"ReadBE64", LUA_ReadBE64},
    {"ReadLE64", LUA_ReadLE64},

    {"WriteU8", LUA_WriteU8},
    {"WriteBE16", LUA_WriteBE16},
    {"WriteLE16", LUA_WriteLE16},
    {"WriteBE32", LUA_WriteBE32},
    {"WriteLE32", LUA_WriteLE32},
    {"WriteBE64", LUA_WriteBE64},
    {"WriteLE64", LUA_WriteLE64},

    {"RWtype", LUA_RWtype},
    {NULL, NULL} };

static const luaL_Reg sdl_funcs[] = {
    {"RWFromFile", LUA_RWFromFile},
    {"RWFromFP", LUA_RWFromFP},
    {"RWFromMem", LUA_RWFromMem},
    {"RWFromConstMem", LUA_RWFromConstMem},
    {"RWFromStr", LUA_RWFromStr},
    //{"AllocRW", LUA_AllocRW},
    //{"FreeRW", LUA_FreeRW},

    {"LoadFile", LUA_LoadFile},
    {NULL, NULL} };

int bind_rwops(lua_State* L)
{
    luaL_newmetatable(L, "SDL_RWops");
    luaL_setfuncs(L, rwops_funcs, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}