#include "gge.h"
#include "SDL_audio.h"

static int LUA_GetNumAudioDrivers(lua_State *L)
{
    lua_pushinteger(L, SDL_GetNumAudioDrivers());
    return 1;
}

static int LUA_GetAudioDriver(lua_State *L)
{
    int index = (int)luaL_checkinteger(L, 1);

    lua_pushstring(L, SDL_GetAudioDriver(index));
    return 1;
}

static int LUA_AudioInit(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);

    lua_pushboolean(L, SDL_AudioInit(name) == 0);
    return 1;
}

static int LUA_AudioQuit(lua_State *L)
{
    SDL_AudioQuit();
    return 0;
}

static int LUA_GetCurrentAudioDriver(lua_State *L)
{
    lua_pushstring(L, SDL_GetCurrentAudioDriver());
    return 1;
}
//TODO
static int LUA_OpenAudio(lua_State *L)
{
    //SDL_AudioSpec* desired;
    //SDL_AudioSpec* obtained;
    //SDL_OpenAudio(desired,obtained)
    return 0;
}

static int LUA_GetNumAudioDevices(lua_State *L)
{
    int iscapture = (int)luaL_checkinteger(L, 1);

    lua_pushinteger(L, SDL_GetNumAudioDevices(iscapture));
    return 1;
}

static int LUA_GetAudioDeviceName(lua_State *L)
{
    int index = (int)luaL_checkinteger(L, 1);
    int iscapture = (int)luaL_checkinteger(L, 2);

    lua_pushstring(L, SDL_GetAudioDeviceName(index, iscapture));
    return 1;
}
//TODO
static int LUA_OpenAudioDevice(lua_State *L)
{
    //const char* device = luaL_checkstring(L, 1);
    //int iscapture = (int)luaL_checkinteger(L, 2);
    //SDL_AudioSpec* desired;
    //SDL_AudioSpec* obtained;
    //int allowed_changes;
    //SDL_OpenAudioDevice();

    return 1;
}

static int LUA_GetAudioStatus(lua_State *L)
{
    lua_pushinteger(L, SDL_GetAudioStatus());
    return 1;
}

static int LUA_GetAudioDeviceStatus(lua_State *L)
{
    SDL_AudioDeviceID dev = (SDL_AudioDeviceID)luaL_checkinteger(L, 1);
    lua_pushinteger(L, SDL_GetAudioDeviceStatus(dev));
    return 1;
}

static int LUA_PauseAudio(lua_State *L)
{
    int pause_on = (int)lua_toboolean(L, 1);
    SDL_PauseAudio(pause_on);
    return 0;
}

static int LUA_PauseAudioDevice(lua_State *L)
{
    SDL_AudioDeviceID dev = (SDL_AudioDeviceID)luaL_checkinteger(L, 1);
    int pause_on = (int)lua_toboolean(L, 2);

    SDL_PauseAudioDevice(dev, pause_on);
    return 0;
}
//TODO
static int LUA_LoadWAV_RW(lua_State *L)
{
    //SDL_RWops* src = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    //SDL_AudioSpec spec;
    //Uint8* buffer;
    //Uint32 length;

    //SDL_LoadWAV_RW(src, SDL_FALSE, &spec, &buffer, &length)

    return 1;
}

static int LUA_LoadWAV(lua_State *L)
{
    //const char *path = luaL_checkstring(L, 1);
    //SDL_AudioSpec spec;
    //Uint8 *buffer;
    //Uint32 length;

    //SDL_LoadWAV(path, &spec, &buffer, &length)

    return 1;
}

static int LUA_FreeWAV(lua_State *L)
{
    //Uint8 * audio_buf;
    //SDL_FreeWAV();
    return 0;
}

static int LUA_BuildAudioCVT(lua_State *L)
{
    //SDL_AudioCVT* cvt;
    //SDL_AudioFormat src_format;
    //Uint8 src_channels;
    //int src_rate;
    //SDL_AudioFormat dst_format;
    //Uint8 dst_channels;
    //int dst_rate;

    //  luaL_checktype(L, index, LUA_TTABLE);
    //
    //  srcformat   = tableGetInt(L, index, "sourceFormat");
    //  srcchannels = tableGetInt(L, index, "sourceChannels");
    //  srcrate     = tableGetInt(L, index, "sourceRate");
    //  dstformat   = tableGetInt(L, index, "destFormat");
    //  dstchannels = tableGetInt(L, index, "destChannels");
    //  dstrate     = tableGetInt(L, index, "destRate");
    //
    //  ret = SDL_BuildAudioCVT(cvt, srcformat, srcchannels, srcrate,
    //      dstformat, dstchannels, dstrate);

    return 0;
}

static int LUA_ConvertAudio(lua_State *L)
{
    //SDL_AudioCVT cvt;

    //lua_pushboolean(L,SDL_ConvertAudio(&cvt)==0);
    return 0;
}
//TODO
//SDL_NewAudioStream
//SDL_AudioStreamPut
//SDL_AudioStreamGet
//SDL_AudioStreamAvailable
//SDL_AudioStreamFlush
//SDL_AudioStreamClear
//SDL_FreeAudioStream

static int LUA_MixAudio(lua_State *L)
{
    //char* dst;
    //size_t len;
    //const char *src = luaL_checklstring(L, 1, &len);
    //int volume = (int)luaL_optinteger(L,2,SDL_MIX_MAXVOLUME);
    //

    //SDL_MixAudio((Uint8 *)data, (const Uint8 *)src, len, volume);
    return 1;
}

static int LUA_MixAudioFormat(lua_State *L)
{
    //char* dst;
    //size_t len;
    //const char *src = luaL_checklstring(L, 1, &len);
    //SDL_AudioFormat format = luaL_checkinteger(L,2);
    //int volume = (int)luaL_optinteger(L,2,SDL_MIX_MAXVOLUME);

    //

    //SDL_MixAudioFormat((Uint8 *)data, (const Uint8 *)src, format, len, volume);

    //lua_pushlstring(L, data, len);
    return 1;
}

static int LUA_QueueAudio(lua_State *L)
{
    //SDL_AudioDeviceID dev = (SDL_AudioDeviceID)luaL_checkinteger(L, 1);
    //size_t len;
    //const char *data    = luaL_checklstring(L, 2, &len);

    //SDL_QueueAudio(dev, (void *)data, len)
    return 0;
}

static int LUA_DequeueAudio(lua_State *L)
{
    //SDL_AudioDeviceID dev = (SDL_AudioDeviceID)luaL_checkinteger(L, 1);
    //size_t len = luaL_checkinteger(L, 2);

    //  len = SDL_DequeueAudio(dev, data, len);
    //
    //  lua_pushlstring(L, (const char *)data, len);

    return 1;
}

static int LUA_GetQueuedAudioSize(lua_State *L)
{
    SDL_AudioDeviceID dev = (SDL_AudioDeviceID)luaL_checkinteger(L, 1);
    lua_pushinteger(L, SDL_GetQueuedAudioSize(dev));

    return 1;
}

static int LUA_ClearQueuedAudio(lua_State *L)
{
    SDL_AudioDeviceID dev = (SDL_AudioDeviceID)luaL_checkinteger(L, 1);

    SDL_ClearQueuedAudio(dev);
    return 0;
}

static int LUA_LockAudio(lua_State *L)
{
    SDL_LockAudio();
    return 0;
}

static int LUA_LockAudioDevice(lua_State *L)
{
    SDL_AudioDeviceID dev = (SDL_AudioDeviceID)luaL_checkinteger(L, 1);

    SDL_LockAudioDevice(dev);
    return 0;
}

static int LUA_UnlockAudio(lua_State *L)
{
    SDL_UnlockAudio();
    return 0;
}

static int LUA_UnlockAudioDevice(lua_State *L)
{
    SDL_AudioDeviceID dev = (SDL_AudioDeviceID)luaL_checkinteger(L, 1);
    SDL_UnlockAudioDevice(dev);

    return 0;
}

static int LUA_CloseAudio(lua_State *L)
{
    SDL_CloseAudio();
    return 0;
}

static int LUA_CloseAudioDevice(lua_State *L)
{
    SDL_AudioDeviceID dev = (SDL_AudioDeviceID)luaL_checkinteger(L, 1);
    SDL_CloseAudioDevice(dev);
    return 0;
}

static const luaL_Reg sdl_funcs[] = {
    {"GetNumAudioDrivers", LUA_GetNumAudioDrivers},
    {"GetAudioDriver", LUA_GetAudioDriver},
    {"AudioInit", LUA_AudioInit},
    {"AudioQuit", LUA_AudioQuit},
    {"GetCurrentAudioDriver", LUA_GetCurrentAudioDriver},
    {"OpenAudio", LUA_OpenAudio},
    {"GetNumAudioDevices", LUA_GetNumAudioDevices},
    {"GetAudioDeviceName", LUA_GetAudioDeviceName},
    {"OpenAudioDevice", LUA_OpenAudioDevice},
    {"GetAudioStatus", LUA_GetAudioStatus},
    {"GetAudioDeviceStatus", LUA_GetAudioDeviceStatus},
    {"PauseAudio", LUA_PauseAudio},
    {"PauseAudioDevice", LUA_PauseAudioDevice},
    {"LoadWAV_RW", LUA_LoadWAV_RW},
    {"LoadWAV", LUA_LoadWAV},
    {"FreeWAV", LUA_FreeWAV},
    {"BuildAudioCVT", LUA_BuildAudioCVT},
    {"ConvertAudio", LUA_ConvertAudio},
    //{"NewAudioStream",LUA_NewAudioStream},
    //{"AudioStreamPut",LUA_AudioStreamPut},
    //{"AudioStreamGet",LUA_AudioStreamGet},
    //{"AudioStreamAvailable",LUA_AudioStreamAvailable},
    //{"AudioStreamFlush",LUA_AudioStreamFlush},
    //{"AudioStreamClear",LUA_AudioStreamClear},
    //{"FreeAudioStream",LUA_FreeAudioStream},
    {"MixAudio", LUA_MixAudio},
    {"MixAudioFormat", LUA_MixAudioFormat},
    {"QueueAudio", LUA_QueueAudio},
    {"DequeueAudio", LUA_DequeueAudio},
    {"GetQueuedAudioSize", LUA_GetQueuedAudioSize},
    {"ClearQueuedAudio", LUA_ClearQueuedAudio},
    {"LockAudio", LUA_LockAudio},
    {"LockAudioDevice", LUA_LockAudioDevice},
    {"UnlockAudio", LUA_UnlockAudio},
    {"UnlockAudioDevice", LUA_UnlockAudioDevice},
    {"CloseAudio", LUA_CloseAudio},
    {"CloseAudioDevice", LUA_CloseAudioDevice},

    {NULL, NULL}};

int bind_audio(lua_State *L)
{
    luaL_newmetatable(L, "SDL_AudioStream");
    lua_pop(L, 1);
    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}