/*
 * resource.c - native resource hot-path queue facade.
 *
 * Lua decides policy and passes a normalized request table. This module owns
 * token allocation, priority queues, cancel state, poll events and stats so the
 * future downloader/WPK cache path can be wired in without changing Lua callers.
 */
#include "lua_proxy.h"

#include <stdlib.h>
#include <string.h>

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
    char* resource_id;
    char* resource_type;
    char* scene;
    char* degrade;
    struct ResourceToken* next;
} ResourceToken;

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

static void resource_free_token(ResourceToken* token)
{
    if (!token)
        return;
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

static ResourceToken* resource_create_token(lua_State* L, int req_idx)
{
    char* resource_id;
    char* resource_type;
    char* scene;
    char* priority;
    char* queue_priority;
    char* degrade;
    ResourceToken* token;

    if (req_idx < 0)
        req_idx = lua_gettop(L) + 1 + req_idx;

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

    token = (ResourceToken*)calloc(1, sizeof(ResourceToken));
    if (!token)
    {
        free(resource_id);
        free(resource_type);
        free(scene);
        free(priority);
        free(queue_priority);
        free(degrade);
        return NULL;
    }
    token->id = ++g_resource.next_token_id;
    token->priority = resource_parse_priority(priority, queue_priority);
    token->allow_cold_download = resource_table_bool(L, req_idx, "allow_cold_download",
        resource_table_bool(L, req_idx, "allow_cold", 1));
    token->allow_cold_download = resource_table_bool(L, req_idx, RESOURCE_KEY_ALLOW_COLD,
        token->allow_cold_download);
    token->status = token->allow_cold_download ? RESOURCE_STATUS_QUEUED : RESOURCE_STATUS_DEGRADED;
    token->resource_id = resource_id ? resource_id : resource_strdup("");
    token->resource_type = resource_type ? resource_type : resource_strdup("resource");
    token->scene = scene ? scene : resource_strdup("");
    token->degrade = degrade;
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
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_newtable(L);
    result_idx = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, 1) != 0)
    {
        if (lua_istable(L, -1))
        {
            ResourceToken* token = resource_create_token(L, -1);
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
    lua_newtable(L);
    while (ev)
    {
        ResourceEvent* next = ev->next;
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

static int resource_lua_stats(lua_State* L)
{
    unsigned int active = 0;
    ResourceToken* token = g_resource.tokens;
    while (token)
    {
        if (!token->cancelled && token->status == RESOURCE_STATUS_QUEUED)
            active++;
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
    return 1;
}

static int resource_lua_gc(lua_State* L)
{
    ResourceToken* token = g_resource.tokens;
    ResourceEvent* ev = g_resource.events_head;
    (void)L;
    while (token)
    {
        ResourceToken* next = token->next;
        resource_free_token(token);
        token = next;
    }
    while (ev)
    {
        ResourceEvent* next = ev->next;
        free(ev->resource_id);
        free(ev->message);
        free(ev);
        ev = next;
    }
    memset(&g_resource, 0, sizeof(g_resource));
    return 0;
}

static const luaL_Reg RESOURCE_FUNCS[] = {
    {"query", resource_lua_query},
    {"request", resource_lua_request},
    {"preload", resource_lua_preload},
    {"poll", resource_lua_poll},
    {"cancel", resource_lua_cancel},
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
