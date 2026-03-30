#include "gge.h"
#include <SDL_mixer.h>
#include <stdio.h>
#include <sndfile.h>

/* Apply fade-in/fade-out to a loaded Mix_Chunk to eliminate pop/click
 * artifacts caused by DC discontinuity at start/end of playback.
 * Simulates the anti-click processing that FMOD did internally. */
static void mix_chunk_apply_fade(Mix_Chunk* mc)
{
    if (!mc || !mc->abuf || mc->alen == 0)
        return;

    int freq, channels;
    Uint16 format;
    if (!Mix_QuerySpec(&freq, &format, &channels))
        return;

    int sample_size;
    switch (format & 0xFF) {
        case 8:  sample_size = 1; break;
        case 16: sample_size = 2; break;
        case 32: sample_size = 4; break;
        default: return;
    }
    int is_float = (format & (1 << 8)) != 0; /* SDL_AUDIO_MASK_DATATYPE */

    int frame_size = sample_size * channels;
    if (frame_size <= 0)
        return;
    int total_frames = (int)(mc->alen / (Uint32)frame_size);
    
    /* Only apply fade-out (anti-click for trailing DC offsets).
     * Do NOT apply fade-in to the static buffer, as doing so destroys 
     * the initial transient "punch" of attack sounds. */
    int fade_frames = 1024; /* ~23ms at 44100Hz, long enough to hide severe cut-offs */
    if (fade_frames > total_frames / 2)
        fade_frames = total_frames / 2;
    if (fade_frames <= 0)
        return;

    if (sample_size == 2) {
        Sint16* s = (Sint16*)mc->abuf;

        /* Fade-out only */
        for (int f = total_frames - fade_frames; f < total_frames; f++) {
            double t = (double)(total_frames - 1 - f) / (double)fade_frames;
            t = t * t * (3.0 - 2.0 * t); /* Smoothstep curve for 0 derivative at edges */
            for (int c = 0; c < channels; c++)
                s[f * channels + c] = (Sint16)(s[f * channels + c] * t);
        }
    } else if (sample_size == 4) {
        if (is_float) {
            float* s = (float*)mc->abuf;
            /* Fade-out only */
            for (int f = total_frames - fade_frames; f < total_frames; f++) {
                double t = (double)(total_frames - 1 - f) / (double)fade_frames;
                t = t * t * (3.0 - 2.0 * t);
                for (int c = 0; c < channels; c++)
                    s[f * channels + c] = (float)(s[f * channels + c] * t);
            }
        } else {
            Sint32* s = (Sint32*)mc->abuf;
            /* Fade-out only (S32 integer path) */
            for (int f = total_frames - fade_frames; f < total_frames; f++) {
                double t = (double)(total_frames - 1 - f) / (double)fade_frames;
                t = t * t * (3.0 - 2.0 * t);
                for (int c = 0; c < channels; c++)
                    s[f * channels + c] = (Sint32)(s[f * channels + c] * t);
            }
        }
    }
}

/* libsndfile virtual IO adapter for SDL_RWops */
typedef struct {
    SDL_RWops* rw;
    Sint64 size;
} RWopsVioData;

static sf_count_t vio_get_filelen(void* user_data) {
    RWopsVioData* d = (RWopsVioData*)user_data;
    return (sf_count_t)d->size;
}

static sf_count_t vio_seek(sf_count_t offset, int whence, void* user_data) {
    RWopsVioData* d = (RWopsVioData*)user_data;
    if (SDL_RWseek(d->rw, (Sint64)offset, whence) < 0) return -1;
    return (sf_count_t)SDL_RWtell(d->rw);
}

static sf_count_t vio_read(void* ptr, sf_count_t count, void* user_data) {
    RWopsVioData* d = (RWopsVioData*)user_data;
    return (sf_count_t)SDL_RWread(d->rw, ptr, 1, (size_t)count);
}

static sf_count_t vio_write(const void* ptr, sf_count_t count, void* user_data) {
    (void)ptr; (void)count; (void)user_data;
    return 0; /* read-only */
}

static sf_count_t vio_tell(void* user_data) {
    RWopsVioData* d = (RWopsVioData*)user_data;
    return (sf_count_t)SDL_RWtell(d->rw);
}

/* High-quality WAV loader using libsndfile + SDL_AudioStream.
 * Replaces SDL_AudioCVT with SDL_AudioStream for artifact-free resampling.
 * This is the PRIMARY loading path for all WAV audio. */
static Mix_Chunk* LoadWAV_RW_sndfile(SDL_RWops* rw)
{
    SF_VIRTUAL_IO vio = { vio_get_filelen, vio_seek, vio_read, vio_write, vio_tell };
    RWopsVioData vdata;
    SF_INFO sfinfo;
    SNDFILE* sf;
    short* pcm;
    sf_count_t total_items, read_items;
    int freq, channels;
    Uint16 format;

    /* Calculate RWops size for libsndfile */
    Sint64 cur = SDL_RWtell(rw);
    if (cur < 0) return NULL;
    if (SDL_RWseek(rw, 0, RW_SEEK_END) < 0) { SDL_RWseek(rw, cur, RW_SEEK_SET); return NULL; }
    vdata.size = SDL_RWtell(rw);
    if (vdata.size <= 0) { SDL_RWseek(rw, cur, RW_SEEK_SET); return NULL; }
    SDL_RWseek(rw, cur, RW_SEEK_SET);
    vdata.rw = rw;

    memset(&sfinfo, 0, sizeof(sfinfo));
    sf = sf_open_virtual(&vio, SFM_READ, &sfinfo, &vdata);
    if (!sf) return NULL;

    total_items = sfinfo.frames * sfinfo.channels;
    pcm = (short*)SDL_malloc(total_items * sizeof(short));
    if (!pcm) { sf_close(sf); return NULL; }

    read_items = sf_read_short(sf, pcm, total_items);
    sf_close(sf);

    if (read_items <= 0) { SDL_free(pcm); return NULL; }

    /* Query mixer spec for conversion target */
    if (!Mix_QuerySpec(&freq, &format, &channels)) {
        SDL_free(pcm);
        return NULL;
    }

    Uint32 src_len = (Uint32)(read_items * sizeof(short));

    /* Use SDL_AudioStream for high-quality resampling (replaces SDL_AudioCVT) */
    SDL_AudioStream* stream = SDL_NewAudioStream(
        AUDIO_S16SYS, (Uint8)sfinfo.channels, sfinfo.samplerate,
        format, (Uint8)channels, freq);
    if (!stream) {
        SDL_free(pcm);
        return NULL;
    }

    if (SDL_AudioStreamPut(stream, pcm, (int)src_len) < 0) {
        SDL_FreeAudioStream(stream);
        SDL_free(pcm);
        return NULL;
    }
    SDL_free(pcm);

    SDL_AudioStreamFlush(stream);

    int avail = SDL_AudioStreamAvailable(stream);
    if (avail <= 0) {
        SDL_FreeAudioStream(stream);
        return NULL;
    }

    Uint8* abuf = (Uint8*)SDL_malloc((size_t)avail);
    if (!abuf) {
        SDL_FreeAudioStream(stream);
        return NULL;
    }

    int got = SDL_AudioStreamGet(stream, abuf, avail);
    SDL_FreeAudioStream(stream);

    if (got <= 0) {
        SDL_free(abuf);
        return NULL;
    }

    Mix_Chunk* chunk = (Mix_Chunk*)SDL_malloc(sizeof(Mix_Chunk));
    if (!chunk) { SDL_free(abuf); return NULL; }

    chunk->allocated = 1;
    chunk->abuf = abuf;
    chunk->alen = (Uint32)got;
    chunk->volume = MIX_MAX_VOLUME;
    return chunk;
}

static int LUA_Mix_Linked_Version(lua_State* L)
{
    const SDL_version* ver = Mix_Linked_Version();
    lua_pushfstring(L, "SDL_MIX %d.%d.%d", ver->major, ver->minor, ver->patch);
    return 1;
}

static int LUA_Mix_Init(lua_State* L)
{
    int flags = (int)luaL_optinteger(L, 1, MIX_INIT_FLAC | MIX_INIT_MP3 | MIX_INIT_OGG);
    lua_pushboolean(L, Mix_Init(flags) == flags);
    return 1;
}

static int LUA_Mix_Quit(lua_State* L)
{
    Mix_Quit();
    return 0;
}

static int LUA_Mix_OpenAudio(lua_State* L)
{
    int frequency = (int)luaL_checkinteger(L, 1);
    int format = (int)luaL_checkinteger(L, 2);
    int channels = (int)luaL_checkinteger(L, 3);
    int chunksize = (int)luaL_checkinteger(L, 4);
    lua_pushboolean(L, Mix_OpenAudio(frequency, format, channels, chunksize) == 0);
    return 1;
}

static int LUA_Mix_OpenAudioDevice(lua_State* L)
{
    int frequency = (int)luaL_checkinteger(L, 1);
    int format = (int)luaL_checkinteger(L, 2);
    int channels = (int)luaL_checkinteger(L, 3);
    int chunksize = (int)luaL_checkinteger(L, 4);
    const char* device = luaL_checkstring(L, 5);
    int allowed_changes = (int)luaL_checkinteger(L, 6);

    lua_pushboolean(L, Mix_OpenAudioDevice(frequency, format, channels, chunksize, device, allowed_changes) == 0);
    return 1;
}

static int LUA_Mix_AllocateChannels(lua_State* L)
{
    int numchans = (int)luaL_checkinteger(L, 1);

    lua_pushinteger(L, Mix_AllocateChannels(numchans));
    return 1;
}

static int LUA_Mix_QuerySpec(lua_State* L)
{
    int frequency = 0, channels = 0;
    Uint16 format = 0;
    if (!Mix_QuerySpec(&frequency, &format, &channels))
        return 0;
    lua_pushinteger(L, frequency);
    lua_pushinteger(L, format);
    lua_pushinteger(L, channels);
    return 3;
}

static int LUA_Mix_LoadWAV_RW(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    Sint64 start = SDL_RWtell(rw);

    /* PRIMARY path: libsndfile + SDL_AudioStream for high-quality loading */
    Mix_Chunk* mc = LoadWAV_RW_sndfile(rw);

    /* Fallback to SDL_mixer's internal loader if sndfile fails */
    if (!mc) {
        SDL_RWseek(rw, start, RW_SEEK_SET);
        mc = Mix_LoadWAV_RW(rw, 0);
    }

    if (mc)
    {
        mix_chunk_apply_fade(mc);
        Mix_Chunk** ud = (Mix_Chunk**)lua_newuserdata(L, sizeof(Mix_Chunk*));
        *ud = mc;
        luaL_setmetatable(L, "Mix_Chunk");
        return 1;
    }
    return 0;
}

static int LUA_Mix_LoadWAV(lua_State* L)
{
    const char* file = luaL_checkstring(L, 1);

    /* Try high-quality sndfile path first via RWops */
    Mix_Chunk* mc = NULL;
    SDL_RWops* rw = SDL_RWFromFile(file, "rb");
    if (rw) {
        mc = LoadWAV_RW_sndfile(rw);
        SDL_RWclose(rw);
    }

    /* Fallback to SDL_mixer's internal loader */
    if (!mc)
        mc = Mix_LoadWAV(file);

    if (mc)
    {
        mix_chunk_apply_fade(mc);
        Mix_Chunk** ud = (Mix_Chunk**)lua_newuserdata(L, sizeof(Mix_Chunk*));
        *ud = mc;
        luaL_setmetatable(L, "Mix_Chunk");
        return 1;
    }
    return 0;
}

static int LUA_Mix_LoadMUS(lua_State* L)
{
    const char* file = luaL_checkstring(L, 1);
    Mix_Music* mm = Mix_LoadMUS(file);

    if (mm)
    {
        Mix_Music** ud = (Mix_Music**)lua_newuserdata(L, sizeof(Mix_Music*));
        *ud = mm;
        luaL_setmetatable(L, "Mix_Music");
        return 1;
    }
    return 0;
}

static int LUA_Mix_LoadMUS_RW(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    Mix_Music* mc = Mix_LoadMUS_RW(rw, 0);

    if (mc)
    {
        Mix_Music** ud = (Mix_Music**)lua_newuserdata(L, sizeof(Mix_Music*));
        *ud = mc;
        luaL_setmetatable(L, "Mix_Music");
        return 1;
    }
    return 0;
}
//TODO
//Mix_LoadMUSType_RW
//Mix_QuickLoad_WAV
//Mix_QuickLoad_RAW

static int LUA_Mix_FreeChunk(lua_State* L)
{
    Mix_Chunk** mc = (Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    if (*mc)
    {
        Mix_FreeChunk(*mc);
        *mc = NULL;
    }
    return 0;
}

static int LUA_Mix_FreeMusic(lua_State* L)
{
    Mix_Music** mm = (Mix_Music**)luaL_checkudata(L, 1, "Mix_Music");
    if (*mm)
    {
        Mix_FreeMusic(*mm);
        *mm = NULL;
    }

    return 0;
}

static int LUA_Mix_GetNumChunkDecoders(lua_State* L)
{
    lua_pushinteger(L, Mix_GetNumChunkDecoders());
    return 1;
}

static int LUA_Mix_GetChunkDecoder(lua_State* L)
{
    int index = (int)luaL_checkinteger(L, 1);
    lua_pushstring(L, Mix_GetChunkDecoder(index));
    return 1;
}

static int LUA_Mix_HasChunkDecoder(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    lua_pushboolean(L, Mix_HasChunkDecoder(name));
    return 1;
}

static int LUA_Mix_GetNumMusicDecoders(lua_State* L)
{
    lua_pushinteger(L, Mix_GetNumMusicDecoders());
    return 1;
}

static int LUA_Mix_GetMusicDecoder(lua_State* L)
{
    int index = (int)luaL_checkinteger(L, 1);
    lua_pushstring(L, Mix_GetMusicDecoder(index));
    return 1;
}

static int LUA_Mix_HasMusicDecoder(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    lua_pushboolean(L, Mix_HasMusicDecoder(name));
    return 1;
}

static int LUA_Mix_GetMusicType(lua_State* L)
{
    Mix_Music* mm = *(Mix_Music**)luaL_checkudata(L, 1, "Mix_Music");
    lua_pushinteger(L, Mix_GetMusicType(mm));
    return 1;
}

//Mix_SetPostMix
//Mix_HookMusic
//Mix_HookMusicFinished
//Mix_GetMusicHookData
//Mix_ChannelFinished
//Mix_RegisterEffect
//Mix_UnregisterEffect
static int LUA_Mix_UnregisterAllEffects(lua_State* L)
{
    int channel = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, Mix_UnregisterAllEffects(channel));
    return 1;
}

static int LUA_Mix_SetPanning(lua_State* L) //左右？
{
    Mix_Chunk* mc = *(Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    int channel = (int)luaL_checkinteger(L, 2);
    Uint8 left = (Uint8)luaL_checkinteger(L, 3);
    Uint8 right = (Uint8)luaL_checkinteger(L, 4);
    if (Mix_GetChunk(channel) == mc)
        lua_pushboolean(L, Mix_SetPanning(channel, left, right));
    else
        lua_pushboolean(L, 0);

    return 1;
}

static int LUA_Mix_SetPosition(lua_State* L) //3D？
{
    Mix_Chunk* mc = *(Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    int channel = (int)luaL_checkinteger(L, 2);
    Sint16 angle = (Sint16)luaL_checkinteger(L, 3);
    Uint8 distance = (Uint8)luaL_checkinteger(L, 4);
    if (Mix_GetChunk(channel) == mc)
        lua_pushboolean(L, Mix_SetPosition(channel, angle, distance));
    else
        lua_pushboolean(L, 0);

    return 1;
}

static int LUA_Mix_SetDistance(lua_State* L)
{
    Mix_Chunk* mc = *(Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    int channel = (int)luaL_checkinteger(L, 2);
    Uint8 distance = (Uint8)luaL_checkinteger(L, 3);
    if (Mix_GetChunk(channel) == mc)
        lua_pushboolean(L, Mix_SetDistance(channel, distance));
    else
        lua_pushboolean(L, 0);

    return 1;
}

static int LUA_Mix_SetReverseStereo(lua_State* L)
{
    Mix_Chunk* mc = *(Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    int channel = (int)luaL_checkinteger(L, 2);
    int flip = (int)luaL_checkinteger(L, 3);
    if (Mix_GetChunk(channel) == mc)
        lua_pushboolean(L, Mix_SetReverseStereo(channel, flip));
    else
        lua_pushboolean(L, 0);

    return 1;
}
static int LUA_Mix_ReserveChannels(lua_State* L) //固定通道
{
    int num = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, Mix_ReserveChannels(num));
    return 1;
}

static int LUA_Mix_GroupChannel(lua_State* L) //通道分组
{
    int which = (int)luaL_checkinteger(L, 1);
    int tag = (int)luaL_checkinteger(L, 2);
    lua_pushinteger(L, Mix_GroupChannel(which, tag));
    return 1;
}

static int LUA_Mix_GroupChannels(lua_State* L) //通道分组
{
    int from = (int)luaL_checkinteger(L, 1);
    int to = (int)luaL_checkinteger(L, 2);
    int tag = (int)luaL_checkinteger(L, 3);
    lua_pushinteger(L, Mix_GroupChannels(from, to, tag));
    return 1;
}

static int LUA_Mix_GroupAvailable(lua_State* L) //查找分组可用通道
{
    int tag = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, Mix_GroupAvailable(tag));
    return 1;
}

static int LUA_Mix_GroupCount(lua_State* L) //分组数量
{
    int tag = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, Mix_GroupCount(tag));
    return 1;
}

static int LUA_Mix_GroupOldest(lua_State* L) //查找分组中最早播放的
{
    int tag = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, Mix_GroupOldest(tag));
    return 1;
}

static int LUA_Mix_GroupNewer(lua_State* L) //查找分组中最后播放的
{
    int tag = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, Mix_GroupNewer(tag));
    return 1;
}

static int LUA_Mix_PlayChannel(lua_State* L)
{
    Mix_Chunk* mc = *(Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    int channel = (int)luaL_checkinteger(L, 2);
    int loops = (int)luaL_checkinteger(L, 3);
    lua_pushinteger(L, Mix_PlayChannel(channel, mc, loops));
    return 1;
}

static int LUA_Mix_PlayChannelTimed(lua_State* L)
{
    Mix_Chunk* mc = *(Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    int channel = (int)luaL_checkinteger(L, 2);
    int loops = (int)luaL_checkinteger(L, 3);
    int ticks = (int)luaL_checkinteger(L, 4);
    lua_pushinteger(L, Mix_PlayChannelTimed(channel, mc, loops, ticks));
    return 1;
}

static int LUA_Mix_PlayMusic(lua_State* L)
{
    Mix_Music* mm = *(Mix_Music**)luaL_checkudata(L, 1, "Mix_Music");
    int loops = (int)luaL_checkinteger(L, 2);
    lua_pushinteger(L, Mix_PlayMusic(mm, loops));
    return 1;
}

static int LUA_Mix_FadeInMusic(lua_State* L)
{
    Mix_Music* mm = *(Mix_Music**)luaL_checkudata(L, 1, "Mix_Music");
    int loops = (int)luaL_checkinteger(L, 2);
    int ms = (int)luaL_checkinteger(L, 3);
    lua_pushinteger(L, Mix_FadeInMusic(mm, loops, ms));
    return 1;
}

static int LUA_Mix_FadeInMusicPos(lua_State* L)
{
    Mix_Music* mm = *(Mix_Music**)luaL_checkudata(L, 1, "Mix_Music");
    int loops = (int)luaL_checkinteger(L, 2);
    int ms = (int)luaL_checkinteger(L, 3);
    double position = luaL_checknumber(L, 4);
    lua_pushinteger(L, Mix_FadeInMusicPos(mm, loops, ms, position));
    return 1;
}

static int LUA_Mix_FadeInChannel(lua_State* L) //淡入淡出
{
    Mix_Chunk* mc = *(Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    int channel = (int)luaL_checkinteger(L, 2);
    int loops = (int)luaL_checkinteger(L, 3);
    int ms = (int)luaL_checkinteger(L, 4);
    lua_pushinteger(L, Mix_FadeInChannel(channel, mc, loops, ms));
    return 1;
}

static int LUA_Mix_FadeInChannelTimed(lua_State* L)
{
    Mix_Chunk* mc = *(Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    int channel = (int)luaL_checkinteger(L, 2);
    int loops = (int)luaL_checkinteger(L, 3);
    int ms = (int)luaL_checkinteger(L, 4);
    int ticks = (int)luaL_checkinteger(L, 5);
    lua_pushinteger(L, Mix_FadeInChannelTimed(channel, mc, loops, ms, ticks));
    return 1;
}

static int LUA_Mix_Volume(lua_State* L)
{
    Mix_Chunk* mc = *(Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    int channel = (int)luaL_checkinteger(L, 2);
    int volume = (int)luaL_checkinteger(L, 3);
    if (Mix_GetChunk(channel) == mc)
        lua_pushinteger(L, Mix_Volume(channel, volume));
    else
        lua_pushinteger(L, 0);

    return 1;
}

static int LUA_Mix_VolumeChunk(lua_State* L)
{
    Mix_Chunk* mc = *(Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    int volume = (int)luaL_checkinteger(L, 2);

    lua_pushinteger(L, Mix_VolumeChunk(mc, volume));
    return 1;
}

static int LUA_Mix_VolumeMusic(lua_State* L)
{
    int volume = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, Mix_VolumeMusic(volume));
    return 1;
}

static int LUA_Mix_HaltChannel(lua_State* L)
{
    Mix_Chunk* mc = *(Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    int channel = (int)luaL_checkinteger(L, 2);
    if (Mix_GetChunk(channel) == mc)
        lua_pushinteger(L, Mix_HaltChannel(channel));
    else
        lua_pushinteger(L, 0);

    return 1;
}

static int LUA_Mix_HaltGroup(lua_State* L)
{
    int tag = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, Mix_HaltGroup(tag));
    return 1;
}

static int LUA_Mix_HaltMusic(lua_State* L)
{
    lua_pushinteger(L, Mix_HaltMusic());
    return 1;
}

static int LUA_Mix_ExpireChannel(lua_State* L) //加时间
{
    Mix_Chunk* mc = *(Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    int channel = (int)luaL_checkinteger(L, 2);
    int ticks = (int)luaL_checkinteger(L, 3);

    if (Mix_GetChunk(channel) == mc)
        lua_pushinteger(L, Mix_ExpireChannel(channel, ticks));
    else
        lua_pushinteger(L, 0);
    return 1;
}

static int LUA_Mix_FadingMusic(lua_State* L)
{
    lua_pushinteger(L, Mix_FadingMusic());
    return 1;
}

static int LUA_Mix_FadingChannel(lua_State* L)
{
    Mix_Chunk* mc = *(Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    int channel = (int)luaL_checkinteger(L, 2);
    if (Mix_GetChunk(channel) == mc)
        lua_pushinteger(L, Mix_FadingChannel(channel));
    else
        lua_pushinteger(L, 0);

    return 1;
}

static int LUA_Mix_Pause(lua_State* L)
{
    Mix_Chunk* mc = *(Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    int channel = (int)luaL_checkinteger(L, 2);
    if (Mix_GetChunk(channel) == mc)
        Mix_Pause(channel);
    return 0;
}

static int LUA_Mix_Resume(lua_State* L)
{
    Mix_Chunk* mc = *(Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    int channel = (int)luaL_checkinteger(L, 2);
    if (Mix_GetChunk(channel) == mc)
        Mix_Resume(channel);
    return 0;
}

static int LUA_Mix_Paused(lua_State* L)
{
    Mix_Chunk* mc = *(Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    int channel = (int)luaL_checkinteger(L, 2);
    if (Mix_GetChunk(channel) == mc)
        lua_pushboolean(L, Mix_Paused(channel));
    else
        lua_pushboolean(L, 0);
    return 1;
}

static int LUA_Mix_PauseMusic(lua_State* L)
{
    Mix_PauseMusic();
    return 0;
}

static int LUA_Mix_ResumeMusic(lua_State* L)
{
    Mix_ResumeMusic();
    return 0;
}

static int LUA_Mix_RewindMusic(lua_State* L)
{
    Mix_RewindMusic();
    return 0;
}

static int LUA_Mix_PausedMusic(lua_State* L)
{
    lua_pushboolean(L, Mix_PausedMusic());
    return 1;
}

static int LUA_Mix_SetMusicPosition(lua_State* L)
{
    double position = (double)luaL_checknumber(L, 1);
    lua_pushboolean(L, Mix_SetMusicPosition(position) == 0);
    return 1;
}

static int LUA_Mix_Playing(lua_State* L)
{
    Mix_Chunk* mc = *(Mix_Chunk**)luaL_checkudata(L, 1, "Mix_Chunk");
    int channel = (int)luaL_checkinteger(L, 2);
    if (Mix_GetChunk(channel) == mc)
        lua_pushboolean(L, Mix_Playing(channel));
    else
        lua_pushboolean(L, 0);
    return 1;
}

static int LUA_Mix_PlayingMusic(lua_State* L)
{
    lua_pushboolean(L, Mix_PlayingMusic());
    return 1;
}
//Mix_SetMusicCMD
//static int    LUA_Mix_SetSynchroValue(lua_State *L)
//{
//  int value = luaL_checkinteger(L, 1);
//  lua_pushinteger(L,Mix_SetSynchroValue(value));
//  return 1;
//}
//static int    LUA_Mix_GetSynchroValue(lua_State *L)
//{
//  lua_pushinteger(L,Mix_GetSynchroValue());
//  return 1;
//}
//Mix_SetSoundFonts
//Mix_GetSoundFonts
//Mix_EachSoundFont
//Mix_GetChunk
static int LUA_Mix_CloseAudio(lua_State* L)
{
    Mix_CloseAudio();
    return 0;
}

static const luaL_Reg mix_funcs[] = {
    {"Init", LUA_Mix_Init},
    {"Quit", LUA_Mix_Quit},
    {"OpenAudio", LUA_Mix_OpenAudio},
    {"CloseAudio", LUA_Mix_CloseAudio},
    {"OpenAudioDevice", LUA_Mix_OpenAudioDevice},
    {"AllocateChannels", LUA_Mix_AllocateChannels},
    {"QuerySpec", LUA_Mix_QuerySpec},

    {"LoadWAV_RW", LUA_Mix_LoadWAV_RW},
    {"LoadWAV", LUA_Mix_LoadWAV},
    {"LoadMUS", LUA_Mix_LoadMUS},
    {"LoadMUS_RW", LUA_Mix_LoadMUS_RW},
    {"GetNumChunkDecoders", LUA_Mix_GetNumChunkDecoders},
    {"GetChunkDecoder", LUA_Mix_GetChunkDecoder},
    {"HasChunkDecoder", LUA_Mix_HasChunkDecoder},
    {"GetNumMusicDecoders", LUA_Mix_GetNumMusicDecoders},
    {"GetMusicDecoder", LUA_Mix_GetMusicDecoder},

    {"UnregisterAllEffects", LUA_Mix_UnregisterAllEffects},
    {"SetPanning", LUA_Mix_SetPanning},
    {"SetPosition", LUA_Mix_SetPosition},
    {"SetDistance", LUA_Mix_SetDistance},
    {"SetReverseStereo", LUA_Mix_SetReverseStereo},
    {"ReserveChannels", LUA_Mix_ReserveChannels},
    {"GroupChannel", LUA_Mix_GroupChannel},
    {"GroupChannels", LUA_Mix_GroupChannels},
    {"GroupAvailable", LUA_Mix_GroupAvailable},
    {"GroupCount", LUA_Mix_GroupCount},
    {"GroupOldest", LUA_Mix_GroupOldest},
    {"GroupNewer", LUA_Mix_GroupNewer},


    {"VolumeMusic", LUA_Mix_VolumeMusic},

    {"HaltGroup", LUA_Mix_HaltGroup},
    {"HaltMusic", LUA_Mix_HaltMusic},
    {"ExpireChannel", LUA_Mix_ExpireChannel},
    {"FadingMusic", LUA_Mix_FadingMusic},


    {"PauseMusic", LUA_Mix_PauseMusic},
    {"ResumeMusic", LUA_Mix_ResumeMusic},
    {"RewindMusic", LUA_Mix_RewindMusic},
    {"PausedMusic", LUA_Mix_PausedMusic},
    {"SetMusicPosition", LUA_Mix_SetMusicPosition},

    {"PlayingMusic", LUA_Mix_PlayingMusic},
    //{"SetSynchroValue", LUA_Mix_SetSynchroValue},
    //{"GetSynchroValue", LUA_Mix_GetSynchroValue},
    {NULL, NULL}
};

static const luaL_Reg chunk_funcs[] = {
    {"__index", NULL},
    {"__gc", LUA_Mix_FreeChunk},
    {"PlayChannel", LUA_Mix_PlayChannel},
    {"PlayChannelTimed", LUA_Mix_PlayChannelTimed},
    {"FadeInChannel", LUA_Mix_FadeInChannel},
    {"FadeInChannelTimed", LUA_Mix_FadeInChannelTimed},
    {"VolumeChunk", LUA_Mix_VolumeChunk},
    {"HaltChannel", LUA_Mix_HaltChannel},
    {"Pause", LUA_Mix_Pause},
    {"Resume", LUA_Mix_Resume},
    {"Paused", LUA_Mix_Paused},
    {"Playing", LUA_Mix_Playing},
    {"FadingChannel", LUA_Mix_FadingChannel},
    {"Volume", LUA_Mix_Volume},
    {NULL, NULL}
};

static const luaL_Reg music_funcs[] = {
    {"__index", NULL},
    {"__gc", LUA_Mix_FreeMusic},
    {"GetMusicType", LUA_Mix_GetMusicType},
    {"PlayMusic", LUA_Mix_PlayMusic},
    {"FadeInMusic", LUA_Mix_FadeInMusic},
    {"FadeInMusicPos", LUA_Mix_FadeInMusicPos},
    //{"LoadTyped_RW", LUA_IMG_LoadTyped_RW},
    {NULL, NULL}
};

LUALIB_API int luaopen_gsdl2_mixer(lua_State* L)
{
    luaL_newmetatable(L, "Mix_Chunk");
    luaL_setfuncs(L, chunk_funcs, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    lua_pop(L, 1);

    luaL_newmetatable(L, "Mix_Music");
    luaL_setfuncs(L, music_funcs, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    lua_pop(L, 1);

    luaL_newlib(L, mix_funcs);
    return 1;
}