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

#define TCP_MT_XY2 "xy2_tcp"
#define TCP_MT_XYQ "xyq_tcp"

/* TCP_UserData.fmt 精灵格式标志 (Uint16) */
#define TCP_FMT_PS  0x5053   /* 'PS' = SP格式 */
#define TCP_FMT_PR  0x5052   /* 'PR' = RP格式 */
#define TCP_FMT_PT  0x5054   /* 'PT' = TP格式 */

#ifndef MYGXY_ASYNC_FRAME_STATUS
#define MYGXY_ASYNC_FRAME_STATUS
#define MYGXY_ASYNC_FRAME_QUEUED      0
#define MYGXY_ASYNC_FRAME_READY       1
#define MYGXY_ASYNC_FRAME_PENDING     2
#define MYGXY_ASYNC_FRAME_QUEUE_FULL -2
#define MYGXY_ASYNC_FRAME_ERROR      -1
#endif
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
    SDL_Surface* surface;
    Uint32 frame_id;
    Uint32 pal_version;
    Uint32 w, h;
    Sint32 x, y;
    Uint32 lru_tick;
} TCP_CacheEntry;

typedef struct TCP_AsyncJob
{
    Uint32 frame_id;
    Uint32 generation;
    Uint32 pal_version;
    Uint32 pal_count;
    Uint32* pal_snapshot;
    SDL_Surface* surface;
    Uint32 w, h;
    Sint32 x, y;
    int ok;
    struct TCP_AsyncJob* next;
} TCP_AsyncJob;

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

    Uint32 pal_version;
    TCP_CacheEntry* cache;
    Uint32 cache_cap;
    Uint32 cache_tick;

    SDL_mutex* async_mutex;
    Uint32 async_generation;
    TCP_AsyncJob* async_done_head;
    TCP_AsyncJob* async_done_tail;
    Uint32 async_ready;
    Uint32 async_submitted;
    Uint32 async_decoded;
    Uint32 async_failed;
    Uint32 async_cancelled;
} TCP_UserData;

typedef struct
{
    SDL_Surface* surface;
    Uint32 frame_id;
    Uint32 pal_version;
    Uint32 w;
    Uint32 h;
    Sint32 x;
    Sint32 y;
} TCP_NativeFrameData;

/* Worker-owned decoded pixels.  The buffer is plain heap memory and must not
 * be passed to SDL/IMG/renderer code until the main thread upload step. */
typedef struct
{
    void* pixels;
    Uint32 pitch;
    Uint32 width;
    Uint32 height;
    Sint32 x;
    Sint32 y;
    Uint32 frame_id;
    Uint32 pal_version;
} TCP_NativeRawFrameData;

int TCP_Create(lua_State* L, Uint8* data, size_t size);
TCP_UserData* TCP_NativeCreateFromData(const Uint8* data, size_t size, char* err, size_t errSize);
void TCP_NativeFree(TCP_UserData* ud);
int TCP_NativePushParsed(lua_State* L, TCP_UserData* ud);
int TCP_NativeWarmFrame(TCP_UserData* ud, Uint32 group, Uint32 frame, char* err, size_t errSize);
int TCP_NativeDecodeGroupFrame(TCP_UserData* ud, Uint32 group, Uint32 frame,
                               TCP_NativeFrameData* out, char* err, size_t errSize);
int TCP_NativeDecodeFrameWithPalette(TCP_UserData* ud, Uint32 id, const Uint32* pal, Uint32 pal_count,
                                      Uint32 pal_version, TCP_NativeFrameData* out, char* err, size_t errSize);
int TCP_NativeDecodeFramePixels(TCP_UserData* ud, Uint32 id, const Uint32* pal, Uint32 pal_count,
                                Uint32 pal_version, TCP_NativeRawFrameData* out, char* err, size_t errSize);
void TCP_NativeFreeFramePixels(TCP_NativeRawFrameData* frame);
int TCP_NativeStoreRawFrame(TCP_UserData* ud, TCP_NativeRawFrameData* frame);
int TCP_NativeStoreDecodedFrame(TCP_UserData* ud, TCP_NativeFrameData* frame);
void TCP_NativeClearFrameData(TCP_NativeFrameData* frame);
int TCP_NativeRequestFrame(TCP_UserData* ud, Uint32 id, const char** status);
int TCP_NativePollAsync(TCP_UserData* ud, Uint32 limit);
int TCP_NativeIsFrameDecoded(TCP_UserData* ud, Uint32 id);
void TCP_PushPerfStats(lua_State* L);
