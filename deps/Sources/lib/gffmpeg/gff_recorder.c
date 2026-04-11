/*
 * gff_recorder.c — 麦克风音频录制模块
 *
 * 功能：通过 SDL 音频采集捕获麦克风输入，使用 FFmpeg 编码器写入文件。
 * 支持输出格式：WAV（PCM）、MP4/AAC、OGG 等
 * 格式由输出文件扩展名自动推断。
 *
 * 采集流程：SDL 回调(硬件原生格式) → PCM 缓冲 → SwrContext 转换 → 编码 → 写文件
 */

#include "gffmpeg.h"
#include <string.h>

/* ==================== 辅助函数 ==================== */

/* 将 SDL 音频格式映射到 FFmpeg 采样格式 */
static enum AVSampleFormat sdl_fmt_to_av(SDL_AudioFormat fmt)
{
    switch (fmt) {
        case AUDIO_U8:     return AV_SAMPLE_FMT_U8;
        case AUDIO_S16SYS: return AV_SAMPLE_FMT_S16;
        case AUDIO_S32SYS: return AV_SAMPLE_FMT_S32;
        case AUDIO_F32SYS: return AV_SAMPLE_FMT_FLT;
        default:           return AV_SAMPLE_FMT_S16;  /* 回退 */
    }
}

/* 获取 SDL 音频格式每个样本的字节数 */
static int sdl_bytes_per_sample(SDL_AudioFormat fmt)
{
    return SDL_AUDIO_BITSIZE(fmt) / 8;
}

/* ==================== SDL 采集回调 ==================== */

/* SDL 音频采集回调：将麦克风原始数据写入缓冲 */
static void capture_callback(void *userdata, Uint8 *stream, int len)
{
    GFF_Recorder *r = (GFF_Recorder *)userdata;
    if (!r->recording) return;

    SDL_LockMutex(r->mutex);
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

        av_packet_rescale_ts(pkt, r->enc_ctx->time_base, r->out_stream->time_base);
        pkt->stream_index = r->out_stream->index;
        av_interleaved_write_frame(r->out_ctx, pkt);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return 0;
}

/*
 * 录制线程：从 PCM 缓冲取采集数据 → SwrContext 转换 → 编码 → 写文件
 * capture_frame_bytes = frame_size * capture_channels * capture_bytes_per_sample
 */
static int record_thread_func(void *data)
{
    GFF_Recorder *r = (GFF_Recorder *)data;
    int cfb = r->capture_frame_bytes;  /* 每帧在采集格式下的总字节数 */

    while (!r->quit_flag) {
        SDL_Delay(5);

        SDL_LockMutex(r->mutex);
        if (r->pcm_buf_used < cfb) {
            SDL_UnlockMutex(r->mutex);
            continue;
        }

        av_frame_make_writable(r->enc_frame);

        /* 始终通过 SwrContext 转换: SDL原生格式 → 编码器格式 */
        const uint8_t *in_data = r->pcm_buf;
        uint8_t *out_ptrs[8];
        for (int i = 0; i < 8; i++)
            out_ptrs[i] = r->enc_frame->data[i];

        swr_convert(r->swr_ctx,
            out_ptrs, r->frame_size,
            &in_data, r->frame_size);

        /* 移除已消费的数据 */
        int remain = r->pcm_buf_used - cfb;
        if (remain > 0)
            memmove(r->pcm_buf, r->pcm_buf + cfb, remain);
        r->pcm_buf_used = remain;
        SDL_UnlockMutex(r->mutex);

        r->enc_frame->pts = r->samples_written;
        r->samples_written += r->frame_size;
        r->duration = (double)r->samples_written / r->enc_ctx->sample_rate;

        encode_and_write(r, r->enc_frame);
    }

    /* 刷新编码器残留数据 */
    encode_and_write(r, NULL);
    return 0;
}

/* ==================== Lua 绑定 ==================== */

/* ff.ListCaptureDevices() → {"设备1", "设备2", ...} */
int gff_list_capture_devices(lua_State *L)
{
    int count = SDL_GetNumAudioDevices(1);
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
    int dev_idx = (int)luaL_optinteger(L, 2, -1);

    GFF_Recorder *r = (GFF_Recorder *)lua_newuserdata(L, sizeof(GFF_Recorder));
    memset(r, 0, sizeof(GFF_Recorder));
    luaL_setmetatable(L, GFF_RECORDER_MT);

    r->mutex = SDL_CreateMutex();
    int sample_rate = GFF_AUDIO_TARGET_FREQ;

    /* ---- 创建 FFmpeg 输出上下文 ---- */
    int ret = avformat_alloc_output_context2(&r->out_ctx, NULL, NULL, filename);
    if (ret < 0 || !r->out_ctx) {
        lua_pushnil(L);
        lua_pushfstring(L, "无法创建输出格式: %s", filename);
        return 2;
    }

    enum AVCodecID codec_id = r->out_ctx->oformat->audio_codec;
    if (codec_id == AV_CODEC_ID_NONE) codec_id = AV_CODEC_ID_PCM_S16LE;

    const AVCodec *encoder = avcodec_find_encoder(codec_id);
    if (!encoder) {
        avformat_free_context(r->out_ctx); r->out_ctx = NULL;
        lua_pushnil(L);
        lua_pushstring(L, "未找到音频编码器");
        return 2;
    }

    r->out_stream = avformat_new_stream(r->out_ctx, NULL);

    /* 配置编码器 */
    r->enc_ctx = avcodec_alloc_context3(encoder);
    r->enc_ctx->sample_rate = sample_rate;
    r->enc_ctx->time_base   = (AVRational){1, sample_rate};

    /* 选择编码器支持的采样格式（优先 S16 以减少转换开销） */
    enum AVSampleFormat enc_fmt = AV_SAMPLE_FMT_S16;
    if (encoder->sample_fmts) {
        enc_fmt = encoder->sample_fmts[0];
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

    /* ---- 打开 SDL 采集设备（允许硬件自动调整参数） ---- */
    const char *dev_name = NULL;
    if (dev_idx >= 0 && dev_idx < SDL_GetNumAudioDevices(1))
        dev_name = SDL_GetAudioDeviceName(dev_idx, 1);

    SDL_AudioSpec wanted;
    SDL_zero(wanted);
    wanted.freq     = sample_rate;
    wanted.format   = AUDIO_S16SYS;
    wanted.channels = 1;
    wanted.samples  = 1024;
    wanted.callback = capture_callback;
    wanted.userdata = r;

    /* 允许 SDL 使用硬件原生参数，避免底层强制转换产生杂音 */
    r->capture_dev = SDL_OpenAudioDevice(dev_name, 1, &wanted, &r->capture_spec,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
        SDL_AUDIO_ALLOW_FORMAT_CHANGE |
        SDL_AUDIO_ALLOW_CHANNELS_CHANGE);

    if (!r->capture_dev) {
        av_frame_free(&r->enc_frame);
        avcodec_free_context(&r->enc_ctx);
        if (r->out_ctx && r->out_ctx->pb) avio_closep(&r->out_ctx->pb);
        avformat_free_context(r->out_ctx); r->out_ctx = NULL;
        SDL_free(r->pcm_buf); r->pcm_buf = NULL;
        lua_pushnil(L);
        lua_pushfstring(L, "无法打开采集设备: %s", SDL_GetError());
        return 2;
    }

    /*
     * 根据 SDL 实际采集参数创建 SwrContext
     * 采集端: capture_spec (可能是 48kHz/stereo/float32)
     * 编码端: sample_rate/mono/enc_fmt
     */
    int cap_freq = r->capture_spec.freq;
    int cap_ch   = r->capture_spec.channels;
    enum AVSampleFormat cap_av_fmt = sdl_fmt_to_av(r->capture_spec.format);

    /* 每消费一帧编码器数据，需要从采集缓冲读取的字节数 */
    int cap_samples_per_frame = r->frame_size;
    /* 如果采集采样率 ≠ 编码器采样率，需要按比例调整输入样本数 */
    if (cap_freq != sample_rate) {
        cap_samples_per_frame = (int)((int64_t)r->frame_size * cap_freq / sample_rate) + 1;
    }
    r->capture_frame_bytes = cap_samples_per_frame * cap_ch * sdl_bytes_per_sample(r->capture_spec.format);

    /* 构建重采样器: 采集格式 → 编码器格式 */
    AVChannelLayout cap_layout;
    if (cap_ch == 1) {
        cap_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    } else {
        cap_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
    }

    r->swr_ctx = swr_alloc();
    av_opt_set_chlayout(r->swr_ctx,    "in_chlayout",   &cap_layout, 0);
    av_opt_set_int(r->swr_ctx,         "in_sample_rate",  cap_freq, 0);
    av_opt_set_sample_fmt(r->swr_ctx,   "in_sample_fmt",   cap_av_fmt, 0);
    av_opt_set_chlayout(r->swr_ctx,    "out_chlayout",  &mono_layout, 0);
    av_opt_set_int(r->swr_ctx,         "out_sample_rate", sample_rate, 0);
    av_opt_set_sample_fmt(r->swr_ctx,   "out_sample_fmt",  enc_fmt, 0);
    swr_init(r->swr_ctx);

    /* 分配 PCM 缓冲区（足够容纳多帧采集数据） */
    r->pcm_buf_size = r->capture_frame_bytes * 8;
    r->pcm_buf      = (uint8_t *)SDL_calloc(1, r->pcm_buf_size);

    return 1;
}

/* recorder:Start() */
static int LUA_RecorderStart(lua_State *L)
{
    GFF_Recorder *r = (GFF_Recorder *)luaL_checkudata(L, 1, GFF_RECORDER_MT);
    if (r->recording) return 0;

    r->recording = 1;
    r->quit_flag = 0;
    r->samples_written = 0;
    r->pcm_buf_used = 0;

    SDL_PauseAudioDevice(r->capture_dev, 0);
    r->record_thread = SDL_CreateThread(record_thread_func, "gff_record", r);
    return 0;
}

/* recorder:Stop() — 停止录制并写入文件尾 */
static int LUA_RecorderStop(lua_State *L)
{
    GFF_Recorder *r = (GFF_Recorder *)luaL_checkudata(L, 1, GFF_RECORDER_MT);
    if (!r->recording) return 0;

    SDL_PauseAudioDevice(r->capture_dev, 1);

    r->quit_flag = 1;
    if (r->record_thread) {
        SDL_WaitThread(r->record_thread, NULL);
        r->record_thread = NULL;
    }
    r->recording = 0;

    /* 写文件尾 + 关闭文件 */
    if (r->out_ctx) {
        av_write_trailer(r->out_ctx);
        if (r->out_ctx->pb) avio_closep(&r->out_ctx->pb);
        avformat_free_context(r->out_ctx);
        r->out_ctx = NULL;
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

/* recorder:Close() 或 __gc */
static int LUA_RecorderClose(lua_State *L)
{
    GFF_Recorder *r = (GFF_Recorder *)luaL_checkudata(L, 1, GFF_RECORDER_MT);

    /* 确保停止录制 */
    if (r->recording) {
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

    if (r->swr_ctx)   swr_free(&r->swr_ctx);
    if (r->enc_frame) av_frame_free(&r->enc_frame);
    if (r->enc_ctx)   avcodec_free_context(&r->enc_ctx);
    if (r->capture_dev) { SDL_CloseAudioDevice(r->capture_dev); r->capture_dev = 0; }
    if (r->pcm_buf) { SDL_free(r->pcm_buf); r->pcm_buf = NULL; }
    if (r->mutex)   { SDL_DestroyMutex(r->mutex); r->mutex = NULL; }

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
