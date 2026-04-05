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
} MAP_Data;

typedef struct
{
    Uint32 id;
    SDL_Surface* sf;
    MAP_MaskInfo info; //遮罩信息
} MASK_Data;

typedef struct
{
    Uint32 flag;
    Uint32 mode;
    SDL_RWops* file;

    void* filebuf;
    size_t filebuf_size;

    Uint32 rownum; //行
    Uint32 colnum; //列
    Uint32 mapnum; //行*列
    Uint32* maplist;  //地表偏移

    Uint32 masknum;// M1.0
    Uint32* masklist; //M1.0 遮罩偏移 

    MAP_Mem mem[2];
    MAP_Data* map;   //缓存

    MAP_Mem jpeh; //MAPX
    SDL_ListNode* list;
    SDL_mutex* mutex;
    SDL_cond* cond;

    int closing;
    int active_tasks;
}MAP_UserData;

/* TIME_Data.type 类型标识常量
 * 替代多字符字符常量 'map'/'mask'，消除 Clang -Wmultichar 警告
 * 数值与 MSVC 多字符常量的编译结果一致 */
#define TIME_TYPE_MAP   0x6D6170     /* 'map'  */
#define TIME_TYPE_MASK  0x6D61736B   /* 'mask' */
#define TIME_TYPE_MAPFULL 0x46554C4C   /* 'FULL' */

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
    MAP_Mem mem[2];  /* 异步任务独立缓冲区，不复用 ud->mem[] */
} TIME_Data;

typedef struct
{
    MAP_Data* map;
    SDL_Surface** mask_sfs;
} MAPFULL_Data;
