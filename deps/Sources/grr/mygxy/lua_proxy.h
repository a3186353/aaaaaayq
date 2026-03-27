#ifndef LUA_PROXY_H
#define LUA_PROXY_H

#if defined(__ANDROID__) || defined(__APPLE__)

#include "lua.h"
#include "lauxlib.h"

typedef lua_State * (*PFN_lua_newstate)(lua_Alloc f, void *ud);
extern PFN_lua_newstate proxy_lua_newstate;
#undef lua_newstate
#define lua_newstate proxy_lua_newstate

typedef void (*PFN_lua_close)(lua_State *L);
extern PFN_lua_close proxy_lua_close;
#undef lua_close
#define lua_close proxy_lua_close

typedef lua_State * (*PFN_lua_newthread)(lua_State *L);
extern PFN_lua_newthread proxy_lua_newthread;
#undef lua_newthread
#define lua_newthread proxy_lua_newthread

typedef int (*PFN_lua_resetthread)(lua_State *L);
extern PFN_lua_resetthread proxy_lua_resetthread;
#undef lua_resetthread
#define lua_resetthread proxy_lua_resetthread

typedef lua_CFunction (*PFN_lua_atpanic)(lua_State *L, lua_CFunction panicf);
extern PFN_lua_atpanic proxy_lua_atpanic;
#undef lua_atpanic
#define lua_atpanic proxy_lua_atpanic

typedef lua_Number (*PFN_lua_version)(lua_State *L);
extern PFN_lua_version proxy_lua_version;
#undef lua_version
#define lua_version proxy_lua_version

typedef int (*PFN_lua_absindex)(lua_State *L, int idx);
extern PFN_lua_absindex proxy_lua_absindex;
#undef lua_absindex
#define lua_absindex proxy_lua_absindex

typedef int (*PFN_lua_gettop)(lua_State *L);
extern PFN_lua_gettop proxy_lua_gettop;
#undef lua_gettop
#define lua_gettop proxy_lua_gettop

typedef void (*PFN_lua_settop)(lua_State *L, int idx);
extern PFN_lua_settop proxy_lua_settop;
#undef lua_settop
#define lua_settop proxy_lua_settop

typedef void (*PFN_lua_pushvalue)(lua_State *L, int idx);
extern PFN_lua_pushvalue proxy_lua_pushvalue;
#undef lua_pushvalue
#define lua_pushvalue proxy_lua_pushvalue

typedef void (*PFN_lua_rotate)(lua_State *L, int idx, int n);
extern PFN_lua_rotate proxy_lua_rotate;
#undef lua_rotate
#define lua_rotate proxy_lua_rotate

typedef void (*PFN_lua_copy)(lua_State *L, int fromidx, int toidx);
extern PFN_lua_copy proxy_lua_copy;
#undef lua_copy
#define lua_copy proxy_lua_copy

typedef int (*PFN_lua_checkstack)(lua_State *L, int n);
extern PFN_lua_checkstack proxy_lua_checkstack;
#undef lua_checkstack
#define lua_checkstack proxy_lua_checkstack

typedef void (*PFN_lua_xmove)(lua_State *from, lua_State *to, int n);
extern PFN_lua_xmove proxy_lua_xmove;
#undef lua_xmove
#define lua_xmove proxy_lua_xmove

typedef int (*PFN_lua_isnumber)(lua_State *L, int idx);
extern PFN_lua_isnumber proxy_lua_isnumber;
#undef lua_isnumber
#define lua_isnumber proxy_lua_isnumber

typedef int (*PFN_lua_isstring)(lua_State *L, int idx);
extern PFN_lua_isstring proxy_lua_isstring;
#undef lua_isstring
#define lua_isstring proxy_lua_isstring

typedef int (*PFN_lua_iscfunction)(lua_State *L, int idx);
extern PFN_lua_iscfunction proxy_lua_iscfunction;
#undef lua_iscfunction
#define lua_iscfunction proxy_lua_iscfunction

typedef int (*PFN_lua_isinteger)(lua_State *L, int idx);
extern PFN_lua_isinteger proxy_lua_isinteger;
#undef lua_isinteger
#define lua_isinteger proxy_lua_isinteger

typedef int (*PFN_lua_isuserdata)(lua_State *L, int idx);
extern PFN_lua_isuserdata proxy_lua_isuserdata;
#undef lua_isuserdata
#define lua_isuserdata proxy_lua_isuserdata

typedef int (*PFN_lua_type)(lua_State *L, int idx);
extern PFN_lua_type proxy_lua_type;
#undef lua_type
#define lua_type proxy_lua_type

typedef const char * (*PFN_lua_typename)(lua_State *L, int tp);
extern PFN_lua_typename proxy_lua_typename;
#undef lua_typename
#define lua_typename proxy_lua_typename

typedef lua_Number (*PFN_lua_tonumberx)(lua_State *L, int idx, int *isnum);
extern PFN_lua_tonumberx proxy_lua_tonumberx;
#undef lua_tonumberx
#define lua_tonumberx proxy_lua_tonumberx

typedef lua_Integer (*PFN_lua_tointegerx)(lua_State *L, int idx, int *isnum);
extern PFN_lua_tointegerx proxy_lua_tointegerx;
#undef lua_tointegerx
#define lua_tointegerx proxy_lua_tointegerx

typedef int (*PFN_lua_toboolean)(lua_State *L, int idx);
extern PFN_lua_toboolean proxy_lua_toboolean;
#undef lua_toboolean
#define lua_toboolean proxy_lua_toboolean

typedef const char * (*PFN_lua_tolstring)(lua_State *L, int idx, size_t *len);
extern PFN_lua_tolstring proxy_lua_tolstring;
#undef lua_tolstring
#define lua_tolstring proxy_lua_tolstring

typedef lua_Unsigned (*PFN_lua_rawlen)(lua_State *L, int idx);
extern PFN_lua_rawlen proxy_lua_rawlen;
#undef lua_rawlen
#define lua_rawlen proxy_lua_rawlen

typedef lua_CFunction (*PFN_lua_tocfunction)(lua_State *L, int idx);
extern PFN_lua_tocfunction proxy_lua_tocfunction;
#undef lua_tocfunction
#define lua_tocfunction proxy_lua_tocfunction

typedef void * (*PFN_lua_touserdata)(lua_State *L, int idx);
extern PFN_lua_touserdata proxy_lua_touserdata;
#undef lua_touserdata
#define lua_touserdata proxy_lua_touserdata

typedef lua_State * (*PFN_lua_tothread)(lua_State *L, int idx);
extern PFN_lua_tothread proxy_lua_tothread;
#undef lua_tothread
#define lua_tothread proxy_lua_tothread

typedef const void * (*PFN_lua_topointer)(lua_State *L, int idx);
extern PFN_lua_topointer proxy_lua_topointer;
#undef lua_topointer
#define lua_topointer proxy_lua_topointer

typedef void (*PFN_lua_arith)(lua_State *L, int op);
extern PFN_lua_arith proxy_lua_arith;
#undef lua_arith
#define lua_arith proxy_lua_arith

typedef int (*PFN_lua_rawequal)(lua_State *L, int idx1, int idx2);
extern PFN_lua_rawequal proxy_lua_rawequal;
#undef lua_rawequal
#define lua_rawequal proxy_lua_rawequal

typedef int (*PFN_lua_compare)(lua_State *L, int idx1, int idx2, int op);
extern PFN_lua_compare proxy_lua_compare;
#undef lua_compare
#define lua_compare proxy_lua_compare

typedef void (*PFN_lua_pushnil)(lua_State *L);
extern PFN_lua_pushnil proxy_lua_pushnil;
#undef lua_pushnil
#define lua_pushnil proxy_lua_pushnil

typedef void (*PFN_lua_pushnumber)(lua_State *L, lua_Number n);
extern PFN_lua_pushnumber proxy_lua_pushnumber;
#undef lua_pushnumber
#define lua_pushnumber proxy_lua_pushnumber

typedef void (*PFN_lua_pushinteger)(lua_State *L, lua_Integer n);
extern PFN_lua_pushinteger proxy_lua_pushinteger;
#undef lua_pushinteger
#define lua_pushinteger proxy_lua_pushinteger

typedef const char * (*PFN_lua_pushlstring)(lua_State *L, const char *s, size_t len);
extern PFN_lua_pushlstring proxy_lua_pushlstring;
#undef lua_pushlstring
#define lua_pushlstring proxy_lua_pushlstring

typedef const char * (*PFN_lua_pushstring)(lua_State *L, const char *s);
extern PFN_lua_pushstring proxy_lua_pushstring;
#undef lua_pushstring
#define lua_pushstring proxy_lua_pushstring

typedef const char * (*PFN_lua_pushvfstring)(lua_State *L, const char *fmt, va_list argp);
extern PFN_lua_pushvfstring proxy_lua_pushvfstring;
#undef lua_pushvfstring
#define lua_pushvfstring proxy_lua_pushvfstring

typedef const char * (*PFN_lua_pushfstring)(lua_State *L, const char *fmt, ...);
extern PFN_lua_pushfstring proxy_lua_pushfstring;
#undef lua_pushfstring
#define lua_pushfstring proxy_lua_pushfstring

typedef void (*PFN_lua_pushcclosure)(lua_State *L, lua_CFunction fn, int n);
extern PFN_lua_pushcclosure proxy_lua_pushcclosure;
#undef lua_pushcclosure
#define lua_pushcclosure proxy_lua_pushcclosure

typedef void (*PFN_lua_pushboolean)(lua_State *L, int b);
extern PFN_lua_pushboolean proxy_lua_pushboolean;
#undef lua_pushboolean
#define lua_pushboolean proxy_lua_pushboolean

typedef void (*PFN_lua_pushlightuserdata)(lua_State *L, void *p);
extern PFN_lua_pushlightuserdata proxy_lua_pushlightuserdata;
#undef lua_pushlightuserdata
#define lua_pushlightuserdata proxy_lua_pushlightuserdata

typedef int (*PFN_lua_pushthread)(lua_State *L);
extern PFN_lua_pushthread proxy_lua_pushthread;
#undef lua_pushthread
#define lua_pushthread proxy_lua_pushthread

typedef int (*PFN_lua_getglobal)(lua_State *L, const char *name);
extern PFN_lua_getglobal proxy_lua_getglobal;
#undef lua_getglobal
#define lua_getglobal proxy_lua_getglobal

typedef int (*PFN_lua_gettable)(lua_State *L, int idx);
extern PFN_lua_gettable proxy_lua_gettable;
#undef lua_gettable
#define lua_gettable proxy_lua_gettable

typedef int (*PFN_lua_getfield)(lua_State *L, int idx, const char *k);
extern PFN_lua_getfield proxy_lua_getfield;
#undef lua_getfield
#define lua_getfield proxy_lua_getfield

typedef int (*PFN_lua_geti)(lua_State *L, int idx, lua_Integer n);
extern PFN_lua_geti proxy_lua_geti;
#undef lua_geti
#define lua_geti proxy_lua_geti

typedef int (*PFN_lua_rawget)(lua_State *L, int idx);
extern PFN_lua_rawget proxy_lua_rawget;
#undef lua_rawget
#define lua_rawget proxy_lua_rawget

typedef int (*PFN_lua_rawgeti)(lua_State *L, int idx, lua_Integer n);
extern PFN_lua_rawgeti proxy_lua_rawgeti;
#undef lua_rawgeti
#define lua_rawgeti proxy_lua_rawgeti

typedef int (*PFN_lua_rawgetp)(lua_State *L, int idx, const void *p);
extern PFN_lua_rawgetp proxy_lua_rawgetp;
#undef lua_rawgetp
#define lua_rawgetp proxy_lua_rawgetp

typedef void (*PFN_lua_createtable)(lua_State *L, int narr, int nrec);
extern PFN_lua_createtable proxy_lua_createtable;
#undef lua_createtable
#define lua_createtable proxy_lua_createtable

typedef void * (*PFN_lua_newuserdatauv)(lua_State *L, size_t sz, int nuvalue);
extern PFN_lua_newuserdatauv proxy_lua_newuserdatauv;
#undef lua_newuserdatauv
#define lua_newuserdatauv proxy_lua_newuserdatauv

typedef int (*PFN_lua_getmetatable)(lua_State *L, int objindex);
extern PFN_lua_getmetatable proxy_lua_getmetatable;
#undef lua_getmetatable
#define lua_getmetatable proxy_lua_getmetatable

typedef int (*PFN_lua_getiuservalue)(lua_State *L, int idx, int n);
extern PFN_lua_getiuservalue proxy_lua_getiuservalue;
#undef lua_getiuservalue
#define lua_getiuservalue proxy_lua_getiuservalue

typedef void (*PFN_lua_setglobal)(lua_State *L, const char *name);
extern PFN_lua_setglobal proxy_lua_setglobal;
#undef lua_setglobal
#define lua_setglobal proxy_lua_setglobal

typedef void (*PFN_lua_settable)(lua_State *L, int idx);
extern PFN_lua_settable proxy_lua_settable;
#undef lua_settable
#define lua_settable proxy_lua_settable

typedef void (*PFN_lua_setfield)(lua_State *L, int idx, const char *k);
extern PFN_lua_setfield proxy_lua_setfield;
#undef lua_setfield
#define lua_setfield proxy_lua_setfield

typedef void (*PFN_lua_seti)(lua_State *L, int idx, lua_Integer n);
extern PFN_lua_seti proxy_lua_seti;
#undef lua_seti
#define lua_seti proxy_lua_seti

typedef void (*PFN_lua_rawset)(lua_State *L, int idx);
extern PFN_lua_rawset proxy_lua_rawset;
#undef lua_rawset
#define lua_rawset proxy_lua_rawset

typedef void (*PFN_lua_rawseti)(lua_State *L, int idx, lua_Integer n);
extern PFN_lua_rawseti proxy_lua_rawseti;
#undef lua_rawseti
#define lua_rawseti proxy_lua_rawseti

typedef void (*PFN_lua_rawsetp)(lua_State *L, int idx, const void *p);
extern PFN_lua_rawsetp proxy_lua_rawsetp;
#undef lua_rawsetp
#define lua_rawsetp proxy_lua_rawsetp

typedef int (*PFN_lua_setmetatable)(lua_State *L, int objindex);
extern PFN_lua_setmetatable proxy_lua_setmetatable;
#undef lua_setmetatable
#define lua_setmetatable proxy_lua_setmetatable

typedef int (*PFN_lua_setiuservalue)(lua_State *L, int idx, int n);
extern PFN_lua_setiuservalue proxy_lua_setiuservalue;
#undef lua_setiuservalue
#define lua_setiuservalue proxy_lua_setiuservalue

typedef void (*PFN_lua_callk)(lua_State *L, int nargs, int nresults, lua_KContext ctx, lua_KFunction k);
extern PFN_lua_callk proxy_lua_callk;
#undef lua_callk
#define lua_callk proxy_lua_callk

typedef int (*PFN_lua_pcallk)(lua_State *L, int nargs, int nresults, int errfunc, lua_KContext ctx, lua_KFunction k);
extern PFN_lua_pcallk proxy_lua_pcallk;
#undef lua_pcallk
#define lua_pcallk proxy_lua_pcallk

typedef int (*PFN_lua_load)(lua_State *L, lua_Reader reader, void *dt, const char *chunkname, const char *mode);
extern PFN_lua_load proxy_lua_load;
#undef lua_load
#define lua_load proxy_lua_load

typedef int (*PFN_lua_dump)(lua_State *L, lua_Writer writer, void *data, int strip);
extern PFN_lua_dump proxy_lua_dump;
#undef lua_dump
#define lua_dump proxy_lua_dump

typedef int (*PFN_lua_yieldk)(lua_State *L, int nresults, lua_KContext ctx, lua_KFunction k);
extern PFN_lua_yieldk proxy_lua_yieldk;
#undef lua_yieldk
#define lua_yieldk proxy_lua_yieldk

typedef int (*PFN_lua_resume)(lua_State *L, lua_State *from, int narg, int *nres);
extern PFN_lua_resume proxy_lua_resume;
#undef lua_resume
#define lua_resume proxy_lua_resume

typedef int (*PFN_lua_status)(lua_State *L);
extern PFN_lua_status proxy_lua_status;
#undef lua_status
#define lua_status proxy_lua_status

typedef int (*PFN_lua_isyieldable)(lua_State *L);
extern PFN_lua_isyieldable proxy_lua_isyieldable;
#undef lua_isyieldable
#define lua_isyieldable proxy_lua_isyieldable

typedef void (*PFN_lua_setwarnf)(lua_State *L, lua_WarnFunction f, void *ud);
extern PFN_lua_setwarnf proxy_lua_setwarnf;
#undef lua_setwarnf
#define lua_setwarnf proxy_lua_setwarnf

typedef void (*PFN_lua_warning)(lua_State *L, const char *msg, int tocont);
extern PFN_lua_warning proxy_lua_warning;
#undef lua_warning
#define lua_warning proxy_lua_warning

typedef int (*PFN_lua_gc)(lua_State *L, int what, ...);
extern PFN_lua_gc proxy_lua_gc;
#undef lua_gc
#define lua_gc proxy_lua_gc

typedef int (*PFN_lua_error)(lua_State *L);
extern PFN_lua_error proxy_lua_error;
#undef lua_error
#define lua_error proxy_lua_error

typedef int (*PFN_lua_next)(lua_State *L, int idx);
extern PFN_lua_next proxy_lua_next;
#undef lua_next
#define lua_next proxy_lua_next

typedef void (*PFN_lua_concat)(lua_State *L, int n);
extern PFN_lua_concat proxy_lua_concat;
#undef lua_concat
#define lua_concat proxy_lua_concat

typedef void (*PFN_lua_len)(lua_State *L, int idx);
extern PFN_lua_len proxy_lua_len;
#undef lua_len
#define lua_len proxy_lua_len

typedef size_t (*PFN_lua_stringtonumber)(lua_State *L, const char *s);
extern PFN_lua_stringtonumber proxy_lua_stringtonumber;
#undef lua_stringtonumber
#define lua_stringtonumber proxy_lua_stringtonumber

typedef lua_Alloc (*PFN_lua_getallocf)(lua_State *L, void **ud);
extern PFN_lua_getallocf proxy_lua_getallocf;
#undef lua_getallocf
#define lua_getallocf proxy_lua_getallocf

typedef void (*PFN_lua_setallocf)(lua_State *L, lua_Alloc f, void *ud);
extern PFN_lua_setallocf proxy_lua_setallocf;
#undef lua_setallocf
#define lua_setallocf proxy_lua_setallocf

typedef void (*PFN_lua_toclose)(lua_State *L, int idx);
extern PFN_lua_toclose proxy_lua_toclose;
#undef lua_toclose
#define lua_toclose proxy_lua_toclose

typedef void (*PFN_lua_closeslot)(lua_State *L, int idx);
extern PFN_lua_closeslot proxy_lua_closeslot;
#undef lua_closeslot
#define lua_closeslot proxy_lua_closeslot

typedef int (*PFN_lua_getstack)(lua_State *L, int level, lua_Debug *ar);
extern PFN_lua_getstack proxy_lua_getstack;
#undef lua_getstack
#define lua_getstack proxy_lua_getstack

typedef int (*PFN_lua_getinfo)(lua_State *L, const char *what, lua_Debug *ar);
extern PFN_lua_getinfo proxy_lua_getinfo;
#undef lua_getinfo
#define lua_getinfo proxy_lua_getinfo

typedef const char * (*PFN_lua_getlocal)(lua_State *L, const lua_Debug *ar, int n);
extern PFN_lua_getlocal proxy_lua_getlocal;
#undef lua_getlocal
#define lua_getlocal proxy_lua_getlocal

typedef const char * (*PFN_lua_setlocal)(lua_State *L, const lua_Debug *ar, int n);
extern PFN_lua_setlocal proxy_lua_setlocal;
#undef lua_setlocal
#define lua_setlocal proxy_lua_setlocal

typedef const char * (*PFN_lua_getupvalue)(lua_State *L, int funcindex, int n);
extern PFN_lua_getupvalue proxy_lua_getupvalue;
#undef lua_getupvalue
#define lua_getupvalue proxy_lua_getupvalue

typedef const char * (*PFN_lua_setupvalue)(lua_State *L, int funcindex, int n);
extern PFN_lua_setupvalue proxy_lua_setupvalue;
#undef lua_setupvalue
#define lua_setupvalue proxy_lua_setupvalue

typedef void * (*PFN_lua_upvalueid)(lua_State *L, int fidx, int n);
extern PFN_lua_upvalueid proxy_lua_upvalueid;
#undef lua_upvalueid
#define lua_upvalueid proxy_lua_upvalueid

typedef void (*PFN_lua_upvaluejoin)(lua_State *L, int fidx1, int n1, int fidx2, int n2);
extern PFN_lua_upvaluejoin proxy_lua_upvaluejoin;
#undef lua_upvaluejoin
#define lua_upvaluejoin proxy_lua_upvaluejoin

typedef void (*PFN_lua_sethook)(lua_State *L, lua_Hook func, int mask, int count);
extern PFN_lua_sethook proxy_lua_sethook;
#undef lua_sethook
#define lua_sethook proxy_lua_sethook

typedef lua_Hook (*PFN_lua_gethook)(lua_State *L);
extern PFN_lua_gethook proxy_lua_gethook;
#undef lua_gethook
#define lua_gethook proxy_lua_gethook

typedef int (*PFN_lua_gethookmask)(lua_State *L);
extern PFN_lua_gethookmask proxy_lua_gethookmask;
#undef lua_gethookmask
#define lua_gethookmask proxy_lua_gethookmask

typedef int (*PFN_lua_gethookcount)(lua_State *L);
extern PFN_lua_gethookcount proxy_lua_gethookcount;
#undef lua_gethookcount
#define lua_gethookcount proxy_lua_gethookcount

typedef int (*PFN_lua_setcstacklimit)(lua_State *L, unsigned int limit);
extern PFN_lua_setcstacklimit proxy_lua_setcstacklimit;
#undef lua_setcstacklimit
#define lua_setcstacklimit proxy_lua_setcstacklimit

typedef void (*PFN_luaL_checkversion_)(lua_State *L, lua_Number ver, size_t sz);
extern PFN_luaL_checkversion_ proxy_luaL_checkversion_;
#undef luaL_checkversion_
#define luaL_checkversion_ proxy_luaL_checkversion_

typedef int (*PFN_luaL_getmetafield)(lua_State *L, int obj, const char *e);
extern PFN_luaL_getmetafield proxy_luaL_getmetafield;
#undef luaL_getmetafield
#define luaL_getmetafield proxy_luaL_getmetafield

typedef int (*PFN_luaL_callmeta)(lua_State *L, int obj, const char *e);
extern PFN_luaL_callmeta proxy_luaL_callmeta;
#undef luaL_callmeta
#define luaL_callmeta proxy_luaL_callmeta

typedef const char * (*PFN_luaL_tolstring)(lua_State *L, int idx, size_t *len);
extern PFN_luaL_tolstring proxy_luaL_tolstring;
#undef luaL_tolstring
#define luaL_tolstring proxy_luaL_tolstring

typedef int (*PFN_luaL_argerror)(lua_State *L, int arg, const char *extramsg);
extern PFN_luaL_argerror proxy_luaL_argerror;
#undef luaL_argerror
#define luaL_argerror proxy_luaL_argerror

typedef int (*PFN_luaL_typeerror)(lua_State *L, int arg, const char *tname);
extern PFN_luaL_typeerror proxy_luaL_typeerror;
#undef luaL_typeerror
#define luaL_typeerror proxy_luaL_typeerror

typedef const char * (*PFN_luaL_checklstring)(lua_State *L, int arg, size_t *l);
extern PFN_luaL_checklstring proxy_luaL_checklstring;
#undef luaL_checklstring
#define luaL_checklstring proxy_luaL_checklstring

typedef const char * (*PFN_luaL_optlstring)(lua_State *L, int arg, const char *def, size_t *l);
extern PFN_luaL_optlstring proxy_luaL_optlstring;
#undef luaL_optlstring
#define luaL_optlstring proxy_luaL_optlstring

typedef lua_Number (*PFN_luaL_checknumber)(lua_State *L, int arg);
extern PFN_luaL_checknumber proxy_luaL_checknumber;
#undef luaL_checknumber
#define luaL_checknumber proxy_luaL_checknumber

typedef lua_Number (*PFN_luaL_optnumber)(lua_State *L, int arg, lua_Number def);
extern PFN_luaL_optnumber proxy_luaL_optnumber;
#undef luaL_optnumber
#define luaL_optnumber proxy_luaL_optnumber

typedef lua_Integer (*PFN_luaL_checkinteger)(lua_State *L, int arg);
extern PFN_luaL_checkinteger proxy_luaL_checkinteger;
#undef luaL_checkinteger
#define luaL_checkinteger proxy_luaL_checkinteger

typedef lua_Integer (*PFN_luaL_optinteger)(lua_State *L, int arg, lua_Integer def);
extern PFN_luaL_optinteger proxy_luaL_optinteger;
#undef luaL_optinteger
#define luaL_optinteger proxy_luaL_optinteger

typedef void (*PFN_luaL_checkstack)(lua_State *L, int sz, const char *msg);
extern PFN_luaL_checkstack proxy_luaL_checkstack;
#undef luaL_checkstack
#define luaL_checkstack proxy_luaL_checkstack

typedef void (*PFN_luaL_checktype)(lua_State *L, int arg, int t);
extern PFN_luaL_checktype proxy_luaL_checktype;
#undef luaL_checktype
#define luaL_checktype proxy_luaL_checktype

typedef void (*PFN_luaL_checkany)(lua_State *L, int arg);
extern PFN_luaL_checkany proxy_luaL_checkany;
#undef luaL_checkany
#define luaL_checkany proxy_luaL_checkany

typedef int (*PFN_luaL_newmetatable)(lua_State *L, const char *tname);
extern PFN_luaL_newmetatable proxy_luaL_newmetatable;
#undef luaL_newmetatable
#define luaL_newmetatable proxy_luaL_newmetatable

typedef void (*PFN_luaL_setmetatable)(lua_State *L, const char *tname);
extern PFN_luaL_setmetatable proxy_luaL_setmetatable;
#undef luaL_setmetatable
#define luaL_setmetatable proxy_luaL_setmetatable

typedef void * (*PFN_luaL_testudata)(lua_State *L, int ud, const char *tname);
extern PFN_luaL_testudata proxy_luaL_testudata;
#undef luaL_testudata
#define luaL_testudata proxy_luaL_testudata

typedef void * (*PFN_luaL_checkudata)(lua_State *L, int ud, const char *tname);
extern PFN_luaL_checkudata proxy_luaL_checkudata;
#undef luaL_checkudata
#define luaL_checkudata proxy_luaL_checkudata

typedef void (*PFN_luaL_where)(lua_State *L, int lvl);
extern PFN_luaL_where proxy_luaL_where;
#undef luaL_where
#define luaL_where proxy_luaL_where

typedef int (*PFN_luaL_error)(lua_State *L, const char *fmt, ...);
extern PFN_luaL_error proxy_luaL_error;
#undef luaL_error
#define luaL_error proxy_luaL_error

typedef int (*PFN_luaL_checkoption)(lua_State *L, int arg, const char *def, const char *const lst[]);
extern PFN_luaL_checkoption proxy_luaL_checkoption;
#undef luaL_checkoption
#define luaL_checkoption proxy_luaL_checkoption

typedef int (*PFN_luaL_fileresult)(lua_State *L, int stat, const char *fname);
extern PFN_luaL_fileresult proxy_luaL_fileresult;
#undef luaL_fileresult
#define luaL_fileresult proxy_luaL_fileresult

typedef int (*PFN_luaL_execresult)(lua_State *L, int stat);
extern PFN_luaL_execresult proxy_luaL_execresult;
#undef luaL_execresult
#define luaL_execresult proxy_luaL_execresult

typedef int (*PFN_luaL_ref)(lua_State *L, int t);
extern PFN_luaL_ref proxy_luaL_ref;
#undef luaL_ref
#define luaL_ref proxy_luaL_ref

typedef void (*PFN_luaL_unref)(lua_State *L, int t, int ref);
extern PFN_luaL_unref proxy_luaL_unref;
#undef luaL_unref
#define luaL_unref proxy_luaL_unref

typedef int (*PFN_luaL_loadfilex)(lua_State *L, const char *filename, const char *mode);
extern PFN_luaL_loadfilex proxy_luaL_loadfilex;
#undef luaL_loadfilex
#define luaL_loadfilex proxy_luaL_loadfilex

typedef int (*PFN_luaL_loadbufferx)(lua_State *L, const char *buff, size_t sz, const char *name, const char *mode);
extern PFN_luaL_loadbufferx proxy_luaL_loadbufferx;
#undef luaL_loadbufferx
#define luaL_loadbufferx proxy_luaL_loadbufferx

typedef int (*PFN_luaL_loadstring)(lua_State *L, const char *s);
extern PFN_luaL_loadstring proxy_luaL_loadstring;
#undef luaL_loadstring
#define luaL_loadstring proxy_luaL_loadstring

typedef lua_State * (*PFN_luaL_newstate)(void);
extern PFN_luaL_newstate proxy_luaL_newstate;
#undef luaL_newstate
#define luaL_newstate proxy_luaL_newstate

typedef lua_Integer (*PFN_luaL_len)(lua_State *L, int idx);
extern PFN_luaL_len proxy_luaL_len;
#undef luaL_len
#define luaL_len proxy_luaL_len

typedef void (*PFN_luaL_addgsub)(luaL_Buffer *b, const char *s, const char *p, const char *r);
extern PFN_luaL_addgsub proxy_luaL_addgsub;
#undef luaL_addgsub
#define luaL_addgsub proxy_luaL_addgsub

typedef const char * (*PFN_luaL_gsub)(lua_State *L, const char *s, const char *p, const char *r);
extern PFN_luaL_gsub proxy_luaL_gsub;
#undef luaL_gsub
#define luaL_gsub proxy_luaL_gsub

typedef void (*PFN_luaL_setfuncs)(lua_State *L, const luaL_Reg *l, int nup);
extern PFN_luaL_setfuncs proxy_luaL_setfuncs;
#undef luaL_setfuncs
#define luaL_setfuncs proxy_luaL_setfuncs

typedef int (*PFN_luaL_getsubtable)(lua_State *L, int idx, const char *fname);
extern PFN_luaL_getsubtable proxy_luaL_getsubtable;
#undef luaL_getsubtable
#define luaL_getsubtable proxy_luaL_getsubtable

typedef void (*PFN_luaL_traceback)(lua_State *L, lua_State *L1, const char *msg, int level);
extern PFN_luaL_traceback proxy_luaL_traceback;
#undef luaL_traceback
#define luaL_traceback proxy_luaL_traceback

typedef void (*PFN_luaL_requiref)(lua_State *L, const char *modname, lua_CFunction openf, int glb);
extern PFN_luaL_requiref proxy_luaL_requiref;
#undef luaL_requiref
#define luaL_requiref proxy_luaL_requiref

typedef void (*PFN_luaL_buffinit)(lua_State *L, luaL_Buffer *B);
extern PFN_luaL_buffinit proxy_luaL_buffinit;
#undef luaL_buffinit
#define luaL_buffinit proxy_luaL_buffinit

typedef char * (*PFN_luaL_prepbuffsize)(luaL_Buffer *B, size_t sz);
extern PFN_luaL_prepbuffsize proxy_luaL_prepbuffsize;
#undef luaL_prepbuffsize
#define luaL_prepbuffsize proxy_luaL_prepbuffsize

typedef void (*PFN_luaL_addlstring)(luaL_Buffer *B, const char *s, size_t l);
extern PFN_luaL_addlstring proxy_luaL_addlstring;
#undef luaL_addlstring
#define luaL_addlstring proxy_luaL_addlstring

typedef void (*PFN_luaL_addstring)(luaL_Buffer *B, const char *s);
extern PFN_luaL_addstring proxy_luaL_addstring;
#undef luaL_addstring
#define luaL_addstring proxy_luaL_addstring

typedef void (*PFN_luaL_addvalue)(luaL_Buffer *B);
extern PFN_luaL_addvalue proxy_luaL_addvalue;
#undef luaL_addvalue
#define luaL_addvalue proxy_luaL_addvalue

typedef void (*PFN_luaL_pushresult)(luaL_Buffer *B);
extern PFN_luaL_pushresult proxy_luaL_pushresult;
#undef luaL_pushresult
#define luaL_pushresult proxy_luaL_pushresult

typedef void (*PFN_luaL_pushresultsize)(luaL_Buffer *B, size_t sz);
extern PFN_luaL_pushresultsize proxy_luaL_pushresultsize;
#undef luaL_pushresultsize
#define luaL_pushresultsize proxy_luaL_pushresultsize

typedef char * (*PFN_luaL_buffinitsize)(lua_State *L, luaL_Buffer *B, size_t sz);
extern PFN_luaL_buffinitsize proxy_luaL_buffinitsize;
#undef luaL_buffinitsize
#define luaL_buffinitsize proxy_luaL_buffinitsize

void init_lua_proxy(void);

#else

#include "lua.h"
#include "lauxlib.h"
#define init_lua_proxy() 

#endif // iOS/Android Proxy Guards

#endif // LUA_PROXY_H
