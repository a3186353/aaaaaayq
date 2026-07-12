/**
 * ghv_download.cpp - HTTP file download Lua binding
 * Exports: luaopen_ghv_download
 *
 * Replacement for ghpsocket.download.
 * Uses a bounded worker pool around libhv's synchronous HttpClient so slow CDN
 * sources cannot create one OS thread per Lua download object.
 *
 * Usage in Lua:
 *   local download = require('ghv.download')
 *   local dl = download(url, [filepath], [range])
 *   local cur, total, status = dl:GetState()
 *   local data = dl:GetData()
 *   local md5 = dl:GetMD5()
 *   dl:Cancel()
 */
#include "ghv_common.h"
#include "HttpClient.h"
#include "hbase.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef GHV_NO_CRYPTO
#include <openssl/evp.h>
#endif

#define GHV_DOWNLOAD_META "GHV_Download"

#define GHV_DOWNLOAD_MAX_ACTIVE 6
#define GHV_DOWNLOAD_QUEUE_CAP 128
#define GHV_DOWNLOAD_STATUS_DOWNLOADING 1
#define GHV_DOWNLOAD_STATUS_DONE 100
#define GHV_DOWNLOAD_STATUS_CANCELLED -10001
#define GHV_DOWNLOAD_STATUS_QUEUE_FULL -10002

struct DownloadCoreState {
    std::string url;
    std::string filepath;   // empty = download to memory
    std::string range;
    int         timeout{60};

    std::atomic<int64_t> current_size{0};
    std::atomic<int64_t> total_size{0};
    std::atomic<int>     status{0};
    std::atomic<bool>    cancelled{false};
    std::atomic<bool>    cancel_recorded{false};
    std::atomic<bool>    queued{false};

    std::string          memory_data;
    std::string          md5_hex;
    std::mutex           data_mutex;
};

static void download_do_download(std::shared_ptr<DownloadCoreState> state);

static bool download_try_finish(const std::shared_ptr<DownloadCoreState>& state, int status)
{
    if (!state)
        return false;
    int expected = GHV_DOWNLOAD_STATUS_DOWNLOADING;
    return state->status.compare_exchange_strong(expected, status);
}

static FILE* download_open_file(const std::string& filepath, const char* mode)
{
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, NULL, 0);
    if (wlen <= 0)
        return nullptr;
    std::wstring wpath(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, &wpath[0], wlen);
    int mlen = MultiByteToWideChar(CP_UTF8, 0, mode, -1, NULL, 0);
    if (mlen <= 0)
        return nullptr;
    std::wstring wmode(mlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, mode, -1, &wmode[0], mlen);
    return _wfopen(wpath.c_str(), wmode.c_str());
#else
    return fopen(filepath.c_str(), mode);
#endif
}

static void download_remove_file(const std::string& filepath)
{
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, NULL, 0);
    if (wlen > 0) {
        std::wstring wpath(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, &wpath[0], wlen);
        _wremove(wpath.c_str());
    }
#else
    remove(filepath.c_str());
#endif
}

static void download_set_content_length(std::shared_ptr<DownloadCoreState> state, const std::string& value)
{
    if (value.empty())
        return;
    try {
        state->total_size = std::stoll(value);
    } catch (...) {
        state->total_size = 0;
    }
}

static void download_clear_memory(std::shared_ptr<DownloadCoreState> state)
{
    std::lock_guard<std::mutex> lock(state->data_mutex);
    std::string().swap(state->memory_data);
}

static int download_final_error(int ret, int status_code)
{
    if (ret != 0)
        return ret < 0 ? ret : -ret;
    return status_code > 0 ? -status_code : -1;
}

static bool download_is_timeout_status(int status)
{
    return status == -ETIMEDOUT || status == -1100 || status == -10060;
}

#ifndef GHV_NO_CRYPTO
static EVP_MD_CTX* download_md5_create()
{
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx && EVP_DigestInit_ex(ctx, EVP_md5(), NULL) == 1)
        return ctx;
    if (ctx)
        EVP_MD_CTX_free(ctx);
    return nullptr;
}

static bool download_md5_reset(EVP_MD_CTX* ctx)
{
    return ctx && EVP_MD_CTX_reset(ctx) == 1
        && EVP_DigestInit_ex(ctx, EVP_md5(), NULL) == 1;
}

static bool download_md5_existing_prefix(
    EVP_MD_CTX* ctx, const std::string& filepath, int64_t expected_size)
{
    if (!ctx)
        return false;
    FILE* f = download_open_file(filepath, "rb");
    if (!f)
        return errno == ENOENT && expected_size == 0;
    unsigned char buf[8192];
    size_t n = 0;
    int64_t total = 0;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (EVP_DigestUpdate(ctx, buf, n) != 1) {
            ok = false;
            break;
        }
        if (n > (size_t)(INT64_MAX - total)) {
            ok = false;
            break;
        }
        total += (int64_t)n;
    }
    if (ferror(f))
        ok = false;
    fclose(f);
    return ok && total == expected_size;
}

static bool download_md5_publish(EVP_MD_CTX* ctx, const std::shared_ptr<DownloadCoreState>& state)
{
    if (!ctx || !state)
        return false;
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;
    if (EVP_DigestFinal_ex(ctx, md, &md_len) != 1)
        return false;
    char value[33] = {0};
    for (unsigned int i = 0; i < md_len && i < 16; i++)
        snprintf(value + i * 2, 3, "%02x", md[i]);
    std::lock_guard<std::mutex> lock(state->data_mutex);
    if (state->cancelled.load())
        return false;
    state->md5_hex.assign(value);
    return true;
}
#endif

static void download_clear_md5(const std::shared_ptr<DownloadCoreState>& state)
{
    if (!state)
        return;
    std::lock_guard<std::mutex> lock(state->data_mutex);
    state->md5_hex.clear();
}

static bool download_parse_range_start(const std::string& value, int64_t* out)
{
    if (!out || value.empty())
        return false;
    const char* p = value.c_str();
    if (value.compare(0, 6, "bytes=") == 0)
        p += 6;
    else if (value.compare(0, 6, "bytes ") == 0)
        p += 6;
    if (*p < '0' || *p > '9')
        return false;
    errno = 0;
    char* end = nullptr;
    const long long start = strtoll(p, &end, 10);
    if (errno != 0 || !end || *end != '-' || start < 0)
        return false;
    *out = static_cast<int64_t>(start);
    return true;
}

class DownloadManager {
public:
    bool enqueue(const std::shared_ptr<DownloadCoreState>& state)
    {
        ensure_started();
        std::lock_guard<std::mutex> lock(mutex_);
        submitted_++;
        if (stopping_ || state->cancelled.load()) {
            state->status = GHV_DOWNLOAD_STATUS_CANCELLED;
            record_cancelled_locked(state);
            return false;
        }
        if (queue_.size() >= GHV_DOWNLOAD_QUEUE_CAP) {
            queue_full_++;
            state->status = GHV_DOWNLOAD_STATUS_QUEUE_FULL;
            return false;
        }
        state->status = GHV_DOWNLOAD_STATUS_DOWNLOADING;
        state->queued = true;
        queue_.push_back(state);
        queued_total_++;
        cv_.notify_one();
        return true;
    }

    void cancel(const std::shared_ptr<DownloadCoreState>& state)
    {
        if (!state)
            return;
        state->cancelled = true;
        int status = state->status.load();
        while (status != GHV_DOWNLOAD_STATUS_DONE && status >= 0) {
            if (state->status.compare_exchange_weak(
                    status, GHV_DOWNLOAD_STATUS_CANCELLED))
                break;
        }
        if (status == GHV_DOWNLOAD_STATUS_DONE || status < 0)
            return;
        download_clear_md5(state);
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = queue_.begin(); it != queue_.end(); ++it) {
            if (it->get() == state.get()) {
                queue_.erase(it);
                state->queued = false;
                record_cancelled_locked(state);
                cv_.notify_all();
                return;
            }
        }
        record_cancelled_locked(state);
    }

    void push_stats(lua_State* L)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lua_createtable(L, 0, 11);
        lua_pushinteger(L, (lua_Integer)active_);
        lua_setfield(L, -2, "active");
        lua_pushinteger(L, (lua_Integer)queue_.size());
        lua_setfield(L, -2, "pending");
        lua_pushinteger(L, (lua_Integer)workers_.size());
        lua_setfield(L, -2, "thread_count");
        lua_pushinteger(L, (lua_Integer)GHV_DOWNLOAD_MAX_ACTIVE);
        lua_setfield(L, -2, "max_active");
        lua_pushinteger(L, (lua_Integer)GHV_DOWNLOAD_QUEUE_CAP);
        lua_setfield(L, -2, "queue_cap");
        lua_pushinteger(L, (lua_Integer)submitted_.load());
        lua_setfield(L, -2, "submitted");
        lua_pushinteger(L, (lua_Integer)started_.load());
        lua_setfield(L, -2, "started");
        lua_pushinteger(L, (lua_Integer)queued_total_.load());
        lua_setfield(L, -2, "queued_total");
        lua_pushinteger(L, (lua_Integer)completed_.load());
        lua_setfield(L, -2, "completed");
        lua_pushinteger(L, (lua_Integer)cancelled_.load());
        lua_setfield(L, -2, "cancelled");
        lua_pushinteger(L, (lua_Integer)queue_full_.load());
        lua_setfield(L, -2, "queue_full");
        lua_pushinteger(L, (lua_Integer)timeout_.load());
        lua_setfield(L, -2, "timeout");
    }

    ~DownloadManager()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            for (auto& state : queue_) {
                if (state) {
                    state->queued = false;
                    state->cancelled = true;
                    state->status = GHV_DOWNLOAD_STATUS_CANCELLED;
                    record_cancelled_locked(state);
                }
            }
            queue_.clear();
        }
        cv_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable())
                worker.join();
        }
    }

private:
    void ensure_started()
    {
        bool start = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!started_pool_) {
                started_pool_ = true;
                start = true;
            }
        }
        if (!start)
            return;
        for (int i = 0; i < GHV_DOWNLOAD_MAX_ACTIVE; ++i) {
            std::lock_guard<std::mutex> lock(mutex_);
            workers_.emplace_back([this]() { worker_loop(); });
        }
    }

    void record_cancelled_locked(const std::shared_ptr<DownloadCoreState>& state)
    {
        bool expected = false;
        if (state && state->cancel_recorded.compare_exchange_strong(expected, true))
            cancelled_++;
    }

    void worker_loop()
    {
        for (;;) {
            std::shared_ptr<DownloadCoreState> state;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() { return stopping_ || !queue_.empty(); });
                if (stopping_ && queue_.empty())
                    return;
                state = queue_.front();
                queue_.pop_front();
                if (state)
                    state->queued = false;
                active_++;
                started_++;
            }

            if (!state || state->cancelled.load()) {
                if (state) {
                    download_try_finish(state, GHV_DOWNLOAD_STATUS_CANCELLED);
                    std::lock_guard<std::mutex> lock(mutex_);
                    record_cancelled_locked(state);
                }
                finish_one(state);
                continue;
            }

            download_do_download(state);
            finish_one(state);
        }
    }

    void finish_one(const std::shared_ptr<DownloadCoreState>& state)
    {
        int status = state ? state->status.load() : 0;
        if (download_is_timeout_status(status))
            timeout_++;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (active_ > 0)
                active_--;
            completed_++;
        }
        cv_.notify_all();
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<DownloadCoreState>> queue_;
    std::vector<std::thread> workers_;
    bool started_pool_{false};
    bool stopping_{false};
    unsigned int active_{0};
    std::atomic<unsigned int> submitted_{0};
    std::atomic<unsigned int> queued_total_{0};
    std::atomic<unsigned int> started_{0};
    std::atomic<unsigned int> completed_{0};
    std::atomic<unsigned int> cancelled_{0};
    std::atomic<unsigned int> queue_full_{0};
    std::atomic<unsigned int> timeout_{0};
};

static DownloadManager& download_manager()
{
    static DownloadManager manager;
    return manager;
}

static void download_do_download(std::shared_ptr<DownloadCoreState> state)
{
    if (!state || state->cancelled.load()) {
        if (state)
            download_try_finish(state, GHV_DOWNLOAD_STATUS_CANCELLED);
        return;
    }

    HttpRequest req;
    req.method = HTTP_GET;
    req.url = state->url;
    req.timeout = state->timeout;

    if (!state->range.empty()) {
        req.headers["Range"] = "bytes=" + state->range;
    }

    HttpResponse resp;
    hv::HttpClient client;

    if (!state->filepath.empty()) {
        bool append_mode = !state->range.empty();
        int64_t requested_offset = 0;
        if (append_mode && !download_parse_range_start(state->range, &requested_offset)) {
            download_try_finish(state, -EINVAL);
            return;
        }
#ifndef GHV_NO_CRYPTO
        EVP_MD_CTX* digest = download_md5_create();
        bool digest_ok = digest != nullptr;
        if (append_mode && digest_ok)
            digest_ok = download_md5_existing_prefix(digest, state->filepath, requested_offset);
#endif
        bool write_failed = false;
        FILE* fp = download_open_file(state->filepath, append_mode ? "ab" : "wb");
        if (!fp) {
#ifndef GHV_NO_CRYPTO
            if (digest) EVP_MD_CTX_free(digest);
#endif
            download_try_finish(state, -1);
            return;
        }

        resp.http_cb = [state, &fp, append_mode, requested_offset, &write_failed
#ifndef GHV_NO_CRYPTO
            , digest, &digest_ok
#endif
        ](HttpMessage* msg, http_parser_state state_h, const char* data, size_t size) {
            if (state->cancelled.load()) return;
            if (state_h == HP_HEADERS_COMPLETE) {
                if (append_mode) {
                    const HttpResponse* response = static_cast<const HttpResponse*>(msg);
                    const std::string content_range = msg->GetHeader("Content-Range");
                    int64_t response_offset = -1;
                    const bool exact_resume = response
                        && response->status_code == HTTP_STATUS_PARTIAL_CONTENT
                        && download_parse_range_start(content_range, &response_offset)
                        && response_offset == requested_offset;
                    const bool full_restart = response
                        && response->status_code == HTTP_STATUS_OK
                        && content_range.empty();
                    if (!exact_resume && !full_restart) {
                        write_failed = true;
                        if (fp) {
                            fclose(fp);
                            fp = nullptr;
                        }
                        return;
                    }
                    if (full_restart) {
                        if (fp) {
                            fclose(fp);
                            fp = download_open_file(state->filepath, "wb");
                        }
#ifndef GHV_NO_CRYPTO
                        digest_ok = download_md5_reset(digest);
#endif
                        if (!fp)
                            write_failed = true;
                    }
                }
                download_set_content_length(state, msg->GetHeader("Content-Length"));
            } else if (state_h == HP_BODY) {
                if (data && size > 0 && fp) {
                    const size_t written = fwrite(data, 1, size, fp);
                    if (written != size) {
                        write_failed = true;
                    } else {
#ifndef GHV_NO_CRYPTO
                        if (digest_ok && EVP_DigestUpdate(digest, data, size) != 1)
                            digest_ok = false;
#endif
                        state->current_size += size;
                    }
                }
            }
        };

        int ret = client.send(&req, &resp);
        if (fp && fclose(fp) != 0)
            write_failed = true;

        if (state->cancelled.load()) {
            download_remove_file(state->filepath);
            download_clear_md5(state);
            download_try_finish(state, GHV_DOWNLOAD_STATUS_CANCELLED);
        } else if (write_failed || ret != 0 || resp.status_code < 200 || resp.status_code >= 400) {
            download_remove_file(state->filepath);
            download_clear_md5(state);
            download_try_finish(state, write_failed ? -EIO : download_final_error(ret, resp.status_code));
        } else {
            bool digest_published = true;
#ifndef GHV_NO_CRYPTO
            digest_published = digest_ok && download_md5_publish(digest, state);
#endif
            if (!digest_published) {
                download_remove_file(state->filepath);
                download_clear_md5(state);
                download_try_finish(state, -EIO);
            } else if (!download_try_finish(state, GHV_DOWNLOAD_STATUS_DONE)) {
                download_remove_file(state->filepath);
                download_clear_md5(state);
            }
        }
#ifndef GHV_NO_CRYPTO
        if (digest) EVP_MD_CTX_free(digest);
#endif
    } else {
#ifndef GHV_NO_CRYPTO
        EVP_MD_CTX* digest = download_md5_create();
        bool digest_ok = digest != nullptr;
#endif
        resp.http_cb = [state
#ifndef GHV_NO_CRYPTO
            , digest, &digest_ok
#endif
        ](HttpMessage* msg, http_parser_state state_h, const char* data, size_t size) {
            if (state->cancelled.load()) return;
            if (state_h == HP_HEADERS_COMPLETE) {
                download_set_content_length(state, msg->GetHeader("Content-Length"));
                if (state->total_size.load() > 0) {
                    std::lock_guard<std::mutex> lock(state->data_mutex);
                    state->memory_data.reserve(static_cast<size_t>(state->total_size.load()));
                }
            } else if (state_h == HP_BODY) {
                if (data && size > 0) {
                    std::lock_guard<std::mutex> lock(state->data_mutex);
                    state->memory_data.append(data, size);
#ifndef GHV_NO_CRYPTO
                    if (digest_ok && EVP_DigestUpdate(digest, data, size) != 1)
                        digest_ok = false;
#endif
                    state->current_size += size;
                }
            }
        };

        int ret = client.send(&req, &resp);

        if (state->cancelled.load()) {
            download_clear_memory(state);
            download_clear_md5(state);
            download_try_finish(state, GHV_DOWNLOAD_STATUS_CANCELLED);
        } else if (ret != 0 || resp.status_code < 200 || resp.status_code >= 400) {
            download_clear_memory(state);
            download_clear_md5(state);
            download_try_finish(state, download_final_error(ret, resp.status_code));
        } else {
            if (state->memory_data.empty() && !resp.body.empty()) {
                std::lock_guard<std::mutex> lock(state->data_mutex);
                state->memory_data = resp.body;
                state->current_size = state->memory_data.size();
                state->total_size = state->memory_data.size();
#ifndef GHV_NO_CRYPTO
                if (digest_ok && EVP_DigestUpdate(digest, state->memory_data.data(), state->memory_data.size()) != 1)
                    digest_ok = false;
#endif
            }
            bool digest_published = true;
#ifndef GHV_NO_CRYPTO
            digest_published = digest_ok && download_md5_publish(digest, state);
#endif
            if (!digest_published) {
                download_clear_memory(state);
                download_clear_md5(state);
                download_try_finish(state, -EIO);
            } else if (!download_try_finish(state, GHV_DOWNLOAD_STATUS_DONE)) {
                download_clear_memory(state);
                download_clear_md5(state);
            }
        }
#ifndef GHV_NO_CRYPTO
        if (digest) EVP_MD_CTX_free(digest);
#endif
    }
}

struct LuaDownload {
    std::shared_ptr<DownloadCoreState> core;

    LuaDownload() : core(std::make_shared<DownloadCoreState>()) {}

    ~LuaDownload() {
        download_manager().cancel(core);
    }

    void start() {
        download_manager().enqueue(core);
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
    std::lock_guard<std::mutex> lock(self->core->data_mutex);
    lua_pushstring(L, self->core->md5_hex.c_str());
    return 1;
}

static int l_download_cancel(lua_State* L) {
    LuaDownload* self = check_download(L);
    download_manager().cancel(self->core);
    return 0;
}

static int l_download_stats(lua_State* L) {
    download_manager().push_stats(L);
    return 1;
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
    int timeout = (int)luaL_optinteger(L, 4, 60);

    LuaDownload* self = new LuaDownload();
    self->core->url = url;
    if (filepath) self->core->filepath = filepath;
    if (range) self->core->range = range;
    self->core->timeout = timeout;

    LuaDownload** ud = (LuaDownload**)lua_newuserdata(L, sizeof(LuaDownload*));
    *ud = self;
    luaL_setmetatable(L, GHV_DOWNLOAD_META);

    self->start();
    return 1;
}

GHV_EXPORT int luaopen_ghv_download(lua_State* L)
{
    ghv_init_libhv_log(L);
    luaL_Reg methods[] = {
        {"GetState", l_download_get_state},
        {"GetData",  l_download_get_data},
        {"GetMD5",   l_download_get_md5},
        {"Cancel",   l_download_cancel},
        {"Stats",    l_download_stats},
        {"GetStats", l_download_stats},
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
