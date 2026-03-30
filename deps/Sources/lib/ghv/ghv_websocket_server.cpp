/**
 * ghv_websocket_server.cpp - libhv WebSocketServer Lua binding
 * Exports: luaopen_ghv_WebSocketServer
 *
 * Architecture:
 *   - WebSocketServer runs on internal IO thread (HttpServer model)
 *   - WS callbacks (open/message/close) buffer events in a thread-safe queue
 *   - Lua-side run() dispatches buffered events to Lua callbacks on main thread
 *   - Send operations use channel->write() which is thread-safe
 *   - HTTP POST routes return 200 immediately, body queued for main thread
 *
 * This replaces the old HPSocket PullServer + hand-written WS protocol stack
 * (pull.lua + buff.lua), eliminating ~530 lines of Lua WS code.
 *
 * Key advantages over old architecture:
 *   - RFC 6455 compliant (Ping/Pong/Close auto-handled by libhv C++)
 *   - No Lua-level XOR decoding (C++ layer handles masking)
 *   - Built-in TLS/WSS support via withSSL()
 *   - Built-in ping_interval heartbeat
 */
#include "ghv_common.h"

// libhv WebSocket/HTTP server headers (使用 libhv 根目录相对路径)
#include "http/server/WebSocketServer.h"
#include "http/WebSocketChannel.h"
#include "http/server/HttpService.h"

#include <cstring>
#include <mutex>
#include <vector>
#include <unordered_map>

#define GHV_WS_SERVER_META "GHV_WebSocketServer"

// ============================================================
// Event types for cross-thread dispatch
// ============================================================

enum WSEventType {
    WS_EVT_OPEN,
    WS_EVT_MESSAGE,
    WS_EVT_CLOSE,
    WS_EVT_HTTP_POST,
};

struct WSEvent {
    WSEventType type;
    uint32_t    channel_id;     // WS channel ID (for WS events)
    std::string data;           // Message text (MESSAGE), POST body (HTTP_POST)
    std::string url;            // Request URL/path
};

// ============================================================
// Server state
// ============================================================

struct LuaWSServer {
    hv::WebSocketServer*    server;
    hv::WebSocketService    ws_service;
    hv::HttpService         http_service;
    lua_State*              L;
    int                     self_ref;
    bool                    started;    // 是否已调用 start()

    // Thread-safe event queue (IO thread → main thread)
    std::mutex              event_mutex;
    std::vector<WSEvent>    pending_events;

    // Channel map for send operations (thread-safe access)
    std::mutex                                          channel_mutex;
    std::unordered_map<uint32_t, WebSocketChannelPtr>   channels;

    LuaWSServer(lua_State* _L)
        : L(_L), self_ref(LUA_NOREF), server(nullptr), started(false)
    {
        ws_service.ping_interval = 0;
        server = new hv::WebSocketServer(&ws_service);
        server->registerHttpService(&http_service);
        server->setProcessNum(0);
        server->setThreadNum(0);  // Minimal: acceptor loop handles all
    }

    ~LuaWSServer() {
        if (server) {
            server->stop();
            delete server;
            server = nullptr;
        }
    }
};

// ============================================================
// Helpers
// ============================================================

static LuaWSServer* check_ws_server(lua_State* L) {
    return *static_cast<LuaWSServer**>(luaL_checkudata(L, 1, GHV_WS_SERVER_META));
}

static void release_self_ref(LuaWSServer* self) {
    if (self->self_ref != LUA_NOREF) {
        luaL_unref(self->L, LUA_REGISTRYINDEX, self->self_ref);
        self->self_ref = LUA_NOREF;
    }
}

static bool push_server_userdata(lua_State* L, LuaWSServer* self) {
    if (self->self_ref == LUA_NOREF) return false;
    lua_rawgeti(L, LUA_REGISTRYINDEX, self->self_ref);
    return true;
}

// ============================================================
// Core methods
// ============================================================

// WSS:start(ip, port) -> boolean
static int l_ws_server_start(lua_State* L) {
    LuaWSServer* self = check_ws_server(L);
    const char* ip = luaL_checkstring(L, 2);
    int port = static_cast<int>(luaL_checkinteger(L, 3));

    // Save registry reference to prevent GC
    if (self->self_ref == LUA_NOREF) {
        lua_pushvalue(L, 1);
        self->self_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    // Setup WebSocket callbacks (fire on IO thread, buffer events)
    self->ws_service.onopen = [self](const WebSocketChannelPtr& channel,
                                      const HttpRequestPtr& req) {
        uint32_t id = static_cast<uint32_t>(channel->id());

        // Track channel for sends
        {
            std::lock_guard<std::mutex> lock(self->channel_mutex);
            self->channels[id] = channel;
        }

        // Buffer open event
        WSEvent evt;
        evt.type = WS_EVT_OPEN;
        evt.channel_id = id;
        evt.url = req ? req->path : "/";
        {
            std::lock_guard<std::mutex> lock(self->event_mutex);
            self->pending_events.push_back(std::move(evt));
        }
    };

    self->ws_service.onmessage = [self](const WebSocketChannelPtr& channel,
                                         const std::string& msg) {
        uint32_t id = static_cast<uint32_t>(channel->id());

        WSEvent evt;
        evt.type = WS_EVT_MESSAGE;
        evt.channel_id = id;
        evt.data = msg;
        {
            std::lock_guard<std::mutex> lock(self->event_mutex);
            self->pending_events.push_back(std::move(evt));
        }
    };

    self->ws_service.onclose = [self](const WebSocketChannelPtr& channel) {
        uint32_t id = static_cast<uint32_t>(channel->id());

        // Remove from channel map
        {
            std::lock_guard<std::mutex> lock(self->channel_mutex);
            self->channels.erase(id);
        }

        WSEvent evt;
        evt.type = WS_EVT_CLOSE;
        evt.channel_id = id;
        {
            std::lock_guard<std::mutex> lock(self->event_mutex);
            self->pending_events.push_back(std::move(evt));
        }
    };

    // Configure server
    self->server->setHost(ip);
    self->server->setPort(port);

    // Start non-blocking (creates internal IO thread)
    int rc = self->server->start();
    if (rc == 0) self->started = true;

    lua_pushboolean(L, rc == 0 ? 1 : 0);
    return 1;
}

// WSS:stop()
static int l_ws_server_stop(lua_State* L) {
    LuaWSServer* self = check_ws_server(L);
    if (self->server) {
        self->server->stop();
    }
    // Clear channel map
    {
        std::lock_guard<std::mutex> lock(self->channel_mutex);
        self->channels.clear();
    }
    release_self_ref(self);
    return 0;
}

// WSS:send(id, text) -> boolean
static int l_ws_server_send(lua_State* L) {
    LuaWSServer* self = check_ws_server(L);
    uint32_t id = ghv_check_uint32(L, 2);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 3, &len);

    WebSocketChannelPtr channel;
    {
        std::lock_guard<std::mutex> lock(self->channel_mutex);
        auto it = self->channels.find(id);
        if (it != self->channels.end()) {
            channel = it->second;
        }
    }

    if (channel && channel->isConnected()) {
        // WebSocketChannel::send is thread-safe
        int rc = channel->send(data, static_cast<int>(len), WS_OPCODE_TEXT);
        lua_pushboolean(L, rc >= 0 ? 1 : 0);
    } else {
        lua_pushboolean(L, 0);
    }
    return 1;
}

// WSS:disconnect(id)
static int l_ws_server_disconnect(lua_State* L) {
    LuaWSServer* self = check_ws_server(L);
    uint32_t id = ghv_check_uint32(L, 2);

    WebSocketChannelPtr channel;
    {
        std::lock_guard<std::mutex> lock(self->channel_mutex);
        auto it = self->channels.find(id);
        if (it != self->channels.end()) {
            channel = it->second;
        }
    }

    if (channel) {
        channel->close();
    }
    return 0;
}

// WSS:connectionNum() -> integer
static int l_ws_server_connection_num(lua_State* L) {
    LuaWSServer* self = check_ws_server(L);
    std::lock_guard<std::mutex> lock(self->channel_mutex);
    lua_pushinteger(L, static_cast<lua_Integer>(self->channels.size()));
    return 1;
}

// WSS:setPingInterval(ms)
static int l_ws_server_set_ping_interval(lua_State* L) {
    LuaWSServer* self = check_ws_server(L);
    int ms = static_cast<int>(luaL_checkinteger(L, 2));
    self->ws_service.setPingInterval(ms);
    return 0;
}

// WSS:withSSL(crt_file, key_file, [verify_peer])
static int l_ws_server_with_ssl(lua_State* L) {
    LuaWSServer* self = check_ws_server(L);
    hssl_ctx_opt_t opt;
    memset(&opt, 0, sizeof(opt));
    opt.endpoint = HSSL_SERVER;

    if (lua_isstring(L, 2)) opt.crt_file = lua_tostring(L, 2);
    if (lua_isstring(L, 3)) opt.key_file = lua_tostring(L, 3);
    if (lua_isboolean(L, 4)) opt.verify_peer = lua_toboolean(L, 4);

    int rc = self->server->newSslCtx(&opt);
    lua_pushboolean(L, rc == 0 ? 1 : 0);
    return 1;
}

// WSS:addRoute(method, path)
// Registers an HTTP POST route. Incoming requests are queued as HTTP_POST events.
// The route always responds with HTTP 200 immediately (webhook pattern).
static int l_ws_server_add_route(lua_State* L) {
    LuaWSServer* self = check_ws_server(L);

    // addRoute 必须在 start() 之前调用，HttpService 路由表在 start 后锁定
    if (self->started) {
        return luaL_error(L, "addRoute must be called before start()");
    }

    const char* method = luaL_checkstring(L, 2);
    const char* path = luaL_checkstring(L, 3);

    if (strcmp(method, "POST") == 0) {
        // Capture self pointer (safe: server outlives routes)
        std::string route_path(path);
        self->http_service.POST(path, [self, route_path](HttpRequest* req, HttpResponse* resp) {
            // Buffer the POST body for main thread processing
            WSEvent evt;
            evt.type = WS_EVT_HTTP_POST;
            evt.channel_id = 0;
            evt.url = route_path;
            evt.data = req->body;
            {
                std::lock_guard<std::mutex> lock(self->event_mutex);
                self->pending_events.push_back(std::move(evt));
            }

            // Respond immediately (standard webhook acknowledgment)
            resp->content_type = TEXT_PLAIN;
            resp->body = "SUCCESS";
            return 200;
        });
    }

    return 0;
}

// ============================================================
// Event dispatch (main thread)
// ============================================================

// WSS:run() -- dispatch buffered events to Lua callbacks
// Must be called from the game's main loop (e.g. via 引擎:注册事件)
static int l_ws_server_run(lua_State* L) {
    LuaWSServer* self = check_ws_server(L);

    // Swap pending events (minimize lock hold time)
    std::vector<WSEvent> events;
    {
        std::lock_guard<std::mutex> lock(self->event_mutex);
        events.swap(self->pending_events);
    }

    if (events.empty()) return 0;

    for (auto& evt : events) {
        if (!push_server_userdata(L, self)) break;
        int ud_idx = lua_gettop(L);

        switch (evt.type) {
            case WS_EVT_OPEN:
                if (ghv_get_lua_ref(L, ud_idx, "on_open")) {
                    lua_pushinteger(L, evt.channel_id);
                    lua_pushlstring(L, evt.url.data(), evt.url.size());
                    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                        fprintf(stderr, "[ghv-ws] open callback error: %s\n",
                                lua_tostring(L, -1));
                        lua_pop(L, 1);
                    }
                }
                break;

            case WS_EVT_MESSAGE:
                if (ghv_get_lua_ref(L, ud_idx, "on_message")) {
                    lua_pushinteger(L, evt.channel_id);
                    lua_pushlstring(L, evt.data.data(), evt.data.size());
                    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                        fprintf(stderr, "[ghv-ws] message callback error: %s\n",
                                lua_tostring(L, -1));
                        lua_pop(L, 1);
                    }
                }
                break;

            case WS_EVT_CLOSE:
                if (ghv_get_lua_ref(L, ud_idx, "on_close")) {
                    lua_pushinteger(L, evt.channel_id);
                    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                        fprintf(stderr, "[ghv-ws] close callback error: %s\n",
                                lua_tostring(L, -1));
                        lua_pop(L, 1);
                    }
                }
                break;

            case WS_EVT_HTTP_POST:
                if (ghv_get_lua_ref(L, ud_idx, "on_http")) {
                    lua_pushlstring(L, evt.url.data(), evt.url.size());
                    lua_pushlstring(L, evt.data.data(), evt.data.size());
                    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                        fprintf(stderr, "[ghv-ws] http callback error: %s\n",
                                lua_tostring(L, -1));
                        lua_pop(L, 1);
                    }
                }
                break;
        }

        lua_pop(L, 1);  // pop server userdata
    }

    return 0;
}

// ============================================================
// Callback setters
// ============================================================

static int l_ws_server_setcb_open(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ghv_set_lua_ref(L, 1, "on_open", 2);
    return 0;
}

static int l_ws_server_setcb_message(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ghv_set_lua_ref(L, 1, "on_message", 2);
    return 0;
}

static int l_ws_server_setcb_close(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ghv_set_lua_ref(L, 1, "on_close", 2);
    return 0;
}

// Callback for HTTP POST routes
static int l_ws_server_setcb_http(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ghv_set_lua_ref(L, 1, "on_http", 2);
    return 0;
}

// ============================================================
// GC and lifecycle
// ============================================================

static int l_ws_server_gc(lua_State* L) {
    auto** ud = static_cast<LuaWSServer**>(luaL_checkudata(L, 1, GHV_WS_SERVER_META));
    LuaWSServer* self = *ud;
    if (!self) return 0;
    *ud = nullptr;

    release_self_ref(self);

    // 正确顺序：先停服务器（join IO 线程），再清回调，最后 delete
    // 如果先清回调，IO 线程可能在回调被置空后仍试图调用，导致空指针崩溃
    if (self->server) {
        self->server->stop();              // 阻塞，等待 IO 线程退出
        self->ws_service.onopen = nullptr;
        self->ws_service.onmessage = nullptr;
        self->ws_service.onclose = nullptr;
    }

    delete self;
    return 0;
}

// ============================================================
// Constructor
// ============================================================

static int l_ws_server_new(lua_State* L) {
    LuaWSServer* self = new LuaWSServer(L);
    auto** ud = static_cast<LuaWSServer**>(lua_newuserdata(L, sizeof(LuaWSServer*)));
    *ud = self;
    lua_newtable(L);
    lua_setiuservalue(L, -2, 1);
    luaL_setmetatable(L, GHV_WS_SERVER_META);
    return 1;
}

// ============================================================
// __index / __newindex for Lua-side custom fields
// ============================================================

// Methods table stored as upvalue(1) in both __index and __newindex closures.
// __index: check methods table first, then fallback to uservalue table.
// __newindex: write to uservalue table (allows WSS:customMethod = func).

static int l_ws_server_index(lua_State* L) {
    // upvalue(1) = methods table
    lua_pushvalue(L, 2);                 // push key
    lua_gettable(L, lua_upvalueindex(1)); // methods[key]
    if (!lua_isnil(L, -1)) {
        return 1;  // found in methods table
    }
    lua_pop(L, 1);

    // Fallback: check uservalue table
    lua_getiuservalue(L, 1, 1);
    if (lua_istable(L, -1)) {
        lua_pushvalue(L, 2);             // push key
        lua_gettable(L, -2);             // uservalue[key]
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

static int l_ws_server_newindex(lua_State* L) {
    // Store in uservalue table: uservalue[key] = value
    lua_getiuservalue(L, 1, 1);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setiuservalue(L, 1, 1);
    }
    lua_pushvalue(L, 2);  // key
    lua_pushvalue(L, 3);  // value
    lua_settable(L, -3);
    return 0;
}

// ============================================================
// Module registration
// ============================================================

GHV_EXPORT int luaopen_ghv_WebSocketServer(lua_State* L)
{
    luaL_Reg methods[] = {
        {"start",               l_ws_server_start},
        {"stop",                l_ws_server_stop},
        {"send",                l_ws_server_send},
        {"disconnect",          l_ws_server_disconnect},
        {"connectionNum",       l_ws_server_connection_num},
        {"setPingInterval",     l_ws_server_set_ping_interval},
        {"withSSL",             l_ws_server_with_ssl},
        {"addRoute",            l_ws_server_add_route},
        {"setcb_open",          l_ws_server_setcb_open},
        {"setcb_message",       l_ws_server_setcb_message},
        {"setcb_close",         l_ws_server_setcb_close},
        {"setcb_http",          l_ws_server_setcb_http},
        {"run",                 l_ws_server_run},
        {NULL, NULL},
    };

    luaL_newmetatable(L, GHV_WS_SERVER_META);

    // Create methods table (shared as upvalue for __index and __newindex)
    luaL_newlib(L, methods);             // stack: meta, methods

    // __index = closure(methods) -> check methods first, then uservalue
    lua_pushvalue(L, -1);                // push methods as upvalue
    lua_pushcclosure(L, l_ws_server_index, 1);
    lua_setfield(L, -3, "__index");

    // __newindex = closure(methods) -> write to uservalue table
    lua_pushvalue(L, -1);                // push methods as upvalue (unused but symmetric)
    lua_pushcclosure(L, l_ws_server_newindex, 1);
    lua_setfield(L, -3, "__newindex");

    lua_pop(L, 1);  // pop methods table

    lua_pushcfunction(L, l_ws_server_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);  // pop metatable

    lua_pushcfunction(L, l_ws_server_new);
    return 1;
}
