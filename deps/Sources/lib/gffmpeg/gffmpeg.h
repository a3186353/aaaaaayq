/*
 * gffmpeg.h — GGELUA3 FFmpeg 多媒体模块
 *
 * 功能：视频/流媒体播放、麦克风录制
 * 依赖：FFmpeg (avcodec/avformat/avutil/swresample/swscale) + SDL2 + Lua 5.4
 */

#ifndef _GFFMPEG_H_
#define _GFFMPEG_H_

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include <SDL.h>

/* FFmpeg 头文件使用 C 链接 */
#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#ifdef __cplusplus
}
#endif

/* ==================== 常量定义 ==================== */

#define GFF_FRAME_QUEUE_SIZE   8       /* 视频帧队列容量 */
#define GFF_AUDIO_BUF_SIZE     (192000) /* 音频缓冲区 ~1s @48kHz/stereo/16bit */
#define GFF_AUDIO_TARGET_FREQ  44100   /* 音频输出采样率 */
#define GFF_AUDIO_TARGET_CH    2       /* 音频输出声道数 */

/* 播放器状态 */
enum {
    GFF_STATE_STOPPED  = 0,
    GFF_STATE_PLAYING  = 1,
    GFF_STATE_PAUSED   = 2,
    GFF_STATE_FINISHED = 3
};

/* ==================== 音频环形缓冲 ==================== */

typedef struct GFF_AudioRing {
    uint8_t    *data;       /* 缓冲区数据 */
    int         capacity;   /* 总容量(字节) */
    int         read_pos;   /* 读指针 */
    int         write_pos;  /* 写指针 */
    int         available;  /* 可读字节数 */
    SDL_mutex  *mutex;
} GFF_AudioRing;

/* ==================== 视频帧队列 ==================== */

typedef struct GFF_FrameQueue {
    AVFrame    *frames[GFF_FRAME_QUEUE_SIZE];
    double      pts[GFF_FRAME_QUEUE_SIZE];  /* 每帧的显示时间戳(秒) */
    int         read_idx;
    int         write_idx;
    int         count;      /* 当前队列中帧数 */
    SDL_mutex  *mutex;
    SDL_cond   *not_full;   /* 队列非满信号 */
    SDL_cond   *not_empty;  /* 队列非空信号 */
} GFF_FrameQueue;

/* ==================== 播放器上下文 ==================== */

typedef struct GFF_Player {
    /* FFmpeg 解封装/解码 */
    AVFormatContext    *fmt_ctx;
    AVCodecContext     *video_dec_ctx;
    AVCodecContext     *audio_dec_ctx;
    int                 video_stream_idx;
    int                 audio_stream_idx;

    /* 视频格式转换 (解码帧 → YUV420P for SDL_Texture) */
    struct SwsContext  *sws_ctx;
    int                 video_width;
    int                 video_height;

    /* 音频重采样 (解码格式 → S16/44100Hz/Stereo) */
    SwrContext         *swr_ctx;
    SDL_AudioSpec       audio_spec;
    SDL_AudioDeviceID   audio_dev;

    /* SDL 渲染 */
    SDL_Renderer       *renderer;   /* 外部传入，不由播放器销毁 */
    SDL_Texture        *texture;    /* YUV 纹理，播放器创建并拥有 */

    /* 帧队列和音频缓冲 */
    GFF_FrameQueue      video_queue;
    GFF_AudioRing       audio_ring;

    /* 线程管理 */
    SDL_Thread         *decode_thread;
    SDL_mutex          *state_mutex;
    volatile int        state;
    volatile int        quit_flag;  /* 通知解码线程退出 */

    /* 时间同步 —— 以音频时钟为主 */
    double              audio_clock;        /* 实际播放位置(秒)，在音频回调中更新 */
    double              decoded_audio_pts;  /* 最后解码的音频帧PTS(秒) */
    double              video_clock;
    double              duration;       /* 总时长(秒)，流媒体为 0 */
    int                 is_streaming;   /* 是否为流媒体(无 duration) */

    /* 音量 (0~128, 与 SDL_MixAudioFormat 兼容) */
    int                 volume;

    /* 跳转请求 */
    volatile int        seek_req;
    double              seek_target;    /* 目标时间(秒) */

    /* 无音频流时的时钟源 (SDL_GetTicks 基准, 毫秒) */
    Uint32              wall_clock_base;
} GFF_Player;

/* ==================== 录音器上下文 ==================== */

typedef struct GFF_Recorder {
    /* FFmpeg 编码/输出 */
    AVFormatContext    *out_ctx;
    AVCodecContext     *enc_ctx;
    AVStream           *out_stream;
    int64_t             samples_written;

    /* SDL 音频采集 */
    SDL_AudioDeviceID   capture_dev;
    SDL_AudioSpec       capture_spec;

    /* 编码缓冲 */
    AVFrame            *enc_frame;
    int                 frame_size;     /* 每帧采样数 */
    uint8_t            *pcm_buf;        /* PCM 采集缓冲 */
    int                 pcm_buf_size;
    int                 pcm_buf_used;

    /* 采样格式转换 (SDL S16 → 编码器要求的格式) */
    SwrContext         *swr_ctx;

    /* 线程管理 */
    SDL_Thread         *record_thread;
    SDL_mutex          *mutex;
    volatile int        recording;
    volatile int        quit_flag;

    /* 统计 */
    double              duration;       /* 已录制时长(秒) */
} GFF_Recorder;

/* ==================== Lua 绑定声明 ==================== */

/* 模块入口 */
#ifdef _WIN32
    #ifdef GFFMPEG_EXPORTS
        #define GFFMPEG_API __declspec(dllexport)
    #else
        #define GFFMPEG_API __declspec(dllimport)
    #endif
#else
    #define GFFMPEG_API
#endif

GFFMPEG_API int luaopen_gffmpeg(lua_State *L);

/* 播放器 Lua 绑定 */
int gff_player_register(lua_State *L);
int gff_player_open(lua_State *L);

/* 录音器 Lua 绑定 */
int gff_recorder_register(lua_State *L);
int gff_recorder_open(lua_State *L);
int gff_list_capture_devices(lua_State *L);

/* Metatable 名称 */
#define GFF_PLAYER_MT   "GFF_Player"
#define GFF_RECORDER_MT "GFF_Recorder"

#endif /* _GFFMPEG_H_ */
