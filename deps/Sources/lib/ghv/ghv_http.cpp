/**
 * ghv_http.cpp - libhv HTTP client Lua binding
 * Exports: luaopen_ghv_HttpRequests
 *
 * Usage in Lua:
 *   local requests = require('ghv.HttpRequests')
 *   requests.get(url, [headers], callback)
 *   requests.head(url, [headers], callback)
 *   requests.post(url, [headers], [body], callback)
 *   requests.run()  -- call in game loop to deliver completed responses
 *
 * Thread model (dual-path):
 *   HTTP  → AsyncHttpClient on shared hloop_t (IOCP), high-performance
 *   HTTPS → Synchronous http_client_send in a detached worker thread
 *
 *   Why: Windows IOCP (overlapio.c) has no SSL handshake support.
 *   AsyncHttpClient calls hio_enable_ssl() which sets io_type to HIO_TYPE_SSL,
 *   but IOCP's hio_write4/post_recv don't handle this type — the connection
 *   fails silently or crashes. The synchronous path uses its own socket/SSL
 *   state, completely independent of the event loop.
 *
 *   Both paths buffer results into s_http_responses (mutex-protected),
 *   delivered to Lua callbacks in l_http_run().
 */
#include "ghv_common.h"
#include "AsyncHttpClient.h"
#include "HttpMessage.h"
#include "HttpClient.h"

#include <cstring>
#include <queue>
#include <thread>

struct HttpPendingResponse {
    int         status;
    std::string body;
    std::string headers_json; // simplified
    int         callback_ref; // Lua registry ref
    bool        has_response;
};

// Global singleton — constructed with shared event loop, NO extra thread.
static hv::AsyncHttpClient* s_http_client = nullptr;
static std::mutex s_http_mutex;
static std::vector<HttpPendingResponse> s_http_responses;
static std::vector<int> s_http_pending_refs;  // Track all outstanding callback refs for cleanup

static void ensure_http_client() {
    if (!s_http_client) {
        // Pass the shared event loop to AsyncHttpClient.
        // Without this, AsyncHttpClient(NULL) calls EventLoopThread::start()
        // which spawns a new OS thread. With a shared loop, it skips start()
        // and reuses our main-thread hloop_t driven by ghv_poll_events().
        auto loop = ghv_shared_event_loop();
        s_http_client = new hv::AsyncHttpClient(loop);
    }
}

// Check if a URL uses HTTPS scheme
static bool is_https_url(const char* url) {
    if (!url) return false;
    return _strnicmp(url, "https:", 6) == 0;
}

// Post a completed response to the shared buffer (thread-safe)
static void post_response(int cb_ref, bool has_response, int status, std::string body) {
    HttpPendingResponse pending;
    pending.callback_ref = cb_ref;
    pending.has_response = has_response;
    pending.status = status;
    pending.body = std::move(body);

    std::lock_guard<std::mutex> lock(s_http_mutex);
    s_http_responses.push_back(std::move(pending));
    // Remove from pending refs (it's now in responses)
    auto& refs = s_http_pending_refs;
    refs.erase(std::remove(refs.begin(), refs.end(), cb_ref), refs.end());
}

// HTTPS worker: synchronous HTTP client in a detached thread.
// Uses its own socket + SSL state, completely independent of the event loop.
// This bypasses the IOCP SSL incompatibility.
static void https_worker(std::shared_ptr<HttpRequest> req, int cb_ref) {
    HttpResponse resp;
    int ret = http_client_send(req.get(), &resp);
    if (ret == 0) {
        post_response(cb_ref, true, resp.status_code, std::move(resp.body));
    } else {
        fprintf(stderr, "[ghv] HTTPS request failed: %s (ret=%d)\n",
                req->url.c_str(), ret);
        post_response(cb_ref, false, -1, "");
    }
}

// Internal: send HTTP/HTTPS request with automatic path selection
static void do_http_request(lua_State* L, const char* method) {
    const char* url = luaL_checkstring(L, 1);

    // Find the callback (always the last argument)
    int nargs = lua_gettop(L);
    luaL_checktype(L, nargs, LUA_TFUNCTION);

    // Store callback in registry
    lua_pushvalue(L, nargs);
    int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    // Build request
    auto req = std::make_shared<HttpRequest>();
    req->method = http_method_enum(method);
    req->url = url;
    req->timeout = 30; // seconds

    // Parse optional headers (table at arg 2)
    if (nargs >= 3 && lua_istable(L, 2)) {
        lua_pushnil(L);
        while (lua_next(L, 2)) {
            const char* key = lua_tostring(L, -2);
            const char* val = lua_tostring(L, -1);
            if (key && val) {
                req->headers[key] = val;
            }
            lua_pop(L, 1);
        }
    }

    // Parse optional body (string at arg 3, only if >= 4 args: url, headers, body, cb)
    if (nargs >= 4 && lua_isstring(L, 3)) {
        size_t body_len = 0;
        const char* body = lua_tolstring(L, 3, &body_len);
        req->body.assign(body, body_len);
    }

    // Track the callback ref for cleanup on shutdown
    {
        std::lock_guard<std::mutex> lock(s_http_mutex);
        s_http_pending_refs.push_back(cb_ref);
    }

    if (is_https_url(url)) {
        // ===== HTTPS path: synchronous client in worker thread =====
        // Windows IOCP (overlapio.c) cannot do SSL handshake.
        // Use the synchronous http_client_send which manages its own
        // socket + OpenSSL state, completely independent of hloop_t.
        std::thread(https_worker, req, cb_ref).detach();
    } else {
        // ===== HTTP path: AsyncHttpClient on shared event loop =====
        ensure_http_client();
        s_http_client->send(req, [cb_ref](const HttpResponsePtr& resp) {
            if (resp) {
                post_response(cb_ref, true, resp->status_code,
                              std::move(resp->body));
            } else {
                post_response(cb_ref, false, -1, "");
            }
        });
    }
}

// requests.get(url, [headers], callback)
static int l_http_get(lua_State* L) {
    do_http_request(L, "GET");
    return 0;
}

// requests.head(url, [headers], callback)
static int l_http_head(lua_State* L) {
    do_http_request(L, "HEAD");
    return 0;
}

// requests.post(url, [headers], [body], callback)
static int l_http_post(lua_State* L) {
    do_http_request(L, "POST");
    return 0;
}

// requests.run() -- dispatch completed HTTP responses to Lua callbacks
// Called from game loop. HTTP IO is driven by ghv_poll_events() (shared loop),
// this function only delivers buffered responses to Lua callbacks.
static int l_http_run(lua_State* L) {
    // Drive the shared event loop (processes HTTP IO + TCP IO in one pass)
    ghv_poll_events();

    std::vector<HttpPendingResponse> responses;
    {
        std::lock_guard<std::mutex> lock(s_http_mutex);
        responses.swap(s_http_responses);
    }

    for (auto& r : responses) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, r.callback_ref);
        if (lua_isfunction(L, -1)) {
            if (r.has_response) {
                lua_pushinteger(L, r.status);
                lua_pushlstring(L, r.body.data(), r.body.size());
                if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                    fprintf(stderr, "[ghv] http callback error: %s\n", lua_tostring(L, -1));
                    lua_pop(L, 1);
                }
            } else {
                // Request failed (timeout, connection error, etc.)
                lua_pushnil(L);
                lua_pushstring(L, "request failed");
                if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                    fprintf(stderr, "[ghv] http callback error: %s\n", lua_tostring(L, -1));
                    lua_pop(L, 1);
                }
            }
        } else {
            lua_pop(L, 1);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, r.callback_ref);
    }

    return 0;
}

// requests.thread_count() -- return current thread count for diagnostics
static int l_http_thread_count(lua_State* L) {
    lua_pushinteger(L, ghv_get_thread_count());
    return 1;
}

// Cleanup sentinel — released when Lua state closes
static int l_http_cleanup_gc(lua_State* L) {
    // Release any pending callback refs that were never consumed by l_http_run
    {
        std::lock_guard<std::mutex> lock(s_http_mutex);
        for (auto& r : s_http_responses) {
            luaL_unref(L, LUA_REGISTRYINDEX, r.callback_ref);
        }
        s_http_responses.clear();
        for (int ref : s_http_pending_refs) {
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
        }
        s_http_pending_refs.clear();
    }
    // Do NOT delete s_http_client here.
    // AsyncHttpClient shares the global event loop (ghv_shared_event_loop).
    // Deleting it would call EventLoopThread::stop() which breaks all IO.
    return 0;
}

GHV_EXPORT int luaopen_ghv_HttpRequests(lua_State* L)
{
    luaL_Reg funcs[] = {
        {"get",  l_http_get},
        {"head", l_http_head},
        {"post", l_http_post},
        {"run",  l_http_run},
        {"thread_count", l_http_thread_count},
        {NULL, NULL},
    };

    luaL_newlib(L, funcs);

    // Create a sentinel userdata with __gc to clean up pending refs
    // when the Lua state is closed. Store it as a field of the module.
    lua_newuserdata(L, 1);  // tiny sentinel
    lua_newtable(L);        // metatable
    lua_pushcfunction(L, l_http_cleanup_gc);
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "_cleanup");  // module._cleanup = sentinel

    return 1;
}
