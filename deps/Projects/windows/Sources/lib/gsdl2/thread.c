#include "gge.h"
#include "SDL_thread.h"

/* ============ Thread message queue ============ */

typedef struct _ThreadMsg {
	int id;
	char* data;
	size_t len;
	struct _ThreadMsg* next;
} ThreadMsg;

typedef struct _MsgQueue {
	ThreadMsg* head;
	ThreadMsg* tail;
	SDL_mutex* mutex;
	SDL_cond* cond;
} MsgQueue;

static void MsgQueue_Init(MsgQueue* q) {
	q->head = NULL;
	q->tail = NULL;
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}

static void MsgQueue_Destroy(MsgQueue* q) {
	ThreadMsg* m = q->head;
	while (m) {
		ThreadMsg* next = m->next;
		if (m->data) SDL_free(m->data);
		SDL_free(m);
		m = next;
	}
	if (q->mutex) SDL_DestroyMutex(q->mutex);
	if (q->cond) SDL_DestroyCond(q->cond);
	q->head = q->tail = NULL;
	q->mutex = NULL;
	q->cond = NULL;
}

static void MsgQueue_Push(MsgQueue* q, int id, const char* data, size_t len) {
	ThreadMsg* m = (ThreadMsg*)SDL_malloc(sizeof(ThreadMsg));
	m->id = id;
	m->data = (char*)SDL_malloc(len);
	SDL_memcpy(m->data, data, len);
	m->len = len;
	m->next = NULL;

	SDL_LockMutex(q->mutex);
	if (q->tail) {
		q->tail->next = m;
	} else {
		q->head = m;
	}
	q->tail = m;
	SDL_CondSignal(q->cond);
	SDL_UnlockMutex(q->mutex);
}

/* Pop with blocking wait */
static ThreadMsg* MsgQueue_Pop(MsgQueue* q) {
	ThreadMsg* m;
	SDL_LockMutex(q->mutex);
	while (!q->head) {
		SDL_CondWait(q->cond, q->mutex);
	}
	m = q->head;
	q->head = m->next;
	if (!q->head) q->tail = NULL;
	SDL_UnlockMutex(q->mutex);
	return m;
}

/* ============ GGE_Thread structure ============ */

typedef struct _GGE_Thread {
	SDL_Thread* th;
	MsgQueue sendq;       /* main -> worker messages */
	MsgQueue recvq;       /* worker -> main results */
	SDL_atomic_t msgid;   /* auto-increment message id */
	Uint32 event_type;    /* SDL user event type for callback */
	Sint32 event_code;    /* SDL user event code */
	int quit;             /* flag to tell worker to exit */
	/* Init data (kept alive until worker reads it) */
	char* init_script;    /* the Lua init code (returned function) */
	char* ggecore;        /* copy of ggecore bytecode */
	size_t ggecore_len;
	char* entry;          /* entry string */
} GGE_Thread;

/* ============ Worker thread function ============ */

static int ThreadFunctionWithQueue(void* data)
{
	GGE_Thread* ud = (GGE_Thread*)data;
	lua_State* L;
	int has_func = 0;

	L = luaL_newstate();
	luaL_openlibs(L);

	/* Load ggecore to initialize engine environment */
	if (ud->ggecore && ud->ggecore_len > 0) {
		if (luaL_loadbuffer(L, ud->ggecore, ud->ggecore_len, "ggelua.lua") == LUA_OK) {
			if (ud->entry && ud->entry[0]) {
				lua_pushstring(L, ud->entry);
				lua_setglobal(L, "_ggelua_entry");
			}
			lua_pcall(L, 0, 0, 0);
		} else {
			lua_pop(L, 1);
		}
	}

	/* Load the init script - it should return a handler function */
	if (ud->init_script) {
		if (luaL_dostring(L, ud->init_script) == LUA_OK) {
			if (lua_isfunction(L, -1)) {
				has_func = 1;
				/* handler function is now at top of stack */
			}
		} else {
			lua_pop(L, 1);
		}
	}

	/* Message processing loop */
	while (!ud->quit) {
		ThreadMsg* msg = MsgQueue_Pop(&ud->sendq);
		if (!msg) break;
		if (ud->quit) {
			SDL_free(msg->data);
			SDL_free(msg);
			break;
		}

		if (has_func) {
			const char* result;
			size_t result_len;

			lua_pushvalue(L, -1);  /* push the handler function */
			lua_pushlstring(L, msg->data, msg->len);  /* push the message data */
			if (lua_pcall(L, 1, 1, 0) == LUA_OK) {
				result = lua_tolstring(L, -1, &result_len);
				if (result && result_len > 0) {
					SDL_Event ev;
					SDL_memset(&ev, 0, sizeof(ev));
					ev.type = ud->event_type;
					ev.user.code = ud->event_code;
					ev.user.data1 = (void*)(intptr_t)msg->id;

					MsgQueue_Push(&ud->recvq, msg->id, result, result_len);
					SDL_PushEvent(&ev);
				}
				lua_pop(L, 1);
			} else {
				lua_pop(L, 1); /* pop error */
			}
		}

		SDL_free(msg->data);
		SDL_free(msg);
	}

	lua_close(L);
	return 0;
}

/* ============ Lua API ============ */

/*
 * SDL.CreateThread(init_script, entry)
 * init_script: Lua code string that returns a handler function
 * entry: optional entry point string for ggecore
 * Returns: SDL_Thread userdata
 */
static int LUA_CreateThread(lua_State *L)
{
	size_t init_len;
	const char* init_script = luaL_checklstring(L, 1, &init_len);
	const char* entry = luaL_optstring(L, 2, "");

	if (lua_getfield(L, LUA_REGISTRYINDEX, "ggecore") == LUA_TSTRING) {
		size_t core_len;
		const char* ggecore = lua_tolstring(L, -1, &core_len);

		GGE_Thread* ud = (GGE_Thread*)lua_newuserdata(L, sizeof(GGE_Thread));
		SDL_memset(ud, 0, sizeof(GGE_Thread));
		luaL_setmetatable(L, "SDL_Thread");

		MsgQueue_Init(&ud->sendq);
		MsgQueue_Init(&ud->recvq);
		SDL_AtomicSet(&ud->msgid, 0);
		ud->quit = 0;

		/* Copy init data so worker thread can safely access it */
		ud->init_script = (char*)SDL_malloc(init_len + 1);
		SDL_memcpy(ud->init_script, init_script, init_len + 1);

		ud->ggecore = (char*)SDL_malloc(core_len);
		SDL_memcpy(ud->ggecore, ggecore, core_len);
		ud->ggecore_len = core_len;

		ud->entry = (char*)SDL_malloc(SDL_strlen(entry) + 1);
		SDL_strlcpy(ud->entry, entry, SDL_strlen(entry) + 1);

		ud->th = SDL_CreateThread(ThreadFunctionWithQueue, NULL, (void*)ud);
		lua_remove(L, -2); /* remove ggecore string, keep userdata on top */
		return 1;
	}
	lua_pop(L, 1);
	return 0;
}

/*
 * thread:Send(event_type, event_code, data_string)
 * Sends data to the worker thread for processing.
 * Returns: message id (integer)
 */
static int LUA_Send(lua_State *L)
{
	GGE_Thread* ud = (GGE_Thread*)luaL_checkudata(L, 1, "SDL_Thread");
	Uint32 event_type = (Uint32)luaL_checkinteger(L, 2);
	Sint32 event_code = (Sint32)luaL_checkinteger(L, 3);
	size_t data_len;
	const char* data = luaL_checklstring(L, 4, &data_len);
	int id;

	if (!ud->th) return 0;

	/* Store event info for the worker to use when pushing results */
	ud->event_type = event_type;
	ud->event_code = event_code;

	id = SDL_AtomicAdd(&ud->msgid, 1) + 1;
	MsgQueue_Push(&ud->sendq, id, data, data_len);

	lua_pushinteger(L, id);
	return 1;
}

/*
 * thread:Receive(data1)
 * Retrieves the result from worker thread.
 * data1: lightuserdata from SDL user event (contains message id)
 * Returns: id, result_string
 */
static int LUA_Receive(lua_State *L)
{
	GGE_Thread* ud = (GGE_Thread*)luaL_checkudata(L, 1, "SDL_Thread");
	int target_id = (int)(intptr_t)lua_touserdata(L, 2);
	ThreadMsg* msg;
	ThreadMsg* prev = NULL;

	SDL_LockMutex(ud->recvq.mutex);
	msg = ud->recvq.head;
	while (msg) {
		if (msg->id == target_id) {
			if (prev) {
				prev->next = msg->next;
			} else {
				ud->recvq.head = msg->next;
			}
			if (msg == ud->recvq.tail) {
				ud->recvq.tail = prev;
			}
			break;
		}
		prev = msg;
		msg = msg->next;
	}
	SDL_UnlockMutex(ud->recvq.mutex);

	if (msg) {
		lua_pushinteger(L, msg->id);
		lua_pushlstring(L, msg->data, msg->len);
		SDL_free(msg->data);
		SDL_free(msg);
		return 2;
	}
	return 0;
}

static int LUA_WaitThread(lua_State *L)
{
	GGE_Thread* ud = (GGE_Thread*)luaL_checkudata(L, 1, "SDL_Thread");
	if (ud->th) {
		int status;
		ud->quit = 1;
		MsgQueue_Push(&ud->sendq, -1, "", 1);
		SDL_WaitThread(ud->th, &status);
		ud->th = NULL;
		MsgQueue_Destroy(&ud->sendq);
		MsgQueue_Destroy(&ud->recvq);
		if (ud->init_script) { SDL_free(ud->init_script); ud->init_script = NULL; }
		if (ud->ggecore) { SDL_free(ud->ggecore); ud->ggecore = NULL; }
		if (ud->entry) { SDL_free(ud->entry); ud->entry = NULL; }
		lua_pushinteger(L, status);
		return 1;
	}
	return 0;
}

static int LUA_DetachThread(lua_State *L)
{
	GGE_Thread* ud = (GGE_Thread*)luaL_checkudata(L, 1, "SDL_Thread");
	if (ud->th) {
		SDL_DetachThread(ud->th);
		ud->th = NULL;
	}
	return 0;
}

static int LUA_SetThreadPriority(lua_State *L)
{
	const char *const opts[] = {"SDL_THREAD_PRIORITY_LOW", "SDL_THREAD_PRIORITY_NORMAL", "SDL_THREAD_PRIORITY_HIGH", "SDL_THREAD_PRIORITY_TIME_CRITICAL", NULL};
	SDL_SetThreadPriority((SDL_ThreadPriority)luaL_checkoption(L, 2, NULL, opts));
	return 0;
}

static int LUA_GetThreadName(lua_State *L)
{
	GGE_Thread* ud = (GGE_Thread*)luaL_checkudata(L, 1, "SDL_Thread");
	if (ud->th) {
		lua_pushstring(L, SDL_GetThreadName(ud->th));
		return 1;
	}
	return 0;
}

static int LUA_GetThreadID(lua_State *L)
{
	GGE_Thread* ud = (GGE_Thread*)luaL_checkudata(L, 1, "SDL_Thread");
	if (ud->th) {
		lua_pushinteger(L, SDL_GetThreadID(ud->th));
		return 1;
	}
	return 0;
}

static int LUA_ThreadID(lua_State *L)
{
	lua_pushinteger(L, SDL_ThreadID());
	return 1;
}

static int LUA_Thread_GC(lua_State *L)
{
	GGE_Thread* ud = (GGE_Thread*)luaL_checkudata(L, 1, "SDL_Thread");
	if (ud->th) {
		ud->quit = 1;
		MsgQueue_Push(&ud->sendq, -1, "", 1);
		SDL_WaitThread(ud->th, NULL);
		ud->th = NULL;
	}
	/* Always clean up queues and allocated memory */
	MsgQueue_Destroy(&ud->sendq);
	MsgQueue_Destroy(&ud->recvq);
	if (ud->init_script) { SDL_free(ud->init_script); ud->init_script = NULL; }
	if (ud->ggecore) { SDL_free(ud->ggecore); ud->ggecore = NULL; }
	if (ud->entry) { SDL_free(ud->entry); ud->entry = NULL; }
	return 0;
}

static const luaL_Reg thread_funcs[] = {
    {"WaitThread"            , LUA_WaitThread}     ,
    {"DetachThread"          , LUA_DetachThread}   ,
    {"SetThreadPriority"     , LUA_SetThreadPriority},
    {"GetThreadName"         , LUA_GetThreadName}  ,
    {"GetThreadID"           , LUA_GetThreadID}    ,
    {"Send"                  , LUA_Send}           ,
    {"Receive"               , LUA_Receive}        ,
    {"__gc"                  , LUA_Thread_GC}      ,
    { NULL, NULL}
};

static const luaL_Reg sdl_funcs[] = {
    {"CreateThread"          , LUA_CreateThread}   ,
    {"ThreadID"              , LUA_ThreadID}       ,
    { NULL, NULL}
};

int bind_thread(lua_State *L)
{
    luaL_newmetatable(L,"SDL_Thread");
    luaL_setfuncs(L,thread_funcs,0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    luaL_setfuncs(L,sdl_funcs,0);
    return 0;
}
