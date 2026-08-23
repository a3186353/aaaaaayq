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
#include "md5.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef GHV_NO_CRYPTO
#include <openssl/evp.h>
#endif

#define GHV_DOWNLOAD_META "GHV_Download"

// 阶段4：worker 池 10 线程，喂满 Lua 层 PC=8 / 移动=5 的并发上限。
#define GHV_DOWNLOAD_MAX_ACTIVE 10
// 阶段4：worker 池 10 线程，喂满 Lua 层 PC=8 / 移动=5 的并发上限。
#define GHV_DOWNLOAD_MAX_ACTIVE 10
#define GHV_DOWNLOAD_QUEUE_CAP 128
#define GHV_DOWNLOAD_STATUS_DOWNLOADING 1
#define GHV_DOWNLOAD_STATUS_DONE 100
#define GHV_DOWNLOAD_STATUS_CANCELLED -10001
#define GHV_DOWNLOAD_STATUS_QUEUE_FULL -10002
#define GHV_DOWNLOAD_CDN_MAX_BYTES (128LL * 1024 * 1024)

struct DownloadCandidate {
    std::string url;
    std::string kind;
    std::string resource_type;
    std::string source_mode;
    std::string cache_key;
    std::string act;
    std::string ver;
    std::string jsoncmd;
    int64_t pid{0};
    int64_t eid{0};
    int dir{0};
    int candidate_index{0};
    int candidate_total{0};
    bool bare{false};
    bool cacheable{true};
};

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

    bool                           candidate_mode{false};
    bool                           mirror_prefer{false};
    bool                           mirror_only{false};
    // 阶段3：明文 http:// 降级默认关闭（spec 可显式开启）。
    bool                           allow_plain_http{false};
    // 阶段3：明文 http:// 降级默认关闭（spec 可显式开启）。
    bool                           allow_plain_http{false};
    std::atomic<int>               attempt_status{0};
    std::vector<DownloadCandidate> candidates;
    DownloadCandidate              result;
    bool                           has_result{false};

    std::string          memory_data;
    std::string          md5_hex;
    std::mutex           data_mutex;
};

struct DownloadReuseKey {
    std::string host;
    int         port{0};
    bool        https{false};
};

static void download_do_download(std::shared_ptr<DownloadCoreState> state,
    hv::HttpClient& client, DownloadReuseKey& reuse);
struct DownloadReuseKey {
    std::string host;
    int         port{0};
    bool        https{false};
};

static void download_do_download(std::shared_ptr<DownloadCoreState> state,
    hv::HttpClient& client, DownloadReuseKey& reuse);
static void download_clear_memory(std::shared_ptr<DownloadCoreState> state);
static void download_clear_md5(const std::shared_ptr<DownloadCoreState>& state);
static void download_remove_file(const std::string& filepath);
static std::string download_key_from_spec(lua_State* L, int index, std::string& error);

static bool download_try_finish(const std::shared_ptr<DownloadCoreState>& state, int status)
{
    if (!state)
        return false;
    int expected = GHV_DOWNLOAD_STATUS_DOWNLOADING;
    if (state->candidate_mode)
        return state->attempt_status.compare_exchange_strong(expected, status);
    return state->status.compare_exchange_strong(expected, status);
}

static std::string download_md5_hex(const std::string& value)
{
    char output[33] = {0};
    hv_md5_hex((unsigned char*)value.data(), (unsigned int)value.size(), output, sizeof(output));
    return std::string(output);
}

static std::string download_json_escape(const std::string& value)
{
    std::ostringstream out;
    for (unsigned char c : value) {
        switch (c) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c
                    << std::dec << std::setfill(' ');
            } else {
                out << (char)c;
            }
        }
    }
    return out.str();
}

static std::string download_spr_json(
    int64_t pid, int64_t eid, int dir, const std::string& act,
    const std::string& ver, const std::string& jsoncmd, bool include_empty_ver)
{
    std::ostringstream out;
    out << "{\"act\":\"" << download_json_escape(act) << "\",\"dir\":" << dir
        << ",\"eid\":" << eid << ",\"jsoncmd\":\"" << download_json_escape(jsoncmd)
        << "\",\"pid\":" << pid << ",\"scale\":1.25";
    if (include_empty_ver || !ver.empty())
        out << ",\"ver\":\"" << download_json_escape(ver) << "\"";
    out << "}";
    return out.str();
}

static std::string download_spr_hash(
    int64_t pid, int64_t eid, int dir, const std::string& act,
    const std::string& ver, const std::string& jsoncmd)
{
    return download_md5_hex(download_spr_json(pid, eid, dir, act, ver, jsoncmd, false));
}

static std::string download_resource_key(
    int64_t pid, int64_t eid, int dir, const std::string& act,
    const std::string& ver, const std::string& jsoncmd, const std::string& source_mode)
{
    std::ostringstream out;
    out << "{\"act\":\"" << download_json_escape(act) << "\",\"dir\":" << dir
        << ",\"eid\":" << eid << ",\"jsoncmd\":\"" << download_json_escape(jsoncmd)
        << "\",\"pid\":" << pid << ",\"scale\":1.25,\"source_mode\":\""
        << download_json_escape(source_mode) << "\",\"ver\":\""
        << download_json_escape(ver) << "\"}";
    return download_md5_hex(out.str());
}

static std::string download_warride_key(
    int64_t pid, int64_t eid, int dir, const std::string& act,
    const std::string& ver, const std::string& jsoncmd, const std::string& kind,
    const std::string& source_mode)
{
    std::ostringstream out;
    // Legacy WPK entries hash this exact colon-delimited identity.
    out << source_mode << ':' << kind << ':' << pid << ':' << eid << ':' << dir
        << ':' << act << ':' << ver << ':' << jsoncmd;
    return download_md5_hex(out.str());
}

static std::string download_addon_key(
    int64_t shape, const std::string& slot, const std::string& act, int dir,
    const std::string& ext, bool nested = false, int64_t ref_idx = 0,
    int64_t layer_offset = 0)
{
    std::ostringstream value;
    value << "addon:" << shape << ':' << slot << ':';
    if (nested) value << ref_idx << ':' << layer_offset << ':';
    value << act << ':' << dir << ':' << ext;
    return download_md5_hex(value.str());
}

static uint32_t download_direct_hash(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        if (c == '/') return (char)'\\';
        return (char)std::tolower(c);
    });
    uint32_t words[70] = {0};
    const size_t max_bytes = sizeof(words) - 8;
    const size_t n = value.size() < max_bytes ? value.size() : max_bytes;
    memcpy(words, value.data(), n);
    size_t count = 0;
    while (count < 64 && words[count]) ++count;
    words[count++] = 0x9BE74448u;
    words[count++] = 0x66F42C48u;
    uint32_t v = 0xF4FA8928u;
    uint32_t edi = 0x7758B42Bu;
    uint32_t esi = 0x37A8470Eu;
    for (size_t i = 0; i < count; ++i) {
        v = (v << 1) | (v >> 31);
        const uint32_t ebx = 0x267B0B11u ^ v;
        esi ^= words[i];
        edi ^= words[i];
        uint32_t edx = (ebx + edi) | 0x02040801u;
        edx &= 0xBFEF7FDFu;
        uint64_t product = (uint64_t)edx * esi;
        uint32_t eax = (uint32_t)product;
        edx = (uint32_t)(product >> 32);
        uint64_t folded = (uint64_t)eax + edx + (edx != 0 ? 1u : 0u);
        eax = (uint32_t)folded;
        if ((uint32_t)(folded >> 32) != 0) ++eax;
        edx = (ebx + esi) | 0x00804021u;
        edx &= 0x7DFEFBFFu;
        esi = eax;
        product = (uint64_t)edi * edx;
        eax = (uint32_t)product;
        edx = (uint32_t)(product >> 32);
        const uint64_t doubled_full = (uint64_t)edx + edx;
        const uint32_t doubled = (uint32_t)doubled_full;
        folded = (uint64_t)eax + doubled + (uint32_t)(doubled_full >> 32);
        eax = (uint32_t)folded;
        if ((uint32_t)(folded >> 32) != 0) eax += 2;
        edi = eax;
    }
    return esi ^ edi;
}

static std::string download_direct_hash_hex(const std::string& act, int dir)
{
    std::ostringstream name;
    name << act << "_d" << dir << "_com";
    std::ostringstream out;
    out << std::hex << std::nouppercase << download_direct_hash(name.str());
    return out.str();
}

static std::string download_url_encode(const std::string& value)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

static bool download_valid_segment(const std::string& value, size_t max_len)
{
    if (value.empty() || value.size() > max_len || value == "." || value == "..")
        return false;
    for (unsigned char c : value) {
        if (!(std::isalnum(c) || c == '_' || c == '-' || c == '.'))
            return false;
    }
    return true;
}

static bool download_valid_extension(const std::string& value)
{
    return value == "json" || value == "png" || value == "graypng"
        || value == "spr" || value == "pal.bmp" || value == "flag";
}

static bool download_valid_action(const std::string& value)
{
    return download_valid_segment(value, 64);
}

static bool download_read_string(lua_State* L, int index, const char* field,
    std::string& out, size_t max_len, bool required = false)
{
    lua_getfield(L, index, field);
    const bool present = !lua_isnil(L, -1);
    if (present) {
        if (lua_type(L, -1) != LUA_TSTRING) {
            lua_pop(L, 1);
            return false;
        }
        size_t len = 0;
        const char* value = lua_tolstring(L, -1, &len);
        if (!value || len > max_len) {
            lua_pop(L, 1);
            return false;
        }
        out.assign(value, len);
    }
    lua_pop(L, 1);
    return present || !required;
}

static bool download_read_integer(lua_State* L, int index, const char* field,
    int64_t& out, int64_t min_value, int64_t max_value, bool required = false)
{
    lua_getfield(L, index, field);
    const bool present = !lua_isnil(L, -1);
    if (present) {
        if (!lua_isinteger(L, -1)) {
            lua_pop(L, 1);
            return false;
        }
        out = (int64_t)lua_tointeger(L, -1);
        if (out < min_value || out > max_value) {
            lua_pop(L, 1);
            return false;
        }
    }
    lua_pop(L, 1);
    return present || !required;
}

static bool download_read_boolean(lua_State* L, int index, const char* field, bool& out)
{
    lua_getfield(L, index, field);
    const bool present = !lua_isnil(L, -1);
    if (present) {
        if (!lua_isboolean(L, -1)) {
            lua_pop(L, 1);
            return false;
        }
        out = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);
    return true;
}

static bool download_read_spec_number(lua_State* L, int index, const char* field,
    int64_t& out, int64_t min_value, int64_t max_value)
{
    return download_read_integer(L, index, field, out, min_value, max_value, true);
}

static std::string download_hash_fragment(const std::string& hash)
{
    return "/" + hash.substr(0, 2) + "/" + hash.substr(2);
}

static std::string download_hash_path(const std::string& hash)
{
    return download_hash_fragment(hash) + ".spr";
}

static std::string download_join_static(const std::string& path)
{
    return "https://xyq.gsf.netease.com/static_h5" + path;
}

static std::string download_join_avtres(const std::string& path)
{
    return "https://xyq.gsf.netease.com/h5avtres/1.25" + path;
}

static std::string download_render_json(const std::map<std::string, std::string>& values)
{
    std::ostringstream out;
    out << '{';
    bool first = true;
    for (const auto& item : values) {
        if (!first) out << ',';
        first = false;
        out << '"' << item.first << "\":" << item.second;
    }
    out << '}';
    return out.str();
}

static std::string download_render_url(
    int64_t pid, int64_t eid, int dir, const std::string& act,
    const std::string& ver, const std::string& jsoncmd,
    const std::string& cdnpath, bool restype)
{
    std::map<std::string, std::string> values;
    values["act"] = "\"" + download_json_escape(act) + "\"";
    if (!cdnpath.empty()) values["cdnpath"] = "\"" + download_json_escape(cdnpath) + "\"";
    values["dir"] = std::to_string(dir);
    values["eid"] = std::to_string(eid);
    if (!jsoncmd.empty()) values["jsoncmd"] = "\"" + download_json_escape(jsoncmd) + "\"";
    values["pid"] = std::to_string(pid);
    values["render_type"] = "\"h5sdk_dynamic\"";
    if (restype) values["restype"] = "\"json\"";
    values["scale"] = "1.25";
    values["ver"] = "\"" + download_json_escape(ver) + "\"";
    return "https://msycloudrender.nie.netease.com/clouderrender/render/?q="
        + download_url_encode(download_render_json(values));
}

static std::string download_mirror_base()
{
    const char* raw = std::getenv("MYGXY_CDN_ROUTE");
    if (!raw) return std::string();
    std::string value(raw);
    while (!value.empty() && value.back() == '/') value.pop_back();
    if (value.size() > 512 || (value.compare(0, 7, "http://") != 0
        && value.compare(0, 8, "https://") != 0))
        return std::string();
    if (value.find_first_of("\\\r\n\t \"") != std::string::npos)
        return std::string();
    return value;
}

static std::string download_mirror_url(const std::string& url)
{
    const std::string base = download_mirror_base();
    if (base.empty()) return std::string();
    const size_t scheme = url.find("://");
    if (scheme == std::string::npos) return std::string();
    const size_t slash = url.find('/', scheme + 3);
    if (slash == std::string::npos) return base + "/";
    return base + url.substr(slash);
}

// 阶段2：从 URL 提取 (host, port, https)，用于 worker 内连接复用的换宿判断。
static void download_split_url_host(const std::string& url,
    std::string& host, int& port, bool& https)
{
    https = url.rfind("https://", 0) == 0;
    const size_t scheme_end = url.find("://");
    const size_t host_begin = scheme_end == std::string::npos ? 0 : scheme_end + 3;
    const size_t path_slash = url.find('/', host_begin);
    std::string authority = url.substr(host_begin,
        path_slash == std::string::npos ? std::string::npos : path_slash - host_begin);
    const size_t at = authority.find('@');
    if (at != std::string::npos) authority = authority.substr(at + 1);
    port = https ? 443 : 80;
    const bool ipv6 = !authority.empty() && authority.front() == '[';
    const size_t colon = authority.rfind(':');
    if (!ipv6 && colon != std::string::npos) {
        host = authority.substr(0, colon);
        const int parsed = atoi(authority.c_str() + colon + 1);
        if (parsed > 0) port = parsed;
    } else {
        host = authority;
    }
}

// 阶段3：part 链裁剪 cloudrender_full 的进程级默认值；环境变量
// MYGXY_SPR_SKIP_RENDER_FULL=0 可整体恢复（覆盖 spec 缺省，供新旧 Lua 混布回退）。
static bool download_default_skip_render_full()
{
    static const bool value = []() {
        const char* raw = std::getenv("MYGXY_SPR_SKIP_RENDER_FULL");
        return !(raw && (raw[0] == '0' || raw[0] == 'f' || raw[0] == 'F'));
    }();
    return value;
}
// 阶段2：连接超时与总超时分离。注意本 libhv 版本 HttpRequest::connect_timeout
// 为 uint16_t、单位秒；默认 3 秒，环境变量 MYGXY_DOWNLOAD_CONNECT_TIMEOUT_S 可覆盖。
static int download_connect_timeout_sec()
{
    static const int value = []() {
        if (const char* raw = std::getenv("MYGXY_DOWNLOAD_CONNECT_TIMEOUT_S")) {
            const long v = strtol(raw, nullptr, 10);
            if (v >= 1 && v <= 30) return static_cast<int>(v);
        }
        return 3;
    }();
    return value;
}

static void download_reset_attempt(const std::shared_ptr<DownloadCoreState>& state)
{
    if (!state) return;
    state->current_size = 0;
    state->total_size = 0;
    state->attempt_status = GHV_DOWNLOAD_STATUS_DOWNLOADING;
    download_clear_memory(state);
    download_clear_md5(state);
    if (!state->filepath.empty()) download_remove_file(state->filepath);
}

static bool download_add_endpoint(
    const std::shared_ptr<DownloadCoreState>& state, const DownloadCandidate& base,
    const std::string& url, std::set<std::string>& seen)
{
    if (!state || url.empty() || !seen.insert(url).second) return false;
    DownloadCandidate item = base;
    item.url = url;
    state->candidates.push_back(item);
    return true;
}

static void download_add_candidate(
    const std::shared_ptr<DownloadCoreState>& state, DownloadCandidate base,
    const std::string& url, std::set<std::string>& seen)
{
    if (!state || url.empty()) return;
    const std::string mirror = download_mirror_url(url);
    const bool has_mirror = !mirror.empty();
    const auto add = [&](const std::string& candidate_url) {
        download_add_endpoint(state, base, candidate_url, seen);
    };
    if (state->mirror_only) {
        if (has_mirror) add(mirror);
        return;
    }
    if (state->mirror_prefer && has_mirror) add(mirror);
    add(url);
    // 阶段3：明文 http:// 降级默认移除——禁明文环境下每档白烧一个超时；
    // spec.allow_plain_http=true 时保留逃生门。
    if (state->allow_plain_http && url.compare(0, 8, "https://") == 0)
    // 阶段3：明文 http:// 降级默认移除——禁明文环境下每档白烧一个超时；
    // spec.allow_plain_http=true 时保留逃生门。
    if (state->allow_plain_http && url.compare(0, 8, "https://") == 0)
        add("http://" + url.substr(8));
    if (!state->mirror_prefer && has_mirror) add(mirror);
}

static bool download_finalize_candidates(const std::shared_ptr<DownloadCoreState>& state)
{
    if (!state || state->candidates.empty()) return false;
    std::map<int, int> normalized;
    int total = 0;
    for (DownloadCandidate& candidate : state->candidates) {
        auto inserted = normalized.emplace(candidate.candidate_index, total + 1);
        if (inserted.second) ++total;
        candidate.candidate_index = inserted.first->second;
    }
    for (DownloadCandidate& candidate : state->candidates)
        candidate.candidate_total = total;
    return true;
}

static DownloadCandidate download_candidate_base(
    const std::string& resource_type, const std::string& kind,
    const std::string& source_mode, const std::string& cache_key,
    int64_t pid, int64_t eid, int dir, const std::string& act,
    const std::string& ver, bool bare, bool cacheable, int index, int total)
{
    DownloadCandidate item;
    item.kind = kind;
    item.resource_type = resource_type;
    item.source_mode = source_mode;
    item.cache_key = cache_key;
    item.pid = pid;
    item.eid = eid;
    item.dir = dir;
    item.act = act;
    item.ver = ver;
    item.bare = bare;
    item.cacheable = cacheable;
    item.candidate_index = index;
    item.candidate_total = total;
    return item;
}

static bool download_build_spr_candidates(
    lua_State* L, int index, const std::shared_ptr<DownloadCoreState>& state,
    std::string& error)
{
    int64_t pid = 0, eid = 0, dir = 0;
    std::string act, ver, jsoncmd, source_mode, policy;
    if (!download_read_spec_number(L, index, "pid", pid, 0, 1000000000)
        || !download_read_spec_number(L, index, "eid", eid, 0, 1000000000)
        || !download_read_spec_number(L, index, "dir", dir, 0, 7)
        || !download_read_string(L, index, "act", act, 64, true)
        || !download_read_string(L, index, "ver", ver, 256)
        || !download_read_string(L, index, "jsoncmd", jsoncmd, 4096)
        || !download_read_string(L, index, "source_mode", source_mode, 64)
        || !download_read_string(L, index, "policy", policy, 32)) {
        error = "invalid spr spec";
        return false;
    }
    if (!download_valid_action(act)) {
        error = "invalid spr action";
        return false;
    }
    if (source_mode.empty()) source_mode = "cdn_composed_v1";
    if (!download_valid_segment(source_mode, 64)) {
        error = "invalid spr source_mode";
        return false;
    }
    if (policy.empty()) policy = "default";
    if (policy != "default" && policy != "basic_weapon"
        && policy != "part" && policy != "hd_weapon") {
        error = "unsupported spr policy";
        return false;
    }

    bool is_body = false, warride = false, disable_render = false;
    bool allow_direct = false;
    // 阶段3：part 链默认裁剪会挂起的 cloudrender_full 档；spec/环境变量可显式恢复。
    bool skip_render_full = download_default_skip_render_full();
    if (!download_read_boolean(L, index, "is_body", is_body)
        || !download_read_boolean(L, index, "warride_weapon", warride)
        || !download_read_boolean(L, index, "disable_render", disable_render)
        || !download_read_boolean(L, index, "allow_direct", allow_direct)
        || !download_read_boolean(L, index, "skip_render_full", skip_render_full)
        || !download_read_boolean(L, index, "skip_render_full", skip_render_full)
        || !download_read_boolean(L, index, "mirror_prefer", state->mirror_prefer)
        || !download_read_boolean(L, index, "mirror_only", state->mirror_only)
        || !download_read_boolean(L, index, "allow_plain_http", state->allow_plain_http)) {
        || !download_read_boolean(L, index, "mirror_only", state->mirror_only)
        || !download_read_boolean(L, index, "allow_plain_http", state->allow_plain_http)) {
        error = "invalid SPR boolean fields";
        return false;
    }

    std::string cache_jsoncmd = jsoncmd;
    std::string mapped_ver = ver, mapped_jsoncmd = jsoncmd;
    std::string original_ver = ver, original_jsoncmd = jsoncmd;
    std::string body_ver = ver, body_jsoncmd = jsoncmd;
    std::string fallback_act, fallback_ver, fallback_jsoncmd, fallback_cache_jsoncmd;
    if (!download_read_string(L, index, "cache_jsoncmd", cache_jsoncmd, 4096)
        || !download_read_string(L, index, "mapped_ver", mapped_ver, 256)
        || !download_read_string(L, index, "mapped_jsoncmd", mapped_jsoncmd, 4096)
        || !download_read_string(L, index, "original_ver", original_ver, 256)
        || !download_read_string(L, index, "original_jsoncmd", original_jsoncmd, 4096)
        || !download_read_string(L, index, "body_ver", body_ver, 256)
        || !download_read_string(L, index, "body_jsoncmd", body_jsoncmd, 4096)
        || !download_read_string(L, index, "fallback_act", fallback_act, 64)
        || !download_read_string(L, index, "fallback_ver", fallback_ver, 256)
        || !download_read_string(L, index, "fallback_jsoncmd", fallback_jsoncmd, 4096)
        || !download_read_string(L, index, "fallback_cache_jsoncmd", fallback_cache_jsoncmd, 4096)) {
        error = "invalid SPR candidate fields";
        return false;
    }
    if (!fallback_act.empty() && !download_valid_action(fallback_act)) {
        error = "invalid fallback action";
        return false;
    }

    std::set<std::string> seen;
    int logical_index = 0;
    const int part_total = fallback_act.empty()
        ? (skip_render_full ? 4 : 5)
        : (skip_render_full ? 8 : 10);
    const int logical_total = warride ? (skip_render_full ? 2 : 3)
    const int part_total = fallback_act.empty()
        ? (skip_render_full ? 4 : 5)
        : (skip_render_full ? 8 : 10);
    const int logical_total = warride ? (skip_render_full ? 2 : 3)
        : (policy == "basic_weapon" ? 4
        : (policy == "part" ? part_total
        : (policy == "part" ? part_total
        : (is_body ? (disable_render ? 2 : 4) : (disable_render ? 1 : 2))));
    auto add_spr = [&](const std::string& candidate_kind, int64_t candidate_eid,
        int candidate_dir, const std::string& candidate_act, const std::string& candidate_ver,
        const std::string& candidate_jsoncmd, const std::string& candidate_cache_jsoncmd,
        bool bare, bool cacheable, const std::string& candidate_source_mode,
        const std::string& path_kind, bool render, bool direct) {
        ++logical_index;
        const std::string hash = download_spr_hash(pid, candidate_eid, candidate_dir,
            candidate_act, candidate_ver, candidate_jsoncmd);
        const std::string cache_key = warride
            ? download_warride_key(pid, candidate_eid, candidate_dir, candidate_act,
                candidate_ver, candidate_jsoncmd, candidate_kind, candidate_source_mode)
            : download_resource_key(pid, candidate_eid, candidate_dir, candidate_act,
                candidate_ver, candidate_cache_jsoncmd, candidate_source_mode);
        DownloadCandidate base = download_candidate_base(
            "spr", candidate_kind, candidate_source_mode, cache_key, pid, candidate_eid,
            candidate_dir, candidate_act, candidate_ver, bare, cacheable,
            logical_index, logical_total);
        base.jsoncmd = candidate_jsoncmd;
        std::string url;
        if (render) {
            std::string cdnpath = path_kind.empty() ? std::string()
                : "h5sdk_hash" + download_hash_fragment(download_spr_hash(
                    pid, candidate_eid, candidate_dir, candidate_act,
                    candidate_ver, candidate_jsoncmd));
            url = download_render_url(pid, candidate_eid, candidate_dir, candidate_act,
                candidate_ver, candidate_jsoncmd, cdnpath, !path_kind.empty());
        } else if (direct) {
            const std::string hash_name = download_direct_hash_hex(candidate_act, candidate_dir);
            std::ostringstream path;
            if (candidate_eid > 0)
                path << "/e" << candidate_eid << "/p" << pid << '/' << hash_name << ".spr";
            else
                path << "/char/p" << pid << '/' << hash_name << ".spr";
            url = "https://xyq.gsf.netease.com/avtres_full_dir" + path.str();
        } else {
            url = "https://xyq.gsf.netease.com/cloud_hash" + download_hash_path(hash);
        }
        download_add_candidate(state, base, url, seen);
    };

    if (policy == "hd_weapon") {
        ++logical_index;
        const std::string hash_name = download_direct_hash_hex(act, (int)dir);
        std::ostringstream path;
        if (eid > 0) path << "/e" << eid << "/p" << pid << '/' << hash_name << ".spr";
        else path << "/char/p" << pid << '/' << hash_name << ".spr";
        DownloadCandidate base = download_candidate_base(
            "spr", "hd_direct_weapon", source_mode, download_resource_key(
                pid, eid, (int)dir, act, ver, cache_jsoncmd, source_mode), pid, eid,
            (int)dir, act, ver, false, true, logical_index, 1);
        base.jsoncmd = jsoncmd;
        const std::string url = "https://xyq.gsf.netease.com/avtres_hd_full_dir" + path.str();
        download_add_candidate(state, base, url, seen);
        return download_finalize_candidates(state);
    }

    if (policy == "basic_weapon") {
        add_spr("semantic_hash", eid, (int)dir, act, ver, jsoncmd, cache_jsoncmd,
            false, true, source_mode, std::string(), false, false);
        add_spr("direct", eid, (int)dir, act, ver, jsoncmd, cache_jsoncmd,
            false, true, source_mode, std::string(), false, true);
        add_spr("cloudrender_full", eid, (int)dir, act, ver, jsoncmd, cache_jsoncmd,
            false, true, source_mode, "full", true, false);
        add_spr("cloudrender_bare", eid, (int)dir, act, ver, jsoncmd, cache_jsoncmd,
            true, false, source_mode, std::string(), true, false);
        return download_finalize_candidates(state);
    }

    if (policy == "part") {
        if (!warride) {
            add_spr("bodymapped_hash", eid, (int)dir, act, mapped_ver, mapped_jsoncmd,
                cache_jsoncmd, false, true, source_mode, std::string(), false, false);
            // 阶段3：cloudrender_full(restype=json) 实测会挂起且返回非 SPR，默认裁剪；
            // spec.skip_render_full=false 可恢复旧行为。
            if (!skip_render_full) {
                add_spr("cloudrender_full", eid, (int)dir, act, mapped_ver, mapped_jsoncmd,
                    cache_jsoncmd, false, true, source_mode, "full", true, false);
            }
            // 阶段3：cloudrender_full(restype=json) 实测会挂起且返回非 SPR，默认裁剪；
            // spec.skip_render_full=false 可恢复旧行为。
            if (!skip_render_full) {
                add_spr("cloudrender_full", eid, (int)dir, act, mapped_ver, mapped_jsoncmd,
                    cache_jsoncmd, false, true, source_mode, "full", true, false);
            }
            add_spr("semantic_hash", eid, (int)dir, act, original_ver, original_jsoncmd,
                cache_jsoncmd, false, true, source_mode, std::string(), false, false);
            if (allow_direct && original_ver.empty() && (original_jsoncmd.empty()
                || original_jsoncmd == "[]")) {
                add_spr("direct", eid, (int)dir, act, original_ver, original_jsoncmd,
                    cache_jsoncmd, false, true, source_mode, std::string(), false, true);
            }
            if (!fallback_act.empty()) {
                add_spr("fallback_bodymapped_hash", eid, (int)dir, fallback_act, fallback_ver,
                    fallback_jsoncmd, fallback_cache_jsoncmd.empty() ? cache_jsoncmd : fallback_cache_jsoncmd,
                    false, true, source_mode, std::string(), false, false);
                if (!skip_render_full) {
                    add_spr("fallback_cloudrender_full", eid, (int)dir, fallback_act, fallback_ver,
                        fallback_jsoncmd, fallback_cache_jsoncmd.empty() ? cache_jsoncmd : fallback_cache_jsoncmd,
                        false, true, source_mode, "full", true, false);
                }
                if (!skip_render_full) {
                    add_spr("fallback_cloudrender_full", eid, (int)dir, fallback_act, fallback_ver,
                        fallback_jsoncmd, fallback_cache_jsoncmd.empty() ? cache_jsoncmd : fallback_cache_jsoncmd,
                        false, true, source_mode, "full", true, false);
                }
                add_spr("fallback_semantic_hash", eid, (int)dir, fallback_act, fallback_ver,
                    fallback_jsoncmd, fallback_cache_jsoncmd.empty() ? cache_jsoncmd : fallback_cache_jsoncmd,
                    false, true, source_mode, std::string(), false, false);
                if (fallback_ver.empty() && (fallback_jsoncmd.empty() || fallback_jsoncmd == "[]")) {
                    add_spr("fallback_direct", eid, (int)dir, fallback_act, fallback_ver,
                        fallback_jsoncmd, fallback_cache_jsoncmd.empty() ? cache_jsoncmd : fallback_cache_jsoncmd,
                        false, true, source_mode, std::string(), false, true);
                }
            }
            if (!disable_render) {
                add_spr("cloudrender_bare", eid, (int)dir, act, original_ver, original_jsoncmd,
                    cache_jsoncmd, true, false, source_mode, std::string(), true, false);
                if (!fallback_act.empty()) {
                    add_spr("fallback_cloudrender_bare", eid, (int)dir, fallback_act, fallback_ver,
                        fallback_jsoncmd, fallback_cache_jsoncmd.empty() ? cache_jsoncmd : fallback_cache_jsoncmd,
                        true, false, source_mode, std::string(), true, false);
                }
            }
        } else {
            add_spr("bodymapped_hash", eid, (int)dir, act, mapped_ver, mapped_jsoncmd,
                cache_jsoncmd, false, true, source_mode, std::string(), false, false);
            add_spr("cloudrender_bare", eid, (int)dir, act, mapped_ver, mapped_jsoncmd,
                cache_jsoncmd, true, true, source_mode, std::string(), true, false);
            if (!skip_render_full) {
                add_spr("cloudrender_full", eid, (int)dir, act, mapped_ver, mapped_jsoncmd,
                    cache_jsoncmd, false, true, source_mode, "full", true, false);
            }
            if (!skip_render_full) {
                add_spr("cloudrender_full", eid, (int)dir, act, mapped_ver, mapped_jsoncmd,
                    cache_jsoncmd, false, true, source_mode, "full", true, false);
            }
        }
        return download_finalize_candidates(state);
    }

    // default policy: cloud_hash, optional body fallback, then cloudrender.
    ++logical_index;
    const std::string hash = download_spr_hash(pid, eid, (int)dir, act, ver, jsoncmd);
    DownloadCandidate base = download_candidate_base(
        "spr", "cloud_hash", source_mode, download_resource_key(
            pid, eid, (int)dir, act, ver, cache_jsoncmd, source_mode), pid, eid,
        (int)dir, act, ver, false, true, logical_index, logical_total);
    base.jsoncmd = jsoncmd;
    download_add_candidate(state, base, "https://xyq.gsf.netease.com/cloud_hash" + download_hash_path(hash), seen);
    if (is_body) {
        ++logical_index;
        const std::string body_hash = download_spr_hash(pid, 0, (int)dir, act, body_ver, body_jsoncmd);
        base = download_candidate_base(
            "spr", "body_cloud_hash", source_mode, download_resource_key(
                pid, 0, (int)dir, act, body_ver, body_jsoncmd, source_mode), pid, 0,
            (int)dir, act, body_ver, false, true, logical_index, logical_total);
        base.jsoncmd = body_jsoncmd;
        download_add_candidate(state, base, "https://xyq.gsf.netease.com/cloud_hash"
            + download_hash_path(body_hash), seen);
    }
    if (!disable_render) {
        ++logical_index;
        base = download_candidate_base(
            "spr", "cloudrender_full", source_mode, download_resource_key(
                pid, eid, (int)dir, act, ver, cache_jsoncmd, source_mode), pid, eid,
            (int)dir, act, ver, false, true, logical_index, logical_total);
        base.jsoncmd = jsoncmd;
        download_add_candidate(state, base, download_render_url(pid, eid, (int)dir, act,
            ver, jsoncmd, "h5sdk_hash" + download_hash_fragment(hash), true), seen);
        if (is_body) {
            ++logical_index;
            const std::string body_hash = download_spr_hash(pid, 0, (int)dir, act, body_ver, body_jsoncmd);
            base = download_candidate_base(
                "spr", "body_cloudrender_full", source_mode, download_resource_key(
                    pid, 0, (int)dir, act, body_ver, body_jsoncmd, source_mode), pid, 0,
                (int)dir, act, body_ver, false, true, logical_index, logical_total);
            base.jsoncmd = body_jsoncmd;
            download_add_candidate(state, base, download_render_url(pid, 0, (int)dir, act,
                body_ver, body_jsoncmd, "h5sdk_hash" + download_hash_fragment(body_hash), true), seen);
        }
    }
    return download_finalize_candidates(state);
}

static bool download_build_static_candidates(
    lua_State* L, int index, const std::shared_ptr<DownloadCoreState>& state,
    std::string& error)
{
    std::string type, ext, act, slot, name;
    int64_t shape = 0, id = 0, pid = 0, eid = 0, dir = 0, ref_idx = 0, layer_offset = 0;
    bool nested = false, palette_file = false;
    if (!download_read_string(L, index, "resource_type", type, 32, true)
        || !download_read_string(L, index, "ext", ext, 16, true)
        || !download_read_string(L, index, "act", act, 64)
        || !download_read_string(L, index, "slot", slot, 64)
        || !download_read_string(L, index, "name", name, 128)
        || !download_read_integer(L, index, "shape", shape, 0, 1000000)
        || !download_read_integer(L, index, "id", id, 0, 1000000000)
        || !download_read_integer(L, index, "pid", pid, 0, 1000000000)
        || !download_read_integer(L, index, "eid", eid, 0, 1000000000)
        || !download_read_integer(L, index, "dir", dir, 0, 7)
        || !download_read_integer(L, index, "ref_idx", ref_idx, 0, 1000000)
        || !download_read_integer(L, index, "layer_offset", layer_offset, 0, 1000000)
        || !download_read_boolean(L, index, "nested", nested)
        || !download_read_boolean(L, index, "palette_file", palette_file)) {
        error = "invalid static CDN spec";
        return false;
    }
    if (!download_valid_extension(ext)) { error = "invalid static extension"; return false; }
    if (!act.empty() && !download_valid_action(act)) { error = "invalid static action"; return false; }
    if (!slot.empty() && !download_valid_segment(slot, 64)) { error = "invalid static slot"; return false; }
    if (!name.empty() && !download_valid_segment(name, 128)) { error = "invalid static name"; return false; }

    std::string path;
    if (type == "palette") {
        std::string palette_kind;
        if (!download_read_string(L, index, "palette_kind", palette_kind, 16, true)
            || !download_valid_segment(palette_kind, 16)) {
            error = "invalid palette kind"; return false;
        }
        if (palette_kind == "hero") path = "/pal/hero/" + std::to_string(id) + "." + ext;
        else if (palette_kind == "hero_json") path = "/pal/hero/" + std::to_string(id) + ".json";
        else if (palette_kind == "equip") path = "/pal/equip/" + std::to_string(id) + "." + ext;
        else if (palette_kind == "equip_json") path = "/pal/equip/" + std::to_string(id) + ".json";
        else if (palette_kind == "weapon") path = "/pal/weapon/" + std::to_string(id) + "." + ext;
        else if (palette_kind == "weapon_json") path = "/pal/weapon/" + std::to_string(id) + ".json";
        else { error = "unsupported palette kind"; return false; }
    } else if (type == "ride_info") {
        if (shape <= 0) { error = "invalid ride shape"; return false; }
        std::ostringstream p; p << "/shape/char/" << std::setw(4) << std::setfill('0') << shape << "/00.json";
        path = p.str();
    } else if (type == "ride_body") {
        if (shape <= 0 || act.empty()) { error = "invalid ride body"; return false; }
        std::ostringstream p; p << "/shape/char/" << std::setw(4) << std::setfill('0') << shape
            << '/' << act;
        if (ext == "pal.bmp") p << '.' << ext;
        else p << "_d" << dir << '.' << ext;
        path = p.str();
    } else if (type == "ride_mask") {
        if (pid <= 0 || eid <= 0 || act.empty()) { error = "invalid ride mask"; return false; }
        path = "/e" + std::to_string(eid) + "/p" + std::to_string(pid) + '/' + act + "_d"
            + std::to_string(dir) + '.' + ext;
    } else if (type == "sockethd") {
        if (shape <= 0) { error = "invalid sockethd shape"; return false; }
        path = "/pal/sockethd/" + std::to_string(shape) + ".json";
    } else if (type == "shape_addon" || type == "race_deco") {
        if (shape <= 0 || slot.empty() || act.empty()) { error = "invalid shape addon"; return false; }
        std::ostringstream p; p << "/shape/char/" << std::setw(4) << std::setfill('0') << shape
            << '/' << slot << '/';
        if (nested) p << ref_idx << '/' << layer_offset << '/';
        p << act;
        if (ext == "pal.bmp") p << '.' << ext;
        else p << "_d" << dir << '.' << ext;
        path = p.str();
    } else if (type == "jy_addon" || type == "aura") {
        if (name.empty()) { error = "invalid jy addon name"; return false; }
        path = "/addon/jy/" + name;
        if (ext == "pal.bmp") path += ".pal.bmp";
        else if (palette_file) path += '.' + ext;
        else path += "_d0." + ext;
    } else if (type == "footprint") {
        if (id <= 0) { error = "invalid footprint id"; return false; }
        path = "/addon/footprint/" + std::to_string(id);
        if (ext == "pal.bmp") path += ".pal.bmp";
        else if (dir >= 0 && act != "palette") path += "_d" + std::to_string(dir) + '.' + ext;
        else path += '.' + ext;
    } else {
        error = "unsupported static resource type";
        return false;
    }

    std::string source_mode;
    if (!download_read_string(L, index, "source_mode", source_mode, 64)
        || !download_read_boolean(L, index, "mirror_prefer", state->mirror_prefer)
        || !download_read_boolean(L, index, "mirror_only", state->mirror_only)
        || !download_read_boolean(L, index, "allow_plain_http", state->allow_plain_http)) {
        || !download_read_boolean(L, index, "mirror_only", state->mirror_only)
        || !download_read_boolean(L, index, "allow_plain_http", state->allow_plain_http)) {
        error = "invalid static CDN routing fields";
        return false;
    }
    if (source_mode.empty()) source_mode = "static_v1";
    if (!download_valid_segment(source_mode, 64)) {
        error = "invalid static source_mode";
        return false;
    }
    std::string key_error;
    const std::string key = download_key_from_spec(L, index, key_error);
    if (key.empty()) {
        error = key_error.empty() ? "invalid static CDN cache key" : key_error;
        return false;
    }
    DownloadCandidate base = download_candidate_base(
        type, "static", source_mode, key, pid, eid, (int)dir, act, std::string(), false, true, 1, 1);
    const bool avtres = type == "ride_mask";
    std::set<std::string> seen;
    download_add_candidate(state, base, avtres ? download_join_avtres(path) : download_join_static(path), seen);
    return download_finalize_candidates(state);
}

static bool download_build_candidates(
    lua_State* L, int index, const std::shared_ptr<DownloadCoreState>& state,
    std::string& error)
{
    std::string type;
    if (!download_read_string(L, index, "resource_type", type, 32, true)) {
        error = "missing resource_type";
        return false;
    }
    if (type == "spr") return download_build_spr_candidates(L, index, state, error);
    return download_build_static_candidates(L, index, state, error);
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

static bool download_response_ok(const std::shared_ptr<DownloadCoreState>& state, int status_code)
{
    if (state && state->candidate_mode)
        return status_code == HTTP_STATUS_OK;
    return status_code >= 200 && status_code < 400;
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
        // 阶段2：每 worker 一个 HttpClient，跨任务复用 keep-alive 连接。
        hv::HttpClient client;
        DownloadReuseKey reuse;
        // 阶段2：每 worker 一个 HttpClient，跨任务复用 keep-alive 连接。
        hv::HttpClient client;
        DownloadReuseKey reuse;
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

            download_do_download(state, client, reuse);
            download_do_download(state, client, reuse);
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

static void download_do_single(std::shared_ptr<DownloadCoreState> state,
    hv::HttpClient& client, DownloadReuseKey& reuse)
static void download_do_single(std::shared_ptr<DownloadCoreState> state,
    hv::HttpClient& client, DownloadReuseKey& reuse)
{
    if (!state || state->cancelled.load()) {
        if (state)
            download_try_finish(state, GHV_DOWNLOAD_STATUS_CANCELLED);
        return;
    }

    // 阶段2：worker 本地连接复用——同 host 直接复用 keep-alive 连接，
    // 换 host 时先 close 旧连接。省掉每个小文件的 DNS+TCP+TLS 握手。
    {
        std::string host; int port = 0; bool is_https = false;
        download_split_url_host(state->url, host, port, is_https);
        if (!host.empty() && (host != reuse.host || port != reuse.port
            || is_https != reuse.https)) {
            client.close();
            reuse.host = host;
            reuse.port = port;
            reuse.https = is_https;
        }
    }

    // 阶段2：worker 本地连接复用——同 host 直接复用 keep-alive 连接，
    // 换 host 时先 close 旧连接。省掉每个小文件的 DNS+TCP+TLS 握手。
    {
        std::string host; int port = 0; bool is_https = false;
        download_split_url_host(state->url, host, port, is_https);
        if (!host.empty() && (host != reuse.host || port != reuse.port
            || is_https != reuse.https)) {
            client.close();
            reuse.host = host;
            reuse.port = port;
            reuse.https = is_https;
        }
    }

    HttpRequest req;
    req.method = HTTP_GET;
    req.url = state->url;
    req.timeout = state->timeout;
    // 阶段2：连接超时与总超时分离（默认 3 秒，环境变量可覆盖）。
    req.connect_timeout = download_connect_timeout_sec();
    // 阶段2：连接超时与总超时分离（默认 3 秒，环境变量可覆盖）。
    req.connect_timeout = download_connect_timeout_sec();

    if (!state->range.empty()) {
        req.headers["Range"] = "bytes=" + state->range;
    }

    HttpResponse resp;

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
                if (state->candidate_mode && state->total_size.load() > GHV_DOWNLOAD_CDN_MAX_BYTES)
                    write_failed = true;
            } else if (state_h == HP_BODY) {
                if (data && size > 0 && fp && !write_failed) {
                    if (state->candidate_mode
                        && (state->current_size.load() > GHV_DOWNLOAD_CDN_MAX_BYTES
                            || size > static_cast<size_t>(GHV_DOWNLOAD_CDN_MAX_BYTES
                                - state->current_size.load()))) {
                        write_failed = true;
                        return;
                    }
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
        } else if (write_failed || ret != 0 || !download_response_ok(state, resp.status_code)
            || (state->candidate_mode && state->current_size.load() <= 0)) {
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
        bool data_failed = false;
#ifndef GHV_NO_CRYPTO
        EVP_MD_CTX* digest = download_md5_create();
        bool digest_ok = digest != nullptr;
#endif
        resp.http_cb = [state
            , &data_failed
#ifndef GHV_NO_CRYPTO
            , digest, &digest_ok
#endif
        ](HttpMessage* msg, http_parser_state state_h, const char* data, size_t size) {
            if (state->cancelled.load()) return;
            if (state_h == HP_HEADERS_COMPLETE) {
                download_set_content_length(state, msg->GetHeader("Content-Length"));
                if (state->candidate_mode && state->total_size.load() > GHV_DOWNLOAD_CDN_MAX_BYTES) {
                    data_failed = true;
                } else if (state->total_size.load() > 0) {
                    std::lock_guard<std::mutex> lock(state->data_mutex);
                    try {
                        state->memory_data.reserve(static_cast<size_t>(state->total_size.load()));
                    } catch (...) {
                        data_failed = true;
                    }
                }
            } else if (state_h == HP_BODY) {
                if (data && size > 0 && !data_failed) {
                    std::lock_guard<std::mutex> lock(state->data_mutex);
                    if (state->candidate_mode
                        && (state->memory_data.size() > static_cast<size_t>(GHV_DOWNLOAD_CDN_MAX_BYTES)
                            || size > static_cast<size_t>(GHV_DOWNLOAD_CDN_MAX_BYTES)
                                - state->memory_data.size())) {
                        data_failed = true;
                        return;
                    }
                    try {
                        state->memory_data.append(data, size);
                    } catch (...) {
                        data_failed = true;
                        return;
                    }
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
        } else if (data_failed || ret != 0 || !download_response_ok(state, resp.status_code)) {
            download_clear_memory(state);
            download_clear_md5(state);
            download_try_finish(state, download_final_error(ret, resp.status_code));
        } else {
            if (state->memory_data.empty() && !resp.body.empty()) {
                std::lock_guard<std::mutex> lock(state->data_mutex);
                if (state->candidate_mode && resp.body.size() > static_cast<size_t>(GHV_DOWNLOAD_CDN_MAX_BYTES)) {
                    data_failed = true;
                } else try {
                    state->memory_data = resp.body;
                } catch (...) {
                    data_failed = true;
                }
                if (data_failed) {
                    state->memory_data.clear();
                } else {
                state->current_size = state->memory_data.size();
                state->total_size = state->memory_data.size();
#ifndef GHV_NO_CRYPTO
                if (digest_ok && EVP_DigestUpdate(digest, state->memory_data.data(), state->memory_data.size()) != 1)
                    digest_ok = false;
#endif
                }
            }
            if (state->candidate_mode && state->current_size.load() <= 0)
                data_failed = true;
            bool digest_published = !data_failed;
#ifndef GHV_NO_CRYPTO
            digest_published = !data_failed && digest_ok && download_md5_publish(digest, state);
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

static void download_do_download(std::shared_ptr<DownloadCoreState> state,
    hv::HttpClient& client, DownloadReuseKey& reuse)
static void download_do_download(std::shared_ptr<DownloadCoreState> state,
    hv::HttpClient& client, DownloadReuseKey& reuse)
{
    if (!state || !state->candidate_mode) {
        download_do_single(state, client, reuse);
        download_do_single(state, client, reuse);
        return;
    }
    int final_status = -1;
    for (const DownloadCandidate& candidate : state->candidates) {
        if (state->cancelled.load()) {
            state->status = GHV_DOWNLOAD_STATUS_CANCELLED;
            return;
        }
        download_reset_attempt(state);
        state->url = candidate.url;
        download_do_single(state, client, reuse);
        download_do_single(state, client, reuse);
        const int attempt_status = state->attempt_status.load();
        if (attempt_status == GHV_DOWNLOAD_STATUS_DONE) {
            bool publish = false;
            {
                std::lock_guard<std::mutex> lock(state->data_mutex);
                if (!state->cancelled.load()) {
                    state->result = candidate;
                    state->has_result = true;
                    publish = true;
                }
            }
            int expected = GHV_DOWNLOAD_STATUS_DOWNLOADING;
            if (publish && state->status.compare_exchange_strong(expected, GHV_DOWNLOAD_STATUS_DONE))
                return;
            {
                std::lock_guard<std::mutex> lock(state->data_mutex);
                state->has_result = false;
            }
            download_clear_memory(state);
            download_clear_md5(state);
            if (!state->filepath.empty()) download_remove_file(state->filepath);
            if (state->cancelled.load()) state->status = GHV_DOWNLOAD_STATUS_CANCELLED;
            return;
        }
        if (state->cancelled.load() || attempt_status == GHV_DOWNLOAD_STATUS_CANCELLED) {
            state->status = GHV_DOWNLOAD_STATUS_CANCELLED;
            return;
        }
        final_status = attempt_status;
    }
    state->status = final_status;
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

static int l_download_get_result(lua_State* L)
{
    LuaDownload* self = check_download(L);
    std::lock_guard<std::mutex> lock(self->core->data_mutex);
    if (!self->core->has_result) {
        lua_pushnil(L);
        return 1;
    }
    const DownloadCandidate& result = self->core->result;
    lua_createtable(L, 0, 15);
    lua_pushstring(L, result.kind.c_str()); lua_setfield(L, -2, "kind");
    lua_pushstring(L, result.resource_type.c_str()); lua_setfield(L, -2, "resource_type");
    lua_pushinteger(L, result.candidate_index); lua_setfield(L, -2, "candidate_index");
    lua_pushinteger(L, result.candidate_total); lua_setfield(L, -2, "candidate_total");
    lua_pushstring(L, result.source_mode.c_str()); lua_setfield(L, -2, "source_mode");
    lua_pushstring(L, result.cache_key.c_str()); lua_setfield(L, -2, "cache_key");
    lua_pushboolean(L, result.bare); lua_setfield(L, -2, "bare");
    lua_pushboolean(L, result.cacheable); lua_setfield(L, -2, "cacheable");
    lua_pushinteger(L, result.pid); lua_setfield(L, -2, "pid");
    lua_pushinteger(L, result.eid); lua_setfield(L, -2, "eid");
    lua_pushinteger(L, result.dir); lua_setfield(L, -2, "dir");
    lua_pushstring(L, result.act.c_str()); lua_setfield(L, -2, "act");
    lua_pushstring(L, result.ver.c_str()); lua_setfield(L, -2, "ver");
    lua_pushlstring(L, result.jsoncmd.data(), result.jsoncmd.size()); lua_setfield(L, -2, "jsoncmd");
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

static std::string download_key_from_spec(lua_State* L, int index, std::string& error)
{
    std::string type, key_mode;
    if (!download_read_string(L, index, "resource_type", type, 32, true)
        || !download_read_string(L, index, "key_mode", key_mode, 32)) {
        error = "invalid CDN key spec";
        return std::string();
    }
    if (type == "spr") {
        int64_t pid = 0, eid = 0, dir = 0;
        std::string act, ver, jsoncmd, source_mode, candidate_kind;
        if (!download_read_spec_number(L, index, "pid", pid, 0, 1000000000)
            || !download_read_spec_number(L, index, "eid", eid, 0, 1000000000)
            || !download_read_spec_number(L, index, "dir", dir, 0, 7)
            || !download_read_string(L, index, "act", act, 64, true)
            || !download_read_string(L, index, "ver", ver, 256)
            || !download_read_string(L, index, "jsoncmd", jsoncmd, 4096)
            || !download_read_string(L, index, "source_mode", source_mode, 64)
            || !download_read_string(L, index, "candidate_kind", candidate_kind, 64)
            || !download_valid_action(act)) {
            error = "invalid SPR key fields";
            return std::string();
        }
        if (!key_mode.empty() && key_mode != "hash" && key_mode != "resource"
            && key_mode != "warride") {
            error = "unsupported SPR key mode";
            return std::string();
        }
        if ((!source_mode.empty() && !download_valid_segment(source_mode, 64))
            || (!candidate_kind.empty() && !download_valid_segment(candidate_kind, 64))) {
            error = "invalid SPR key identity fields";
            return std::string();
        }
        if (key_mode == "hash") return download_spr_hash(pid, eid, (int)dir, act, ver, jsoncmd);
        if (key_mode == "warride") {
            if (source_mode.empty()) source_mode = "warride_weapon_exact_v3";
            if (!download_valid_segment(source_mode, 64)) {
                error = "invalid SPR key source_mode";
                return std::string();
            }
            return download_warride_key(
                pid, eid, (int)dir, act, ver, jsoncmd, candidate_kind, source_mode);
        }
        if (source_mode.empty()) source_mode = "cdn_composed_v1";
        return download_resource_key(pid, eid, (int)dir, act, ver, jsoncmd, source_mode);
    }
    if (type == "palette") {
        int64_t id = 0;
        std::string palette_kind;
        if (!download_read_string(L, index, "palette_kind", palette_kind, 32, true)
            || !download_valid_segment(palette_kind, 32)) {
            error = "invalid palette key fields"; return std::string();
        }
        const int64_t max_id = palette_kind == "weapon_v5"
            ? 1000001000000000LL : 1000000000LL;
        if (!download_read_spec_number(L, index, "id", id, 0, max_id)) {
            error = "invalid palette key fields"; return std::string();
        }
        return download_md5_hex("pal:" + palette_kind + ':' + std::to_string(id));
    }
    if (type == "addon" || type == "shape_addon" || type == "race_deco") {
        int64_t shape = 0, dir = 0, ref_idx = 0, layer_offset = 0;
        std::string slot, act, ext;
        bool nested = false;
        if (!download_read_spec_number(L, index, "shape", shape, 0, 1000000)
            || !download_read_spec_number(L, index, "dir", dir, 0, 7)
            || !download_read_string(L, index, "slot", slot, 64, true)
            || !download_read_string(L, index, "act", act, 64, true)
            || !download_read_string(L, index, "ext", ext, 16, true)
            || !download_read_integer(L, index, "ref_idx", ref_idx, 0, 1000000)
            || !download_read_integer(L, index, "layer_offset", layer_offset, 0, 1000000)
            || !download_read_boolean(L, index, "nested", nested)
            || !download_valid_segment(slot, 64) || !download_valid_action(act)
            || !download_valid_extension(ext)) {
            error = "invalid addon key fields"; return std::string();
        }
        return download_addon_key(shape, slot, act, (int)dir, ext, nested, ref_idx, layer_offset);
    }
    if (type == "ride_info" || type == "sockethd") {
        int64_t shape = 0;
        if (!download_read_spec_number(L, index, "shape", shape, 1, 1000000)) {
            error = "invalid ride info key fields"; return std::string();
        }
        if (type == "ride_info")
            return download_addon_key(shape, "_horse_body", "_color_info", 0, "json");
        return download_addon_key(shape, "_sockethd", "_sockethd", 0, "json");
    }
    if (type == "ride_body") {
        int64_t shape = 0, dir = 0;
        std::string act, ext;
        if (!download_read_spec_number(L, index, "shape", shape, 1, 1000000)
            || !download_read_spec_number(L, index, "dir", dir, 0, 7)
            || !download_read_string(L, index, "act", act, 64, true)
            || !download_read_string(L, index, "ext", ext, 16, true)
            || !download_valid_action(act) || !download_valid_extension(ext)) {
            error = "invalid ride body key fields"; return std::string();
        }
        return download_addon_key(shape, "_horse_body", act, (int)dir, ext);
    }
    if (type == "ride_mask") {
        int64_t pid = 0, eid = 0, dir = 0;
        std::string act, ext;
        if (!download_read_spec_number(L, index, "pid", pid, 1, 1000000000)
            || !download_read_spec_number(L, index, "eid", eid, 1, 1000000000)
            || !download_read_spec_number(L, index, "dir", dir, 0, 7)
            || !download_read_string(L, index, "act", act, 64, true)
            || !download_read_string(L, index, "ext", ext, 16, true)
            || !download_valid_action(act) || !download_valid_extension(ext)) {
            error = "invalid ride mask key fields"; return std::string();
        }
        return download_addon_key(pid, "_horse_mask_" + std::to_string(eid),
            act, (int)dir, ext);
    }
    if (type == "ride_palette" || type == "synth_palette") {
        int64_t shape = 0;
        std::string ride_desc, act, hs_key;
        bool skip = false;
        if (!download_read_spec_number(L, index, "shape", shape, 0, 1000000)
            || !download_read_string(L, index, "ride_desc", ride_desc, 256, true)
            || !download_read_string(L, index, "act", act, 64)
            || !download_read_string(L, index, "hs_key", hs_key, 256)
            || !download_read_boolean(L, index, "skip_palettes", skip)) {
            error = "invalid ride palette key fields"; return std::string();
        }
        if (type == "ride_palette")
            return download_md5_hex("ride_pal:" + std::to_string(shape) + ':' + ride_desc);
        std::ostringstream value;
        value << "synth_pal_v3:" << shape << ':' << ride_desc << ':' << act << ':'
            << (skip ? '1' : '0') << ':' << hs_key;
        return download_md5_hex(value.str());
    }
    if (type == "jy_addon" || type == "aura") {
        std::string name, ext;
        bool palette_file = false;
        if (!download_read_string(L, index, "name", name, 128, true)
            || !download_read_string(L, index, "ext", ext, 16, true)
            || !download_read_boolean(L, index, "palette_file", palette_file)
            || !download_valid_segment(name, 128) || !download_valid_extension(ext)) {
            error = "invalid aura key fields"; return std::string();
        }
        const char* kind = !palette_file ? "image" : (ext == "pal.bmp" ? "pal_bmp" : "pal_json");
        return download_md5_hex(std::string("halo_jy:") + kind + ':' + name + ':' + ext);
    }
    if (type == "footprint" || type == "footprint_key") {
        std::string kind, id, ext;
        int64_t dir = 0;
        if (type == "footprint_key") {
            if (!download_read_string(L, index, "kind", kind, 32, true)
                || !download_read_string(L, index, "id_text", id, 32, true)
                || !download_read_string(L, index, "ext", ext, 16, true)
                || !download_read_integer(L, index, "dir", dir, 0, 7)) {
                error = "invalid footprint key fields"; return std::string();
            }
        } else {
            int64_t id_value = 0;
            std::string act;
            bool palette_file = false;
            if (!download_read_spec_number(L, index, "id", id_value, 1, 1000000000)
                || !download_read_spec_number(L, index, "dir", dir, 0, 7)
                || !download_read_string(L, index, "act", act, 64)
                || !download_read_string(L, index, "ext", ext, 16, true)
                || !download_read_boolean(L, index, "palette_file", palette_file)
                || !download_valid_extension(ext)) {
                error = "invalid footprint request key fields"; return std::string();
            }
            id = std::to_string(id_value);
            if (act == "palette") {
                kind = ext == "pal.bmp" ? "pal_bmp" : "pal_json";
            } else {
                kind = "image";
            }
        }
        if (!download_valid_segment(kind, 32) || !download_valid_segment(id, 32)
            || !download_valid_extension(ext)) {
            error = "invalid footprint key fields"; return std::string();
        }
        if (type == "footprint" && kind == "image")
            return download_md5_hex("footprint:image:" + id + ":d" + std::to_string(dir) + ':' + ext);
        if (key_mode == "image")
            return download_md5_hex("footprint:image:" + id + ":d" + std::to_string(dir) + ':' + ext);
        return download_md5_hex("footprint:" + kind + ':' + id + ':' + ext);
    }
    error = "unsupported CDN key type";
    return std::string();
}

static int l_download_cdn_key(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    std::string error;
    const std::string key = download_key_from_spec(L, 1, error);
    if (key.empty()) return luaL_error(L, "%s", error.c_str());
    lua_pushlstring(L, key.data(), key.size());
    return 1;
}

static int l_download_new(lua_State* L) {
    const int first = lua_istable(L, 1) ? 2 : 1;
    const char* url = luaL_checkstring(L, first);
    const char* filepath = luaL_optstring(L, first + 1, NULL);
    const char* range = luaL_optstring(L, first + 2, NULL);
    int timeout = (int)luaL_optinteger(L, first + 3, 60);

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

static int l_download_cdn(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    const char* filepath = luaL_optstring(L, 2, NULL);
    const int timeout = (int)luaL_optinteger(L, 3, 60);
    if (timeout <= 0 || timeout > 600)
        return luaL_error(L, "invalid CDN timeout");

    LuaDownload* self = new LuaDownload();
    self->core->candidate_mode = true;
    self->core->attempt_status = GHV_DOWNLOAD_STATUS_DOWNLOADING;
    if (filepath) self->core->filepath = filepath;
    self->core->timeout = timeout;
    std::string error;
    if (!download_build_candidates(L, 1, self->core, error) || self->core->candidates.empty()) {
        delete self;
        return luaL_error(L, "%s", error.empty() ? "empty CDN candidates" : error.c_str());
    }

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
        {"GetResult",l_download_get_result},
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

    lua_createtable(L, 0, 4);
    lua_pushcfunction(L, l_download_cdn);
    lua_setfield(L, -2, "CDN");
    lua_pushcfunction(L, l_download_cdn_key);
    lua_setfield(L, -2, "CDNKey");
    lua_pushcfunction(L, l_download_stats);
    lua_setfield(L, -2, "Stats");
    lua_newtable(L);
    lua_pushcfunction(L, l_download_new);
    lua_setfield(L, -2, "__call");
    lua_setmetatable(L, -2);
    return 1;
}
