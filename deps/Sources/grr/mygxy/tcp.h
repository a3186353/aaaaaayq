/*
 * @Author: a3186353 377411161@qq.com
 * @Date: 2025-12-28 03:22:04
 * @LastEditors: a3186353 377411161@qq.com
 * @LastEditTime: 2025-12-28 03:51:37
 * @FilePath: \xiaoAo-main\Sources\grr\mygxy\tcp.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#pragma once
#include "lua_proxy.h"
#include "sdl_proxy.h"
#include "lua_proxy.h"
#include "sdl_proxy.h"

#define TCP_MT_XY2 "xy2_tcp"
#define TCP_MT_XYQ "xyq_tcp"

/* TCP_UserData.fmt 精灵格式标志 (Uint16) */
#define TCP_FMT_PS  0x5053   /* 'PS' = SP格式 */
#define TCP_FMT_PR  0x5052   /* 'PR' = RP格式 */
#define TCP_FMT_PT  0x5054   /* 'PT' = TP格式 */
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

//TCP TP
typedef struct
{
    Uint16 flag;        // 精灵文件标志 TP 0x5054
    Uint16 unknown;     //未知
    Uint16 group;       //方向
    Uint16 frame;       //帧
    Uint16 width;       //全局宽
    Uint16 height;      //全局高
    Sint16 x;           //全局关键位X
    Sint16 y;           //全局关键位Y
    Uint16 palette_len; //调色板颜色数
    Uint16 unknown1;    //未知
} TP_HEAD;

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
    Uint32* tplist;  //TP格式

    Uint32 number;   //帧数
    Uint32 pal[256]; //调色板

    Uint32* pal_dyn;   //实际解码用调色板(可能>256)
    Uint32 pal_count;  //pal_dyn长度
    Uint16 fmt;        //'PS'/'PR'/'PT'
    Uint8 sp_rgb565;   //1=RGB565调色板, 0=RGB/BGRA调色板
} TCP_UserData;

int TCP_Create(lua_State* L, Uint8* data, size_t size);
