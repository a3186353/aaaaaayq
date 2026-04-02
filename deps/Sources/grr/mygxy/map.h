#pragma once
#include "lua_proxy.h"
#include "sdl_proxy.h"
#include "lua_proxy.h"
#include "sdl_proxy.h"

#include "SDL_timer.h"

#include "SDL_rect.h"
#include "SDL_list.h"

#include "ujpeg/ujpeg.h"
#define MAP_NAME "xyq_map"

typedef struct
{
    void* mem;
    size_t size;
} MAP_Mem;

typedef struct
{
    Uint32 flag;   //文件标志M1.0
    Uint32 width;  //地图宽
    Uint32 height; //地图高
} MAP_Header;

typedef struct
{
    Uint32 flag; //块类型JPG2，PNG1，WEBP，JPEG，CELL
    Uint32 size;
} MAP_BlockInfo;

typedef struct
{
    SDL_Rect rect;
    Uint32 offset;
    Uint32 size;
    Uint32 head;
    Uint32 mode;
} MAP_MaskInfo;

typedef struct
{
    Uint32 id;
    SDL_Surface* sf;
    MAP_MaskInfo* mask;
    Uint32 masknum;
    int loading;
    Uint32 lru_prev;
    Uint32 lru_next;
    int in_lru;
    int lua_ref;
} MAP_Data;

typedef struct
{
    Uint32 id;
    SDL_Surface* sf;
    MAP_MaskInfo info; //遮罩信息
} MASK_Data;

typedef struct MAP_UserData MAP_UserData;

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
    MAP_UserData* ud;
} MAP_Worker;

struct MAP_UserData
{
    Uint32 flag;
    Uint32 mode;
    SDL_RWops* file; // 主线程使用的句柄

    void* filebuf;
    size_t filebuf_size;

    Uint32 rownum; //行
    Uint32 colnum; //列
    Uint32 mapnum; //行*列
    Uint32* maplist;  //地表偏移

    Uint32 masknum;// M1.0
    Uint32* masklist; //M1.0 遮罩偏移 

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

    Uint32 lru_head;
    Uint32 lru_tail;
    int lru_size;
    int lru_limit;

    SDL_mutex* req_mutex;
    SDL_cond* req_cond;
    SDL_mutex* res_mutex;

    int closing;
    int active_tasks;
};

/* TIME_Data.type 类型标识常量
 * 替代多字符字符常量 'map'/'mask'，消除 Clang -Wmultichar 警告
 * 数值与 MSVC 多字符常量的编译结果一致 */
#define TIME_TYPE_MAP   0x6D6170     /* 'map'  */
#define TIME_TYPE_MASK  0x6D61736B   /* 'mask' */

/* MAP_Header.flag 文件标志 */
#define MAP_FLAG_M10    0x4D312E30   /* 'M1.0' */
#define MAP_FLAG_MAPX   0x4D415058   /* 'MAPX' */

/* MAP_BlockInfo.flag 块类型标志 */
#define MAP_BLOCK_JPG2  0x4A504732   /* 'JPG2' */
#define MAP_BLOCK_PNG1  0x504E4731   /* 'PNG1' */
#define MAP_BLOCK_WEBP  0x57454250   /* 'WEBP' */
#define MAP_BLOCK_0PNG  0x00504E47   /* '\0PNG' */
#define MAP_BLOCK_JPEG  0x4A504547   /* 'JPEG' */
#define MAP_BLOCK_MASK  0x4D41534B   /* 'MASK' */
#define MAP_BLOCK_CELL  0x43454C4C   /* 'CELL' */

typedef struct
{
    Uint32 type;
    Uint32 id;
    void* data;
    MAP_UserData* ud;
    int cb_ref;
} TIME_Data;
