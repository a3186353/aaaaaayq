#pragma once
#include "lua_proxy.h"
#include "sdl_proxy.h"

#include "SDL_timer.h"

#include "SDL_rect.h"
#include "SDL_list.h"

#include "ujpeg/ujpeg.h"

/* ==========================================================================
 * iOS 隔离 malloc zone —— 防止 nanov2 堆腐蚀
 * 后台 JPEG 解码线程的碎片化 malloc/free 会破坏与 Metal 渲染管线
 * 共享的 nano zone guard page，导致 nanov2_guard_corruption_detected。
 * 将 mygxy 模块的所有内存分配隔离到独立 zone。
 * 非 Apple 平台直接转发到 SDL_malloc，零开销。
 * ========================================================================== */
#if defined(__APPLE__) && !defined(_WIN32)
/* 在 map.c 中定义 */
void* _map_zone_malloc(size_t sz);
void* _map_zone_calloc(size_t count, size_t sz);
void* _map_zone_realloc(void* p, size_t sz);
void  _map_zone_free(void* p);

#define MAP_MALLOC(sz)         _map_zone_malloc(sz)
#define MAP_CALLOC(c,sz)       _map_zone_calloc(c,sz)
#define MAP_REALLOC(p,sz)      _map_zone_realloc(p,sz)
#define MAP_FREE(p)            _map_zone_free(p)

#else
#define MAP_MALLOC(sz)         SDL_malloc(sz)
#define MAP_CALLOC(c,sz)       SDL_calloc(c,sz)
#define MAP_REALLOC(p,sz)      SDL_realloc(p,sz)
#define MAP_FREE(p)            SDL_free(p)
#endif

#define MAP_NAME "xyq_map"

/* 后台线程安全的像素缓冲区（不依赖任何 SDL Surface API）
 * 后台线程只产出此结构，主线程消费时才创建 SDL_Surface */
typedef struct {
    Uint32* pixels;   /* ARGB8888 像素数据，SDL_malloc 分配 */
    int     width;
    int     height;
} MAP_RawPixels;

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
    SDL_Surface* sf;       /* 主线程缓存（同步路径） */
    Uint8* brig;           /* GIRB 解压后 2400 字节（40x60x1），无则 NULL */
    MAP_MaskInfo* mask;    /* 主线程消费后的遮罩信息 */
    Uint32 masknum;
    int loading;
} MAP_Data;

typedef struct
{
    Uint32 id;
    SDL_Surface* sf;       /* 主线程（同步路径） */
    MAP_RawPixels raw;     /* 后台线程输出（异步路径） */
    MAP_MaskInfo info;     /* 遮罩信息 */
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
#define MAP_BLOCK_GIRB  0x42524947   /* 'GIRB' 地表明暗格（LZO→2400） */

typedef struct
{
    Uint32 type;
    Uint32 id;
    void* data;
    MAP_UserData* ud;
    int cb_ref;
    MAP_Mem mem[2];        /* 异步任务独立缓冲区，不复用 ud->mem[] */
    /* ---- 异步解码结果（Timer线程写入，主线程消费） ---- */
    MAP_RawPixels result_raw;       /* 地表裸像素 */
    Uint8* result_brig;             /* GIRB 解压缓冲 2400 字节，无则 NULL */
    MAP_MaskInfo* result_mask;      /* 遮罩信息数组 */
    Uint32 result_masknum;          /* 遮罩数量 */
} TIME_Data;

typedef struct
{
    MAP_Data* map;
    MAP_RawPixels* mask_raws;  /* 后台线程产出的遮罩裸像素 */
    Uint32 masknum;            /* 保存遮罩数量，drain 时不依赖 map->masknum */
} MAPFULL_Data;