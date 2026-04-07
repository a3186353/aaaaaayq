/**
 * ghv - libhv Lua bindings for GGELUA engine
 * Common header with shared utilities
 *
 * Refactored: single-thread model with shared hloop_t
 * - All TcpClient/TcpServer share one hloop
 * - ghv:run() calls hloop_process_events(loop, 0) for non-blocking poll
 * - Callbacks fire directly in main thread, no cross-thread event queue
 */
#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

// libhv C++ wrappers (these handle extern "C" internally)
#include "EventLoop.h"
#include "ThreadLocalStorage.h"

#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

// ============================================================
// Shared EventLoop management
// ============================================================

// Defined in ghv_loop_init.c (compiled as C to access hloop_s internals)
extern "C" void ghv_init_loop_for_polling(hloop_t* loop);

// Global shared EventLoop (singleton per process)
// All TcpClient/TcpServer instances share this loop
inline hv::EventLoopPtr& ghv_shared_event_loop() {
    static hv::EventLoopPtr s_loop;
    if (!s_loop) {
        s_loop = std::make_shared<hv::EventLoop>();
        // CRITICAL: Set BOTH statuses so runInLoop() executes directly
        // 1. C++ EventLoop::Status -> kRunning (checked by runInLoop/isRunning)
        //    Without this, all callbacks go through queueInLoop -> hloop_post_event
        //    which requires eventfds that are only initialized by hloop_run()
        s_loop->setStatus(hv::Status::kRunning);
        // 2. hloop_t status -> RUNNING (checked by hloop_process_events)
        ghv_init_loop_for_polling(s_loop->loop());
        // 3. Set ThreadLocalStorage so TcpServer's currentThreadEventLoop works
        //    This is normally done by EventLoop::run(), which is never called
        //    in polling mode. Without this, TcpServer::onAccept -> newConnEvent
        //    gets NULL from currentThreadEventLoop, breaking all server accepts.
        hv::ThreadLocalStorage::set(hv::ThreadLocalStorage::EVENT_LOOP, s_loop.get());
        // 4. Force eventfds creation (socketpair on Windows) by posting a wakeup.
        //    This is normally done by hloop_run(). Without eventfds,
        //    hloop_post_event() (used by reconnect timers etc.) would fail.
        hloop_wakeup(s_loop->loop());
    }
    return s_loop;
}

// Get shared hloop_t pointer
inline hloop_t* ghv_get_shared_loop() {
    auto& evloop = ghv_shared_event_loop();
    return evloop ? evloop->loop() : nullptr;
}

// Non-blocking poll: process IO events in the shared event loop.
// Called from GOL._UPDATE() every frame via Common:循环事件().
//
// IMPORTANT: Single-pass only. The game's main loop relies on frame gaps
// between poll calls to execute deferred callbacks (e.g. 切换地图回调 in
// main.lua which sets up __map). A multi-pass approach would deliver
// multiple RPC messages in the same call (map switch + NPC data), but the
// map switch callback hasn't executed yet, causing NPC data to be silently
// dropped by the Lua application layer. Single-pass matches the old
// HPSocket/IOCP timing where messages were processed one-at-a-time with
// main loop iterations in between.
inline int ghv_poll_events() {
    hloop_t* loop = ghv_get_shared_loop();
    if (!loop) return 0;
    return hloop_process_events(loop, 0);
}

// ============================================================
// Platform socket includes (shared by TcpClient / TcpServer)
// ============================================================

#include <cstring>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/tcp.h>
#endif

// ============================================================
// libhv internal: per-IO private read buffer allocation
// ============================================================
// Allocates a private readbuf for an IO handle, detaching it from the
// shared loop->readbuf. Required for encrypted sessions to prevent
// cross-talk when multiple sockets are ready in the same poll cycle.

extern "C" void hio_alloc_readbuf(hio_t* io, int len);
#ifndef HLOOP_READ_BUFSIZE
#define HLOOP_READ_BUFSIZE 65536
#endif

// ============================================================
// Shared network helpers
// ============================================================

#include "ghv_net_protocol.h"  // GHV_MAX_FRAME_SIZE, GHV_KEY_SIZE, etc.

// Optimize a TCP socket for low-latency game RPC traffic.
// Called once per connection in both TcpClient and TcpServer.
inline void ghv_optimize_game_socket(int fd) {
    int flag = 1;
    // Disable Nagle algorithm for low-latency RPC
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&flag), sizeof(flag));
    // Enable TCP keepalive (detect dead connections)
    setsockopt(fd, SOL_SOCKET,  SO_KEEPALIVE,
               reinterpret_cast<const char*>(&flag), sizeof(flag));
}

// Initialize an unpack_setting_t for length-field-based defragmentation.
// Used by both TcpClient and TcpServer's setUnpack() Lua API.
inline void ghv_init_unpack_setting(unpack_setting_t* s,
                                     int head_flag_len,
                                     int len_field_bytes) {
    memset(s, 0, sizeof(unpack_setting_t));
    s->mode                = UNPACK_BY_LENGTH_FIELD;
    s->package_max_length  = GHV_MAX_FRAME_SIZE;  // 1MB, 防恶意超大包
    s->body_offset         = head_flag_len + len_field_bytes;
    s->length_field_offset = head_flag_len;
    s->length_field_bytes  = len_field_bytes;
    s->length_field_coding = ENCODE_BY_LITTEL_ENDIAN;
    s->length_adjustment   = 0;
}

// Lua callback reference helpers
// ============================================================

// Store Lua callback reference in uservalue table
inline void ghv_set_lua_ref(lua_State* L, int ud_idx, const char* field, int func_idx) {
    lua_getiuservalue(L, ud_idx, 1);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setiuservalue(L, ud_idx, 1);
    }
    lua_pushvalue(L, func_idx);
    lua_setfield(L, -2, field);
    lua_pop(L, 1);
}

// Get Lua callback and push onto stack, returns true if found
inline bool ghv_get_lua_ref(lua_State* L, int ud_idx, const char* field) {
    lua_getiuservalue(L, ud_idx, 1);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    lua_getfield(L, -1, field);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        return false;
    }
    lua_remove(L, -2);
    return true;
}

// ============================================================
// Lua integer range-check helpers
// ============================================================

// Safely extract uint32_t from Lua stack. Raises lua_error on out-of-range.
// Prevents silent truncation when Lua (int64) → C++ (uint32_t).
inline uint32_t ghv_check_uint32(lua_State* L, int arg) {
    lua_Integer raw = luaL_checkinteger(L, arg);
    if (raw < 0 || raw > static_cast<lua_Integer>(UINT32_MAX)) {
        luaL_error(L, "argument #%d: value %lld out of uint32 range",
                   arg, static_cast<long long>(raw));
    }
    return static_cast<uint32_t>(raw);
}

// DLL export macro
#if defined(LUA_BUILD_AS_DLL)
#define GHV_EXPORT extern "C" LUAMOD_API
#else
#define GHV_EXPORT extern "C"
#endif
