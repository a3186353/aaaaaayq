/*
 * gff_recorder.c — 麦克风音频录制模块
 *
 * 功能：通过 SDL 音频采集捕获麦克风输入，使用 FFmpeg 编码器写入文件。
 * 支持输出格式：WAV（PCM）、MP4/AAC、OGG 等
 * 格式由输出文件扩展名自动推断。
 */

#include "gffmpeg.h"
#include <string.h>

/* ==================== SDL 采集回调 ==================== */

/* SDL 音频采集回调：将麦克风数据写入录制缓冲 */
static void capture_callback(void *userdata, Uint8 *stream, int len)
{
    GFF_Recorder *r = (GFF_Recorder *)userdata;
    if (!r->recording) return;

    SDL_LockMutex(r->mutex);
    /* 累积 PCM 数据到缓冲区 */
    int space = r->pcm_buf_size - r->pcm_buf_used;
    int copy_len = (len < space) ? len : space;
    if (copy_len > 0 && r->pcm_buf) {
        memcpy(r->pcm_buf + r->pcm_buf_used, stream, copy_len);
        r->pcm_buf_used += copy_len;
    }
    SDL_UnlockMutex(r->mutex);
}

/* ==================== 录制线程 ==================== */

/* 编码并写入一帧音频 */
static int encode_and_write(GFF_Recorder *r, AVFrame *frame)
{
    int ret = avcodec_send_frame(r->enc_ctx, frame);
    if (ret < 0) return ret;

    AVPacket *pkt = av_packet_alloc();
    while (ret >= 0) {
        ret = avcodec_receive_packet(r->enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        /* 调整时间基 */
        av_packet_rescale_ts(pkt, r->enc_ctx->time_base, r->out_stream->time_base);
        pkt->stream_index = r->out_stream->index;
        av_interleaved_write_frame(r->out_ctx, pkt);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return 0;
}

/*
 * 录制线程：从缓冲区取数据 → 可选重采样 → 编码 → 写文件
 * 关键：mutex 只保护 pcm_buf 拷贝操作，编码在锁外进行
 *       避免阻塞 SDL 采集回调导致音频丢失
 */
static int record_thread_func(void *data)
{
    GFF_Recorder *r = (GFF_Recorder *)data;
    /* SDL 采集格式: S16 单声道, 每帧所需字节数 */
    int frame_bytes = r->frame_size * sizeof(int16_t);

    /* 线程本地缓冲，用于从 pcm_buf 快速拷出数据后立即释放锁 */
    uint8_t *local_buf = (uint8_t *)SDL_malloc(frame_bytes);
    if (!local_buf) return -1;

    while (!r->quit_flag) {
        SDL_Delay(2); /* 降低延迟，更频繁检查数据 */

        /* ---- 临界区：最小化锁持有时间 ---- */
        SDL_LockMutex(r->mutex);
        if (r->pcm_buf_used < frame_bytes) {
            SDL_UnlockMutex(r->mutex);
            continue;
        }
        /* 快速拷贝到本地缓冲 */
        memcpy(local_buf, r->pcm_buf, frame_bytes);
        /* 移动剩余数据 */
        int remain = r->pcm_buf_used - frame_bytes;
        if (remain > 0)
            memmove(r->pcm_buf, r->pcm_buf + frame_bytes, remain);
        r->pcm_buf_used = remain;
        SDL_UnlockMutex(r->mutex);
        /* ---- 临界区结束：以下操作不持锁 ---- */

        /* 准备编码帧 */
        if (av_frame_make_writable(r->enc_frame) < 0)
            continue;

        if (r->swr_ctx) {
            /* 重采样：SDL S16 → 编码器格式（如 FLTP） */
            const uint8_t *in_data = local_buf;
            uint8_t *out_data = r->enc_frame->data[0];
            swr_convert(r->swr_ctx,
                &out_data, r->frame_size,
                &in_data, r->frame_size);
        } else {
            /* PCM S16 编码器直接拷贝 */
            memcpy(r->enc_frame->data[0], local_buf, frame_bytes);
        }

        /* 设置 PTS 并编码写入 */
        r->enc_frame->pts = r->samples_written;
        r->samples_written += r->frame_size;
        r->duration = (double)r->samples_written / r->enc_ctx->sample_rate;

        encode_and_write(r, r->enc_frame);
    }

    /* 刷新编码器残留数据 */
    encode_and_write(r, NULL);
    SDL_free(local_buf);
    return 0;
}

/* ==================== Lua 绑定 ==================== */

/* ff.ListCaptureDevices() → {"设备1", "设备2", ...} */
int gff_list_capture_devices(lua_State *L)
{
    int count = SDL_GetNumAudioDevices(1); /* 1 = capture */
    lua_createtable(L, count, 0);
    for (int i = 0; i < count; i++) {
        lua_pushstring(L, SDL_GetAudioDeviceName(i, 1));
        lua_seti(L, -2, i + 1);
    }
    return 1;
}

/* ff.RecorderOpen(filename [, device_index]) → recorder userdata */
int gff_recorder_open(lua_State *L)
{
    const char *filename = luaL_checkstring(L, 1);
    int dev_idx = (int)luaL_optinteger(L, 2, -1); /* -1 = 默认设备 */

    GFF_Recorder *r = (GFF_Recorder *)lua_newuserdata(L, sizeof(GFF_Recorder));
    memset(r, 0, sizeof(GFF_Recorder));
    luaL_setmetatable(L, GFF_RECORDER_MT);

    r->mutex = SDL_CreateMutex();
    int sample_rate = GFF_AUDIO_TARGET_FREQ;

    /* ---- 创建 FFmpeg 输出上下文（根据文件扩展名自动选择格式） ---- */
    int ret = avformat_alloc_output_context2(&r->out_ctx, NULL, NULL, filename);
    if (ret < 0 || !r->out_ctx) {
        lua_pushnil(L);
        lua_pushfstring(L, "无法创建输出格式: %s", filename);
        return 2;
    }

    /* 根据输出格式选择编码器 */
    enum AVCodecID codec_id = r->out_ctx->oformat->audio_codec;
    if (codec_id == AV_CODEC_ID_NONE) codec_id = AV_CODEC_ID_PCM_S16LE; /* WAV 默认 */

    const AVCodec *encoder = avcodec_find_encoder(codec_id);
    if (!encoder) {
        avformat_free_context(r->out_ctx); r->out_ctx = NULL;
        lua_pushnil(L);
        lua_pushstring(L, "未找到音频编码器");
        return 2;
    }

    /* 创建输出流 */
    r->out_stream = avformat_new_stream(r->out_ctx, NULL);

    /* 配置编码器 */
    r->enc_ctx = avcodec_alloc_context3(encoder);
    r->enc_ctx->sample_rate = sample_rate;
    r->enc_ctx->time_base   = (AVRational){1, sample_rate};

    /* 选择编码器支持的采样格式 */
    enum AVSampleFormat enc_fmt = AV_SAMPLE_FMT_S16;
    if (encoder->sample_fmts) {
        enc_fmt = encoder->sample_fmts[0];
        /* 优先选择 S16（如果编码器支持），避免不必要的重采样 */
        for (int i = 0; encoder->sample_fmts[i] != AV_SAMPLE_FMT_NONE; i++) {
            if (encoder->sample_fmts[i] == AV_SAMPLE_FMT_S16) {
                enc_fmt = AV_SAMPLE_FMT_S16;
                break;
            }
        }
    }
    r->enc_ctx->sample_fmt = enc_fmt;

    AVChannelLayout mono_layout = AV_CHANNEL_LAYOUT_MONO;
    av_channel_layout_copy(&r->enc_ctx->ch_layout, &mono_layout);

    /* 比特率（仅压缩格式需要） */
    if (codec_id != AV_CODEC_ID_PCM_S16LE && codec_id != AV_CODEC_ID_PCM_S16BE)
        r->enc_ctx->bit_rate = 128000;

    if (r->out_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        r->enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_open2(r->enc_ctx, encoder, NULL);
    if (ret < 0) {
        avcodec_free_context(&r->enc_ctx);
        avformat_free_context(r->out_ctx); r->out_ctx = NULL;
        lua_pushnil(L);
        lua_pushstring(L, "无法打开编码器");
        return 2;
    }

    avcodec_parameters_from_context(r->out_stream->codecpar, r->enc_ctx);
    r->out_stream->time_base = r->enc_ctx->time_base;

    /* 打开输出文件 */
    if (!(r->out_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&r->out_ctx->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            avcodec_free_context(&r->enc_ctx);
            avformat_free_context(r->out_ctx); r->out_ctx = NULL;
            lua_pushnil(L);
            lua_pushfstring(L, "无法打开输出文件: %s", filename);
            return 2;
        }
    }
    avformat_write_header(r->out_ctx, NULL);

    /* 分配编码帧 */
    r->frame_size = r->enc_ctx->frame_size > 0 ? r->enc_ctx->frame_size : 1024;
    r->enc_frame = av_frame_alloc();
    r->enc_frame->nb_samples = r->frame_size;
    r->enc_frame->format     = r->enc_ctx->sample_fmt;
    av_channel_layout_copy(&r->enc_frame->ch_layout, &r->enc_ctx->ch_layout);
    av_frame_get_buffer(r->enc_frame, 0);

    /*
     * 如果编码器要求的格式不是 S16，需要创建重采样器
     * SDL 采集: S16 / mono / 44100Hz
     * 编码器: enc_fmt / mono / 44100Hz
     */
    r->swr_ctx = NULL;
    if (enc_fmt != AV_SAMPLE_FMT_S16) {
        r->swr_ctx = swr_alloc();
        av_opt_set_chlayout(r->swr_ctx, "in_chlayout",  &mono_layout, 0);
        av_opt_set_int(r->swr_ctx,      "in_sample_rate",  sample_rate, 0);
        av_opt_set_sample_fmt(r->swr_ctx, "in_sample_fmt",  AV_SAMPLE_FMT_S16, 0);

        av_opt_set_chlayout(r->swr_ctx, "out_chlayout", &mono_layout, 0);
        av_opt_set_int(r->swr_ctx,      "out_sample_rate", sample_rate, 0);
        av_opt_set_sample_fmt(r->swr_ctx, "out_sample_fmt", enc_fmt, 0);
        swr_init(r->swr_ctx);
    }

    /* 分配 PCM 缓冲区（约 1 秒音频，充分吸收编码延迟） */
    r->pcm_buf_size = sample_rate * 1 * sizeof(int16_t);  /* 1秒 mono S16 */
    r->pcm_buf      = (uint8_t *)SDL_calloc(1, r->pcm_buf_size);

    /* ---- 打开 SDL 采集设备 ---- */
    const char *dev_name = NULL;
    if (dev_idx >= 0 && dev_idx < SDL_GetNumAudioDevices(1))
        dev_name = SDL_GetAudioDeviceName(dev_idx, 1);

    SDL_AudioSpec wanted;
    SDL_zero(wanted);
    wanted.freq     = sample_rate;
    wanted.format   = AUDIO_S16SYS;
    wanted.channels = 1; /* 单声道录音 */
    wanted.samples  = 1024;
    wanted.callback = capture_callback;
    wanted.userdata = r;

    r->capture_dev = SDL_OpenAudioDevice(dev_name, 1, &wanted, &r->capture_spec, 0);
    if (!r->capture_dev) {
        /* 清理已分配资源 */
        if (r->swr_ctx) swr_free(&r->swr_ctx);
        av_frame_free(&r->enc_frame);
        avcodec_free_context(&r->enc_ctx);
        if (r->out_ctx && r->out_ctx->pb) avio_closep(&r->out_ctx->pb);
        avformat_free_context(r->out_ctx); r->out_ctx = NULL;
        SDL_free(r->pcm_buf); r->pcm_buf = NULL;
        lua_pushnil(L);
        lua_pushfstring(L, "无法打开采集设备: %s", SDL_GetError());
        return 2;
    }

    return 1; /* 返回 recorder userdata */
}

/* recorder:Start() — 开始录制 */
static int LUA_RecorderStart(lua_State *L)
{
    GFF_Recorder *r = (GFF_Recorder *)luaL_checkudata(L, 1, GFF_RECORDER_MT);
    if (r->recording) return 0;

    r->recording = 1;
    r->quit_flag = 0;
    r->samples_written = 0;
    r->pcm_buf_used = 0;

    /* 启动采集设备 */
    SDL_PauseAudioDevice(r->capture_dev, 0);

    /* 启动编码线程 */
    r->record_thread = SDL_CreateThread(record_thread_func, "gff_record", r);
    return 0;
}

/* recorder:Stop() — 停止录制并写入文件尾 */
static int LUA_RecorderStop(lua_State *L)
{
    GFF_Recorder *r = (GFF_Recorder *)luaL_checkudata(L, 1, GFF_RECORDER_MT);
    if (!r->recording) return 0;

    /* 暂停采集 */
    SDL_PauseAudioDevice(r->capture_dev, 1);

    /* 停止编码线程 */
    r->quit_flag = 1;
    if (r->record_thread) {
        SDL_WaitThread(r->record_thread, NULL);
        r->record_thread = NULL;
    }
    r->recording = 0;

    /* 写文件尾 + 关闭文件（WAV 需要此步写入正确的 RIFF 大小） */
    if (r->out_ctx) {
        av_write_trailer(r->out_ctx);
        if (r->out_ctx->pb) avio_closep(&r->out_ctx->pb);
        avformat_free_context(r->out_ctx);
        r->out_ctx = NULL;  /* 防止 Close/GC 重复操作 */
    }

    return 0;
}

/* recorder:GetDuration() → 秒 */
static int LUA_RecorderGetDuration(lua_State *L)
{
    GFF_Recorder *r = (GFF_Recorder *)luaL_checkudata(L, 1, GFF_RECORDER_MT);
    lua_pushnumber(L, r->duration);
    return 1;
}

/* recorder:Close() 或 __gc — 释放所有资源 */
static int LUA_RecorderClose(lua_State *L)
{
    GFF_Recorder *r = (GFF_Recorder *)luaL_checkudata(L, 1, GFF_RECORDER_MT);

    /* 确保停止录制 */
    if (r->recording) {
        /* 仅在 SDL 音频子系统仍在运行时操作设备 */
        if (r->capture_dev && SDL_WasInit(SDL_INIT_AUDIO))
            SDL_PauseAudioDevice(r->capture_dev, 1);
        r->quit_flag = 1;
        if (r->record_thread) {
            SDL_WaitThread(r->record_thread, NULL);
            r->record_thread = NULL;
        }
        r->recording = 0;
    }

    /* 写文件尾（如果 Stop 没有调用过） */
    if (r->out_ctx) {
        av_write_trailer(r->out_ctx);
        if (r->out_ctx->pb) avio_closep(&r->out_ctx->pb);
        avformat_free_context(r->out_ctx);
        r->out_ctx = NULL;
    }

    /* 释放 FFmpeg */
    if (r->swr_ctx)   swr_free(&r->swr_ctx);
    if (r->enc_frame) av_frame_free(&r->enc_frame);
    if (r->enc_ctx)   avcodec_free_context(&r->enc_ctx);

    /* 释放 SDL（仅在子系统仍活跃时） */
    if (r->capture_dev && SDL_WasInit(SDL_INIT_AUDIO)) {
        SDL_CloseAudioDevice(r->capture_dev);
    }
    r->capture_dev = 0;

    /* 释放缓冲 */
    if (r->pcm_buf) { SDL_free(r->pcm_buf); r->pcm_buf = NULL; }
    if (r->mutex && SDL_WasInit(0))  { SDL_DestroyMutex(r->mutex); }
    r->mutex = NULL;

    return 0;
}

/* ==================== Recorder Metatable 注册 ==================== */

static const luaL_Reg recorder_methods[] = {
    {"Start",       LUA_RecorderStart},
    {"Stop",        LUA_RecorderStop},
    {"GetDuration", LUA_RecorderGetDuration},
    {"Close",       LUA_RecorderClose},
    {"__gc",        LUA_RecorderClose},
    {NULL, NULL}
};

int gff_recorder_register(lua_State *L)
{
    luaL_newmetatable(L, GFF_RECORDER_MT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, recorder_methods, 0);
    lua_pop(L, 1);
    return 0;
}
