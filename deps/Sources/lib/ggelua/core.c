/*
 * core.c — 脚本加解密引擎 (嵌入 libggelua)
 *
 * 与 mygxy/core.c 完全兼容的加解密算法，但直接使用标准 Lua API（无需 proxy 层）。
 * 注册为 package.preload["gge.core"]，在引擎初始化时立即可用，
 * 从根本上消除 ggelua.lua 引导阶段的跨库依赖死循环。
 *
 * 导出的 Lua 方法：
 *   gge.core.Seal(data)   — 用默认密钥加密
 *   gge.core.Open(data)   — 用默认密钥解密
 *   gge.core.Pack(data, key)  — 用指定密钥加密
 *   gge.core.Unpack(data, key) — 用指定密钥解密
 *   gge.core.Key()        — 获取默认密钥
 *   gge.core.Idx(str)     — 计算字符串哈希
 */
#include "luaopen.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * 核心算法 (与 mygxy/core.c 产生完全相同的密文/明文)
 * MBA_XOR(x,y) ≡ x^y,  MBA_ADD(x,y) ≡ x+y  (数学恒等式)
 * ============================================================================ */

static const volatile uint32_t C1 = 0x11111111;
static const volatile uint32_t C2 = 0x55555555;
static const volatile uint32_t C3 = 0xAAAAAAAA;

static uint32_t core_next_key(uint32_t seed)
{
    uint32_t m[4];
    m[0] = seed;
    m[1] = seed ^ (C1 ^ 0x8AF65559);
    m[2] = seed ^ (C2 ^ 0x33A1791D);
    m[3] = seed;

    uint32_t v   = C3 ^ 0x5E502382;
    uint32_t edi = C1 ^ 0x6649A53A;
    uint32_t esi = C2 ^ 0x62FD125B;

    for (int i = 0; i < 4; i++)
    {
        uint32_t ebp = C3 ^ 0x8CD1A1BB;

        /* rotate left 1 */
        v = (v >> 31) | (v << 1);
        ebp ^= v;

        uint32_t mi      = m[i];
        uint32_t old_esi = esi ^ mi;
        uint32_t old_edi = edi ^ mi;

        /* esi 更新 */
        uint32_t t1 = ebp + old_edi;
        t1 &= (C1 ^ 0xACFA66CF);
        t1 |= (C2 ^ 0x57515D54);
        uint64_t p1 = (uint64_t)t1 * (uint64_t)old_esi;
        uint32_t lo1 = (uint32_t)p1;
        uint32_t hi1 = (uint32_t)(p1 >> 32);
        uint32_t eax = lo1;
        if (hi1 != 0) eax = lo1 + 1;
        esi = hi1 + eax;

        /* edi 更新 */
        uint32_t t2 = ebp + old_esi;
        t2 &= (C3 ^ 0xD7D41174);
        t2 |= (C1 ^ 0x11915130);
        uint64_t p2 = (uint64_t)t2 * (uint64_t)old_edi;
        uint32_t lo2 = (uint32_t)p2;
        uint32_t hi2 = (uint32_t)(p2 >> 32);
        edi = lo2 + hi2 * 2;
    }
    return esi ^ edi;
}

static uint32_t core_default_seed(void)
{
    uint32_t s1 = C2 ^ 0x62FD125B;
    uint32_t s2 = C1 ^ 0x6649A53A;
    uint32_t s3 = C3 ^ 0x5E502382;
    return s1 ^ s2 ^ s3;
}

/* 对称加解密 (Seal == Open, Pack == Unpack) */
static void core_crypt(uint8_t *dst, const uint8_t *src, size_t len, uint32_t key_seed)
{
    uint32_t state = core_next_key(key_seed);
    size_t i;
    for (i = 0; i < len; i++)
    {
        uint8_t key_byte = (uint8_t)(state >> ((i & 3) * 8));
        uint8_t pos_byte = (uint8_t)(i * 0xB9u);
        dst[i] = src[i] ^ key_byte ^ pos_byte;

        if ((i & 0xFF) == 0xFF)
            state = core_next_key(state ^ (uint32_t)i);
    }
}

/* ============================================================================
 * Lua 绑定
 * ============================================================================ */

/* Pack(data, key) / Unpack(data, key) — 用指定密钥 */
static int lua_core_Pack(lua_State *L)
{
    size_t len;
    const uint8_t *src = (const uint8_t *)luaL_checklstring(L, 1, &len);
    uint32_t key = (uint32_t)luaL_checkinteger(L, 2);

    uint8_t *dst = (uint8_t *)malloc(len);
    if (!dst) return luaL_error(L, "out of memory");

    core_crypt(dst, src, len, key);
    lua_pushlstring(L, (const char *)dst, len);
    free(dst);
    return 1;
}

/* Seal(data) / Open(data) — 用默认密钥 */
static int lua_core_Seal(lua_State *L)
{
    size_t len;
    const uint8_t *src = (const uint8_t *)luaL_checklstring(L, 1, &len);
    uint32_t key = core_next_key(core_default_seed());

    uint8_t *dst = (uint8_t *)malloc(len);
    if (!dst) return luaL_error(L, "out of memory");

    core_crypt(dst, src, len, key);
    lua_pushlstring(L, (const char *)dst, len);
    free(dst);
    return 1;
}

/* Key() — 返回默认密钥 */
static int lua_core_Key(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)core_next_key(core_default_seed()));
    return 1;
}

/* Idx(str) — 字符串哈希 */
static int lua_core_Idx(lua_State *L)
{
    size_t len;
    const char *str = luaL_checklstring(L, 1, &len);
    uint32_t hash = 0;
    size_t i;
    for (i = 0; i < len; i++)
        hash = hash * 31 + (uint8_t)str[i];
    lua_pushinteger(L, (lua_Integer)hash);
    return 1;
}

/* iOS 网络权限请求 (弱引用, 仅 Apple 平台可用) */
#if defined(__APPLE__)
extern int lua_gge_request_network(lua_State *L) __attribute__((weak));
#endif

static const luaL_Reg core_funcs[] = {
    {"Pack",   lua_core_Pack},
    {"Unpack", lua_core_Pack},   /* 对称算法，加解密同一函数 */
    {"Seal",   lua_core_Seal},
    {"Open",   lua_core_Seal},   /* 同上 */
    {"Key",    lua_core_Key},
    {"Idx",    lua_core_Idx},
    {NULL, NULL}
};

int luaopen_ggecore(lua_State *L)
{
    lua_createtable(L, 0, 8);
    luaL_setfuncs(L, core_funcs, 0);

#if defined(__APPLE__)
    /* iOS: 注册网络权限请求函数 (仅当 ios_network.m 被链接时可用) */
    if (lua_gge_request_network) {
        lua_pushcfunction(L, lua_gge_request_network);
        lua_setfield(L, -2, "request_network");
    }
#endif

    return 1;
}
