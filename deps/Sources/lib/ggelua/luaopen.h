#pragma once
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

/*
** iOS/macOS 使用 Mach-O 格式（非 ELF），luaconf.h 中的 LUA_API / LUAMOD_API
** 退化为纯 extern，不带 visibility 属性。当工具链设置 -fvisibility=hidden 时，
** 所有符号默认隐藏，导致 DYLD 在启动时找不到 luaopen_ggelua 而 SIGABRT。
** GGE_EXPORT 显式标记需要导出的符号。
*/
#if defined(__GNUC__) || defined(__clang__)
  #define GGE_EXPORT __attribute__((visibility("default")))
#else
  #define GGE_EXPORT
#endif

unsigned int g_tohash(const char* path);

int luaopen_tohash(lua_State* L);

int luaopen_ggescript(lua_State* L);
int luaopen_zlib(lua_State* L);
int luaopen_md5(lua_State* L);
int luaopen_base64(lua_State* L);
int luaopen_cmsgpack(lua_State* L);
int luaopen_cmsgpack_safe(lua_State* L);
int luaopen_lfs(lua_State* L);
int luaopen_uuid(lua_State* L);
int luaopen_cprint(lua_State* L);
int luaopen_nanoid(lua_State* L);
int luaopen_aes(lua_State* L);

// ghv (libhv bindings)
int luaopen_ghv_TcpClient(lua_State* L);
int luaopen_ghv_TcpServer(lua_State* L);
int luaopen_ghv_HttpRequests(lua_State* L);
int luaopen_ghv_download(lua_State* L);

// gge.core (脚本加解密引擎, 内嵌于 libggelua)
int luaopen_ggecore(lua_State* L);

GGE_EXPORT int luaopen_ggelua(lua_State* L);