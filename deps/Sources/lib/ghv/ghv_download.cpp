/**
 * ghv_download.cpp - HTTP file download Lua binding
 * Exports: luaopen_ghv_download
 *
 * Replacement for ghpsocket.download
 * Uses libhv AsyncHttpClient for async downloads
 *
 * Usage in Lua:
 *   local download = require('ghv.download')
 *   local dl = download(url, [filepath], [range])
 *   -- In game loop, poll:
 *   local cur, total, status = dl:GetState()
 *   -- After download to memory:
 *   local data = dl:GetData()
 *   local md5 = dl:GetMD5()
 *   -- Cancel
 *   dl:Cancel()
 */
#include "ghv_common.h"
#include "HttpClient.h"
#include "hbase.h"

#include <cstring>
#include <fstream>
#include <atomic>
#include <thread>
#include <memory>

#ifndef GHV_NO_CRYPTO
#include <openssl/evp.h>
#endif

#define GHV_DOWNLOAD_META "GHV_Download"

struct LuaDownload {
    struct CoreState {
        std::string url;
        std::string filepath;   // empty = download to memory
        std::string range;

        std::atomic<int64_t> current_size{0};
        std::atomic<int64_t> total_size{0};
        std::atomic<int>     status{0};
        std::atomic<bool>    cancelled{false};

        std::string          memory_data;
        std::string          md5_hex;
        std::mutex           data_mutex;
    };

    std::shared_ptr<CoreState> core;
    std::thread          worker;

    LuaDownload() : core(std::make_shared<CoreState>()) {}

    ~LuaDownload() {
        core->cancelled = true;
        if (worker.joinable()) {
            if (core->status.load() == 100 || core->status.load() < 0) {
                worker.join();
            } else {
                for (int i = 0; i < 50; ++i) {
                    if (core->status.load() == 100 || core->status.load() < 0) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                if (core->status.load() == 100 || core->status.load() < 0) {
                    worker.join();
                } else {
                    worker.detach();
                }
            }
        }
    }

    void start() {
        core->status = 1; // downloading
        auto st = core;
        worker = std::thread([st]() {
            doDownload(st);
        });
    }

    static void doDownload(std::shared_ptr<CoreState> state) {
        HttpRequest req;
        req.method = HTTP_GET;
        req.url = state->url;
        req.timeout = 300; // 5 minutes timeout for downloads

        if (!state->range.empty()) {
            req.headers["Range"] = "bytes=" + state->range;
        }

        HttpResponse resp;
        hv::HttpClient client;

        if (!state->filepath.empty()) {
            // Download to file
            bool append_mode = !state->range.empty();
            FILE* fp = nullptr;
#ifdef _WIN32
            int wlen = MultiByteToWideChar(CP_UTF8, 0, state->filepath.c_str(), -1, NULL, 0);
            if (wlen > 0) {
                std::wstring wpath(wlen, 0);
                MultiByteToWideChar(CP_UTF8, 0, state->filepath.c_str(), -1, &wpath[0], wlen);
                fp = _wfopen(wpath.c_str(), append_mode ? L"ab" : L"wb");
            }
#else
            fp = fopen(state->filepath.c_str(), append_mode ? "ab" : "wb");
#endif
            if (!fp) {
                state->status = -1;
                return;
            }

            resp.http_cb = [state, &fp, append_mode](HttpMessage* msg, http_parser_state state_h, const char* data, size_t size) {
                if (state->cancelled.load()) return;
                if (state_h == HP_HEADERS_COMPLETE) {
                    if (append_mode && msg->GetHeader("Content-Range").empty()) {
                        if (fp) {
                            fclose(fp);
#ifdef _WIN32
                            int twl = MultiByteToWideChar(CP_UTF8, 0, state->filepath.c_str(), -1, NULL, 0);
                            if (twl > 0) {
                                std::wstring twp(twl, 0);
                                MultiByteToWideChar(CP_UTF8, 0, state->filepath.c_str(), -1, &twp[0], twl);
                                fp = _wfopen(twp.c_str(), L"wb");
                            }
#else
                            fp = fopen(state->filepath.c_str(), "wb");
#endif
                        }
                    }
                    std::string cl = msg->GetHeader("Content-Length");
                    if (!cl.empty()) {
                        state->total_size = std::stoll(cl);
                    }
                } else if (state_h == HP_BODY) {
                    if (data && size > 0 && fp) {
                        fwrite(data, 1, size, fp);
                        state->current_size += size;
                    }
                } else if (state_h == HP_MESSAGE_COMPLETE) {
                    // done
                }
            };

            int ret = client.send(&req, &resp);
            if (fp) fclose(fp);

            if (state->cancelled.load() || ret != 0 || resp.status_code < 200 || resp.status_code >= 400) {
#ifdef _WIN32
                int rwl = MultiByteToWideChar(CP_UTF8, 0, state->filepath.c_str(), -1, NULL, 0);
                if (rwl > 0) {
                    std::wstring rwp(rwl, 0);
                    MultiByteToWideChar(CP_UTF8, 0, state->filepath.c_str(), -1, &rwp[0], rwl);
                    _wremove(rwp.c_str());
                }
#else
                remove(state->filepath.c_str());
#endif
                state->status = -(resp.status_code ? resp.status_code : ret);
            } else {
                state->status = 100;
            }
        } else {
            // Download to memory
            resp.http_cb = [state](HttpMessage* msg, http_parser_state state_h, const char* data, size_t size) {
                if (state->cancelled.load()) return;
                if (state_h == HP_HEADERS_COMPLETE) {
                    std::string cl = msg->GetHeader("Content-Length");
                    if (!cl.empty()) {
                        state->total_size = std::stoll(cl);
                        std::lock_guard<std::mutex> lock(state->data_mutex);
                        state->memory_data.reserve(static_cast<size_t>(state->total_size.load()));
                    }
                } else if (state_h == HP_BODY) {
                    if (data && size > 0) {
                        std::lock_guard<std::mutex> lock(state->data_mutex);
                        state->memory_data.append(data, size);
                        state->current_size += size;
                    }
                }
            };

            int ret = client.send(&req, &resp);

            if (ret != 0 || resp.status_code < 200 || resp.status_code >= 400) {
                state->status = -(resp.status_code ? resp.status_code : ret);
            } else {
                if (state->memory_data.empty() && !resp.body.empty()) {
                    std::lock_guard<std::mutex> lock(state->data_mutex);
                    state->memory_data = resp.body;
                    state->current_size = state->memory_data.size();
                    state->total_size = state->memory_data.size();
                }
                state->status = 100;
            }
        }
    }
};

static LuaDownload* check_download(lua_State* L) {
    return *(LuaDownload**)luaL_checkudata(L, 1, GHV_DOWNLOAD_META);
}

static int l_download_get_state(lua_State* L) {
    LuaDownload* self = check_download(L);
    lua_pushinteger(L, (lua_Integer)self->core->current_size.load());
    lua_pushinteger(L, (lua_Integer)self->core->total_size.load());
    lua_pushinteger(L, self->core->status.load());
    return 3;
}

static int l_download_get_data(lua_State* L) {
    LuaDownload* self = check_download(L);
    std::lock_guard<std::mutex> lock(self->core->data_mutex);
    lua_pushlstring(L, self->core->memory_data.data(), self->core->memory_data.size());
    return 1;
}

static int l_download_get_md5(lua_State* L) {
    LuaDownload* self = check_download(L);
#ifndef GHV_NO_CRYPTO
    if (self->core->md5_hex.empty() && self->core->status == 100) {
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (ctx && EVP_DigestInit_ex(ctx, EVP_md5(), NULL)) {
            if (!self->core->filepath.empty()) {
                FILE* f = nullptr;
#ifdef _WIN32
                int md5wl = MultiByteToWideChar(CP_UTF8, 0, self->core->filepath.c_str(), -1, NULL, 0);
                if (md5wl > 0) {
                    std::wstring md5wp(md5wl, 0);
                    MultiByteToWideChar(CP_UTF8, 0, self->core->filepath.c_str(), -1, &md5wp[0], md5wl);
                    f = _wfopen(md5wp.c_str(), L"rb");
                }
#else
                f = fopen(self->core->filepath.c_str(), "rb");
#endif
                if (f) {
                    unsigned char buf[8192];
                    size_t n;
                    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
                        EVP_DigestUpdate(ctx, buf, n);
                    }
                    fclose(f);
                }
            } else {
                std::lock_guard<std::mutex> lock(self->core->data_mutex);
                EVP_DigestUpdate(ctx, self->core->memory_data.data(), self->core->memory_data.size());
            }
            unsigned char md[EVP_MAX_MD_SIZE];
            unsigned int md_len = 0;
            if (EVP_DigestFinal_ex(ctx, md, &md_len)) {
                char hex[33] = {0};
                for (unsigned int i = 0; i < md_len && i < 16; i++) {
                    sprintf(hex + i * 2, "%02x", md[i]);
                }
                self->core->md5_hex = hex;
            }
        }
        if (ctx) EVP_MD_CTX_free(ctx);
    }
#endif
    lua_pushstring(L, self->core->md5_hex.c_str());
    return 1;
}

static int l_download_cancel(lua_State* L) {
    LuaDownload* self = check_download(L);
    self->core->cancelled = true;
    return 0;
}

static int l_download_gc(lua_State* L) {
    LuaDownload** ud = (LuaDownload**)luaL_checkudata(L, 1, GHV_DOWNLOAD_META);
    LuaDownload* self = *ud;
    if (!self) return 0;
    *ud = nullptr;
    delete self;
    return 0;
}

static int l_download_new(lua_State* L) {
    const char* url = luaL_checkstring(L, 1);
    const char* filepath = luaL_optstring(L, 2, NULL);
    const char* range = luaL_optstring(L, 3, NULL);

    LuaDownload* self = new LuaDownload();
    self->core->url = url;
    if (filepath) self->core->filepath = filepath;
    if (range) self->core->range = range;

    LuaDownload** ud = (LuaDownload**)lua_newuserdata(L, sizeof(LuaDownload*));
    *ud = self;
    luaL_setmetatable(L, GHV_DOWNLOAD_META);

    self->start();
    return 1;
}

GHV_EXPORT int luaopen_ghv_download(lua_State* L)
{
    luaL_Reg methods[] = {
        {"GetState", l_download_get_state},
        {"GetData",  l_download_get_data},
        {"GetMD5",   l_download_get_md5},
        {"Cancel",   l_download_cancel},
        {NULL, NULL},
    };

    luaL_newmetatable(L, GHV_DOWNLOAD_META);
    luaL_newlib(L, methods);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, l_download_gc);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    lua_pushcfunction(L, l_download_new);
    return 1;
}
