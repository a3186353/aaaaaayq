#pragma once
#include "lua.h"
#include "lauxlib.h"

#include "SDL_rwops.h"
#include "SDL_surface.h"
#include "SDL_image.h"

#define TCP_NAME "xy2_tcp"
//TCA TCP SP
typedef struct
{
    Uint16 flag;   // 精灵文件标志 SP 0x5053
    Uint16 len;    // 文件头的长度 默认为 12
    Uint16 group;  // 精灵图片的组数，即方向数
    Uint16 frame;  // 每组的图片数，即帧数
    Uint16 width;  // 精灵动画的宽度，单位像素
    Uint16 height; // 精灵动画的高度，单位像素
    short x;       // 精灵动画的关键位X
    short y;       // 精灵动画的关键位Y
} SP_HEAD;

typedef struct
{
    Sint32 x;      // 图片的关键位X
    Sint32 y;      // 图片的关键位Y
    Uint32 width;  // 图片的宽度，单位像素
    Uint32 height; // 图片的高度，单位像素
} SP_INFO;

//TCP RP
typedef struct
{
    Uint16 flag;   // 精灵文件标志 RP 0x5052
    Uint16 group;  // 精灵图片的组数，即方向数
    Uint16 frame;  // 每组的图片数，即帧数
    Uint16 width;  // 精灵动画的宽度，单位像素
    Uint16 height; // 精灵动画的高度，单位像素
    short x;       // 精灵动画的关键位X
    short y;       // 精灵动画的关键位Y
    Uint16 number; //总帧数?
} RP_HEAD;

typedef struct
{
    Uint16 id;
    Sint16 u1;     //未知
    Sint16 u2;     //未知
    Uint16 width;  // 图片的宽度，单位像素
    Uint16 height; // 图片的高度，单位像素
    Sint16 x;      // 图片的关键位X
    Sint16 y;      // 图片的关键位Y
} RP_INFO;

typedef struct
{
    Uint32 offset;
    Uint32 len;
} RP_LIST;

typedef struct
{
    Uint8* data;
    Uint32 len;
    Uint32* splist; //SP格式
    //Union?
    RP_INFO* rpinfo; //RP格式
    RP_LIST* rplist; //RP格式

    Uint32 number;   //帧数
    Uint32 pal[256]; //调色板
} TCP_UserData;

int TCP_Create(lua_State* L, Uint8* data, size_t size);