#pragma once
#include "lua.h"
#include "lauxlib.h"

#include "SDL_rwops.h"
#include "SDL_timer.h"
#include "SDL_surface.h"
#include "SDL_image.h"
#include "SDL_stdinc.h"
#include "SDL_rect.h"
#include "SDL_list.h"

#include "ujpeg/ujpeg.h"
#define MAP_NAME "xy2_map"

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
} MAP_MaskInfo;


typedef struct
{
    Uint32 id;
    SDL_Surface* sf;
    MAP_MaskInfo* mask;
    Uint32 masknum;
} MAP_Data;

typedef struct
{
    Uint32 id;
    SDL_Surface* sf;
    MAP_MaskInfo info; //遮罩信息
    int ref;
} MASK_Data;

typedef struct
{
    Uint32 flag;
    Uint32 mode;
    SDL_RWops* file;
    Sint32 event;
    Sint32 code;

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
}MAP_UserData;

typedef struct
{
    Uint32 type;
    void* data;
    MAP_UserData* ud;
} TIME_Data;