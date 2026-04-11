/*
 * gff_player.c — 视频/流媒体播放器核心
 *
 * 功能：打开本地文件或 RTMP/RTSP 流，解码音视频，
 *       视频帧渲染到 SDL_Texture，音频通过 SDL 音频设备播放。
 * 线程模型：解码线程负责读取和解码，主线程通过 Update() 刷新纹理，
 *           SDL 音频回调线程消费音频缓冲。
 */

#include "gffmpeg.h"
#include <string.h>

/* ==================== 音频环形缓冲操作 ==================== */

/* 初始化音频环形缓冲 */
static int ring_init(GFF_AudioRing *ring, int capacity)
{
    ring->data = (uint8_t *)SDL_calloc(1, capacity);
    if (!ring->data) return -1;
    ring->capacity  = capacity;
    ring->read_pos  = 0;
    ring->write_pos = 0;
    ring->available = 0;
    ring->mutex     = SDL_CreateMutex();
    return 0;
}

/* 向环形缓冲写入数据 */
static int ring_write(GFF_AudioRing *ring, const uint8_t *src, int len)
{
    SDL_LockMutex(ring->mutex);
    int free_space = ring->capacity - ring->available;
    if (len > free_space) len = free_space;

    /* 分两段拷贝（环形边界处理） */
    int first = ring->capacity - ring->write_pos;
    if (first > len) first = len;
    memcpy(ring->data + ring->write_pos, src, first);

    int second = len - first;
    if (second > 0)
        memcpy(ring->data, src + first, second);

    ring->write_pos = (ring->write_pos + len) % ring->capacity;
    ring->available += len;
    SDL_UnlockMutex(ring->mutex);
    return len;
}

/* 从环形缓冲读取数据 */
static int ring_read(GFF_AudioRing *ring, uint8_t *dst, int len)
{
    SDL_LockMutex(ring->mutex);
    if (len > ring->available) len = ring->available;

    int first = ring->capacity - ring->read_pos;
    if (first > len) first = len;
    memcpy(dst, ring->data + ring->read_pos, first);

    int second = len - first;
    if (second > 0)
        memcpy(dst + first, ring->data, second);

    ring->read_pos = (ring->read_pos + len) % ring->capacity;
    ring->available -= len;
    SDL_UnlockMutex(ring->mutex);
    return len;
}

static void ring_free(GFF_AudioRing *ring)
{
    if (ring->mutex) { SDL_DestroyMutex(ring->mutex); ring->mutex = NULL; }
    if (ring->data)  { SDL_free(ring->data); ring->data = NULL; }
}

static void ring_flush(GFF_AudioRing *ring)
{
    SDL_LockMutex(ring->mutex);
    ring->read_pos  = 0;
    ring->write_pos = 0;
    ring->available = 0;
    SDL_UnlockMutex(ring->mutex);
}

/* ==================== 帧队列操作 ==================== */

static int fq_init(GFF_FrameQueue *fq)
{
    memset(fq, 0, sizeof(*fq));
    fq->mutex     = SDL_CreateMutex();
    fq->not_full  = SDL_CreateCond();
    fq->not_empty = SDL_CreateCond();
    for (int i = 0; i < GFF_FRAME_QUEUE_SIZE; i++)
        fq->frames[i] = av_frame_alloc();
    return 0;
}

/* 推入一帧（解码线程调用），队列满时短暂等待 */
static int fq_push(GFF_FrameQueue *fq, AVFrame *frame, double pts, volatile int *quit)
{
    SDL_LockMutex(fq->mutex);
    while (fq->count >= GFF_FRAME_QUEUE_SIZE && !(*quit)) {
        SDL_CondWaitTimeout(fq->not_full, fq->mutex, 10); /* 10ms 快速响应退出 */
    }
    if (*quit) { SDL_UnlockMutex(fq->mutex); return -1; }

    /* 将帧数据拷贝到队列槽位 */
    av_frame_unref(fq->frames[fq->write_idx]);
    av_frame_ref(fq->frames[fq->write_idx], frame);
    fq->pts[fq->write_idx] = pts;
    fq->write_idx = (fq->write_idx + 1) % GFF_FRAME_QUEUE_SIZE;
    fq->count++;

    SDL_CondSignal(fq->not_empty);
    SDL_UnlockMutex(fq->mutex);
    return 0;
}

/* 查看队首帧（不弹出），返回 0 表示有帧可读 */
static int fq_peek(GFF_FrameQueue *fq, AVFrame **out_frame, double *out_pts)
{
    SDL_LockMutex(fq->mutex);
    if (fq->count <= 0) {
        SDL_UnlockMutex(fq->mutex);
        return -1;
    }
    *out_frame = fq->frames[fq->read_idx];
    *out_pts   = fq->pts[fq->read_idx];
    SDL_UnlockMutex(fq->mutex);
    return 0;
}

/* 弹出队首帧 */
static void fq_pop(GFF_FrameQueue *fq)
{
    SDL_LockMutex(fq->mutex);
    if (fq->count > 0) {
        av_frame_unref(fq->frames[fq->read_idx]);
        fq->read_idx = (fq->read_idx + 1) % GFF_FRAME_QUEUE_SIZE;
        fq->count--;
        SDL_CondSignal(fq->not_full);
    }
    SDL_UnlockMutex(fq->mutex);
}

/* 清空帧队列 */
static void fq_flush(GFF_FrameQueue *fq)
{
    SDL_LockMutex(fq->mutex);
    for (int i = 0; i < GFF_FRAME_QUEUE_SIZE; i++)
        av_frame_unref(fq->frames[i]);
    fq->read_idx = fq->write_idx = fq->count = 0;
    SDL_CondSignal(fq->not_full);
    SDL_UnlockMutex(fq->mutex);
}

static void fq_free(GFF_FrameQueue *fq)
{
    for (int i = 0; i < GFF_FRAME_QUEUE_SIZE; i++) {
        if (fq->frames[i]) { av_frame_free(&fq->frames[i]); }
    }
    if (fq->mutex)     SDL_DestroyMutex(fq->mutex);
    if (fq->not_full)  SDL_DestroyCond(fq->not_full);
    if (fq->not_empty) SDL_DestroyCond(fq->not_empty);
}

/* ==================== 音频回调 ==================== */

/*
 * SDL 音频播放回调：从音频环形缓冲拉取数据
 * 同时更新 audio_clock 为实际播放位置（解码PTS - 缓冲中未播数据时长）
 */
static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    GFF_Player *p = (GFF_Player *)userdata;
    int filled = 0;

    /* 先用静音填充，避免数据不足时出现噪音 */
    SDL_memset(stream, 0, len);

    if (p->state != GFF_STATE_PLAYING) return;

    /* 从环形缓冲读取实际音频数据 */
    uint8_t tmp[8192];
    while (filled < len) {
        int chunk = len - filled;
        if (chunk > (int)sizeof(tmp)) chunk = (int)sizeof(tmp);
        int got = ring_read(&p->audio_ring, tmp, chunk);
        if (got <= 0) break;

        /* 混合音频（支持音量控制） */
        SDL_MixAudioFormat(stream + filled, tmp, AUDIO_S16SYS, got, p->volume);
        filled += got;
    }

    /* 计算实际播放位置 = 最后解码PTS - 缓冲中剩余未播数据的时长 */
    if (filled > 0) {
        double bytes_per_sec = (double)(GFF_AUDIO_TARGET_FREQ * GFF_AUDIO_TARGET_CH * sizeof(int16_t));
        double buffered_sec  = (double)p->audio_ring.available / bytes_per_sec;
        p->audio_clock = p->decoded_audio_pts - buffered_sec;
    }
}

/* ==================== 解码线程 ==================== */

/* 计算 PTS（秒） */
static double calc_pts(AVFrame *frame, AVStream *stream)
{
    int64_t pts = frame->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE) pts = frame->pts;
    if (pts == AV_NOPTS_VALUE) return 0.0;
    return (double)pts * av_q2d(stream->time_base);
}

/* 解码线程主函数 */
static int decode_thread_func(void *data)
{
    GFF_Player *p = (GFF_Player *)data;
    AVPacket *pkt = av_packet_alloc();
    AVFrame  *frame = av_frame_alloc();
    uint8_t  *audio_out_buf = NULL;
    int       audio_out_linesize = 0;

    /* 预分配音频重采样输出缓冲 */
    if (p->swr_ctx) {
        int out_samples = 8192;
        int buf_size = av_samples_get_buffer_size(
            &audio_out_linesize, GFF_AUDIO_TARGET_CH, out_samples,
            AV_SAMPLE_FMT_S16, 1);
        audio_out_buf = (uint8_t *)av_malloc(buf_size);
    }

    while (!p->quit_flag) {
        /* ---- 处理跳转请求 ---- */
        if (p->seek_req) {
            int64_t target = (int64_t)(p->seek_target * AV_TIME_BASE);
            av_seek_frame(p->fmt_ctx, -1, target, AVSEEK_FLAG_BACKWARD);

            /* 清空解码器缓冲和队列 */
            if (p->video_dec_ctx) avcodec_flush_buffers(p->video_dec_ctx);
            if (p->audio_dec_ctx) avcodec_flush_buffers(p->audio_dec_ctx);
            fq_flush(&p->video_queue);
            ring_flush(&p->audio_ring);
            p->seek_req = 0;
        }

        /* ---- 暂停时等待 ---- */
        if (p->state == GFF_STATE_PAUSED) {
            SDL_Delay(20);
            continue;
        }

        /* ---- 读取一个数据包 ---- */
        int ret = av_read_frame(p->fmt_ctx, pkt);
        if (ret < 0) {
            /* EOF 或错误：流媒体可能需要重连，文件则标记结束 */
            if (ret == AVERROR_EOF || avio_feof(p->fmt_ctx->pb)) {
                p->state = GFF_STATE_FINISHED;
                break;
            }
            /* 网络超时等临时错误，短暂等待后重试 */
            SDL_Delay(10);
            continue;
        }

        /* ---- 视频包解码 ---- */
        if (pkt->stream_index == p->video_stream_idx && p->video_dec_ctx) {
            ret = avcodec_send_packet(p->video_dec_ctx, pkt);
            while (ret >= 0) {
                ret = avcodec_receive_frame(p->video_dec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) break;

                double pts = calc_pts(frame, p->fmt_ctx->streams[p->video_stream_idx]);
                p->video_clock = pts;

                /* 如需格式转换则在此处进行（目标: YUV420P） */
                if (p->sws_ctx) {
                    AVFrame *conv = av_frame_alloc();
                    conv->format = AV_PIX_FMT_YUV420P;
                    conv->width  = p->video_width;
                    conv->height = p->video_height;
                    av_frame_get_buffer(conv, 0);
                    sws_scale(p->sws_ctx,
                        (const uint8_t *const *)frame->data, frame->linesize,
                        0, frame->height,
                        conv->data, conv->linesize);
                    conv->pts = frame->pts;
                    conv->best_effort_timestamp = frame->best_effort_timestamp;
                    fq_push(&p->video_queue, conv, pts, &p->quit_flag);
                    av_frame_free(&conv);
                } else {
                    fq_push(&p->video_queue, frame, pts, &p->quit_flag);
                }
                av_frame_unref(frame);
            }
        }

        /* ---- 音频包解码 ---- */
        if (pkt->stream_index == p->audio_stream_idx && p->audio_dec_ctx) {
            ret = avcodec_send_packet(p->audio_dec_ctx, pkt);
            while (ret >= 0) {
                ret = avcodec_receive_frame(p->audio_dec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) break;

                double pts = calc_pts(frame, p->fmt_ctx->streams[p->audio_stream_idx]);
                p->decoded_audio_pts = pts;  /* 记录解码PTS，实际播放位置在回调中计算 */

                if (p->swr_ctx && audio_out_buf) {
                    /* 重采样到目标格式 */
                    int out_count = swr_get_out_samples(p->swr_ctx, frame->nb_samples);
                    uint8_t *out_ptr = audio_out_buf;
                    int converted = swr_convert(p->swr_ctx,
                        &out_ptr, out_count,
                        (const uint8_t **)frame->extended_data, frame->nb_samples);

                    if (converted > 0) {
                        int data_size = converted * GFF_AUDIO_TARGET_CH * sizeof(int16_t);
                        ring_write(&p->audio_ring, audio_out_buf, data_size);
                    }
                }
                av_frame_unref(frame);
            }
        }

        av_packet_unref(pkt);
    }

    if (audio_out_buf) av_free(audio_out_buf);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    return 0;
}

/* ==================== 播放器生命周期 ==================== */

/* FFmpeg IO 中断回调：quit_flag 为 1 时中止 av_read_frame 等阻塞操作 */
static int decode_interrupt_cb(void *opaque)
{
    GFF_Player *p = (GFF_Player *)opaque;
    return p->quit_flag;
}

/* 释放播放器所有资源 */
static void player_destroy(GFF_Player *p)
{
    if (!p) return;

    /* 通知解码线程退出（中断回调已在 open 时注册，会自动生效） */
    p->quit_flag = 1;

    if (p->decode_thread) {
        /* 唤醒所有可能阻塞在条件变量上的线程 */
        if (p->video_queue.not_full)  SDL_CondBroadcast(p->video_queue.not_full);
        if (p->video_queue.not_empty) SDL_CondBroadcast(p->video_queue.not_empty);
        SDL_WaitThread(p->decode_thread, NULL);
        p->decode_thread = NULL;
    }

    /* 关闭 SDL 音频设备 */
    if (p->audio_dev) {
        SDL_CloseAudioDevice(p->audio_dev);
        p->audio_dev = 0;
    }

    /* 释放 SDL 纹理（通过 GGE_Texture 引用计数） */
    if (p->texture) {
        SDL_DestroyTexture(p->texture);
        p->texture = NULL;
    }

    /* 释放 FFmpeg 上下文 */
    if (p->sws_ctx) { sws_freeContext(p->sws_ctx); p->sws_ctx = NULL; }
    if (p->swr_ctx) { swr_free(&p->swr_ctx); }
    if (p->video_dec_ctx) { avcodec_free_context(&p->video_dec_ctx); }
    if (p->audio_dec_ctx) { avcodec_free_context(&p->audio_dec_ctx); }
    if (p->fmt_ctx) { avformat_close_input(&p->fmt_ctx); }

    /* 释放队列和缓冲 */
    fq_free(&p->video_queue);
    ring_free(&p->audio_ring);
    if (p->state_mutex) { SDL_DestroyMutex(p->state_mutex); p->state_mutex = NULL; }
}

/* 打开指定音/视频流的解码器 */
static int open_codec(AVFormatContext *fmt, int stream_idx, AVCodecContext **out_ctx)
{
    AVStream *st = fmt->streams[stream_idx];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) return -1;

    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    if (!ctx) return -1;

    if (avcodec_parameters_to_context(ctx, st->codecpar) < 0) {
        avcodec_free_context(&ctx);
        return -1;
    }
    /* 多线程解码加速 */
    ctx->thread_count = SDL_GetCPUCount();
    if (ctx->thread_count > 4) ctx->thread_count = 4;

    if (avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        return -1;
    }
    *out_ctx = ctx;
    return 0;
}

/* ==================== Lua 绑定：Player 方法 ==================== */

/* ff.PlayerOpen(renderer, path_or_url) → player userdata */
int gff_player_open(lua_State *L)
{
    SDL_Renderer *renderer = *(SDL_Renderer **)luaL_checkudata(L, 1, "SDL_Renderer");
    const char *url = luaL_checkstring(L, 2);

    /* 创建 Player userdata */
    GFF_Player *p = (GFF_Player *)lua_newuserdata(L, sizeof(GFF_Player));
    memset(p, 0, sizeof(GFF_Player));
    luaL_setmetatable(L, GFF_PLAYER_MT);

    p->renderer = renderer;
    p->volume   = SDL_MIX_MAXVOLUME;
    p->state    = GFF_STATE_STOPPED;
    p->video_stream_idx = -1;
    p->audio_stream_idx = -1;

    /* 初始化同步原语 */
    p->state_mutex = SDL_CreateMutex();
    fq_init(&p->video_queue);
    ring_init(&p->audio_ring, GFF_AUDIO_BUF_SIZE);

    /* ---- 打开媒体源（支持本地文件和网络流） ---- */
    AVDictionary *opts = NULL;
    /* 流媒体优化选项 */
    av_dict_set(&opts, "stimeout", "5000000", 0);       /* RTSP 超时 5s */
    av_dict_set(&opts, "reconnect", "1", 0);             /* HTTP 自动重连 */
    av_dict_set(&opts, "reconnect_streamed", "1", 0);

    /* 注册 FFmpeg IO 中断回调（必须在 avformat_open_input 之前） */
    p->fmt_ctx = avformat_alloc_context();
    p->fmt_ctx->interrupt_callback.callback = decode_interrupt_cb;
    p->fmt_ctx->interrupt_callback.opaque   = p;

    int ret = avformat_open_input(&p->fmt_ctx, url, NULL, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        player_destroy(p);
        lua_pushnil(L);
        lua_pushfstring(L, "无法打开: %s", url);
        return 2;
    }

    if (avformat_find_stream_info(p->fmt_ctx, NULL) < 0) {
        player_destroy(p);
        lua_pushnil(L);
        lua_pushstring(L, "无法获取流信息");
        return 2;
    }

    /* 判断是否为流媒体 */
    p->is_streaming = (p->fmt_ctx->duration <= 0 || p->fmt_ctx->duration == AV_NOPTS_VALUE);
    p->duration = p->is_streaming ? 0.0 : (double)p->fmt_ctx->duration / AV_TIME_BASE;

    /* ---- 查找并打开视频流 ---- */
    p->video_stream_idx = av_find_best_stream(p->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (p->video_stream_idx >= 0) {
        if (open_codec(p->fmt_ctx, p->video_stream_idx, &p->video_dec_ctx) < 0) {
            p->video_stream_idx = -1;
        } else {
            p->video_width  = p->video_dec_ctx->width;
            p->video_height = p->video_dec_ctx->height;

            /* 创建 YUV420P 纹理用于视频渲染 */
            p->texture = SDL_CreateTexture(renderer,
                SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
                p->video_width, p->video_height);

            /* 如果解码输出格式不是 YUV420P，则需要格式转换 */
            if (p->video_dec_ctx->pix_fmt != AV_PIX_FMT_YUV420P) {
                p->sws_ctx = sws_getContext(
                    p->video_width, p->video_height, p->video_dec_ctx->pix_fmt,
                    p->video_width, p->video_height, AV_PIX_FMT_YUV420P,
                    SWS_BILINEAR, NULL, NULL, NULL);
            }
        }
    }

    /* ---- 查找并打开音频流 ---- */
    p->audio_stream_idx = av_find_best_stream(p->fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (p->audio_stream_idx >= 0) {
        if (open_codec(p->fmt_ctx, p->audio_stream_idx, &p->audio_dec_ctx) < 0) {
            p->audio_stream_idx = -1;
        } else {
            /* 配置音频重采样：源格式 → S16/44100Hz/Stereo */
            AVCodecContext *ac = p->audio_dec_ctx;
            p->swr_ctx = swr_alloc();
            av_opt_set_chlayout(p->swr_ctx, "in_chlayout",  &ac->ch_layout, 0);
            av_opt_set_int(p->swr_ctx,      "in_sample_rate",  ac->sample_rate, 0);
            av_opt_set_sample_fmt(p->swr_ctx,"in_sample_fmt",  ac->sample_fmt, 0);

            AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
            av_opt_set_chlayout(p->swr_ctx, "out_chlayout", &out_layout, 0);
            av_opt_set_int(p->swr_ctx,      "out_sample_rate", GFF_AUDIO_TARGET_FREQ, 0);
            av_opt_set_sample_fmt(p->swr_ctx,"out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
            swr_init(p->swr_ctx);

            /* 打开 SDL 音频输出设备 */
            SDL_AudioSpec wanted;
            SDL_zero(wanted);
            wanted.freq     = GFF_AUDIO_TARGET_FREQ;
            wanted.format   = AUDIO_S16SYS;
            wanted.channels = GFF_AUDIO_TARGET_CH;
            wanted.samples  = 1024;
            wanted.callback = audio_callback;
            wanted.userdata = p;
            p->audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted, &p->audio_spec, 0);
        }
    }

    /* 如果既没有视频也没有音频，打开失败 */
    if (p->video_stream_idx < 0 && p->audio_stream_idx < 0) {
        player_destroy(p);
        lua_pushnil(L);
        lua_pushstring(L, "未找到可用的音视频流");
        return 2;
    }

    return 1; /* 返回 player userdata */
}

/* player:Play() — 开始/恢复播放 */
static int LUA_PlayerPlay(lua_State *L)
{
    GFF_Player *p = (GFF_Player *)luaL_checkudata(L, 1, GFF_PLAYER_MT);
    if (p->state == GFF_STATE_FINISHED) return 0;

    p->state = GFF_STATE_PLAYING;

    /* 首次播放时创建解码线程 */
    if (!p->decode_thread) {
        p->quit_flag = 0;
        p->decode_thread = SDL_CreateThread(decode_thread_func, "gff_decode", p);
    }

    /* 恢复音频设备播放 */
    if (p->audio_dev)
        SDL_PauseAudioDevice(p->audio_dev, 0);

    return 0;
}

/* player:Pause() */
static int LUA_PlayerPause(lua_State *L)
{
    GFF_Player *p = (GFF_Player *)luaL_checkudata(L, 1, GFF_PLAYER_MT);
    p->state = GFF_STATE_PAUSED;
    if (p->audio_dev) SDL_PauseAudioDevice(p->audio_dev, 1);
    return 0;
}

/* player:Stop() */
static int LUA_PlayerStop(lua_State *L)
{
    GFF_Player *p = (GFF_Player *)luaL_checkudata(L, 1, GFF_PLAYER_MT);

    p->quit_flag = 1;
    if (p->decode_thread) {
        SDL_CondBroadcast(p->video_queue.not_full);
        SDL_WaitThread(p->decode_thread, NULL);
        p->decode_thread = NULL;
    }
    if (p->audio_dev) SDL_PauseAudioDevice(p->audio_dev, 1);

    fq_flush(&p->video_queue);
    ring_flush(&p->audio_ring);
    p->state = GFF_STATE_STOPPED;
    p->audio_clock = 0;
    p->video_clock = 0;
    return 0;
}

/* player:Seek(seconds) — 跳转到指定秒 */
static int LUA_PlayerSeek(lua_State *L)
{
    GFF_Player *p = (GFF_Player *)luaL_checkudata(L, 1, GFF_PLAYER_MT);
    double target = luaL_checknumber(L, 2);

    if (p->is_streaming) return 0; /* 流媒体不支持跳转 */
    if (target < 0) target = 0;
    if (target > p->duration) target = p->duration;

    p->seek_target = target;
    p->seek_req = 1;
    return 0;
}

/* player:SetVolume(0~128) */
static int LUA_PlayerSetVolume(lua_State *L)
{
    GFF_Player *p = (GFF_Player *)luaL_checkudata(L, 1, GFF_PLAYER_MT);
    int vol = (int)luaL_checkinteger(L, 2);
    if (vol < 0) vol = 0;
    if (vol > SDL_MIX_MAXVOLUME) vol = SDL_MIX_MAXVOLUME;
    p->volume = vol;
    return 0;
}

/*
 * player:Update() — 每帧调用，将已解码视频帧更新到 SDL_Texture
 * 使用音频时钟同步：只有当视频帧 PTS <= 音频时钟时才显示
 */
static int LUA_PlayerUpdate(lua_State *L)
{
    GFF_Player *p = (GFF_Player *)luaL_checkudata(L, 1, GFF_PLAYER_MT);
    if (p->state != GFF_STATE_PLAYING || !p->texture) return 0;

    AVFrame *frame = NULL;
    double pts = 0;

    /* 尝试弹出所有 PTS <= audio_clock 的帧，保留最后一帧用于显示 */
    while (fq_peek(&p->video_queue, &frame, &pts) == 0) {
        /* 若无音频流，直接显示每一帧 */
        if (p->audio_stream_idx < 0 || pts <= p->audio_clock + 0.05) {
            /* 更新 YUV 纹理 */
            SDL_UpdateYUVTexture(p->texture, NULL,
                frame->data[0], frame->linesize[0],
                frame->data[1], frame->linesize[1],
                frame->data[2], frame->linesize[2]);
            fq_pop(&p->video_queue);
        } else {
            break; /* 帧还太早，等下一次 Update */
        }
    }

    return 0;
}

/* player:GetTexture() → SDL_Texture userdata */
static int LUA_PlayerGetTexture(lua_State *L)
{
    GFF_Player *p = (GFF_Player *)luaL_checkudata(L, 1, GFF_PLAYER_MT);
    if (!p->texture) return 0;

    SDL_Texture **ud = (SDL_Texture **)lua_newuserdata(L, sizeof(SDL_Texture *));
    *ud = p->texture;
    luaL_setmetatable(L, "SDL_Texture");
    return 1;
}

/* player:GetDuration() → 秒 */
static int LUA_PlayerGetDuration(lua_State *L)
{
    GFF_Player *p = (GFF_Player *)luaL_checkudata(L, 1, GFF_PLAYER_MT);
    lua_pushnumber(L, p->duration);
    return 1;
}

/* player:GetPosition() → 秒 */
static int LUA_PlayerGetPosition(lua_State *L)
{
    GFF_Player *p = (GFF_Player *)luaL_checkudata(L, 1, GFF_PLAYER_MT);
    /* 优先使用音频时钟，无音频则用视频时钟 */
    double pos = (p->audio_stream_idx >= 0) ? p->audio_clock : p->video_clock;
    lua_pushnumber(L, pos);
    return 1;
}

/* player:GetSize() → w, h */
static int LUA_PlayerGetSize(lua_State *L)
{
    GFF_Player *p = (GFF_Player *)luaL_checkudata(L, 1, GFF_PLAYER_MT);
    lua_pushinteger(L, p->video_width);
    lua_pushinteger(L, p->video_height);
    return 2;
}

/* player:GetState() → "playing"/"paused"/"stopped"/"finished" */
static int LUA_PlayerGetState(lua_State *L)
{
    GFF_Player *p = (GFF_Player *)luaL_checkudata(L, 1, GFF_PLAYER_MT);
    const char *states[] = {"stopped", "playing", "paused", "finished"};
    lua_pushstring(L, states[p->state]);
    return 1;
}

/* player:Close() 或 __gc */
static int LUA_PlayerClose(lua_State *L)
{
    GFF_Player *p = (GFF_Player *)luaL_checkudata(L, 1, GFF_PLAYER_MT);
    player_destroy(p);
    return 0;
}

/* ==================== Player Metatable 注册 ==================== */

static const luaL_Reg player_methods[] = {
    {"Play",         LUA_PlayerPlay},
    {"Pause",        LUA_PlayerPause},
    {"Stop",         LUA_PlayerStop},
    {"Seek",         LUA_PlayerSeek},
    {"SetVolume",    LUA_PlayerSetVolume},
    {"Update",       LUA_PlayerUpdate},
    {"GetTexture",   LUA_PlayerGetTexture},
    {"GetDuration",  LUA_PlayerGetDuration},
    {"GetPosition",  LUA_PlayerGetPosition},
    {"GetSize",      LUA_PlayerGetSize},
    {"GetState",     LUA_PlayerGetState},
    {"Close",        LUA_PlayerClose},
    {"__gc",         LUA_PlayerClose},
    {NULL, NULL}
};

int gff_player_register(lua_State *L)
{
    luaL_newmetatable(L, GFF_PLAYER_MT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index"); /* metatable.__index = metatable */
    luaL_setfuncs(L, player_methods, 0);
    lua_pop(L, 1);
    return 0;
}
