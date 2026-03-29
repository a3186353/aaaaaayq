/*
 * gphysfs.c — PhysFS Lua bindings for GGELUA engine
 *
 * Provides a 'physfs' module to Lua with mount/unmount/exists/read/enumerate
 * capabilities, designed to unify resource access across Windows/Android/iOS.
 *
 * API contract (matching GGE.资源.lua expectations):
 *   physfs.init(argv0)              → boolean
 *   physfs.deinit()                 → boolean
 *   physfs.mount(path [, mountPt])  → boolean
 *   physfs.unmount(path)            → boolean
 *   physfs.exists(path)             → boolean
 *   physfs.realDir(path)            → string | nil
 *   physfs.files(path)              → {string...}
 *   physfs.openRead(path)           → filehandle | nil
 *   physfs.setWriteDir(path)        → boolean
 *   physfs.getWriteDir()            → string | nil
 *   physfs.mkdir(path)              → boolean
 *   physfs.getBaseDir()             → string
 *   physfs.getPrefDir(org, app)     → string
 *
 * File handle methods:
 *   fh:read(fmt)    — fmt = 'a' read all, or number = read N bytes
 *   fh:close()      — close file handle
 *   fh:size()       — file length in bytes
 */

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <string.h>
#include <stdlib.h>

#if __has_include("physfs.h")
#include "physfs.h"
#else
/* Xcode generator 多目标场景下 -I 路径可能遗漏，使用相对路径兜底 */
#include "../../../Dependencies/physfs/src/physfs.h"
#endif

#define PHYSFS_FILE_MT "PhysFS.File"

/* =====================================================================
 *  Helper: push PhysFS error string
 * ===================================================================== */
static int push_physfs_error(lua_State *L)
{
    PHYSFS_ErrorCode code = PHYSFS_getLastErrorCode();
    const char *msg = PHYSFS_getErrorByCode(code);
    lua_pushnil(L);
    lua_pushstring(L, msg ? msg : "unknown physfs error");
    return 2;
}

/* =====================================================================
 *  physfs.init(argv0)
 * ===================================================================== */
static int l_init(lua_State *L)
{
    const char *argv0 = luaL_optstring(L, 1, NULL);
    if (PHYSFS_isInit())
    {
        lua_pushboolean(L, 1);
        return 1;
    }
    if (!PHYSFS_init(argv0))
    {
        return push_physfs_error(L);
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* =====================================================================
 *  physfs.deinit()
 * ===================================================================== */
static int l_deinit(lua_State *L)
{
    if (PHYSFS_isInit())
    {
        if (!PHYSFS_deinit())
        {
            return push_physfs_error(L);
        }
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* =====================================================================
 *  physfs.mount(path [, mountPoint [, appendToPath]])
 * ===================================================================== */
static int l_mount(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    const char *mount_point = luaL_optstring(L, 2, NULL);
    int append = (int)luaL_optinteger(L, 3, 1); /* default: append */
    if (!PHYSFS_mount(path, mount_point, append))
    {
        return push_physfs_error(L);
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* =====================================================================
 *  physfs.unmount(path)
 * ===================================================================== */
static int l_unmount(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    if (!PHYSFS_unmount(path))
    {
        return push_physfs_error(L);
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* =====================================================================
 *  physfs.exists(path) → boolean
 * ===================================================================== */
static int l_exists(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    lua_pushboolean(L, PHYSFS_exists(path));
    return 1;
}

/* =====================================================================
 *  physfs.realDir(path) → string | nil
 * ===================================================================== */
static int l_realDir(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    const char *dir = PHYSFS_getRealDir(path);
    if (dir)
    {
        lua_pushstring(L, dir);
    }
    else
    {
        lua_pushnil(L);
    }
    return 1;
}

/* =====================================================================
 *  physfs.files(path) → {string...}
 * ===================================================================== */
static int l_files(lua_State *L)
{
    const char *path = luaL_optstring(L, 1, "");
    char **list = PHYSFS_enumerateFiles(path);
    if (!list)
    {
        lua_newtable(L);
        return 1;
    }
    lua_newtable(L);
    int idx = 1;
    for (char **i = list; *i != NULL; i++)
    {
        lua_pushstring(L, *i);
        lua_rawseti(L, -2, idx++);
    }
    PHYSFS_freeList(list);
    return 1;
}

/* =====================================================================
 *  physfs.setWriteDir(path)
 * ===================================================================== */
static int l_setWriteDir(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    if (!PHYSFS_setWriteDir(path))
    {
        return push_physfs_error(L);
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* =====================================================================
 *  physfs.getWriteDir()
 * ===================================================================== */
static int l_getWriteDir(lua_State *L)
{
    const char *dir = PHYSFS_getWriteDir();
    if (dir)
    {
        lua_pushstring(L, dir);
    }
    else
    {
        lua_pushnil(L);
    }
    return 1;
}

/* =====================================================================
 *  physfs.mkdir(path)
 * ===================================================================== */
static int l_mkdir(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    if (!PHYSFS_mkdir(path))
    {
        return push_physfs_error(L);
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* =====================================================================
 *  physfs.getBaseDir()
 * ===================================================================== */
static int l_getBaseDir(lua_State *L)
{
    const char *dir = PHYSFS_getBaseDir();
    lua_pushstring(L, dir ? dir : "");
    return 1;
}

/* =====================================================================
 *  physfs.getPrefDir(org, app)
 * ===================================================================== */
static int l_getPrefDir(lua_State *L)
{
    const char *org = luaL_checkstring(L, 1);
    const char *app = luaL_checkstring(L, 2);
    const char *dir = PHYSFS_getPrefDir(org, app);
    if (dir)
    {
        lua_pushstring(L, dir);
    }
    else
    {
        return push_physfs_error(L);
    }
    return 1;
}

/* =====================================================================
 *  physfs.stat(path) → table {type, size, modtime, ...} | nil
 * ===================================================================== */
static int l_stat(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    PHYSFS_Stat st;
    if (!PHYSFS_stat(path, &st))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    /* type */
    switch (st.filetype)
    {
    case PHYSFS_FILETYPE_REGULAR:
        lua_pushstring(L, "file");
        break;
    case PHYSFS_FILETYPE_DIRECTORY:
        lua_pushstring(L, "directory");
        break;
    case PHYSFS_FILETYPE_SYMLINK:
        lua_pushstring(L, "symlink");
        break;
    default:
        lua_pushstring(L, "other");
        break;
    }
    lua_setfield(L, -2, "type");
    lua_pushinteger(L, (lua_Integer)st.filesize);
    lua_setfield(L, -2, "size");
    lua_pushinteger(L, (lua_Integer)st.modtime);
    lua_setfield(L, -2, "modtime");
    lua_pushinteger(L, (lua_Integer)st.createtime);
    lua_setfield(L, -2, "createtime");
    lua_pushinteger(L, (lua_Integer)st.accesstime);
    lua_setfield(L, -2, "accesstime");
    lua_pushboolean(L, st.readonly);
    lua_setfield(L, -2, "readonly");
    return 1;
}

/* =====================================================================
 *  File handle implementation
 * ===================================================================== */

static PHYSFS_File *check_file(lua_State *L, int idx)
{
    PHYSFS_File **fp = (PHYSFS_File **)luaL_checkudata(L, idx, PHYSFS_FILE_MT);
    if (*fp == NULL)
    {
        luaL_error(L, "attempt to use a closed PhysFS file");
    }
    return *fp;
}

static int fh_read(lua_State *L)
{
    PHYSFS_File *f = check_file(L, 1);
    if (lua_type(L, 2) == LUA_TSTRING)
    {
        /* read('a') — read entire file */
        PHYSFS_sint64 len = PHYSFS_fileLength(f);
        if (len < 0)
        {
            return push_physfs_error(L);
        }
        /* rewind to beginning for full read */
        PHYSFS_sint64 cur = PHYSFS_tell(f);
        if (cur > 0) {
            /* already partially read, read remaining */
            len = len - cur;
        }
        if (len == 0)
        {
            lua_pushliteral(L, "");
            return 1;
        }
        luaL_Buffer b;
        char *buf = luaL_buffinitsize(L, &b, (size_t)len);
        PHYSFS_sint64 got = PHYSFS_readBytes(f, buf, (PHYSFS_uint64)len);
        if (got < 0)
        {
            return push_physfs_error(L);
        }
        luaL_pushresultsize(&b, (size_t)got);
        return 1;
    }
    else
    {
        /* read(n) — read N bytes */
        lua_Integer n = luaL_checkinteger(L, 2);
        if (n <= 0)
        {
            lua_pushliteral(L, "");
            return 1;
        }
        luaL_Buffer b;
        char *buf = luaL_buffinitsize(L, &b, (size_t)n);
        PHYSFS_sint64 got = PHYSFS_readBytes(f, buf, (PHYSFS_uint64)n);
        if (got < 0)
        {
            return push_physfs_error(L);
        }
        if (got == 0)
        {
            lua_pushnil(L);
            return 1;
        }
        luaL_pushresultsize(&b, (size_t)got);
        return 1;
    }
}

static int fh_close(lua_State *L)
{
    PHYSFS_File **fp = (PHYSFS_File **)luaL_checkudata(L, 1, PHYSFS_FILE_MT);
    if (*fp)
    {
        PHYSFS_close(*fp);
        *fp = NULL;
    }
    return 0;
}

static int fh_size(lua_State *L)
{
    PHYSFS_File *f = check_file(L, 1);
    PHYSFS_sint64 len = PHYSFS_fileLength(f);
    lua_pushinteger(L, (lua_Integer)len);
    return 1;
}

static int fh_tell(lua_State *L)
{
    PHYSFS_File *f = check_file(L, 1);
    PHYSFS_sint64 pos = PHYSFS_tell(f);
    lua_pushinteger(L, (lua_Integer)pos);
    return 1;
}

static int fh_seek(lua_State *L)
{
    PHYSFS_File *f = check_file(L, 1);
    lua_Integer pos = luaL_checkinteger(L, 2);
    lua_pushboolean(L, PHYSFS_seek(f, (PHYSFS_uint64)pos));
    return 1;
}

static int fh_eof(lua_State *L)
{
    PHYSFS_File *f = check_file(L, 1);
    lua_pushboolean(L, PHYSFS_eof(f));
    return 1;
}

static int fh_gc(lua_State *L)
{
    return fh_close(L);
}

static int fh_tostring(lua_State *L)
{
    PHYSFS_File **fp = (PHYSFS_File **)luaL_checkudata(L, 1, PHYSFS_FILE_MT);
    if (*fp)
    {
        lua_pushfstring(L, "PhysFS.File (%p)", *fp);
    }
    else
    {
        lua_pushliteral(L, "PhysFS.File (closed)");
    }
    return 1;
}

static const luaL_Reg fh_methods[] = {
    {"read", fh_read},
    {"close", fh_close},
    {"size", fh_size},
    {"tell", fh_tell},
    {"seek", fh_seek},
    {"eof", fh_eof},
    {NULL, NULL}};

static const luaL_Reg fh_meta[] = {
    {"__gc", fh_gc},
    {"__close", fh_gc},
    {"__tostring", fh_tostring},
    {NULL, NULL}};

/* =====================================================================
 *  physfs.openRead(path) → filehandle | nil
 * ===================================================================== */
static int l_openRead(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    PHYSFS_File *f = PHYSFS_openRead(path);
    if (!f)
    {
        lua_pushnil(L);
        return 1;
    }
    PHYSFS_File **fp = (PHYSFS_File **)lua_newuserdata(L, sizeof(PHYSFS_File *));
    *fp = f;
    luaL_getmetatable(L, PHYSFS_FILE_MT);
    lua_setmetatable(L, -2);
    return 1;
}

/* =====================================================================
 *  physfs.openWrite(path) → filehandle | nil
 * ===================================================================== */
static int l_openWrite(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    PHYSFS_File *f = PHYSFS_openWrite(path);
    if (!f)
    {
        return push_physfs_error(L);
    }
    PHYSFS_File **fp = (PHYSFS_File **)lua_newuserdata(L, sizeof(PHYSFS_File *));
    *fp = f;
    luaL_getmetatable(L, PHYSFS_FILE_MT);
    lua_setmetatable(L, -2);
    return 1;
}

/* =====================================================================
 *  physfs.writeBytes(filehandle, data) → boolean
 * (convenience, also available as fh:write in future)
 * ===================================================================== */

/* =====================================================================
 *  Module registration
 * ===================================================================== */
static const luaL_Reg physfs_funcs[] = {
    {"init", l_init},
    {"deinit", l_deinit},
    {"mount", l_mount},
    {"unmount", l_unmount},
    {"exists", l_exists},
    {"realDir", l_realDir},
    {"files", l_files},
    {"openRead", l_openRead},
    {"openWrite", l_openWrite},
    {"setWriteDir", l_setWriteDir},
    {"getWriteDir", l_getWriteDir},
    {"mkdir", l_mkdir},
    {"getBaseDir", l_getBaseDir},
    {"getPrefDir", l_getPrefDir},
    {"stat", l_stat},
    {NULL, NULL}};

/* Auto-init PhysFS when the module is loaded */
int luaopen_physfs(lua_State *L)
{
    /* Create file handle metatable */
    luaL_newmetatable(L, PHYSFS_FILE_MT);
    /* methods */
    lua_newtable(L);
    luaL_setfuncs(L, fh_methods, 0);
    lua_setfield(L, -2, "__index");
    /* metamethods */
    luaL_setfuncs(L, fh_meta, 0);
#if LUA_VERSION_NUM >= 504
    lua_pushcfunction(L, fh_gc);
    lua_setfield(L, -2, "__close");
#endif
    lua_pop(L, 1); /* pop metatable */

    /* Auto-initialize PhysFS */
    if (!PHYSFS_isInit())
    {
        PHYSFS_init(NULL);
    }

    /* Create module table */
    luaL_newlib(L, physfs_funcs);

    /* Version info */
    PHYSFS_Version ver;
    PHYSFS_getLinkedVersion(&ver);
    lua_pushfstring(L, "%d.%d.%d", ver.major, ver.minor, ver.patch);
    lua_setfield(L, -2, "version");

    return 1;
}
