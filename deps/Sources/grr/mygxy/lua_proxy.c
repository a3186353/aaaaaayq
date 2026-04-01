#include "lua_proxy.h"
#if defined(__ANDROID__)
#include <dlfcn.h>
#include <stdio.h>
#if defined(__ANDROID__)
#include <android/log.h>
#define LOG_TAG "MYGXY_LUA_PROXY"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGE(...) printf(__VA_ARGS__)
#endif

#define LUA_PROXY_SYMBOLS(X) \
    X(lua_newstate, PFN_lua_newstate) \
    X(lua_close, PFN_lua_close) \
    X(lua_newthread, PFN_lua_newthread) \
    X(lua_resetthread, PFN_lua_resetthread) \
    X(lua_atpanic, PFN_lua_atpanic) \
    X(lua_version, PFN_lua_version) \
    X(lua_absindex, PFN_lua_absindex) \
    X(lua_gettop, PFN_lua_gettop) \
    X(lua_settop, PFN_lua_settop) \
    X(lua_pushvalue, PFN_lua_pushvalue) \
    X(lua_rotate, PFN_lua_rotate) \
    X(lua_copy, PFN_lua_copy) \
    X(lua_checkstack, PFN_lua_checkstack) \
    X(lua_xmove, PFN_lua_xmove) \
    X(lua_isnumber, PFN_lua_isnumber) \
    X(lua_isstring, PFN_lua_isstring) \
    X(lua_iscfunction, PFN_lua_iscfunction) \
    X(lua_isinteger, PFN_lua_isinteger) \
    X(lua_isuserdata, PFN_lua_isuserdata) \
    X(lua_type, PFN_lua_type) \
    X(lua_typename, PFN_lua_typename) \
    X(lua_tonumberx, PFN_lua_tonumberx) \
    X(lua_tointegerx, PFN_lua_tointegerx) \
    X(lua_toboolean, PFN_lua_toboolean) \
    X(lua_tolstring, PFN_lua_tolstring) \
    X(lua_rawlen, PFN_lua_rawlen) \
    X(lua_tocfunction, PFN_lua_tocfunction) \
    X(lua_touserdata, PFN_lua_touserdata) \
    X(lua_tothread, PFN_lua_tothread) \
    X(lua_topointer, PFN_lua_topointer) \
    X(lua_arith, PFN_lua_arith) \
    X(lua_rawequal, PFN_lua_rawequal) \
    X(lua_compare, PFN_lua_compare) \
    X(lua_pushnil, PFN_lua_pushnil) \
    X(lua_pushnumber, PFN_lua_pushnumber) \
    X(lua_pushinteger, PFN_lua_pushinteger) \
    X(lua_pushlstring, PFN_lua_pushlstring) \
    X(lua_pushstring, PFN_lua_pushstring) \
    X(lua_pushvfstring, PFN_lua_pushvfstring) \
    X(lua_pushfstring, PFN_lua_pushfstring) \
    X(lua_pushcclosure, PFN_lua_pushcclosure) \
    X(lua_pushboolean, PFN_lua_pushboolean) \
    X(lua_pushlightuserdata, PFN_lua_pushlightuserdata) \
    X(lua_pushthread, PFN_lua_pushthread) \
    X(lua_getglobal, PFN_lua_getglobal) \
    X(lua_gettable, PFN_lua_gettable) \
    X(lua_getfield, PFN_lua_getfield) \
    X(lua_geti, PFN_lua_geti) \
    X(lua_rawget, PFN_lua_rawget) \
    X(lua_rawgeti, PFN_lua_rawgeti) \
    X(lua_rawgetp, PFN_lua_rawgetp) \
    X(lua_createtable, PFN_lua_createtable) \
    X(lua_newuserdatauv, PFN_lua_newuserdatauv) \
    X(lua_getmetatable, PFN_lua_getmetatable) \
    X(lua_getiuservalue, PFN_lua_getiuservalue) \
    X(lua_setglobal, PFN_lua_setglobal) \
    X(lua_settable, PFN_lua_settable) \
    X(lua_setfield, PFN_lua_setfield) \
    X(lua_seti, PFN_lua_seti) \
    X(lua_rawset, PFN_lua_rawset) \
    X(lua_rawseti, PFN_lua_rawseti) \
    X(lua_rawsetp, PFN_lua_rawsetp) \
    X(lua_setmetatable, PFN_lua_setmetatable) \
    X(lua_setiuservalue, PFN_lua_setiuservalue) \
    X(lua_callk, PFN_lua_callk) \
    X(lua_pcallk, PFN_lua_pcallk) \
    X(lua_load, PFN_lua_load) \
    X(lua_dump, PFN_lua_dump) \
    X(lua_yieldk, PFN_lua_yieldk) \
    X(lua_resume, PFN_lua_resume) \
    X(lua_status, PFN_lua_status) \
    X(lua_isyieldable, PFN_lua_isyieldable) \
    X(lua_setwarnf, PFN_lua_setwarnf) \
    X(lua_warning, PFN_lua_warning) \
    X(lua_gc, PFN_lua_gc) \
    X(lua_error, PFN_lua_error) \
    X(lua_next, PFN_lua_next) \
    X(lua_concat, PFN_lua_concat) \
    X(lua_len, PFN_lua_len) \
    X(lua_stringtonumber, PFN_lua_stringtonumber) \
    X(lua_getallocf, PFN_lua_getallocf) \
    X(lua_setallocf, PFN_lua_setallocf) \
    X(lua_toclose, PFN_lua_toclose) \
    X(lua_closeslot, PFN_lua_closeslot) \
    X(lua_getstack, PFN_lua_getstack) \
    X(lua_getinfo, PFN_lua_getinfo) \
    X(lua_getlocal, PFN_lua_getlocal) \
    X(lua_setlocal, PFN_lua_setlocal) \
    X(lua_getupvalue, PFN_lua_getupvalue) \
    X(lua_setupvalue, PFN_lua_setupvalue) \
    X(lua_upvalueid, PFN_lua_upvalueid) \
    X(lua_upvaluejoin, PFN_lua_upvaluejoin) \
    X(lua_sethook, PFN_lua_sethook) \
    X(lua_gethook, PFN_lua_gethook) \
    X(lua_gethookmask, PFN_lua_gethookmask) \
    X(lua_gethookcount, PFN_lua_gethookcount) \
    X(lua_setcstacklimit, PFN_lua_setcstacklimit) \
    X(luaL_checkversion_, PFN_luaL_checkversion_) \
    X(luaL_getmetafield, PFN_luaL_getmetafield) \
    X(luaL_callmeta, PFN_luaL_callmeta) \
    X(luaL_tolstring, PFN_luaL_tolstring) \
    X(luaL_argerror, PFN_luaL_argerror) \
    X(luaL_typeerror, PFN_luaL_typeerror) \
    X(luaL_checklstring, PFN_luaL_checklstring) \
    X(luaL_optlstring, PFN_luaL_optlstring) \
    X(luaL_checknumber, PFN_luaL_checknumber) \
    X(luaL_optnumber, PFN_luaL_optnumber) \
    X(luaL_checkinteger, PFN_luaL_checkinteger) \
    X(luaL_optinteger, PFN_luaL_optinteger) \
    X(luaL_checkstack, PFN_luaL_checkstack) \
    X(luaL_checktype, PFN_luaL_checktype) \
    X(luaL_checkany, PFN_luaL_checkany) \
    X(luaL_newmetatable, PFN_luaL_newmetatable) \
    X(luaL_setmetatable, PFN_luaL_setmetatable) \
    X(luaL_testudata, PFN_luaL_testudata) \
    X(luaL_checkudata, PFN_luaL_checkudata) \
    X(luaL_where, PFN_luaL_where) \
    X(luaL_error, PFN_luaL_error) \
    X(luaL_checkoption, PFN_luaL_checkoption) \
    X(luaL_fileresult, PFN_luaL_fileresult) \
    X(luaL_execresult, PFN_luaL_execresult) \
    X(luaL_ref, PFN_luaL_ref) \
    X(luaL_unref, PFN_luaL_unref) \
    X(luaL_loadfilex, PFN_luaL_loadfilex) \
    X(luaL_loadbufferx, PFN_luaL_loadbufferx) \
    X(luaL_loadstring, PFN_luaL_loadstring) \
    X(luaL_newstate, PFN_luaL_newstate) \
    X(luaL_len, PFN_luaL_len) \
    X(luaL_addgsub, PFN_luaL_addgsub) \
    X(luaL_gsub, PFN_luaL_gsub) \
    X(luaL_setfuncs, PFN_luaL_setfuncs) \
    X(luaL_getsubtable, PFN_luaL_getsubtable) \
    X(luaL_traceback, PFN_luaL_traceback) \
    X(luaL_requiref, PFN_luaL_requiref) \
    X(luaL_buffinit, PFN_luaL_buffinit) \
    X(luaL_prepbuffsize, PFN_luaL_prepbuffsize) \
    X(luaL_addlstring, PFN_luaL_addlstring) \
    X(luaL_addstring, PFN_luaL_addstring) \
    X(luaL_addvalue, PFN_luaL_addvalue) \
    X(luaL_pushresult, PFN_luaL_pushresult) \
    X(luaL_pushresultsize, PFN_luaL_pushresultsize) \
    X(luaL_buffinitsize, PFN_luaL_buffinitsize) \

#define DECLARE_LUA_PROXY(name, type) type proxy_##name = NULL;
LUA_PROXY_SYMBOLS(DECLARE_LUA_PROXY)


#if defined(_MSC_VER)
#pragma section(".CRT$XCU",read)
static void __cdecl _init_lua_proxy(void);
__declspec(allocate(".CRT$XCU")) void (__cdecl *_init_lua_proxy_) (void) = _init_lua_proxy;
static void __cdecl _init_lua_proxy(void) { init_lua_proxy(); }
#else
__attribute__((constructor))
#endif

#if defined(_MSC_VER)
#pragma section(".CRT$XCU",read)
static void __cdecl _init_lua_proxy(void);
__declspec(allocate(".CRT$XCU")) void (__cdecl *_init_lua_proxy_) (void) = _init_lua_proxy;
static void __cdecl _init_lua_proxy(void) { init_lua_proxy(); }
#else
__attribute__((constructor))
#endif

#if defined(_MSC_VER)
#pragma section(".CRT$XCU",read)
static void __cdecl _init_lua_proxy(void);
__declspec(allocate(".CRT$XCU")) void (__cdecl *_init_lua_proxy_) (void) = _init_lua_proxy;
static void __cdecl _init_lua_proxy(void) { init_lua_proxy(); }
#else
__attribute__((constructor))
#endif
void init_lua_proxy(void) {
    static int is_initialized = 0;
    if (is_initialized) return;
    is_initialized = 1;

    // Search order for Lua symbols:
    // 1. RTLD_DEFAULT  - globally exported shared library symbols
    // 2. dlopen(NULL)  - entire process symbol space (finds statically linked Lua)
    // 3. Platform-specific library names (Android .so / iOS .framework)
    void* handle = RTLD_DEFAULT;
    void* self_handle = dlopen(NULL, RTLD_LAZY); // process-wide symbol table
    
    void* gge_handle = NULL;
    void* lua_handle = NULL;
#if defined(__ANDROID__)
    gge_handle = dlopen("libggelua.so", RTLD_LAZY | RTLD_GLOBAL);
    lua_handle = dlopen("liblua54.so", RTLD_LAZY | RTLD_GLOBAL);
#elif defined(__APPLE__)
    // iOS: try framework paths, then RTLD_MAIN_ONLY for statically linked symbols
    gge_handle = dlopen("libggelua.framework/libggelua", RTLD_LAZY | RTLD_GLOBAL);
    if (!gge_handle) gge_handle = dlopen("libggelua", RTLD_LAZY | RTLD_GLOBAL);
    if (!gge_handle) gge_handle = RTLD_MAIN_ONLY;
    lua_handle = dlopen("liblua54.framework/liblua54", RTLD_LAZY | RTLD_GLOBAL);
    if (!lua_handle) lua_handle = dlopen("liblua54", RTLD_LAZY | RTLD_GLOBAL);
#endif
    
    // Macro for 4-level symbol lookup: RTLD_DEFAULT -> dlopen(NULL) -> engine lib -> lua lib
    #define RESOLVE_LUA_PROXY(name, type) \
        proxy_##name = (type)dlsym(handle, #name); \
        if (!proxy_##name && self_handle) proxy_##name = (type)dlsym(self_handle, #name); \
        if (!proxy_##name && gge_handle) proxy_##name = (type)dlsym(gge_handle, #name); \
        if (!proxy_##name && lua_handle) proxy_##name = (type)dlsym(lua_handle, #name); \
        if (!proxy_##name) LOGE("Failed to resolve Lua symbol: " #name "\n");

    LUA_PROXY_SYMBOLS(RESOLVE_LUA_PROXY)
}

#endif // Android Proxy Guards#define LUA_PROXY_SYMBOLS(X) \

#define DECLARE_LUA_PROXY(name, type) type proxy_##name = NULL;
LUA_PROXY_SYMBOLS(DECLARE_LUA_PROXY)#define LUA_PROXY_SYMBOLS(X) \

#define DECLARE_LUA_PROXY(name, type) type proxy_##name = NULL;
LUA_PROXY_SYMBOLS(DECLARE_LUA_PROXY)
