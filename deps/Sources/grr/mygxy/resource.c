/*
 * resource.c - native resource hot-path queue facade.
 *
 * Lua decides policy and passes a normalized request table. This module owns
 * token allocation, priority queues, cancel state, poll events, download
 * dispatch and stats. HTTP transfer is performed by the existing native
 * ghv.download binding; Lua no longer maintains the hot download queue.
 */
#include "lua_proxy.h"

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
    char* degrade;
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
    ResourceStatus status;
    char* resource_id;
    char* message;
    struct ResourceEvent* next;
} ResourceEvent;

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
    ResourceToken* tokens;
    ResourceEvent* events_head;
    ResourceEvent* events_tail;
} ResourceState;

static ResourceState g_resource = {0};

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
    resource_free_urls(token->urls, token->url_count);
    free(token->save_path);
    free(token->dedupe_key);
    free(token->expected_md5);
    free(token->resource_id);
    free(token->resource_type);
    free(token->scene);
    free(token->degrade);
    free(token);
}

static void resource_push_event(unsigned int token_id, ResourceStatus status, const char* resource_id, const char* message)
{
    ResourceEvent* ev = (ResourceEvent*)calloc(1, sizeof(ResourceEvent));
    if (!ev)
        return;
    ev->token_id = token_id;
    ev->status = status;
    ev->resource_id = resource_strdup(resource_id);
    ev->message = resource_strdup(message);
    if (g_resource.events_tail)
        g_resource.events_tail->next = ev;
    else
        g_resource.events_head = ev;
    g_resource.events_tail = ev;
}

static void resource_set_status(ResourceToken* token, ResourceStatus status, const char* message)
{
    ResourceStatus old_status;
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
    resource_push_event(token->id, status, token->resource_id, message);
}

static unsigned int resource_max_active(void)
{
    return g_resource.max_active ? g_resource.max_active : RESOURCE_DEFAULT_MAX_ACTIVE;
}

static unsigned int resource_low_active_max(void)
{
    return g_resource.low_active_max ? g_resource.low_active_max : RESOURCE_DEFAULT_LOW_ACTIVE;
}

static void resource_count_active(unsigned int* out_total, unsigned int* out_low)
{
    ResourceToken* p = g_resource.tokens;
    unsigned int total = 0;
    unsigned int low = 0;
    while (p)
    {
        if (!p->cancelled && p->download_ref != LUA_NOREF)
        {
            total++;
            if (p->priority == RESOURCE_PRIORITY_PREHEAT)
                low++;
        }
        p = p->next;
    }
    if (out_total) *out_total = total;
    if (out_low) *out_low = low;
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
    resource_set_status(token, RESOURCE_STATUS_READY, "ready");
    resource_dispatch_callbacks(L, token, 1, data, len, NULL);
    if (data_idx)
        lua_pop(L, 1);
}

static void resource_finish_failed(lua_State* L, ResourceToken* token, const char* message)
{
    if (!token)
        return;
    resource_unref_download(L, token);
    resource_set_status(token, RESOURCE_STATUS_FAILED, message ? message : "download failed");
    resource_dispatch_callbacks(L, token, 0, NULL, 0, message ? message : "download failed");
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
        resource_unref_download(L, token);
        if (token->current_url < token->url_count)
        {
            if (!resource_start_download(L, token))
                resource_finish_failed(L, token, "download start failed");
        }
        else
        {
            resource_finish_failed(L, token, "all urls failed");
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
            if ((want_preheat == RESOURCE_PRIORITY_PREHEAT) == (p->priority == RESOURCE_PRIORITY_PREHEAT))
            {
                if (!best || p->priority > best->priority
                    || (p->priority == best->priority && p->id < best->id))
                    best = p;
            }
        }
        p = p->next;
    }
    return best;
}

static void resource_update_downloads(lua_State* L)
{
    ResourceToken* p = g_resource.tokens;
    unsigned int active = 0;
    unsigned int low_active = 0;
    while (p)
    {
        ResourceToken* next = p->next;
        if (p->cancelled && p->download_ref != LUA_NOREF)
            resource_cancel_download(L, p);
        else
            resource_check_active_download(L, p);
        p = next;
    }

    resource_count_active(&active, &low_active);
    while (active < resource_max_active())
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
    }

    resource_count_active(&active, &low_active);
    while (active < resource_max_active() && low_active < resource_low_active_max())
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

    p = g_resource.tokens;
    while (p)
    {
        if (!p->callbacks_done && resource_is_terminal(p->status))
        {
            const char* message = resource_status_name(p->status);
            resource_dispatch_callbacks(L, p, 0, NULL, 0, message);
        }
        p = p->next;
    }
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

    parsed_priority = resource_parse_priority(priority, queue_priority);
    dedupe_key = resource_make_dedupe_key(L, req_idx, urls, url_count, resource_id);
    existing = resource_find_by_dedupe_key(dedupe_key);
    if (existing)
    {
        if (parsed_priority > existing->priority)
            existing->priority = parsed_priority;
        if (callback_ref != LUA_NOREF && !resource_add_callback_ref(existing, callback_ref))
        {
            luaL_unref(L, LUA_REGISTRYINDEX, callback_ref);
            free(resource_id);
            free(resource_type);
            free(scene);
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
        free(priority);
        free(queue_priority);
        free(degrade);
        free(save_path);
        free(dedupe_key);
        free(expected_md5);
        return NULL;
    }
    token->id = ++g_resource.next_token_id;
    token->priority = parsed_priority;
    token->allow_cold_download = resource_table_bool(L, req_idx, "allow_cold_download",
        resource_table_bool(L, req_idx, "allow_cold", 1));
    token->allow_cold_download = resource_table_bool(L, req_idx, RESOURCE_KEY_ALLOW_COLD,
        token->allow_cold_download);
    token->status = token->allow_cold_download ? RESOURCE_STATUS_QUEUED : RESOURCE_STATUS_DEGRADED;
    token->download_ref = LUA_NOREF;
    token->timeout = timeout;
    token->urls = urls;
    token->url_count = url_count;
    token->save_path = save_path;
    token->dedupe_key = dedupe_key;
    token->expected_md5 = expected_md5;
    token->resource_id = resource_id ? resource_id : resource_strdup("");
    token->resource_type = resource_type ? resource_type : resource_strdup("resource");
    token->scene = scene ? scene : resource_strdup("");
    token->degrade = degrade;
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
        resource_push_event(token->id, RESOURCE_STATUS_DEGRADED, token->resource_id, "cold download disabled");
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
    resource_push_token_table(L, token);
    return 1;
}

static int resource_lua_preload(lua_State* L)
{
    lua_Integer out_index = 1;
    int result_idx;
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
            resource_push_token_table(L, token);
            lua_rawseti(L, result_idx, out_index++);
        }
        lua_pop(L, 1);
    }
    return 1;
}

static int resource_lua_poll(lua_State* L)
{
    lua_Integer i = 1;
    ResourceEvent* ev = g_resource.events_head;
    ResourceEvent* next;
    resource_update_downloads(L);
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

        free(ev->resource_id);
        free(ev->message);
        free(ev);
        ev = next;
    }
    g_resource.events_head = NULL;
    g_resource.events_tail = NULL;
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
            resource_set_status(token, RESOURCE_STATUS_CANCELLED, "cancelled");
            count++;
        }
        token = token->next;
    }
    lua_pushinteger(L, (lua_Integer)count);
    return 1;
}

static int resource_lua_stats(lua_State* L)
{
    unsigned int active = 0;
    unsigned int low_active = 0;
    ResourceToken* token = g_resource.tokens;
    resource_update_downloads(L);
    while (token)
    {
        if (!token->cancelled && token->download_ref != LUA_NOREF)
        {
            active++;
            if (token->priority == RESOURCE_PRIORITY_PREHEAT)
                low_active++;
        }
        token = token->next;
    }

    lua_createtable(L, 0, 12);
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
    lua_pushinteger(L, (lua_Integer)resource_max_active());
    lua_setfield(L, -2, "max_active");
    lua_pushinteger(L, (lua_Integer)resource_low_active_max());
    lua_setfield(L, -2, "low_active_max");
    return 1;
}

static int resource_lua_update(lua_State* L)
{
    resource_update_downloads(L);
    lua_pushboolean(L, 1);
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
        if (max_active > 0)
            g_resource.max_active = (unsigned int)max_active;
        if (low_active >= 0)
            g_resource.low_active_max = (unsigned int)low_active;
    }
    lua_createtable(L, 0, 2);
    lua_pushinteger(L, (lua_Integer)resource_max_active());
    lua_setfield(L, -2, "max_active");
    lua_pushinteger(L, (lua_Integer)resource_low_active_max());
    lua_setfield(L, -2, "low_active_max");
    return 1;
}

static int resource_lua_gc(lua_State* L)
{
    ResourceToken* token = g_resource.tokens;
    ResourceEvent* ev = g_resource.events_head;
    ResourceToken* next_token;
    ResourceEvent* next_event;
    (void)L;
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
    {"preload", resource_lua_preload},
    {"poll", resource_lua_poll},
    {"update", resource_lua_update},
    {"cancel", resource_lua_cancel},
    {"cancel_all", resource_lua_cancel_all},
    {"config", resource_lua_config},
    {"stats", resource_lua_stats},
    {"__gc", resource_lua_gc},
    {NULL, NULL},
};

MYGXY_API int luaopen_mygxy_resource(lua_State* L)
{
    lua_createtable(L, 0, 8);
    luaL_setfuncs(L, RESOURCE_FUNCS, 0);
    return 1;
}
