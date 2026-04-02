# mygxy 模块地图加载多线程重构计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不改变 Lua 层接口调用方式的前提下，将 `mygxy` 模块的地图解析 (`map.c`) 重构为真正的多线程无锁并发加载，并修复已知的内存泄漏和崩溃隐患。

**Architecture:** 
1. 移除 `SDL_AddTimer`，引入基于 `SDL_CreateThread` 的固定数量的 Worker 线程池。
2. 引入任务队列（Request Queue）和完成队列（Result Queue）。主线程将请求放入请求队列，Worker 线程争抢处理；处理完毕后放入完成队列，主线程在 `LUA_Run` 中轮询并触发 Lua 回调。
3. 为每个 Worker 线程分配独立的解码缓冲区和独立的文件读写句柄（指向同一个内存中的只读 `filebuf`），实现完全无锁的并发 IO 和图像解码。

**Tech Stack:** C, SDL2, Lua

---

### Task 1: 定义线程池与队列数据结构

**Files:**
- Modify: `deps/Sources/grr/mygxy/map.h`

- [ ] **Step 1: 修改 `MAP_UserData` 结构体以支持线程池和双队列**

```c
// 在 map.h 中修改 MAP_UserData 结构体
typedef struct MAP_Task MAP_Task;
struct MAP_Task {
    Uint32 type;
    Uint32 id;
    void* data;
    MAP_UserData* ud;
    int cb_ref;
    MAP_Task* next;
};

typedef struct {
    SDL_Thread* thread;
    void* mem0; // 线程专属缓冲区 0
    size_t mem0_size;
    void* mem1; // 线程专属缓冲区 1
    size_t mem1_size;
    SDL_RWops* rw; // 线程专属独立文件句柄（指向共享只读内存）
} MAP_Worker;

typedef struct
{
    Uint32 flag;
    Uint32 mode;
    SDL_RWops* file; // 主线程使用的句柄

    void* filebuf;
    size_t filebuf_size;

    Uint32 rownum;
    Uint32 colnum;
    Uint32 mapnum;
    Uint32* maplist;

    Uint32 masknum;
    Uint32* masklist;

    MAP_Mem mem[2]; // 仅主线程使用
    MAP_Data* map;

    MAP_Mem jpeh;

    // 线程池与并发相关
    MAP_Worker* workers;
    int num_workers;
    
    MAP_Task* req_queue_head;
    MAP_Task* req_queue_tail;
    MAP_Task* res_queue_head;
    MAP_Task* res_queue_tail;

    SDL_mutex* req_mutex;
    SDL_cond* req_cond;
    SDL_mutex* res_mutex;

    int closing;
    int active_tasks;
} MAP_UserData;
```

- [ ] **Step 2: Commit**

```bash
git add deps/Sources/grr/mygxy/map.h
git commit -m "refactor: define thread pool and queue structures in map.h"
```

### Task 2: 实现线程专属缓冲与无锁 RWops

**Files:**
- Modify: `deps/Sources/grr/mygxy/map.c`

- [ ] **Step 1: 修改 `_getmem` 支持传入线程私有状态，修改 `_getmapsf` 接收私有状态**

为了不改变太多签名，我们定义一个上下文结构体传递给解析函数。

```c
typedef struct {
    void** p_mem0;
    size_t* p_mem0_size;
    void** p_mem1;
    size_t* p_mem1_size;
    SDL_RWops* rw;
} MAP_DecodeContext;

// 修改 _getmem 签名
static void* _getmem_ctx(void** mem_ptr, size_t* mem_size, size_t size)
{
    if (*mem_size >= size)
        return *mem_ptr;
    *mem_ptr = SDL_realloc(*mem_ptr, size);
    *mem_size = size;
    if (*mem_ptr == NULL) {
        SDL_OutOfMemory();
        *mem_size = 0;
    }
    return *mem_ptr;
}
```

- [ ] **Step 2: 修改 `_getmapsf` 和 `_getmasksinfo` 使用 `MAP_DecodeContext`**

在 `map.c` 中将所有 `_getmem(&ud->mem[x], size)` 替换为 `_getmem_ctx(ctx->p_memX, ctx->p_memX_size, size)`。
将所有 `SDL_RWxxx(ud->file, ...)` 替换为 `SDL_RWxxx(ctx->rw, ...)`。

- [ ] **Step 3: 修复 JPEG 解析边界漏洞**

在 `_fixjpeg` 中加入边界检查：
```c
// 替换 _fixjpeg 中原有的 while 条件和内部循环，加入对 insize 的严格判断，防止越界读取
```

- [ ] **Step 4: Commit**

```bash
git add deps/Sources/grr/mygxy/map.c
git commit -m "refactor: introduce thread-local decode context and fix jpeg bounds"
```

### Task 3: 实现 Worker 线程与双向队列

**Files:**
- Modify: `deps/Sources/grr/mygxy/map.c`

- [ ] **Step 1: 实现队列操作函数**

```c
static void PushReqQueue(MAP_UserData* ud, MAP_Task* task) {
    SDL_LockMutex(ud->req_mutex);
    task->next = NULL;
    if (ud->req_queue_tail) {
        ud->req_queue_tail->next = task;
    } else {
        ud->req_queue_head = task;
    }
    ud->req_queue_tail = task;
    SDL_CondSignal(ud->req_cond);
    SDL_UnlockMutex(ud->req_mutex);
}

static MAP_Task* PopReqQueue(MAP_UserData* ud) {
    MAP_Task* task = ud->req_queue_head;
    if (task) {
        ud->req_queue_head = task->next;
        if (!ud->req_queue_head) ud->req_queue_tail = NULL;
    }
    return task;
}

static void PushResQueue(MAP_UserData* ud, MAP_Task* task) {
    SDL_LockMutex(ud->res_mutex);
    task->next = NULL;
    if (ud->res_queue_tail) {
        ud->res_queue_tail->next = task;
    } else {
        ud->res_queue_head = task;
    }
    ud->res_queue_tail = task;
    SDL_UnlockMutex(ud->res_mutex);
}
```

- [ ] **Step 2: 实现 Worker 线程主循环**

```c
static int SDLCALL WorkerThreadMain(void* data) {
    MAP_Worker* worker = (MAP_Worker*)data;
    MAP_UserData* ud = (MAP_UserData*)worker->thread_data; // 需想办法传递 ud，建议将 ud 存入 worker 结构体

    MAP_DecodeContext ctx;
    ctx.p_mem0 = &worker->mem0;
    ctx.p_mem0_size = &worker->mem0_size;
    ctx.p_mem1 = &worker->mem1;
    ctx.p_mem1_size = &worker->mem1_size;
    ctx.rw = worker->rw;

    while (1) {
        SDL_LockMutex(ud->req_mutex);
        while (!ud->closing && ud->req_queue_head == NULL) {
            SDL_CondWait(ud->req_cond, ud->req_mutex);
        }
        if (ud->closing && ud->req_queue_head == NULL) {
            SDL_UnlockMutex(ud->req_mutex);
            break;
        }
        MAP_Task* task = PopReqQueue(ud);
        SDL_UnlockMutex(ud->req_mutex);

        if (task) {
            // 执行真正的解码任务，传入 ctx
            if (task->type == TIME_TYPE_MAP) {
                MAP_Data* map = (MAP_Data*)task->data;
                map->sf = _getmapsf_ctx(ud, task->id, &ctx);
                _getmasksinfo_ctx(ud, task->id, &map->mask, &map->masknum, &ctx);
            } else if (task->type == TIME_TYPE_MASK) {
                MASK_Data* mask = (MASK_Data*)task->data;
                if (ud->mode == 0x9527) _getmasksf2_ctx(ud, mask->id, mask, &ctx);
                else _getmasksf_ctx(ud, mask->id, mask, &ctx);
            }
            
            PushResQueue(ud, task);
        }
    }
    return 0;
}
```

- [ ] **Step 3: Commit**

```bash
git add deps/Sources/grr/mygxy/map.c
git commit -m "feat: implement worker thread loop and task queues"
```

### Task 4: 替换 Timer 与生命周期管理

**Files:**
- Modify: `deps/Sources/grr/mygxy/map.c`

- [ ] **Step 1: 在 `MAP_NEW` 中初始化线程池和双队列**

```c
    ud->req_mutex = SDL_CreateMutex();
    ud->req_cond = SDL_CreateCond();
    ud->res_mutex = SDL_CreateMutex();
    // 移除重复的 cond 创建
    
    ud->num_workers = 4; // 根据需要设置
    ud->workers = SDL_calloc(ud->num_workers, sizeof(MAP_Worker));
    for (int i = 0; i < ud->num_workers; i++) {
        ud->workers[i].rw = MAP_RWFromOwnedMem(ud->filebuf, ud->filebuf_size);
        // ... 启动线程 ...
    }
```

- [ ] **Step 2: 修改 `LUA_GetMap` 和 `LUA_GetMask` 抛入请求队列**

将 `SDL_AddTimer` 替换为 `PushReqQueue(ud, task)`。

- [ ] **Step 3: 修改 `LUA_Run` 从完成队列获取结果**

替换原有的 `ud->list` 轮询逻辑，改为获取 `res_queue`，并安全地使用 `lua_pcall` 触发回调，防止 Lua 异常导致 C 语言 `longjmp` 泄漏。

- [ ] **Step 4: 修改 `LUA_GC` 实现优雅退出**

```c
    SDL_LockMutex(ud->req_mutex);
    ud->closing = 1;
    SDL_CondBroadcast(ud->req_cond);
    SDL_UnlockMutex(ud->req_mutex);

    for (int i = 0; i < ud->num_workers; i++) {
        SDL_WaitThread(ud->workers[i].thread, NULL);
        if (ud->workers[i].rw) ud->workers[i].rw->close(ud->workers[i].rw);
        SDL_free(ud->workers[i].mem0);
        SDL_free(ud->workers[i].mem1);
    }
    SDL_free(ud->workers);
```

- [ ] **Step 5: 修复 MAP_NEW 的乘法溢出漏洞**

在计算 `ud->mapnum = ud->rownum * ud->colnum;` 前检查溢出。

- [ ] **Step 6: Commit**

```bash
git add deps/Sources/grr/mygxy/map.c
git commit -m "refactor: wire up thread pool, replace timer, and fix leaks in GC"
```
