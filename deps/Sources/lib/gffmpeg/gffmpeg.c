/*
 * gffmpeg.c — GGELUA3 FFmpeg 多媒体模块入口
 *
 * 职责：DLL 入口(luaopen_gffmpeg)、注册 Player/Recorder 的 Lua metatable 和模块函数
 */

#include "gffmpeg.h"

/* ==================== 模块级函数 ==================== */

/* 获取 FFmpeg 版本信息 */
static int LUA_GetVersion(lua_State *L)
{
    lua_pushfstring(L, "gffmpeg (avcodec %d.%d.%d)",
        LIBAVCODEC_VERSION_MAJOR,
        LIBAVCODEC_VERSION_MINOR,
        LIBAVCODEC_VERSION_MICRO);
    return 1;
}

/* ==================== 模块入口 ==================== */

static const luaL_Reg module_funcs[] = {
    /* 播放器 */
    {"PlayerOpen",          gff_player_open},
    /* 录音器 */
    {"RecorderOpen",        gff_recorder_open},
    {"ListCaptureDevices",  gff_list_capture_devices},
    /* 工具 */
    {"GetVersion",          LUA_GetVersion},
    {NULL, NULL}
};

LUALIB_API int luaopen_gffmpeg(lua_State *L)
{
    /* 注册 Player metatable 及其方法 */
    gff_player_register(L);

    /* 注册 Recorder metatable 及其方法 */
    gff_recorder_register(L);

    /* 创建模块表并注册函数 */
    luaL_newlib(L, module_funcs);

    return 1;
}
