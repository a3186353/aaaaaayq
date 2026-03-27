#pragma once
#include "lua_proxy.h"
#include "sdl_proxy.h"
#include "lua_proxy.h"
#include "sdl_proxy.h"

#define WDF_NAME "xyq_wdf"
#define WDF_FLAG_WDFP   0x57444650   /* 'WDFP' */
typedef struct
{
    Uint32 flag;   // 包裹的标签
    Uint32 number; // 包裹中的文件数量
    Uint32 offset; // 包裹中的文件列表偏移位置
} WDF_HEAD;

typedef struct
{
    Uint32 hash;   // 文件的名字散列
    Uint32 offset; // 文件的偏移
    Uint32 size;   // 文件的大小
    Uint32 unused; // 文件的空间
} WDF_FileInfo;

typedef struct
{
    Uint32 number;
    SDL_RWops* file;
    WDF_FileInfo* list;
    char path[256];
} WDF_UserData;
