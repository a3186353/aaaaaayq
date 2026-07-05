/*
 * resource.c - native resource hot-path queue facade.
 *
 * Lua decides policy and passes a normalized request table. This module owns
 * token allocation, priority queues, cancel state, poll events, download
 * dispatch and stats. HTTP transfer is performed by the existing native
 * ghv.download binding; Lua no longer maintains the hot download queue.
 */
#include "lua_proxy.h"
#include "tcp.h"
#include "jy.h"
#include "wpk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LUA_NOREF
#define LUA_NOREF (-2)
#endif

#if defined(_WIN32)
#define MYGXY_API __declspec(dllexport)
#else
#define MYGXY_API LUAMOD_API
#endif

#define RESOURCE_KEY_ID "\xe8\xb5\x84\xe6\xba\x90ID"
#define RESOURCE_KEY_TYPE "\xe7\xb1\xbb\xe5\x9e\x8b"
#define RESOURCE_KEY_SCENE "\xe5\x9c\xba\xe6\x99\xaf"
#define RESOURCE_KEY_PRIORITY "\xe4\xbc\x98\xe5\x85\x88\xe7\xba\xa7"
#define RESOURCE_KEY_QUEUE_PRIORITY "\xe9\x98\x9f\xe5\x88\x97\xe4\xbc\x98\xe5\x85\x88\xe7\xba\xa7"
#define RESOURCE_KEY_ALLOW_COLD "\xe5\x85\x81\xe8\xae\xb8\xe5\x86\xb7\xe4\xb8\x8b\xe8\xbd\xbd"
#define RESOURCE_KEY_STATUS "\xe7\x8a\xb6\xe6\x80\x81"
#define RESOURCE_QUEUE_LOW "\xe4\xbd\x8e"

#define RESOURCE_KEY_SAVE_PATH "\xe4\xbf\x9d\xe5\xad\x98\xe8\xb7\xaf\xe5\xbe\x84"
#define RESOURCE_KEY_FILE_PATH "\xe6\x96\x87\xe4\xbb\xb6\xe8\xb7\xaf\xe5\xbe\x84"

#define RESOURCE_DEFAULT_MAX_ACTIVE 6
#define RESOURCE_DEFAULT_LOW_ACTIVE 2
#define RESOURCE_DEFAULT_TIMEOUT 10
#define RESOURCE_DEFAULT_CALLBACK_BUDGET 4
#define RESOURCE_PREHEAT_AGING_TICKS 40
#define RESOURCE_DEFAULT_NATIVE_RETRIES 600
#define RESOURCE_WORKER_THREADS 3
#define RESOURCE_RESULT_TTL_TICKS 600

typedef enum ResourceNativeKind
{
    RESOURCE_NATIVE_NONE = 0,
    RESOURCE_NATIVE_TCP = 1,
    RESOURCE_NATIVE_JY = 2,
    RESOURCE_NATIVE_WPK = 3
} ResourceNativeKind;

typedef enum ResourcePriority
{
    RESOURCE_PRIORITY_PREHEAT = 0,
    RESOURCE_PRIORITY_NORMAL = 1,
    RESOURCE_PRIORITY_HIGH = 2,
    RESOURCE_PRIORITY_CRITICAL = 3
} ResourcePriority;

typedef enum ResourceStatus
{
    RESOURCE_STATUS_QUEUED = 0,
    RESOURCE_STATUS_READY = 1,
    RESOURCE_STATUS_FAILED = 2,
    RESOURCE_STATUS_DEGRADED = 3,
    RESOURCE_STATUS_CANCELLED = 4
} ResourceStatus;

typedef struct ResourceToken
{
    unsigned int id;
    ResourcePriority priority;
    ResourceStatus status;
    int allow_cold_download;
    int cancelled;
    int download_ref;
    int timeout;
    unsigned int current_url;
    unsigned int url_count;
    unsigned int hit_url;
    int callbacks_done;
    char** urls;
    char* save_path;
    char* dedupe_key;
    char* expected_md5;
    char* resource_id;
    char* resource_type;
    char* scene;
    char* scope;
    char* domain;
    char* degrade;
    lua_Integer generation;
    int has_generation;
    Uint32 frame;
    int has_frame;
    Uint32 shell_group;
    Uint32 shell_frame;
    unsigned int created_tick;
    unsigned int terminal_tick;
    unsigned int event_pending;
    unsigned int retry_tick;
    unsigned int submit_attempts;
    unsigned int queue_full_count;
    unsigned int max_tries;
    ResourceNativeKind native_kind;
    void* native_ud;
    WPK_UserData* wpk_ud;
    Uint32 wpk_id;
    int has_wpk_id;
    int wpk_ref;
    int shell_started;
    int worker_started;
    int native_ref;
    int result_ready;
    int result_success;
    char* result_data;
    size_t result_data_len;
    void* result_native;
    char* result_error;
    struct ResourceCallback* callbacks;
    struct ResourceToken* next;
} ResourceToken;

typedef struct ResourceCallback
{
    int ref;
    struct ResourceCallback* next;
} ResourceCallback;

typedef struct ResourceEvent
{
    unsigned int token_id;
    ResourceToken* token;
    ResourceStatus status;
    char* resource_id;
    char* message;
    struct ResourceEvent* next;
} ResourceEvent;

typedef struct ResourceWorkerJob
{
    ResourceToken* token;
    ResourcePriority priority;
    unsigned int token_id;
    WPK_UserData* wpk_ud;
    Uint32 wpk_id;
    Uint32 shell_group;
    Uint32 shell_frame;
    TCP_UserData* tcp_ud;
    Uint32 frame_id;
    Uint32* pal_snapshot;
    Uint32 pal_count;
    Uint32 pal_version;
    int is_tcp_shell;
    int is_tcp_frame;
    int warm_requested;
    int warm_success;
    int result_ready;
    int result_success;
    TCP_UserData* result_tcp;
    TCP_NativeFrameData result_frame;
    char result_error[160];
    struct ResourceWorkerJob* next;
} ResourceWorkerJob;

typedef struct ResourceState
{
    unsigned int next_token_id;
    unsigned int max_active;
    unsigned int low_active_max;
    unsigned int created;
    unsigned int preload;
    unsigned int cancelled;
    unsigned int queued_high;
    unsigned int queued_low;
    unsigned int ready;
    unsigned int failed;
    unsigned int degraded;
    unsigned int shell_degraded;
    unsigned int callback_budget;
    unsigned int callback_dispatched;
    unsigned int render_submitted;
    unsigned int render_cancelled;
    unsigned int native_ready;
    unsigned int native_failed;
    unsigned int native_queue_full;
    unsigned int native_retried;
    unsigned int shell_submitted;
    unsigned int shell_ready;
    unsigned int shell_failed;
    unsigned int shell_warm_ready;
    unsigned int shell_warm_failed;
    unsigned int frame_submitted;
    unsigned int frame_ready;
    unsigned int frame_failed;
    unsigned int tokens_freed;
    unsigned int worker_active;
    unsigned int worker_thread_count;
    unsigned int worker_poll_tick;
    unsigned int poll_tick;
    unsigned int native_cursor_id;
    ResourceToken* tokens;
    ResourceEvent* events_head;
    ResourceEvent* events_tail;
    SDL_mutex* worker_mutex;
    SDL_cond* worker_cond;
    SDL_Thread* worker_threads[RESOURCE_WORKER_THREADS];
    int worker_stop;
    ResourceWorkerJob* worker_head;
    ResourceWorkerJob* worker_tail;
    ResourceWorkerJob* worker_done_head;
    ResourceWorkerJob* worker_done_tail;
} ResourceState;

static ResourceState g_resource = {0};

static int resource_is_tcp_shell(const ResourceToken* token);
static int resource_submit_native_producer(ResourceToken* token);
static void resource_worker_promote_token(ResourceToken* token);

static char* resource_strdup(const char* s)
{
    size_t n;
    char* out = NULL;
    if (!s)
        return NULL;
    n = strlen(s) + 1;
    out = (char*)malloc(n);
    if (!out)
        return NULL;
    memcpy(out, s, n);
    return out;
}

static const char* resource_status_name(ResourceStatus status)
{
    switch (status)
    {
    case RESOURCE_STATUS_READY: return "ready";
    case RESOURCE_STATUS_FAILED: return "failed";
    case RESOURCE_STATUS_DEGRADED: return "degraded";
    case RESOURCE_STATUS_CANCELLED: return "cancelled";
    case RESOURCE_STATUS_QUEUED:
    default:
        return "queued";
    }
}

static const char* resource_priority_name(ResourcePriority priority)
{
    switch (priority)
    {
    case RESOURCE_PRIORITY_CRITICAL: return "critical";
    case RESOURCE_PRIORITY_HIGH: return "high";
    case RESOURCE_PRIORITY_PREHEAT: return "preheat";
    case RESOURCE_PRIORITY_NORMAL:
    default:
        return "normal";
    }
}

static int resource_is_shell_type(const char* type)
{
    if (!type)
        return 0;
    return strcmp(type, "tcp_shell") == 0 || strcmp(type, "jy_shell") == 0;
}

static ResourcePriority resource_parse_priority(const char* s, const char* queue_priority)
{
    if (s)
    {
        if (strcmp(s, "critical") == 0) return RESOURCE_PRIORITY_CRITICAL;
        if (strcmp(s, "high") == 0) return RESOURCE_PRIORITY_HIGH;
        if (strcmp(s, "normal") == 0) return RESOURCE_PRIORITY_NORMAL;
        if (strcmp(s, "preheat") == 0 || strcmp(s, "low") == 0) return RESOURCE_PRIORITY_PREHEAT;
    }
    if (queue_priority && strcmp(queue_priority, RESOURCE_QUEUE_LOW) == 0)
        return RESOURCE_PRIORITY_PREHEAT;
    return RESOURCE_PRIORITY_NORMAL;
}

static int resource_is_terminal(ResourceStatus status)
{
    return status == RESOURCE_STATUS_READY
        || status == RESOURCE_STATUS_FAILED
        || status == RESOURCE_STATUS_DEGRADED
        || status == RESOURCE_STATUS_CANCELLED;
}

static void resource_unref_download(lua_State* L, ResourceToken* token)
{
    if (!token || token->download_ref == LUA_NOREF)
        return;
    luaL_unref(L, LUA_REGISTRYINDEX, token->download_ref);
    token->download_ref = LUA_NOREF;
}

static void resource_unref_callbacks(lua_State* L, ResourceToken* token)
{
    ResourceCallback* cb;
    ResourceCallback* next;
    if (!token)
        return;
    cb = token->callbacks;
    while (cb)
    {
        next = cb->next;
        if (cb->ref != LUA_NOREF)
            luaL_unref(L, LUA_REGISTRYINDEX, cb->ref);
        free(cb);
        cb = next;
    }
    token->callbacks = NULL;
}

static void resource_clear_result(ResourceToken* token)
{
    if (!token)
        return;
    free(token->result_data);
    free(token->result_error);
    if (token->result_native && token->resource_type && strcmp(token->resource_type, "tcp_shell") == 0)
        TCP_NativeFree((TCP_UserData*)token->result_native);
    token->result_data = NULL;
    token->result_data_len = 0;
    token->result_native = NULL;
    token->result_error = NULL;
    token->result_ready = 0;
    token->result_success = 0;
}

static void resource_free_urls(char** urls, unsigned int count)
{
    unsigned int i;
    if (!urls)
        return;
    for (i = 0; i < count; i++)
        free(urls[i]);
    free(urls);
}

static void resource_free_token(lua_State* L, ResourceToken* token)
{
    if (!token)
        return;
    resource_unref_download(L, token);
    resource_unref_callbacks(L, token);
    resource_clear_result(token);
    resource_free_urls(token->urls, token->url_count);
    free(token->save_path);
    free(token->dedupe_key);
    free(token->expected_md5);
    free(token->resource_id);
    free(token->resource_type);
    free(token->scene);
    free(token->scope);
    free(token->domain);
    free(token->degrade);
    if (token->native_ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, token->native_ref);
    if (token->wpk_ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, token->wpk_ref);
    free(token);
}

static int resource_token_can_reap(const ResourceToken* token)
{
    if (!token || !resource_is_terminal(token->status))
        return 0;
    if (token->download_ref != LUA_NOREF)
        return 0;
    if (token->worker_started)
        return 0;
    if (token->callbacks && !token->callbacks_done)
        return 0;
    if (token->event_pending)
        return 0;
    if (token->result_native)
    {
        if (token->terminal_tick
            && g_resource.poll_tick > token->terminal_tick
            && g_resource.poll_tick - token->terminal_tick >= RESOURCE_RESULT_TTL_TICKS)
            return 1;
        return 0;
    }
    return 1;
}

static unsigned int resource_reap_finished(lua_State* L)
{
    ResourceToken* token = g_resource.tokens;
    ResourceToken* prev = NULL;
    unsigned int count = 0;
    while (token)
    {
        ResourceToken* next = token->next;
        if (resource_token_can_reap(token))
        {
            if (prev)
                prev->next = next;
            else
                g_resource.tokens = next;
            token->next = NULL;
            resource_free_token(L, token);
            g_resource.tokens_freed++;
            count++;
        }
        else
        {
            prev = token;
        }
        token = next;
    }
    return count;
}

static ResourceToken* resource_find_token_ptr(unsigned int id)
{
    ResourceToken* token = g_resource.tokens;
    while (token)
    {
        if (token->id == id)
            return token;
        token = token->next;
    }
    return NULL;
}

static int resource_push_event(ResourceToken* token, ResourceStatus status, const char* resource_id, const char* message)
{
    ResourceEvent* ev = (ResourceEvent*)calloc(1, sizeof(ResourceEvent));
    if (!ev)
        return 0;
    ev->token_id = token ? token->id : 0;
    ev->token = token;
    ev->status = status;
    ev->resource_id = resource_strdup(resource_id);
    ev->message = resource_strdup(message);
    if (g_resource.events_tail)
        g_resource.events_tail->next = ev;
    else
        g_resource.events_head = ev;
    g_resource.events_tail = ev;
    return 1;
}

static unsigned int resource_drop_events(unsigned int token_id)
{
    ResourceEvent* ev = g_resource.events_head;
    ResourceEvent* prev = NULL;
    unsigned int count = 0;
    while (ev)
    {
        ResourceEvent* next = ev->next;
        if (token_id == 0 || ev->token_id == token_id)
        {
            ResourceToken* token = ev->token ? ev->token : resource_find_token_ptr(ev->token_id);
            if (token)
                token->event_pending = 0;
            if (prev)
                prev->next = next;
            else
                g_resource.events_head = next;
            if (g_resource.events_tail == ev)
                g_resource.events_tail = prev;
            free(ev->resource_id);
            free(ev->message);
            free(ev);
            count++;
        }
        else
        {
            prev = ev;
        }
        ev = next;
    }
    return count;
}

static unsigned int resource_drop_update_events(void)
{
    ResourceEvent* ev = g_resource.events_head;
    ResourceEvent* prev = NULL;
    unsigned int count = 0;
    while (ev)
    {
        ResourceEvent* next = ev->next;
        ResourceToken* token = ev->token ? ev->token : resource_find_token_ptr(ev->token_id);
        int drop = 0;
        if (!token)
        {
            drop = 1;
        }
        else if (token->callbacks_done)
        {
            drop = 1;
        }
        else if (!token->callbacks && token->terminal_tick
            && g_resource.poll_tick > token->terminal_tick
            && g_resource.poll_tick - token->terminal_tick >= RESOURCE_RESULT_TTL_TICKS)
        {
            drop = 1;
        }
        if (drop)
        {
            if (token)
                token->event_pending = 0;
            if (prev)
                prev->next = next;
            else
                g_resource.events_head = next;
            if (g_resource.events_tail == ev)
                g_resource.events_tail = prev;
            free(ev->resource_id);
            free(ev->message);
            free(ev);
            count++;
        }
        else
        {
            prev = ev;
        }
        ev = next;
    }
    return count;
}

static void resource_set_status(ResourceToken* token, ResourceStatus status, const char* message)
{
    ResourceStatus old_status;
    int event_queued;
    if (!token)
        return;
    old_status = token->status;
    if (old_status == status)
        return;
    token->status = status;
    if (old_status == RESOURCE_STATUS_QUEUED)
    {
        if (token->priority == RESOURCE_PRIORITY_PREHEAT)
        {
            if (g_resource.queued_low > 0) g_resource.queued_low--;
        }
        else
        {
            if (g_resource.queued_high > 0) g_resource.queued_high--;
        }
    }
    if (status == RESOURCE_STATUS_READY) g_resource.ready++;
    else if (status == RESOURCE_STATUS_FAILED) g_resource.failed++;
    else if (status == RESOURCE_STATUS_DEGRADED) g_resource.degraded++;
    event_queued = resource_push_event(token, status, token->resource_id, message);
    if (resource_is_terminal(status))
    {
        token->terminal_tick = g_resource.poll_tick;
        token->event_pending = event_queued ? 1u : 0u;
    }
}

static void resource_promote_priority(ResourceToken* token, ResourcePriority priority)
{
    ResourcePriority old_priority;
    if (!token || priority <= token->priority)
        return;
    old_priority = token->priority;
    token->priority = priority;
    if (token->status == RESOURCE_STATUS_QUEUED
        && old_priority == RESOURCE_PRIORITY_PREHEAT
        && priority != RESOURCE_PRIORITY_PREHEAT)
    {
        if (g_resource.queued_low > 0)
            g_resource.queued_low--;
        g_resource.queued_high++;
    }
    resource_worker_promote_token(token);
}

static ResourcePriority resource_effective_priority(const ResourceToken* token)
{
    if (!token)
        return RESOURCE_PRIORITY_NORMAL;
    if (token->priority == RESOURCE_PRIORITY_PREHEAT
        && g_resource.poll_tick > token->created_tick
        && g_resource.poll_tick - token->created_tick >= RESOURCE_PREHEAT_AGING_TICKS)
        return RESOURCE_PRIORITY_NORMAL;
    return token->priority;
}

static ResourcePriority resource_effective_priority_at(const ResourceToken* token, unsigned int tick)
{
    if (!token)
        return RESOURCE_PRIORITY_NORMAL;
    if (token->priority == RESOURCE_PRIORITY_PREHEAT
        && tick > token->created_tick
        && tick - token->created_tick >= RESOURCE_PREHEAT_AGING_TICKS)
        return RESOURCE_PRIORITY_NORMAL;
    return token->priority;
}

static unsigned int resource_max_active(void)
{
    return g_resource.max_active ? g_resource.max_active : RESOURCE_DEFAULT_MAX_ACTIVE;
}

static unsigned int resource_low_active_max(void)
{
    return g_resource.low_active_max ? g_resource.low_active_max : RESOURCE_DEFAULT_LOW_ACTIVE;
}

static int resource_call_download_method(lua_State* L, ResourceToken* token, const char* method, int nargs, int nret)
{
    int base;
    if (!token || token->download_ref == LUA_NOREF)
        return 0;
    base = lua_gettop(L) - nargs;
    lua_rawgeti(L, LUA_REGISTRYINDEX, token->download_ref);
    lua_getfield(L, -1, method);
    if (!lua_isfunction(L, -1))
    {
        lua_pop(L, 2);
        return 0;
    }
    lua_insert(L, base + 1);
    lua_insert(L, base + 2);
    if (lua_pcall(L, nargs + 1, nret, 0) != LUA_OK)
    {
        fprintf(stderr, "[resource] download method %s failed: %s\n",
            method, lua_tostring(L, -1));
        lua_pop(L, 1);
        return 0;
    }
    return 1;
}

static void resource_cancel_download(lua_State* L, ResourceToken* token)
{
    if (!token || token->download_ref == LUA_NOREF)
        return;
    resource_call_download_method(L, token, "Cancel", 0, 0);
    resource_unref_download(L, token);
}

static void resource_finish_failed(lua_State* L, ResourceToken* token, const char* message);

static void resource_dispatch_callbacks(lua_State* L, ResourceToken* token,
    int success, const char* data, size_t data_len, const char* err)
{
    ResourceCallback* cb;
    const char* hit_url = "";
    if (!token || token->callbacks_done)
        return;
    token->callbacks_done = 1;
    if (token->hit_url > 0 && token->hit_url <= token->url_count && token->urls)
        hit_url = token->urls[token->hit_url - 1] ? token->urls[token->hit_url - 1] : "";

    cb = token->callbacks;
    while (cb)
    {
        if (cb->ref != LUA_NOREF)
        {
            lua_rawgeti(L, LUA_REGISTRYINDEX, cb->ref);
            if (success)
            {
                if (token->save_path)
                    lua_pushboolean(L, 1);
                else
                    lua_pushlstring(L, data ? data : "", data ? data_len : 0);
                lua_pushnil(L);
            }
            else
            {
                if (token->save_path)
                    lua_pushboolean(L, 0);
                else
                    lua_pushnil(L);
                lua_pushstring(L, err ? err : "download failed");
            }
            lua_pushstring(L, hit_url);
            lua_pushinteger(L, (lua_Integer)token->hit_url);
            lua_pushinteger(L, (lua_Integer)token->url_count);
            if (lua_pcall(L, 5, 0, 0) != LUA_OK)
            {
                fprintf(stderr, "[resource] callback failed: %s\n", lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }
        cb = cb->next;
    }
    resource_unref_callbacks(L, token);
}

static int resource_store_success(ResourceToken* token, const char* data, size_t data_len)
{
    if (!token)
        return 0;
    resource_clear_result(token);
    token->result_success = 1;
    token->result_ready = 1;
    if (data && data_len > 0)
    {
        token->result_data = (char*)malloc(data_len);
        if (!token->result_data)
        {
            token->result_ready = 0;
            token->result_success = 0;
            return 0;
        }
        memcpy(token->result_data, data, data_len);
        token->result_data_len = data_len;
    }
    return 1;
}

static int resource_store_failure(ResourceToken* token, const char* message)
{
    if (!token)
        return 0;
    resource_clear_result(token);
    token->result_success = 0;
    token->result_error = resource_strdup(message ? message : "download failed");
    token->result_ready = 1;
    return token->result_error != NULL;
}

static int resource_store_native_success(ResourceToken* token, void* native)
{
    if (!token || !native)
        return 0;
    resource_clear_result(token);
    token->result_success = 1;
    token->result_ready = 1;
    token->result_native = native;
    return 1;
}

static unsigned int resource_callback_budget(void)
{
    return g_resource.callback_budget ? g_resource.callback_budget : RESOURCE_DEFAULT_CALLBACK_BUDGET;
}

static unsigned int resource_dispatch_pending_callbacks(lua_State* L)
{
    ResourceToken* p = g_resource.tokens;
    unsigned int dispatched = 0;
    unsigned int budget = resource_callback_budget();
    while (p && dispatched < budget)
    {
        ResourceToken* next = p->next;
        if (!p->callbacks_done && p->callbacks && p->result_ready && resource_is_terminal(p->status))
        {
            resource_dispatch_callbacks(L, p, p->result_success,
                p->result_data, p->result_data_len, p->result_error);
            if (!(resource_is_tcp_shell(p) && p->result_native))
                resource_clear_result(p);
            dispatched++;
        }
        p = next;
    }
    g_resource.callback_dispatched += dispatched;
    return dispatched;
}

static int resource_start_download(lua_State* L, ResourceToken* token)
{
    const char* url;
    if (!token || token->cancelled || token->status != RESOURCE_STATUS_QUEUED)
        return 0;
    if (token->download_ref != LUA_NOREF)
        return 1;
    if (!token->allow_cold_download)
        return 0;
    if (!token->urls || token->url_count == 0)
        return 0;
    if (token->current_url >= token->url_count)
        return 0;

    token->current_url++;
    url = token->urls[token->current_url - 1];

    lua_getglobal(L, "require");
    lua_pushstring(L, "ghv.download");
    if (lua_pcall(L, 1, 1, 0) != LUA_OK)
    {
        fprintf(stderr, "[resource] require ghv.download failed: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return 0;
    }
    if (!lua_isfunction(L, -1))
    {
        lua_pop(L, 1);
        return 0;
    }
    lua_pushstring(L, url);
    if (token->save_path)
        lua_pushstring(L, token->save_path);
    else
        lua_pushnil(L);
    lua_pushnil(L);
    lua_pushinteger(L, (lua_Integer)token->timeout);
    if (lua_pcall(L, 4, 1, 0) != LUA_OK)
    {
        fprintf(stderr, "[resource] start download failed: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return 0;
    }
    token->download_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return 1;
}

static void resource_finish_success(lua_State* L, ResourceToken* token)
{
    const char* data = NULL;
    size_t len = 0;
    int data_idx = 0;
    if (!token)
        return;
    token->hit_url = token->current_url;
    if (token->expected_md5 && token->expected_md5[0])
    {
        const char* got = NULL;
        int md5_idx = 0;
        int matched = 0;
        if (resource_call_download_method(L, token, "GetMD5", 0, 1))
        {
            md5_idx = lua_gettop(L);
            got = lua_tostring(L, md5_idx);
            matched = got && strcmp(got, token->expected_md5) == 0;
        }
        if (md5_idx)
            lua_pop(L, 1);
        if (!matched)
        {
            resource_unref_download(L, token);
            if (token->current_url < token->url_count)
            {
                if (!resource_start_download(L, token))
                    resource_finish_failed(L, token, "download start failed");
            }
            else
            {
                resource_finish_failed(L, token, got ? "md5 mismatch" : "md5 unavailable");
            }
            return;
        }
    }
    if (!token->save_path)
    {
        if (resource_call_download_method(L, token, "GetData", 0, 1))
        {
            data_idx = lua_gettop(L);
            data = lua_tolstring(L, data_idx, &len);
        }
    }
    resource_unref_download(L, token);
    if (!resource_store_success(token, data, len))
    {
        if (data_idx)
            lua_pop(L, 1);
        resource_finish_failed(L, token, "out of memory");
        return;
    }
    resource_set_status(token, RESOURCE_STATUS_READY, "ready");
    if (data_idx)
        lua_pop(L, 1);
}

static void resource_finish_failed(lua_State* L, ResourceToken* token, const char* message)
{
    if (!token)
        return;
    resource_unref_download(L, token);
    resource_store_failure(token, message ? message : "download failed");
    resource_set_status(token, RESOURCE_STATUS_FAILED, message ? message : "download failed");
}

static void resource_check_active_download(lua_State* L, ResourceToken* token)
{
    int top;
    lua_Integer status = 0;
    if (!token || token->download_ref == LUA_NOREF || token->status != RESOURCE_STATUS_QUEUED)
        return;
    if (!resource_call_download_method(L, token, "GetState", 0, 3))
    {
        resource_unref_download(L, token);
        resource_finish_failed(L, token, "download state failed");
        return;
    }
    top = lua_gettop(L);
    status = lua_tointeger(L, top);
    lua_pop(L, 3);
    if (status == 100)
    {
        resource_finish_success(L, token);
    }
    else if (status < 0)
    {
        char error_buf[64];
        resource_unref_download(L, token);
        if (token->current_url < token->url_count)
        {
            if (!resource_start_download(L, token))
                resource_finish_failed(L, token, "download start failed");
        }
        else
        {
            if (status <= -100 && status >= -599)
                snprintf(error_buf, sizeof(error_buf), "http %d", (int)(-status));
            else
                snprintf(error_buf, sizeof(error_buf), "download error %d", (int)status);
            resource_finish_failed(L, token, error_buf);
        }
    }
}

static ResourceToken* resource_pick_queued(ResourcePriority want_preheat)
{
    ResourceToken* p = g_resource.tokens;
    ResourceToken* best = NULL;
    while (p)
    {
        if (!p->cancelled && p->status == RESOURCE_STATUS_QUEUED
            && p->download_ref == LUA_NOREF && p->allow_cold_download
            && p->urls && p->url_count > 0)
        {
            ResourcePriority eff = resource_effective_priority(p);
            if ((want_preheat == RESOURCE_PRIORITY_PREHEAT) == (eff == RESOURCE_PRIORITY_PREHEAT))
            {
                ResourcePriority best_eff = resource_effective_priority(best);
                if (!best || eff > best_eff
                    || (eff == best_eff && p->id < best->id))
                    best = p;
            }
        }
        p = p->next;
    }
    return best;
}

static int resource_native_is_frame_ready(ResourceToken* token)
{
    if (!token || !token->native_ud || !token->has_frame)
        return 0;
    if (token->native_kind == RESOURCE_NATIVE_TCP)
        return TCP_NativeIsFrameDecoded((TCP_UserData*)token->native_ud, token->frame);
    if (token->native_kind == RESOURCE_NATIVE_JY)
        return JY_NativeIsFrameDecoded((JY_UserData*)token->native_ud, token->frame);
    return 0;
}

static int resource_native_request_frame(ResourceToken* token, const char** out_status)
{
    if (!token || !token->native_ud || !token->has_frame)
    {
        if (out_status) *out_status = "native handle missing";
        return MYGXY_ASYNC_FRAME_ERROR;
    }
    if (token->native_kind == RESOURCE_NATIVE_TCP)
        return TCP_NativeRequestFrame((TCP_UserData*)token->native_ud, token->frame, out_status);
    if (token->native_kind == RESOURCE_NATIVE_JY)
        return JY_NativeRequestFrame((JY_UserData*)token->native_ud, token->frame, out_status);
    if (out_status) *out_status = "unsupported native type";
    return MYGXY_ASYNC_FRAME_ERROR;
}

static int resource_native_poll_frame(ResourceToken* token, Uint32 limit)
{
    if (!token || !token->native_ud)
        return 0;
    if (token->native_kind == RESOURCE_NATIVE_TCP)
        return TCP_NativePollAsync((TCP_UserData*)token->native_ud, limit);
    if (token->native_kind == RESOURCE_NATIVE_JY)
        return JY_NativePollAsync((JY_UserData*)token->native_ud, limit);
    return 0;
}

static void resource_finish_native_ready(ResourceToken* token)
{
    if (!token || resource_is_terminal(token->status))
        return;
    if (!resource_store_success(token, NULL, 0))
    {
        resource_store_failure(token, "out of memory");
        resource_set_status(token, RESOURCE_STATUS_FAILED, "out of memory");
        g_resource.native_failed++;
        return;
    }
    resource_set_status(token, RESOURCE_STATUS_READY, "ready");
    g_resource.native_ready++;
}

static void resource_finish_native_failed(ResourceToken* token, const char* message)
{
    if (!token || resource_is_terminal(token->status))
        return;
    resource_store_failure(token, message ? message : "native decode failed");
    resource_set_status(token, RESOURCE_STATUS_FAILED, message ? message : "native decode failed");
    g_resource.native_failed++;
}

static int resource_is_tcp_shell(const ResourceToken* token)
{
    return token && token->resource_type && strcmp(token->resource_type, "tcp_shell") == 0;
}

static void resource_finish_shell_ready(ResourceToken* token, TCP_UserData* tcp)
{
    if (!token || resource_is_terminal(token->status))
    {
        TCP_NativeFree(tcp);
        return;
    }
    if (!tcp || !resource_store_native_success(token, tcp))
    {
        TCP_NativeFree(tcp);
        resource_store_failure(token, "out of memory");
        resource_set_status(token, RESOURCE_STATUS_FAILED, "out of memory");
        g_resource.shell_failed++;
        return;
    }
    resource_set_status(token, RESOURCE_STATUS_READY, "ready");
    g_resource.shell_ready++;
}

static void resource_finish_shell_failed(ResourceToken* token, const char* message)
{
    if (!token || resource_is_terminal(token->status))
        return;
    resource_store_failure(token, message ? message : "shell async failed");
    resource_set_status(token, RESOURCE_STATUS_FAILED, message ? message : "shell async failed");
    g_resource.shell_failed++;
}

static void resource_worker_push_done(ResourceWorkerJob* job)
{
    if (!job)
        return;
    SDL_LockMutex(g_resource.worker_mutex);
    if (g_resource.worker_done_tail)
        g_resource.worker_done_tail->next = job;
    else
        g_resource.worker_done_head = job;
    g_resource.worker_done_tail = job;
    job->next = NULL;
    if (g_resource.worker_active > 0)
        g_resource.worker_active--;
    SDL_UnlockMutex(g_resource.worker_mutex);
}

static void resource_worker_job_free(ResourceWorkerJob* job)
{
    if (!job)
        return;
    TCP_NativeFree(job->result_tcp);
    TCP_NativeClearFrameData(&job->result_frame);
    SDL_free(job->pal_snapshot);
    free(job);
}

static void resource_worker_job_list_free(ResourceWorkerJob* job)
{
    while (job)
    {
        ResourceWorkerJob* next = job->next;
        job->next = NULL;
        resource_worker_job_free(job);
        job = next;
    }
}

static void resource_worker_enqueue(ResourceWorkerJob* job)
{
    ResourceWorkerJob* p;
    ResourceWorkerJob* prev = NULL;
    if (!job)
        return;
    if (job->token)
        job->priority = resource_effective_priority(job->token);
    p = g_resource.worker_head;
    while (p)
    {
        if (job->priority > p->priority)
            break;
        if (job->priority == p->priority && job->token_id < p->token_id)
            break;
        prev = p;
        p = p->next;
    }
    if (prev)
    {
        job->next = prev->next;
        prev->next = job;
    }
    else
    {
        job->next = g_resource.worker_head;
        g_resource.worker_head = job;
    }
    if (!job->next)
        g_resource.worker_tail = job;
}

static void resource_worker_promote_token(ResourceToken* token)
{
    ResourceWorkerJob* p;
    ResourceWorkerJob* prev = NULL;
    if (!token || !g_resource.worker_mutex)
        return;
    SDL_LockMutex(g_resource.worker_mutex);
    p = g_resource.worker_head;
    while (p)
    {
        if (p->token == token)
            break;
        prev = p;
        p = p->next;
    }
    if (p)
    {
        if (prev)
            prev->next = p->next;
        else
            g_resource.worker_head = p->next;
        if (g_resource.worker_tail == p)
            g_resource.worker_tail = prev;
        p->priority = resource_effective_priority(token);
        p->shell_group = token->shell_group;
        p->shell_frame = token->shell_frame;
        p->next = NULL;
        resource_worker_enqueue(p);
    }
    SDL_UnlockMutex(g_resource.worker_mutex);
}

static ResourceWorkerJob* resource_worker_pop_locked(void)
{
    ResourceWorkerJob* p = g_resource.worker_head;
    ResourceWorkerJob* prev = NULL;
    ResourceWorkerJob* best = NULL;
    ResourceWorkerJob* best_prev = NULL;
    ResourcePriority best_priority = RESOURCE_PRIORITY_PREHEAT;
    while (p)
    {
        ResourcePriority eff = p->token ? resource_effective_priority_at(p->token, g_resource.worker_poll_tick) : p->priority;
        p->priority = eff;
        if (!best || eff > best_priority || (eff == best_priority && p->token_id < best->token_id))
        {
            best = p;
            best_prev = prev;
            best_priority = eff;
        }
        prev = p;
        p = p->next;
    }
    if (!best)
        return NULL;
    if (best_prev)
        best_prev->next = best->next;
    else
        g_resource.worker_head = best->next;
    if (g_resource.worker_tail == best)
        g_resource.worker_tail = best_prev;
    best->next = NULL;
    return best;
}

static unsigned int resource_update_shell_results(unsigned int max_ops)
{
    unsigned int ops = 0;
    ResourceWorkerJob* job;
    ResourceToken* token;
    if (!g_resource.worker_mutex)
        return 0;
    if (max_ops == 0)
        max_ops = 8;

    while (ops < max_ops)
    {
        job = NULL;
        SDL_LockMutex(g_resource.worker_mutex);
        job = g_resource.worker_done_head;
        if (job)
        {
            g_resource.worker_done_head = job->next;
            if (!g_resource.worker_done_head)
                g_resource.worker_done_tail = NULL;
            job->next = NULL;
        }
        SDL_UnlockMutex(g_resource.worker_mutex);

        if (!job)
            break;

        token = job->token;
        if (token)
            token->worker_started = 0;
        if (job->warm_requested)
        {
            if (job->warm_success)
                g_resource.shell_warm_ready++;
            else
                g_resource.shell_warm_failed++;
        }
        if (!token || token->cancelled || resource_is_terminal(token->status))
        {
            TCP_NativeFree(job->result_tcp);
            job->result_tcp = NULL;
        }
        else if (job->is_tcp_frame)
        {
            if (job->result_ready && job->result_success
                && TCP_NativeStoreDecodedFrame((TCP_UserData*)token->native_ud, &job->result_frame))
            {
                resource_finish_native_ready(token);
                g_resource.frame_ready++;
            }
            else
            {
                resource_finish_native_failed(token, job->result_error[0] ? job->result_error : "frame decode failed");
                g_resource.frame_failed++;
            }
        }
        else if (job->result_ready && job->result_success && job->result_tcp)
        {
            if (job->warm_requested && job->warm_success)
                TCP_NativeStoreDecodedFrame(job->result_tcp, &job->result_frame);
            resource_finish_shell_ready(token, job->result_tcp);
            job->result_tcp = NULL;
        }
        else
        {
            resource_finish_shell_failed(token, job->result_error[0] ? job->result_error : "shell async failed");
        }

        resource_worker_job_free(job);
        ops++;
    }
    return ops;
}

static unsigned int resource_worker_count_jobs(ResourceWorkerJob* job)
{
    unsigned int count = 0;
    while (job)
    {
        count++;
        job = job->next;
    }
    return count;
}

static void resource_worker_queue_counts(unsigned int* out_pending, unsigned int* out_done)
{
    if (out_pending) *out_pending = 0;
    if (out_done) *out_done = 0;
    if (!g_resource.worker_mutex)
        return;
    SDL_LockMutex(g_resource.worker_mutex);
    if (out_pending)
        *out_pending = resource_worker_count_jobs(g_resource.worker_head);
    if (out_done)
        *out_done = resource_worker_count_jobs(g_resource.worker_done_head);
    SDL_UnlockMutex(g_resource.worker_mutex);
}

static unsigned int resource_worker_cancel_scope(const char* scope)
{
    ResourceWorkerJob* job;
    ResourceWorkerJob* keep_head = NULL;
    ResourceWorkerJob* keep_tail = NULL;
    ResourceWorkerJob* drop_head = NULL;
    ResourceWorkerJob* drop_tail = NULL;
    unsigned int dropped = 0;
    if (!scope || !scope[0] || !g_resource.worker_mutex)
        return 0;
    SDL_LockMutex(g_resource.worker_mutex);
    job = g_resource.worker_head;
    g_resource.worker_head = NULL;
    g_resource.worker_tail = NULL;
    while (job)
    {
        ResourceWorkerJob* next = job->next;
        ResourceToken* token = job->token;
        int match = token && ((token->scope && strcmp(token->scope, scope) == 0)
            || (token->scene && strcmp(token->scene, scope) == 0));
        job->next = NULL;
        if (match)
        {
            token->worker_started = 0;
            if (drop_tail)
                drop_tail->next = job;
            else
                drop_head = job;
            drop_tail = job;
            dropped++;
        }
        else
        {
            if (keep_tail)
                keep_tail->next = job;
            else
                keep_head = job;
            keep_tail = job;
        }
        job = next;
    }
    g_resource.worker_head = keep_head;
    g_resource.worker_tail = keep_tail;
    SDL_UnlockMutex(g_resource.worker_mutex);
    resource_worker_job_list_free(drop_head);
    return dropped;
}

static int resource_worker_loop(void* ptr)
{
    (void)ptr;
    for (;;)
    {
        ResourceWorkerJob* job = NULL;
        char err[160];
        unsigned char* data;
        size_t size;
        TCP_UserData* tcp;
        char warm_err[160];
        SDL_LockMutex(g_resource.worker_mutex);
        while (!g_resource.worker_stop && !g_resource.worker_head)
            SDL_CondWait(g_resource.worker_cond, g_resource.worker_mutex);
        if (g_resource.worker_stop)
        {
            SDL_UnlockMutex(g_resource.worker_mutex);
            break;
        }
        job = resource_worker_pop_locked();
        if (job)
            g_resource.worker_active++;
        SDL_UnlockMutex(g_resource.worker_mutex);

        if (!job)
            continue;

        if (!job->token)
        {
            job->result_ready = 1;
            job->result_success = 0;
            SDL_snprintf(job->result_error, sizeof(job->result_error), "token missing");
            resource_worker_push_done(job);
            continue;
        }
        if (job->token->cancelled || resource_is_terminal(job->token->status))
        {
            job->result_ready = 1;
            job->result_success = 0;
            SDL_snprintf(job->result_error, sizeof(job->result_error), "cancelled");
            resource_worker_push_done(job);
            continue;
        }

        if (job->is_tcp_shell)
        {
            SDL_memset(err, 0, sizeof(err));
            data = NULL;
            size = 0;
            tcp = NULL;
            if (!WPK_NativeReadData(job->wpk_ud, job->wpk_id, &data, &size, err, sizeof(err)))
            {
                job->result_ready = 1;
                job->result_success = 0;
                SDL_snprintf(job->result_error, sizeof(job->result_error), "%s", err[0] ? err : "wpk_read failed");
            }
            else
            {
                tcp = TCP_NativeCreateFromData(data, size, err, sizeof(err));
                SDL_free(data);
                if (!tcp)
                {
                    job->result_ready = 1;
                    job->result_success = 0;
                    SDL_snprintf(job->result_error, sizeof(job->result_error), "%s", err[0] ? err : "tcp_parse failed");
                }
                else
                {
                    if (job->shell_frame > 0)
                    {
                        warm_err[0] = '\0';
                        job->warm_requested = 1;
                        if (TCP_NativeDecodeGroupFrame(tcp, job->shell_group ? job->shell_group : 1,
                                job->shell_frame, &job->result_frame, warm_err, sizeof(warm_err)))
                        {
                            job->warm_success = 1;
                        }
                    }
                    job->result_ready = 1;
                    job->result_success = 1;
                    job->result_tcp = tcp;
                }
            }
        }
        else if (job->is_tcp_frame)
        {
            SDL_memset(err, 0, sizeof(err));
            if (TCP_NativeDecodeFrameWithPalette(job->tcp_ud, job->frame_id, job->pal_snapshot,
                    job->pal_count, job->pal_version, &job->result_frame, err, sizeof(err)))
            {
                job->result_ready = 1;
                job->result_success = 1;
            }
            else
            {
                job->result_ready = 1;
                job->result_success = 0;
                SDL_snprintf(job->result_error, sizeof(job->result_error), "%s", err[0] ? err : "frame_decode failed");
            }
        }
        else
        {
            job->result_ready = 1;
            job->result_success = 0;
            SDL_snprintf(job->result_error, sizeof(job->result_error), "unsupported shell type");
        }

        resource_worker_push_done(job);
    }
    return 0;
}

static int resource_worker_submit(ResourceToken* token)
{
    ResourceWorkerJob* job;
    TCP_UserData* tcp;
    unsigned int started;
    if (!token)
        return 0;
    if (!g_resource.worker_mutex)
    {
        g_resource.worker_mutex = SDL_CreateMutex();
        if (!g_resource.worker_mutex)
            return 0;
    }
    if (!g_resource.worker_cond)
    {
        g_resource.worker_cond = SDL_CreateCond();
        if (!g_resource.worker_cond)
            return 0;
    }
    started = g_resource.worker_thread_count;
    while (started < RESOURCE_WORKER_THREADS)
    {
        char name[32];
        SDL_snprintf(name, sizeof(name), "resource_worker_%u", started + 1);
        g_resource.worker_stop = 0;
        g_resource.worker_threads[started] = SDL_CreateThread(resource_worker_loop, name, NULL);
        if (!g_resource.worker_threads[started])
        {
            if (started == 0)
                return 0;
            break;
        }
        started++;
    }
    g_resource.worker_thread_count = started;

    job = (ResourceWorkerJob*)calloc(1, sizeof(ResourceWorkerJob));
    if (!job)
        return 0;
    job->token = token;
    job->priority = token->priority;
    job->token_id = token->id;
    job->wpk_ud = token->wpk_ud;
    job->wpk_id = token->wpk_id;
    job->shell_group = token->shell_group;
    job->shell_frame = token->shell_frame;
    job->is_tcp_shell = resource_is_tcp_shell(token);
    job->is_tcp_frame = !job->is_tcp_shell && token->native_kind == RESOURCE_NATIVE_TCP && token->native_ud && token->has_frame;
    if (job->is_tcp_frame)
    {
        tcp = (TCP_UserData*)token->native_ud;
        if (TCP_NativeIsFrameDecoded(tcp, token->frame))
        {
            resource_worker_job_free(job);
            resource_finish_native_ready(token);
            return 1;
        }
        job->tcp_ud = tcp;
        job->frame_id = token->frame;
        job->pal_count = tcp->pal_count ? tcp->pal_count : 256;
        if (!tcp->pal_dyn && job->pal_count > 256)
            job->pal_count = 256;
        job->pal_version = tcp->pal_version;
        job->pal_snapshot = (Uint32*)SDL_malloc(sizeof(Uint32) * job->pal_count);
        if (!job->pal_snapshot)
        {
            resource_worker_job_free(job);
            return 0;
        }
        SDL_memcpy(job->pal_snapshot, tcp->pal_dyn ? tcp->pal_dyn : tcp->pal, sizeof(Uint32) * job->pal_count);
    }

    SDL_LockMutex(g_resource.worker_mutex);
    resource_worker_enqueue(job);
    SDL_CondSignal(g_resource.worker_cond);
    SDL_UnlockMutex(g_resource.worker_mutex);
    if (job->is_tcp_shell)
        g_resource.shell_submitted++;
    else if (job->is_tcp_frame)
        g_resource.frame_submitted++;
    return 1;
}

static int resource_try_native_frame(ResourceToken* token)
{
    const char* status = NULL;
    int ret;
    if (!token || token->status != RESOURCE_STATUS_QUEUED || token->cancelled)
        return 0;
    if (resource_is_shell_type(token->resource_type))
        return 0;
    if (token->native_kind == RESOURCE_NATIVE_NONE)
        return 0;

    if (token->native_kind != RESOURCE_NATIVE_TCP)
        resource_native_poll_frame(token, 2);
    if (resource_native_is_frame_ready(token))
    {
        resource_finish_native_ready(token);
        return 1;
    }
    if (token->native_kind == RESOURCE_NATIVE_TCP)
    {
        if (token->worker_started)
            return 0;
        if (resource_worker_submit(token))
        {
            if (!resource_is_terminal(token->status))
                token->worker_started = 1;
            return resource_is_terminal(token->status) ? 1 : 0;
        }
        resource_finish_native_failed(token, "worker init failed");
        return 1;
    }

    if (token->retry_tick && g_resource.poll_tick < token->retry_tick)
        return 0;

    token->submit_attempts++;
    ret = resource_native_request_frame(token, &status);
    if (ret == MYGXY_ASYNC_FRAME_READY)
    {
        resource_finish_native_ready(token);
        return 1;
    }
    if (ret == MYGXY_ASYNC_FRAME_QUEUED || ret == MYGXY_ASYNC_FRAME_PENDING)
    {
        if (token->submit_attempts >= token->max_tries)
        {
            resource_finish_native_failed(token, "native frame timeout");
            return 1;
        }
        token->retry_tick = g_resource.poll_tick + 1;
        return 0;
    }
    if (ret == MYGXY_ASYNC_FRAME_QUEUE_FULL)
    {
        token->queue_full_count++;
        g_resource.native_queue_full++;
        token->retry_tick = g_resource.poll_tick + 2 + (token->queue_full_count > 8 ? 8 : token->queue_full_count);
        if (token->submit_attempts < token->max_tries)
            return 0;
        resource_finish_native_failed(token, status ? status : "queue full");
        return 1;
    }

    resource_finish_native_failed(token, status ? status : "native request failed");
    return 1;
}

static int resource_native_frame_candidate(const ResourceToken* token)
{
    return token
        && !resource_is_shell_type(token->resource_type)
        && token->native_kind != RESOURCE_NATIVE_NONE
        && token->status == RESOURCE_STATUS_QUEUED
        && !(token->native_kind == RESOURCE_NATIVE_TCP && token->worker_started);
}

static void resource_insert_native_candidate(ResourceToken** slots,
    unsigned int* count, unsigned int cap, ResourceToken* token)
{
    unsigned int pos;
    if (!slots || !count || !token || cap == 0)
        return;
    if (*count < cap)
    {
        pos = *count;
        (*count)++;
    }
    else
    {
        if (token->id >= slots[*count - 1]->id)
            return;
        pos = *count - 1;
    }
    while (pos > 0 && slots[pos - 1]->id > token->id)
    {
        slots[pos] = slots[pos - 1];
        pos--;
    }
    slots[pos] = token;
}

static unsigned int resource_update_native_frames(unsigned int max_ops)
{
    ResourceToken* after_cursor[8];
    ResourceToken* wrap_cursor[8];
    ResourceToken* p;
    unsigned int after_count = 0;
    unsigned int wrap_count = 0;
    unsigned int i;
    unsigned int ops = 0;
    if (max_ops == 0)
        max_ops = 8;
    if (max_ops > 8)
        max_ops = 8;

    p = g_resource.tokens;
    while (p)
    {
        if (resource_native_frame_candidate(p))
        {
            if (p->id > g_resource.native_cursor_id)
                resource_insert_native_candidate(after_cursor, &after_count, max_ops, p);
            else
                resource_insert_native_candidate(wrap_cursor, &wrap_count, max_ops, p);
        }
        p = p->next;
    }
    for (i = 0; i < after_count && ops < max_ops; i++)
    {
        ResourceToken* token = after_cursor[i];
        if (resource_native_frame_candidate(token))
        {
            g_resource.native_cursor_id = token->id;
            resource_try_native_frame(token);
            ops++;
        }
    }
    for (i = 0; i < wrap_count && ops < max_ops; i++)
    {
        ResourceToken* token = wrap_cursor[i];
        if (resource_native_frame_candidate(token))
        {
            g_resource.native_cursor_id = token->id;
            resource_try_native_frame(token);
            ops++;
        }
    }
    g_resource.native_retried += ops;
    return ops;
}

static void resource_update_downloads(lua_State* L)
{
    ResourceToken* p = g_resource.tokens;
    unsigned int max_active = resource_max_active();
    unsigned int low_active_max = resource_low_active_max();
    unsigned int active = 0;
    unsigned int low_active = 0;
    while (p)
    {
        ResourceToken* next = p->next;
        if (p->cancelled && p->download_ref != LUA_NOREF)
            resource_cancel_download(L, p);
        else
            resource_check_active_download(L, p);
        if (!p->cancelled && p->download_ref != LUA_NOREF)
        {
            active++;
            if (p->priority == RESOURCE_PRIORITY_PREHEAT)
                low_active++;
        }
        p = next;
    }

    while (active < max_active)
    {
        ResourceToken* token = resource_pick_queued(RESOURCE_PRIORITY_NORMAL);
        if (!token)
            break;
        if (!resource_start_download(L, token))
        {
            resource_finish_failed(L, token, "download start failed");
            continue;
        }
        active++;
        if (token->priority == RESOURCE_PRIORITY_PREHEAT)
            low_active++;
    }

    while (active < max_active && low_active < low_active_max)
    {
        ResourceToken* token = resource_pick_queued(RESOURCE_PRIORITY_PREHEAT);
        if (!token)
            break;
        if (!resource_start_download(L, token))
        {
            resource_finish_failed(L, token, "download start failed");
            continue;
        }
        active++;
        low_active++;
    }

    resource_dispatch_pending_callbacks(L);
}

static void resource_update_all(lua_State* L, unsigned int native_ops)
{
    g_resource.poll_tick++;
    if (g_resource.worker_mutex)
    {
        SDL_LockMutex(g_resource.worker_mutex);
        g_resource.worker_poll_tick = g_resource.poll_tick;
        SDL_UnlockMutex(g_resource.worker_mutex);
    }
    resource_update_shell_results(native_ops);
    resource_update_native_frames(native_ops);
    resource_update_downloads(L);
    resource_reap_finished(L);
}

static ResourceToken* resource_find_token(unsigned int id)
{
    ResourceToken* p = g_resource.tokens;
    while (p)
    {
        if (p->id == id)
            return p;
        p = p->next;
    }
    return NULL;
}

static ResourceToken* resource_find_by_resource_id(const char* resource_id)
{
    ResourceToken* p;
    if (!resource_id)
        return NULL;
    p = g_resource.tokens;
    while (p)
    {
        if (p->resource_id && strcmp(p->resource_id, resource_id) == 0)
            return p;
        p = p->next;
    }
    return NULL;
}

static char* resource_table_strdup(lua_State* L, int idx, const char* key)
{
    char* out = NULL;
    lua_getfield(L, idx, key);
    if (lua_isstring(L, -1))
        out = resource_strdup(lua_tostring(L, -1));
    lua_pop(L, 1);
    return out;
}

static int resource_table_bool(lua_State* L, int idx, const char* key, int fallback)
{
    int value = fallback;
    lua_getfield(L, idx, key);
    if (lua_isboolean(L, -1))
        value = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);
    return value;
}

static int resource_table_int(lua_State* L, int idx, const char* key, int fallback)
{
    int value = fallback;
    lua_getfield(L, idx, key);
    if (lua_isnumber(L, -1))
        value = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return value;
}

static void* resource_test_userdata(lua_State* L, int idx, const char* tname)
{
    void* p = lua_touserdata(L, idx);
    int eq;
    if (!p)
        return NULL;
    if (!lua_getmetatable(L, idx))
        return NULL;
    luaL_getmetatable(L, tname);
    eq = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    return eq ? p : NULL;
}

static int resource_read_native_ref(lua_State* L, int idx, ResourceNativeKind* out_kind, void** out_ud)
{
    void* ud = NULL;
    ResourceNativeKind kind = RESOURCE_NATIVE_NONE;
    int ref = LUA_NOREF;

    lua_getfield(L, idx, "native_ud");
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_getfield(L, idx, "ud");
    }
    if (lua_isuserdata(L, -1))
    {
        ud = resource_test_userdata(L, -1, TCP_MT_XYQ);
        if (!ud)
            ud = resource_test_userdata(L, -1, TCP_MT_XY2);
        if (ud)
            kind = RESOURCE_NATIVE_TCP;
        else
        {
            ud = resource_test_userdata(L, -1, JY_MT);
            if (ud)
                kind = RESOURCE_NATIVE_JY;
            else
            {
                ud = resource_test_userdata(L, -1, WPK_NAME);
                if (ud)
                    kind = RESOURCE_NATIVE_WPK;
            }
        }
    }

    if (ud)
    {
        lua_pushvalue(L, -1);
        ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_pop(L, 1);

    if (out_kind) *out_kind = kind;
    if (out_ud) *out_ud = ud;
    return ref;
}

static int resource_read_wpk_ref(lua_State* L, int idx, WPK_UserData** out_ud)
{
    WPK_UserData* ud = NULL;
    int ref = LUA_NOREF;
    lua_getfield(L, idx, "wpk_ud");
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_getfield(L, idx, "wpk");
    }
    if (lua_isuserdata(L, -1))
        ud = (WPK_UserData*)resource_test_userdata(L, -1, WPK_NAME);
    if (ud)
    {
        lua_pushvalue(L, -1);
        ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_pop(L, 1);
    if (out_ud) *out_ud = ud;
    return ref;
}

static int resource_add_callback_ref(ResourceToken* token, int ref)
{
    ResourceCallback* cb;
    ResourceCallback* tail;
    if (!token || ref == LUA_NOREF)
        return 1;
    cb = (ResourceCallback*)calloc(1, sizeof(ResourceCallback));
    if (!cb)
        return 0;
    cb->ref = ref;
    if (!token->callbacks)
    {
        token->callbacks = cb;
        return 1;
    }
    tail = token->callbacks;
    while (tail->next)
        tail = tail->next;
    tail->next = cb;
    return 1;
}

static int resource_read_callback_ref(lua_State* L, int idx)
{
    int ref = LUA_NOREF;
    lua_getfield(L, idx, "callback");
    if (!lua_isfunction(L, -1))
    {
        lua_pop(L, 1);
        lua_getfield(L, idx, "\xe5\x9b\x9e\xe8\xb0\x83");
    }
    if (lua_isfunction(L, -1))
        ref = luaL_ref(L, LUA_REGISTRYINDEX);
    else
        lua_pop(L, 1);
    return ref;
}

static int resource_read_urls(lua_State* L, int idx, char*** out_urls, unsigned int* out_count)
{
    unsigned int count = 0;
    unsigned int i;
    char** urls = NULL;
    *out_urls = NULL;
    *out_count = 0;

    lua_getfield(L, idx, "urls");
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_getfield(L, idx, "url");
    }

    if (lua_istable(L, -1))
    {
        lua_Integer n = lua_rawlen(L, -1);
        if (n > 0)
        {
            urls = (char**)calloc((size_t)n, sizeof(char*));
            if (!urls)
            {
                lua_pop(L, 1);
                return 0;
            }
            for (i = 1; i <= (unsigned int)n; i++)
            {
                lua_rawgeti(L, -1, (lua_Integer)i);
                if (lua_isstring(L, -1))
                {
                    const char* s = lua_tostring(L, -1);
                    if (s && s[0])
                    {
                        urls[count] = resource_strdup(s);
                        if (!urls[count])
                        {
                            lua_pop(L, 2);
                            resource_free_urls(urls, count);
                            return 0;
                        }
                        count++;
                    }
                }
                lua_pop(L, 1);
            }
        }
    }
    else if (lua_isstring(L, -1))
    {
        const char* s = lua_tostring(L, -1);
        if (s && s[0])
        {
            urls = (char**)calloc(1, sizeof(char*));
            if (!urls)
            {
                lua_pop(L, 1);
                return 0;
            }
            urls[0] = resource_strdup(s);
            if (!urls[0])
            {
                lua_pop(L, 1);
                free(urls);
                return 0;
            }
            count = 1;
        }
    }
    lua_pop(L, 1);

    *out_urls = urls;
    *out_count = count;
    return 1;
}

static char* resource_make_dedupe_key(lua_State* L, int req_idx, char** urls, unsigned int url_count, const char* resource_id)
{
    char* key = resource_table_strdup(L, req_idx, "dedupe_key");
    if (!key) key = resource_table_strdup(L, req_idx, "\xe5\x8e\xbb\xe9\x87\x8dkey");
    if (!key && url_count > 0 && urls && urls[0])
        key = resource_strdup(urls[0]);
    if (!key && resource_id)
        key = resource_strdup(resource_id);
    return key;
}

static ResourceToken* resource_find_by_dedupe_key(const char* dedupe_key)
{
    ResourceToken* p;
    if (!dedupe_key || !dedupe_key[0])
        return NULL;
    p = g_resource.tokens;
    while (p)
    {
        if (!p->cancelled && !resource_is_terminal(p->status)
            && p->dedupe_key && strcmp(p->dedupe_key, dedupe_key) == 0)
            return p;
        p = p->next;
    }
    return NULL;
}

static ResourceToken* resource_create_token(lua_State* L, int req_idx)
{
    char* resource_id;
    char* resource_type;
    char* scene;
    char* scope;
    char* domain;
    char* priority;
    char* queue_priority;
    char* degrade;
    char* save_path;
    char* dedupe_key;
    char* expected_md5;
    char** urls;
    unsigned int url_count;
    int callback_ref;
    int timeout;
    int requested_frame;
    Uint32 requested_shell_group;
    Uint32 requested_shell_frame;
    ResourcePriority parsed_priority;
    ResourceToken* existing;
    ResourceToken* token;

    if (req_idx < 0)
        req_idx = lua_gettop(L) + 1 + req_idx;

    urls = NULL;
    url_count = 0;
    callback_ref = resource_read_callback_ref(L, req_idx);
    if (!resource_read_urls(L, req_idx, &urls, &url_count))
    {
        if (callback_ref != LUA_NOREF)
            luaL_unref(L, LUA_REGISTRYINDEX, callback_ref);
        return NULL;
    }

    resource_id = resource_table_strdup(L, req_idx, "id");
    if (!resource_id) resource_id = resource_table_strdup(L, req_idx, RESOURCE_KEY_ID);
    resource_type = resource_table_strdup(L, req_idx, "type");
    if (!resource_type) resource_type = resource_table_strdup(L, req_idx, RESOURCE_KEY_TYPE);
    scene = resource_table_strdup(L, req_idx, "scene");
    if (!scene) scene = resource_table_strdup(L, req_idx, RESOURCE_KEY_SCENE);
    scope = resource_table_strdup(L, req_idx, "scope");
    if (!scope) scope = resource_table_strdup(L, req_idx, "\xe8\x8c\x83\xe5\x9b\xb4");
    domain = resource_table_strdup(L, req_idx, "domain");
    priority = resource_table_strdup(L, req_idx, "priority");
    if (!priority) priority = resource_table_strdup(L, req_idx, RESOURCE_KEY_PRIORITY);
    queue_priority = resource_table_strdup(L, req_idx, "queue_priority");
    if (!queue_priority) queue_priority = resource_table_strdup(L, req_idx, RESOURCE_KEY_QUEUE_PRIORITY);
    degrade = resource_table_strdup(L, req_idx, "degrade");
    save_path = resource_table_strdup(L, req_idx, "save_path");
    if (!save_path) save_path = resource_table_strdup(L, req_idx, "path");
    if (!save_path) save_path = resource_table_strdup(L, req_idx, RESOURCE_KEY_SAVE_PATH);
    if (!save_path) save_path = resource_table_strdup(L, req_idx, RESOURCE_KEY_FILE_PATH);
    expected_md5 = resource_table_strdup(L, req_idx, "expected_md5");
    if (!expected_md5) expected_md5 = resource_table_strdup(L, req_idx, "md5");
    if (!expected_md5) expected_md5 = resource_table_strdup(L, req_idx, "\xe6\xa0\xa1\xe9\xaa\x8cMD5");
    timeout = resource_table_int(L, req_idx, "timeout",
        resource_table_int(L, req_idx, "\xe8\xb6\x85\xe6\x97\xb6", RESOURCE_DEFAULT_TIMEOUT));
    if (timeout <= 0)
        timeout = RESOURCE_DEFAULT_TIMEOUT;
    requested_frame = resource_table_int(L, req_idx, "frame", -2147483647);
    requested_shell_group = (Uint32)resource_table_int(L, req_idx, "shell_group",
        resource_table_int(L, req_idx, "group",
            resource_table_int(L, req_idx, "direction",
                resource_table_int(L, req_idx, "\xe6\x96\xb9\xe5\x90\x91", 1))));
    requested_shell_frame = (Uint32)resource_table_int(L, req_idx, "shell_frame",
        resource_table_int(L, req_idx, "first_frame",
            resource_table_int(L, req_idx, "firstFrame", requested_frame != -2147483647 ? requested_frame : 1)));
    if (requested_shell_group == 0)
        requested_shell_group = 1;
    if (requested_shell_frame == 0)
        requested_shell_frame = 1;

    parsed_priority = resource_parse_priority(priority, queue_priority);
    dedupe_key = resource_make_dedupe_key(L, req_idx, urls, url_count, resource_id);
    existing = resource_find_by_dedupe_key(dedupe_key);
    if (existing)
    {
        if (resource_is_shell_type(existing->resource_type))
        {
            existing->shell_group = requested_shell_group;
            existing->shell_frame = requested_shell_frame;
        }
        resource_promote_priority(existing, parsed_priority);
        if (callback_ref != LUA_NOREF && !resource_add_callback_ref(existing, callback_ref))
        {
            luaL_unref(L, LUA_REGISTRYINDEX, callback_ref);
            free(resource_id);
            free(resource_type);
            free(scene);
            free(scope);
            free(domain);
            free(priority);
            free(queue_priority);
            free(degrade);
            free(save_path);
            free(dedupe_key);
            free(expected_md5);
            resource_free_urls(urls, url_count);
            return NULL;
        }
        free(resource_id);
        free(resource_type);
        free(scene);
        free(scope);
        free(domain);
        free(priority);
        free(queue_priority);
        free(degrade);
        free(save_path);
        free(dedupe_key);
        free(expected_md5);
        resource_free_urls(urls, url_count);
        return existing;
    }

    token = (ResourceToken*)calloc(1, sizeof(ResourceToken));
    if (!token)
    {
        if (callback_ref != LUA_NOREF)
            luaL_unref(L, LUA_REGISTRYINDEX, callback_ref);
        resource_free_urls(urls, url_count);
        free(resource_id);
        free(resource_type);
        free(scene);
        free(scope);
        free(domain);
        free(priority);
        free(queue_priority);
        free(degrade);
        free(save_path);
        free(dedupe_key);
        free(expected_md5);
        return NULL;
    }
    token->id = ++g_resource.next_token_id;
    token->created_tick = g_resource.poll_tick;
    token->priority = parsed_priority;
    token->allow_cold_download = resource_table_bool(L, req_idx, "allow_cold_download",
        resource_table_bool(L, req_idx, "allow_cold", 1));
    token->allow_cold_download = resource_table_bool(L, req_idx, RESOURCE_KEY_ALLOW_COLD,
        token->allow_cold_download);
    token->status = token->allow_cold_download ? RESOURCE_STATUS_QUEUED : RESOURCE_STATUS_DEGRADED;
    token->download_ref = LUA_NOREF;
    token->native_ref = LUA_NOREF;
    token->wpk_ref = LUA_NOREF;
    token->max_tries = (unsigned int)resource_table_int(L, req_idx, "max_tries",
        resource_table_int(L, req_idx, "\xe6\x9c\x80\xe5\xa4\xa7\xe9\x87\x8d\xe8\xaf\x95", RESOURCE_DEFAULT_NATIVE_RETRIES));
    if (token->max_tries == 0)
        token->max_tries = RESOURCE_DEFAULT_NATIVE_RETRIES;
    token->generation = (lua_Integer)resource_table_int(L, req_idx, "generation", 0);
    token->has_generation = resource_table_int(L, req_idx, "generation", -2147483647) != -2147483647;
    token->frame = requested_frame != -2147483647 ? (Uint32)requested_frame : 0;
    token->has_frame = requested_frame != -2147483647;
    token->shell_group = requested_shell_group;
    token->shell_frame = requested_shell_frame;
    token->wpk_id = (Uint32)resource_table_int(L, req_idx, "wpk_id",
        resource_table_int(L, req_idx, "wpkid",
            resource_table_int(L, req_idx, "\xe5\x8c\x85ID", 0)));
    token->has_wpk_id = token->wpk_id > 0;
    token->timeout = timeout;
    token->urls = urls;
    token->url_count = url_count;
    token->save_path = save_path;
    token->dedupe_key = dedupe_key;
    token->expected_md5 = expected_md5;
    token->resource_id = resource_id ? resource_id : resource_strdup("");
    token->resource_type = resource_type ? resource_type : resource_strdup("resource");
    token->scene = scene ? scene : resource_strdup("");
    token->scope = scope ? scope : resource_strdup("");
    token->domain = domain ? domain : resource_strdup("");
    token->degrade = degrade;
    token->native_ref = resource_read_native_ref(L, req_idx, &token->native_kind, &token->native_ud);
    token->wpk_ref = resource_read_wpk_ref(L, req_idx, &token->wpk_ud);
    if (callback_ref != LUA_NOREF && !resource_add_callback_ref(token, callback_ref))
    {
        luaL_unref(L, LUA_REGISTRYINDEX, callback_ref);
        resource_free_token(L, token);
        free(priority);
        free(queue_priority);
        return NULL;
    }
    free(priority);
    free(queue_priority);

    token->next = g_resource.tokens;
    g_resource.tokens = token;
    g_resource.created++;

    if (token->status == RESOURCE_STATUS_DEGRADED)
    {
        g_resource.degraded++;
        resource_store_failure(token, "cold download disabled");
        token->terminal_tick = g_resource.poll_tick;
        token->event_pending = resource_push_event(token, RESOURCE_STATUS_DEGRADED,
            token->resource_id, "cold download disabled") ? 1u : 0u;
    }
    else if (token->priority == RESOURCE_PRIORITY_PREHEAT)
    {
        g_resource.queued_low++;
    }
    else
    {
        g_resource.queued_high++;
    }
    return token;
}

static void resource_push_token_table(lua_State* L, const ResourceToken* token)
{
    lua_createtable(L, 0, 8);
    lua_pushinteger(L, (lua_Integer)token->id);
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, (lua_Integer)token->id);
    lua_setfield(L, -2, "token");
    lua_pushstring(L, token->resource_id ? token->resource_id : "");
    lua_setfield(L, -2, RESOURCE_KEY_ID);
    lua_pushstring(L, token->resource_id ? token->resource_id : "");
    lua_setfield(L, -2, "resource_id");
    lua_pushstring(L, token->resource_type ? token->resource_type : "resource");
    lua_setfield(L, -2, RESOURCE_KEY_TYPE);
    lua_pushstring(L, token->resource_type ? token->resource_type : "resource");
    lua_setfield(L, -2, "type");
    lua_pushstring(L, token->scene ? token->scene : "");
    lua_setfield(L, -2, RESOURCE_KEY_SCENE);
    lua_pushstring(L, token->scene ? token->scene : "");
    lua_setfield(L, -2, "scene");
    lua_pushstring(L, token->scope ? token->scope : "");
    lua_setfield(L, -2, "scope");
    lua_pushstring(L, token->domain ? token->domain : "");
    lua_setfield(L, -2, "domain");
    if (token->has_frame)
    {
        lua_pushinteger(L, (lua_Integer)token->frame);
        lua_setfield(L, -2, "frame");
    }
    if (token->has_generation)
    {
        lua_pushinteger(L, token->generation);
        lua_setfield(L, -2, "generation");
    }
    lua_pushstring(L, token->priority == RESOURCE_PRIORITY_PREHEAT ? "preheat" :
        (token->priority == RESOURCE_PRIORITY_CRITICAL ? "critical" :
            (token->priority == RESOURCE_PRIORITY_HIGH ? "high" : "normal")));
    lua_setfield(L, -2, RESOURCE_KEY_PRIORITY);
    lua_pushstring(L, token->priority == RESOURCE_PRIORITY_PREHEAT ? "preheat" :
        (token->priority == RESOURCE_PRIORITY_CRITICAL ? "critical" :
            (token->priority == RESOURCE_PRIORITY_HIGH ? "high" : "normal")));
    lua_setfield(L, -2, "priority");
    lua_pushstring(L, token->priority == RESOURCE_PRIORITY_PREHEAT ? RESOURCE_QUEUE_LOW : "\xe9\xab\x98");
    lua_setfield(L, -2, RESOURCE_KEY_QUEUE_PRIORITY);
    lua_pushstring(L, token->priority == RESOURCE_PRIORITY_PREHEAT ? "low" : "high");
    lua_setfield(L, -2, "queue_priority");
    lua_pushboolean(L, token->allow_cold_download);
    lua_setfield(L, -2, RESOURCE_KEY_ALLOW_COLD);
    lua_pushboolean(L, token->allow_cold_download);
    lua_setfield(L, -2, "allow_cold_download");
    lua_pushstring(L, resource_status_name(token->status));
    lua_setfield(L, -2, RESOURCE_KEY_STATUS);
    lua_pushstring(L, resource_status_name(token->status));
    lua_setfield(L, -2, "status");
    lua_pushboolean(L, token->cancelled);
    lua_setfield(L, -2, "cancelled");
    lua_pushboolean(L, token->native_kind != RESOURCE_NATIVE_NONE);
    lua_setfield(L, -2, "native_decode");
    lua_pushboolean(L, resource_is_tcp_shell(token) && token->result_native != NULL);
    lua_setfield(L, -2, "shell_ready");
    if (token->has_wpk_id)
    {
        lua_pushinteger(L, (lua_Integer)token->wpk_id);
        lua_setfield(L, -2, "wpk_id");
    }
    if (resource_is_tcp_shell(token))
    {
        lua_pushinteger(L, (lua_Integer)token->shell_group);
        lua_setfield(L, -2, "group");
        lua_pushinteger(L, (lua_Integer)token->shell_frame);
        lua_setfield(L, -2, "first_frame");
    }
    lua_pushinteger(L, (lua_Integer)token->submit_attempts);
    lua_setfield(L, -2, "attempts");
    lua_pushinteger(L, (lua_Integer)token->queue_full_count);
    lua_setfield(L, -2, "queue_full");
    if (token->result_error)
    {
        lua_pushstring(L, token->result_error);
        lua_setfield(L, -2, "message");
        lua_pushstring(L, token->result_error);
        lua_setfield(L, -2, "error");
    }
}

static int resource_lua_query(lua_State* L)
{
    ResourceToken* token = NULL;
    if (lua_istable(L, 1))
    {
        lua_getfield(L, 1, "token");
        if (lua_isnumber(L, -1))
            token = resource_find_token((unsigned int)lua_tointeger(L, -1));
        lua_pop(L, 1);

        if (!token)
        {
            lua_getfield(L, 1, "id");
            if (lua_isnumber(L, -1))
                token = resource_find_token((unsigned int)lua_tointeger(L, -1));
            else if (lua_isstring(L, -1))
                token = resource_find_by_resource_id(lua_tostring(L, -1));
            lua_pop(L, 1);
        }

        if (!token)
        {
            lua_getfield(L, 1, "resource_id");
            if (lua_isstring(L, -1))
                token = resource_find_by_resource_id(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    else if (lua_isnumber(L, 1))
    {
        token = resource_find_token((unsigned int)lua_tointeger(L, 1));
    }
    else if (lua_isstring(L, 1))
    {
        token = resource_find_by_resource_id(lua_tostring(L, 1));
    }

    if (!token)
    {
        lua_pushnil(L);
        return 1;
    }
    resource_push_token_table(L, token);
    return 1;
}

static int resource_lua_request(lua_State* L)
{
    ResourceToken* token;
    luaL_checktype(L, 1, LUA_TTABLE);
    token = resource_create_token(L, 1);
    if (!token)
        return luaL_error(L, "resource: out of memory");
    if (token->status == RESOURCE_STATUS_QUEUED
        && (!token->urls || token->url_count == 0))
        resource_submit_native_producer(token);
    resource_push_token_table(L, token);
    return 1;
}

static int resource_submit_native_producer(ResourceToken* token)
{
    const char* message = NULL;
    int accepted;
    if (!token || resource_is_terminal(token->status))
        return 0;

    if (resource_is_tcp_shell(token))
    {
        if (!token->wpk_ud || !token->has_wpk_id)
            message = "tcp_shell requires wpk_ud+wpk_id";
    }
    else if (resource_is_shell_type(token->resource_type))
    {
        message = "shell async only supports tcp_shell";
    }
    else if (token->native_kind != RESOURCE_NATIVE_NONE)
    {
        if (!token->has_frame)
            message = "native frame missing";
    }
    else if (token->urls && token->url_count > 0)
    {
        return 0;
    }
    else
    {
        message = "native producer unavailable";
    }

    if (message)
    {
        if (resource_is_shell_type(token->resource_type))
            g_resource.shell_degraded++;
        resource_store_failure(token, message);
        resource_set_status(token, RESOURCE_STATUS_DEGRADED, message);
        return 0;
    }

    accepted = (resource_is_tcp_shell(token) && token->wpk_ud && token->has_wpk_id)
        || (token->native_kind != RESOURCE_NATIVE_NONE && token->has_frame);
    if (!accepted)
        return 0;

    if (resource_is_tcp_shell(token) || token->native_kind == RESOURCE_NATIVE_TCP)
    {
        if (token->worker_started)
        {
            return 1;
        }
        if (resource_worker_submit(token))
        {
            if (!resource_is_terminal(token->status))
                token->worker_started = 1;
            token->shell_started = resource_is_tcp_shell(token);
            return 1;
        }
        resource_store_failure(token, "worker init failed");
        resource_set_status(token, RESOURCE_STATUS_DEGRADED, "worker init failed");
        return 0;
    }

    resource_try_native_frame(token);
    return 1;
}

static int resource_lua_submit(lua_State* L)
{
    ResourceToken* token;
    int accepted;
    luaL_checktype(L, 1, LUA_TTABLE);
    g_resource.render_submitted++;
    token = resource_create_token(L, 1);
    if (!token)
        return luaL_error(L, "resource: out of memory");

    accepted = resource_submit_native_producer(token);

    resource_push_token_table(L, token);
    lua_pushboolean(L, accepted);
    lua_setfield(L, -2, "accepted");
    return 1;
}

static int resource_lua_preload(lua_State* L)
{
    lua_Integer out_index = 1;
    int result_idx;
    int accepted;
    ResourceToken* token;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_newtable(L);
    result_idx = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, 1) != 0)
    {
        if (lua_istable(L, -1))
        {
            token = resource_create_token(L, -1);
            if (!token)
                return luaL_error(L, "resource: out of memory");
            g_resource.preload++;
            accepted = resource_submit_native_producer(token);
            resource_push_token_table(L, token);
            lua_pushboolean(L, accepted);
            lua_setfield(L, -2, "accepted");
            lua_rawseti(L, result_idx, out_index++);
        }
        lua_pop(L, 1);
    }
    return 1;
}

static int resource_lua_poll(lua_State* L)
{
    lua_Integer i = 1;
    ResourceEvent* ev;
    ResourceEvent* next;
    unsigned int max_ops = 8;
    if (lua_isnumber(L, 3))
        max_ops = (unsigned int)lua_tointeger(L, 3);
    else if (lua_isnumber(L, 2))
    {
        lua_Number n = lua_tonumber(L, 2);
        if (n >= 1)
            max_ops = (unsigned int)n;
    }
    resource_update_all(L, max_ops);
    ev = g_resource.events_head;
    lua_newtable(L);
    while (ev)
    {
        next = ev->next;
        lua_createtable(L, 0, 4);
        lua_pushinteger(L, (lua_Integer)ev->token_id);
        lua_setfield(L, -2, "token");
        lua_pushstring(L, resource_status_name(ev->status));
        lua_setfield(L, -2, RESOURCE_KEY_STATUS);
        lua_pushstring(L, resource_status_name(ev->status));
        lua_setfield(L, -2, "status");
        lua_pushstring(L, ev->resource_id ? ev->resource_id : "");
        lua_setfield(L, -2, RESOURCE_KEY_ID);
        lua_pushstring(L, ev->resource_id ? ev->resource_id : "");
        lua_setfield(L, -2, "resource_id");
        if (ev->message)
        {
            lua_pushstring(L, ev->message);
            lua_setfield(L, -2, "message");
        }
        lua_rawseti(L, -2, i++);

        if (ev->token)
            ev->token->event_pending = 0;
        free(ev->resource_id);
        free(ev->message);
        free(ev);
        ev = next;
    }
    g_resource.events_head = NULL;
    g_resource.events_tail = NULL;
    resource_reap_finished(L);
    return 1;
}

static int resource_lua_cancel(lua_State* L)
{
    unsigned int id;
    ResourceToken* token;
    if (lua_istable(L, 1))
    {
        lua_getfield(L, 1, "token");
        if (lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            lua_getfield(L, 1, "id");
        }
        id = (unsigned int)luaL_checkinteger(L, -1);
        lua_pop(L, 1);
    }
    else
    {
        id = (unsigned int)luaL_checkinteger(L, 1);
    }
    token = resource_find_token(id);
    if (!token)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!token->cancelled)
    {
        token->cancelled = 1;
        g_resource.cancelled++;
        if (token->download_ref != LUA_NOREF)
            resource_cancel_download(L, token);
        resource_store_failure(token, "cancelled");
        resource_set_status(token, RESOURCE_STATUS_CANCELLED, "cancelled");
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int resource_lua_cancel_all(lua_State* L)
{
    unsigned int count = 0;
    ResourceToken* token = g_resource.tokens;
    while (token)
    {
        if (!resource_is_terminal(token->status) && !token->cancelled)
        {
            token->cancelled = 1;
            g_resource.cancelled++;
            if (token->download_ref != LUA_NOREF)
                resource_cancel_download(L, token);
            resource_store_failure(token, "cancelled");
            resource_set_status(token, RESOURCE_STATUS_CANCELLED, "cancelled");
            count++;
        }
        token = token->next;
    }
    lua_pushinteger(L, (lua_Integer)count);
    return 1;
}

static int resource_lua_cancel_scope(lua_State* L)
{
    const char* scope = luaL_optstring(L, 1, "");
    ResourceToken* token = g_resource.tokens;
    unsigned int count = 0;
    unsigned int worker_dropped = resource_worker_cancel_scope(scope);
    while (token)
    {
        if (!resource_is_terminal(token->status) && !token->cancelled
            && ((token->scope && strcmp(token->scope, scope) == 0)
                || (token->scene && strcmp(token->scene, scope) == 0)))
        {
            token->cancelled = 1;
            g_resource.cancelled++;
            g_resource.render_cancelled++;
            if (token->download_ref != LUA_NOREF)
                resource_cancel_download(L, token);
            resource_store_failure(token, "cancelled");
            resource_set_status(token, RESOURCE_STATUS_CANCELLED, "cancelled");
            count++;
        }
        token = token->next;
    }
    lua_pushinteger(L, (lua_Integer)(count + worker_dropped));
    return 1;
}

static int resource_push_stats(lua_State* L)
{
    unsigned int active = 0;
    unsigned int low_active = 0;
    unsigned int tokens_alive = 0;
    unsigned int tokens_terminal = 0;
    unsigned int events_pending = 0;
    unsigned int worker_pending = 0;
    unsigned int worker_done = 0;
    ResourceToken* token = g_resource.tokens;
    ResourceEvent* ev;
    while (token)
    {
        tokens_alive++;
        if (resource_is_terminal(token->status))
            tokens_terminal++;
        if (!token->cancelled && token->download_ref != LUA_NOREF)
        {
            active++;
            if (token->priority == RESOURCE_PRIORITY_PREHEAT)
                low_active++;
        }
        token = token->next;
    }
    ev = g_resource.events_head;
    while (ev)
    {
        events_pending++;
        ev = ev->next;
    }
    resource_worker_queue_counts(&worker_pending, &worker_done);

    lua_createtable(L, 0, 24);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "native");
    lua_pushinteger(L, (lua_Integer)g_resource.created);
    lua_setfield(L, -2, "request");
    lua_pushinteger(L, (lua_Integer)g_resource.preload);
    lua_setfield(L, -2, "preload");
    lua_pushinteger(L, (lua_Integer)g_resource.cancelled);
    lua_setfield(L, -2, "cancel");
    lua_pushinteger(L, (lua_Integer)active);
    lua_setfield(L, -2, "active");
    lua_pushinteger(L, (lua_Integer)low_active);
    lua_setfield(L, -2, "active_low");
    lua_pushinteger(L, (lua_Integer)g_resource.queued_high);
    lua_setfield(L, -2, "queued_high");
    lua_pushinteger(L, (lua_Integer)g_resource.queued_low);
    lua_setfield(L, -2, "queued_low");
    lua_pushinteger(L, (lua_Integer)g_resource.ready);
    lua_setfield(L, -2, "ready");
    lua_pushinteger(L, (lua_Integer)g_resource.failed);
    lua_setfield(L, -2, "failed");
    lua_pushinteger(L, (lua_Integer)g_resource.degraded);
    lua_setfield(L, -2, "degraded");
    lua_pushinteger(L, (lua_Integer)g_resource.shell_degraded);
    lua_setfield(L, -2, "shell_degraded");
    lua_pushinteger(L, (lua_Integer)resource_max_active());
    lua_setfield(L, -2, "max_active");
    lua_pushinteger(L, (lua_Integer)resource_low_active_max());
    lua_setfield(L, -2, "low_active_max");
    lua_pushinteger(L, (lua_Integer)resource_callback_budget());
    lua_setfield(L, -2, "callback_budget");
    lua_pushinteger(L, (lua_Integer)g_resource.callback_dispatched);
    lua_setfield(L, -2, "callback_dispatched");
    lua_pushinteger(L, (lua_Integer)g_resource.render_submitted);
    lua_setfield(L, -2, "render_submitted");
    lua_pushinteger(L, (lua_Integer)g_resource.render_cancelled);
    lua_setfield(L, -2, "render_cancelled");
    lua_pushinteger(L, (lua_Integer)g_resource.native_ready);
    lua_setfield(L, -2, "native_ready");
    lua_pushinteger(L, (lua_Integer)g_resource.native_failed);
    lua_setfield(L, -2, "native_failed");
    lua_pushinteger(L, (lua_Integer)g_resource.native_queue_full);
    lua_setfield(L, -2, "native_queue_full");
    lua_pushinteger(L, (lua_Integer)g_resource.native_retried);
    lua_setfield(L, -2, "native_retried");
    lua_pushinteger(L, (lua_Integer)g_resource.shell_submitted);
    lua_setfield(L, -2, "shell_submitted");
    lua_pushinteger(L, (lua_Integer)g_resource.shell_ready);
    lua_setfield(L, -2, "shell_ready");
    lua_pushinteger(L, (lua_Integer)g_resource.shell_failed);
    lua_setfield(L, -2, "shell_failed");
    lua_pushinteger(L, (lua_Integer)g_resource.shell_warm_ready);
    lua_setfield(L, -2, "shell_warm_ready");
    lua_pushinteger(L, (lua_Integer)g_resource.shell_warm_failed);
    lua_setfield(L, -2, "shell_warm_failed");
    lua_pushinteger(L, (lua_Integer)g_resource.frame_submitted);
    lua_setfield(L, -2, "frame_submitted");
    lua_pushinteger(L, (lua_Integer)g_resource.frame_ready);
    lua_setfield(L, -2, "frame_ready");
    lua_pushinteger(L, (lua_Integer)g_resource.frame_failed);
    lua_setfield(L, -2, "frame_failed");
    lua_pushinteger(L, (lua_Integer)tokens_alive);
    lua_setfield(L, -2, "tokens_alive");
    lua_pushinteger(L, (lua_Integer)tokens_terminal);
    lua_setfield(L, -2, "tokens_terminal");
    lua_pushinteger(L, (lua_Integer)g_resource.tokens_freed);
    lua_setfield(L, -2, "tokens_freed");
    lua_pushinteger(L, (lua_Integer)events_pending);
    lua_setfield(L, -2, "events_pending");
    lua_pushinteger(L, (lua_Integer)g_resource.worker_active);
    lua_setfield(L, -2, "worker_active");
    lua_pushinteger(L, (lua_Integer)g_resource.worker_thread_count);
    lua_setfield(L, -2, "worker_threads");
    lua_pushinteger(L, (lua_Integer)worker_pending);
    lua_setfield(L, -2, "worker_pending");
    lua_pushinteger(L, (lua_Integer)worker_done);
    lua_setfield(L, -2, "worker_done");
    lua_pushinteger(L, (lua_Integer)g_resource.poll_tick);
    lua_setfield(L, -2, "poll_tick");
    lua_pushinteger(L, (lua_Integer)g_resource.native_cursor_id);
    lua_setfield(L, -2, "native_cursor");
    lua_pushboolean(L, g_resource.render_submitted > 0);
    lua_setfield(L, -2, "render_native");
    lua_pushboolean(L, g_resource.native_ready > 0 || g_resource.native_retried > 0);
    lua_setfield(L, -2, "native_decode");
    return 1;
}

static int resource_lua_stats(lua_State* L)
{
    return resource_push_stats(L);
}

static int resource_lua_peek_stats(lua_State* L)
{
    return resource_push_stats(L);
}

static int resource_lua_update(lua_State* L)
{
    unsigned int max_ops = 8;
    if (lua_isnumber(L, 1))
    {
        lua_Number n = lua_tonumber(L, 1);
        if (n >= 1)
            max_ops = (unsigned int)n;
    }
    resource_update_all(L, max_ops);
    resource_drop_update_events();
    resource_reap_finished(L);
    lua_pushboolean(L, 1);
    return 1;
}

static int resource_lua_get_ready(lua_State* L)
{
    ResourceToken* token = NULL;
    if (lua_isnumber(L, 1))
        token = resource_find_token((unsigned int)lua_tointeger(L, 1));
    else if (lua_isstring(L, 1))
        token = resource_find_by_resource_id(lua_tostring(L, 1));
    else if (lua_istable(L, 1))
        return resource_lua_query(L);

    resource_update_shell_results(8);

    if (token && token->native_kind != RESOURCE_NATIVE_NONE
        && token->native_kind != RESOURCE_NATIVE_TCP
        && token->status == RESOURCE_STATUS_QUEUED
        && !resource_is_shell_type(token->resource_type))
    {
        resource_native_poll_frame(token, 2);
        if (resource_native_is_frame_ready(token))
            resource_finish_native_ready(token);
    }

    if (!token || token->status != RESOURCE_STATUS_READY)
    {
        lua_pushnil(L);
        return 1;
    }
    if (resource_is_tcp_shell(token) && token->result_native)
    {
        TCP_UserData* tcp = (TCP_UserData*)token->result_native;
        int nret;
        token->result_native = NULL;
        token->result_ready = 0;
        token->result_success = 0;
        nret = TCP_NativePushParsed(L, tcp);
        resource_drop_events(token->id);
        resource_reap_finished(L);
        return nret;
    }
    resource_push_token_table(L, token);
    resource_drop_events(token->id);
    resource_reap_finished(L);
    return 1;
}

static int resource_lua_config(lua_State* L)
{
    if (lua_istable(L, 1))
    {
        int max_active = resource_table_int(L, 1, "max_active",
            resource_table_int(L, 1, "\xe6\x9c\x80\xe5\xa4\xa7\xe5\xb9\xb6\xe5\x8f\x91", (int)resource_max_active()));
        int low_active = resource_table_int(L, 1, "low_active_max",
            resource_table_int(L, 1, "\xe4\xbd\x8e\xe4\xbc\x98\xe5\x85\x88\xe7\xba\xa7\xe6\x9c\x80\xe5\xa4\xa7\xe6\xb4\xbb\xe8\xb7\x83", (int)resource_low_active_max()));
        int callback_budget = resource_table_int(L, 1, "callback_budget",
            resource_table_int(L, 1, "\xe5\x9b\x9e\xe8\xb0\x83\xe9\xa2\x84\xe7\xae\x97", (int)resource_callback_budget()));
        if (max_active > 0)
            g_resource.max_active = (unsigned int)max_active;
        if (low_active >= 0)
            g_resource.low_active_max = (unsigned int)low_active;
        if (callback_budget > 0)
            g_resource.callback_budget = (unsigned int)callback_budget;
    }
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, (lua_Integer)resource_max_active());
    lua_setfield(L, -2, "max_active");
    lua_pushinteger(L, (lua_Integer)resource_low_active_max());
    lua_setfield(L, -2, "low_active_max");
    lua_pushinteger(L, (lua_Integer)resource_callback_budget());
    lua_setfield(L, -2, "callback_budget");
    return 1;
}

static int resource_lua_gc(lua_State* L)
{
    ResourceToken* token = g_resource.tokens;
    ResourceEvent* ev = g_resource.events_head;
    ResourceToken* next_token;
    ResourceEvent* next_event;
    unsigned int i;
    (void)L;
    if (g_resource.worker_mutex)
    {
        SDL_LockMutex(g_resource.worker_mutex);
        g_resource.worker_stop = 1;
        if (g_resource.worker_cond)
        {
            for (i = 0; i < g_resource.worker_thread_count && i < RESOURCE_WORKER_THREADS; i++)
                SDL_CondSignal(g_resource.worker_cond);
        }
        SDL_UnlockMutex(g_resource.worker_mutex);
    }
    for (i = 0; i < g_resource.worker_thread_count && i < RESOURCE_WORKER_THREADS; i++)
    {
        if (g_resource.worker_threads[i])
        {
            SDL_WaitThread(g_resource.worker_threads[i], NULL);
            g_resource.worker_threads[i] = NULL;
        }
    }
    g_resource.worker_thread_count = 0;
    resource_worker_job_list_free(g_resource.worker_head);
    resource_worker_job_list_free(g_resource.worker_done_head);
    g_resource.worker_head = NULL;
    g_resource.worker_tail = NULL;
    g_resource.worker_done_head = NULL;
    g_resource.worker_done_tail = NULL;
    if (g_resource.worker_cond)
    {
        SDL_DestroyCond(g_resource.worker_cond);
        g_resource.worker_cond = NULL;
    }
    if (g_resource.worker_mutex)
    {
        SDL_DestroyMutex(g_resource.worker_mutex);
        g_resource.worker_mutex = NULL;
    }
    while (token)
    {
        next_token = token->next;
        resource_free_token(L, token);
        token = next_token;
    }
    while (ev)
    {
        next_event = ev->next;
        free(ev->resource_id);
        free(ev->message);
        free(ev);
        ev = next_event;
    }
    memset(&g_resource, 0, sizeof(g_resource));
    return 0;
}

static const luaL_Reg RESOURCE_FUNCS[] = {
    {"query", resource_lua_query},
    {"request", resource_lua_request},
    {"submit", resource_lua_submit},
    {"get_ready", resource_lua_get_ready},
    {"preload", resource_lua_preload},
    {"poll", resource_lua_poll},
    {"update", resource_lua_update},
    {"cancel", resource_lua_cancel},
    {"cancel_scope", resource_lua_cancel_scope},
    {"cancel_all", resource_lua_cancel_all},
    {"config", resource_lua_config},
    {"stats", resource_lua_stats},
    {"peek_stats", resource_lua_peek_stats},
    {"__gc", resource_lua_gc},
    {NULL, NULL},
};

MYGXY_API int luaopen_mygxy_resource(lua_State* L)
{
    lua_createtable(L, 0, 8);
    luaL_setfuncs(L, RESOURCE_FUNCS, 0);
    return 1;
}
