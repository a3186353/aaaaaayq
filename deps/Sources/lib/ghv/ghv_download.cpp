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
 */
#include "ghv_common.h"
#include "HttpClient.h"
#include "hbase.h"

#include <cstring>
#include <fstream>
#include <atomic>
#include <thread>

// MD5 via OpenSSL EVP (cross-platform: Windows/iOS/Android)
// Android 在 GHV_NO_CRYPTO 模式下不链接 OpenSSL，GetMD5 将返回空字符串
#ifndef GHV_NO_CRYPTO
#include <openssl/evp.h>
#endif

#define GHV_DOWNLOAD_META "GHV_Download"

struct LuaDownload {
    std::string url;
    std::string filepath;   // empty = download to memory
    std::string range;

    std::atomic<int64_t> current_size;
    std::atomic<int64_t> total_size;
    std::atomic<int>     status;     // 0=pending, 1=downloading, 100=done, <0=error
    std::atomic<bool>    cancelled;  // cancellation flag for early termination

    std::string          memory_data;  // for in-memory download
    std::string          md5_hex;
    std::mutex           data_mutex;
    std::thread          worker;

    LuaDownload() : current_size(0), total_size(0), status(0), cancelled(false) {}

    ~LuaDownload() {
        cancelled = true;
        if (worker.joinable()) {
            // 已完成的下载可以直接 join（瞬间返回）
            if (status.load() == 100 || status.load() < 0) {
                worker.join();
            } else {
                // 未完成：给 worker 最多 500ms 响应 cancelled 标志后退出
                // 避免 iOS 上直接 detach 导致系统终止进程时资源泄漏/crash
                for (int i = 0; i < 50; ++i) {
                    if (status.load() == 100 || status.load() < 0) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                if (status.load() == 100 || status.load() < 0) {
                    worker.join();
                } else {
                    // 超时后仍未结束，只能 detach（最后手段）
                    worker.detach();
                }
            }
        }
    }

    void start() {
        status = 1; // downloading
        worker = std::thread([this]() {
            doDownload();
        });
    }

    void doDownload() {
        HttpRequest req;
        req.method = HTTP_GET;
        req.url = url;
        req.timeout = 300; // 5 minutes timeout for downloads

        if (!range.empty()) {
            req.headers["Range"] = "bytes=" + range;
        }

        HttpResponse resp;

        // Use synchronous client in worker thread for simplicity
        hv::HttpClient client;

        // For file download, we need streaming
        if (!filepath.empty()) {
            // Download to file
            // 断点续传：当 range 非空时以追加模式打开，否则覆写
            bool append_mode = !range.empty();
            FILE* fp = nullptr;
#ifdef _WIN32
            int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, NULL, 0);
            if (wlen > 0) {
                std::wstring wpath(wlen, 0);
                MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, &wpath[0], wlen);
                fp = _wfopen(wpath.c_str(), append_mode ? L"ab" : L"wb");
            }
#else
            fp = fopen(filepath.c_str(), append_mode ? "ab" : "wb");
#endif
            if (!fp) {
                status = -1;
                return;
            }

            resp.http_cb = [this, &fp, append_mode](HttpMessage* msg, http_parser_state state, const char* data, size_t size) {
                if (cancelled.load()) return;
                if (state == HP_HEADERS_COMPLETE) {
                    // 断点续传安全检测：请求了 Range 但服务器未返回 Content-Range 头（不支持），
                    // 需要截断文件重新写入，防止追加导致数据损坏
                    if (append_mode && msg->GetHeader("Content-Range").empty()) {
                        if (fp) {
                            fclose(fp);
#ifdef _WIN32
                            int twl = MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, NULL, 0);
                            if (twl > 0) {
                                std::wstring twp(twl, 0);
                                MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, &twp[0], twl);
                                fp = _wfopen(twp.c_str(), L"wb");
                            }
#else
                            fp = fopen(filepath.c_str(), "wb");
#endif
                        }
                    }
                    // Get content-length
                    std::string cl = msg->GetHeader("Content-Length");
                    if (!cl.empty()) {
                        total_size = std::stoll(cl);
                    }
                } else if (state == HP_BODY) {
                    if (data && size > 0 && fp) {
                        fwrite(data, 1, size, fp);
                        current_size += size;
                    }
                } else if (state == HP_MESSAGE_COMPLETE) {
                    // done
                }
            };

            int ret = client.send(&req, &resp);
            if (fp) fclose(fp);

            if (cancelled.load() || ret != 0 || resp.status_code < 200 || resp.status_code >= 400) {
                // 删除下载失败/取消的残留文件，防止热更加载损坏资源
#ifdef _WIN32
                int rwl = MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, NULL, 0);
                if (rwl > 0) {
                    std::wstring rwp(rwl, 0);
                    MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, &rwp[0], rwl);
                    _wremove(rwp.c_str());
                }
#else
                remove(filepath.c_str());
#endif
                status = -(resp.status_code ? resp.status_code : ret);
            } else {
                status = 100;
            }
        } else {
            // Download to memory
            resp.http_cb = [this](HttpMessage* msg, http_parser_state state, const char* data, size_t size) {
                if (cancelled.load()) return;
                if (state == HP_HEADERS_COMPLETE) {
                    std::string cl = msg->GetHeader("Content-Length");
                    if (!cl.empty()) {
                        total_size = std::stoll(cl);
                        // 预分配内存减少大文件下载时的反复 realloc
                        std::lock_guard<std::mutex> lock(data_mutex);
                        memory_data.reserve(static_cast<size_t>(total_size.load()));
                    }
                } else if (state == HP_BODY) {
                    if (data && size > 0) {
                        std::lock_guard<std::mutex> lock(data_mutex);
                        memory_data.append(data, size);
                        current_size += size;
                    }
                }
            };

            int ret = client.send(&req, &resp);

            if (ret != 0 || resp.status_code < 200 || resp.status_code >= 400) {
                status = -(resp.status_code ? resp.status_code : ret);
            } else {
                // If no streaming data was captured, use response body
                if (memory_data.empty() && !resp.body.empty()) {
                    std::lock_guard<std::mutex> lock(data_mutex);
                    memory_data = resp.body;
                    current_size = memory_data.size();
                    total_size = memory_data.size();
                }
                status = 100;
            }
        }
    }
};

static LuaDownload* check_download(lua_State* L) {
    return *(LuaDownload**)luaL_checkudata(L, 1, GHV_DOWNLOAD_META);
}

// dl:GetState() -> current_size, total_size, status
static int l_download_get_state(lua_State* L) {
    LuaDownload* self = check_download(L);
    lua_pushinteger(L, (lua_Integer)self->current_size.load());
    lua_pushinteger(L, (lua_Integer)self->total_size.load());
    lua_pushinteger(L, self->status.load());
    return 3;
}

// dl:GetData() -> string
static int l_download_get_data(lua_State* L) {
    LuaDownload* self = check_download(L);
    std::lock_guard<std::mutex> lock(self->data_mutex);
    lua_pushlstring(L, self->memory_data.data(), self->memory_data.size());
    return 1;
}

// dl:GetMD5() -> string  (cross-platform via OpenSSL EVP)
static int l_download_get_md5(lua_State* L) {
    LuaDownload* self = check_download(L);
#ifndef GHV_NO_CRYPTO
    if (self->md5_hex.empty() && self->status == 100) {
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (ctx && EVP_DigestInit_ex(ctx, EVP_md5(), NULL)) {
            if (!self->filepath.empty()) {
                FILE* f = nullptr;
#ifdef _WIN32
                int md5wl = MultiByteToWideChar(CP_UTF8, 0, self->filepath.c_str(), -1, NULL, 0);
                if (md5wl > 0) {
                    std::wstring md5wp(md5wl, 0);
                    MultiByteToWideChar(CP_UTF8, 0, self->filepath.c_str(), -1, &md5wp[0], md5wl);
                    f = _wfopen(md5wp.c_str(), L"rb");
                }
#else
                f = fopen(self->filepath.c_str(), "rb");
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
                std::lock_guard<std::mutex> lock(self->data_mutex);
                EVP_DigestUpdate(ctx, self->memory_data.data(), self->memory_data.size());
            }
            unsigned char md[EVP_MAX_MD_SIZE];
            unsigned int md_len = 0;
            if (EVP_DigestFinal_ex(ctx, md, &md_len)) {
                char hex[33] = {0};
                for (unsigned int i = 0; i < md_len && i < 16; i++) {
                    sprintf(hex + i * 2, "%02x", md[i]);
                }
                self->md5_hex = hex;
            }
        }
        if (ctx) EVP_MD_CTX_free(ctx);
    }
#endif
    lua_pushstring(L, self->md5_hex.c_str());
    return 1;
}

// GC — prevent double-free and handle ongoing downloads safely
static int l_download_gc(lua_State* L) {
    LuaDownload** ud = (LuaDownload**)luaL_checkudata(L, 1, GHV_DOWNLOAD_META);
    LuaDownload* self = *ud;
    if (!self) return 0;
    *ud = nullptr;  // Prevent double-free
    delete self;
    return 0;
}

// Constructor: download(url, [filepath], [range])
static int l_download_new(lua_State* L) {
    const char* url = luaL_checkstring(L, 1);
    const char* filepath = luaL_optstring(L, 2, NULL);
    const char* range = luaL_optstring(L, 3, NULL);

    LuaDownload* self = new LuaDownload();
    self->url = url;
    if (filepath) self->filepath = filepath;
    if (range) self->range = range;

    LuaDownload** ud = (LuaDownload**)lua_newuserdata(L, sizeof(LuaDownload*));
    *ud = self;
    luaL_setmetatable(L, GHV_DOWNLOAD_META);

    // Start download immediately
    self->start();

    return 1;
}

GHV_EXPORT int luaopen_ghv_download(lua_State* L)
{
    luaL_Reg methods[] = {
        {"GetState", l_download_get_state},
        {"GetData",  l_download_get_data},
        {"GetMD5",   l_download_get_md5},
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
