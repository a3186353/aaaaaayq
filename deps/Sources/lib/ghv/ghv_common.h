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
// Thread count monitoring (for diagnosing thread leaks)
// ============================================================

#ifdef _WIN32
#include <tlhelp32.h>
#endif

// Returns the current number of threads in this process.
// Useful for verifying that thread count stays stable after fixes.
inline int ghv_get_thread_count() {
#ifdef _WIN32
    DWORD pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return -1;
    int count = 0;
    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) ++count;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return count;
#elif defined(__linux__) || defined(__ANDROID__)
    // Read /proc/self/status for "Threads:" line
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    int count = -1;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Threads:", 8) == 0) {
            count = atoi(line + 8);
            break;
        }
    }
    fclose(f);
    return count;
#else
    return -1; // Unsupported platform
#endif
}

// ============================================================
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
