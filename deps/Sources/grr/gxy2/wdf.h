#pragma once
#include "lua.h"
#include "lauxlib.h"
#include "SDL_rwops.h"

#define WDF_NAME "xy2_wdf"
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