#include "fsb.h"

// from http://www.hydrogenaudio.org/forums/index.php?showtopic=85125
uint16_t mpg_get_frame_size(char* hdr) {

    // MPEG versions - use [version]
    //const uint8_t mpeg_versions[4] = { 25, 0, 2, 1 };

    // Layers - use [layer]
    //const uint8_t mpeg_layers[4] = { 0, 3, 2, 1 };

    // Bitrates - use [version][layer][bitrate]
    const uint16_t mpeg_bitrates[4][4][16] = {
      { // Version 2.5
        { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Reserved
        { 0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, 0 }, // Layer 3
        { 0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, 0 }, // Layer 2
        { 0,  32,  48,  56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256, 0 }  // Layer 1
      },
      { // Reserved
        { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Invalid
        { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Invalid
        { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Invalid
        { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }  // Invalid
      },
      { // Version 2
        { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Reserved
        { 0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, 0 }, // Layer 3
        { 0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, 0 }, // Layer 2
        { 0,  32,  48,  56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256, 0 }  // Layer 1
      },
      { // Version 1
        { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Reserved
        { 0,  32,  40,  48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 0 }, // Layer 3
        { 0,  32,  48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384, 0 }, // Layer 2
        { 0,  32,  64,  96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0 }, // Layer 1
      }
    };

    // Sample rates - use [version][srate]
    const uint16_t mpeg_srates[4][4] = {
        { 11025, 12000,  8000, 0 }, // MPEG 2.5
        {     0,     0,     0, 0 }, // Reserved
        { 22050, 24000, 16000, 0 }, // MPEG 2
        { 44100, 48000, 32000, 0 }  // MPEG 1
    };

    // Samples per frame - use [version][layer]
    const uint16_t mpeg_frame_samples[4][4] = {
        //    Rsvd     3     2     1  < Layer  v Version
            {    0,  576, 1152,  384 }, //       2.5
            {    0,    0,    0,    0 }, //       Reserved
            {    0,  576, 1152,  384 }, //       2
            {    0, 1152, 1152,  384 }  //       1
    };

    // Slot size (MPEG unit of measurement) - use [layer]
    const uint8_t mpeg_slot_size[4] = { 0, 1, 1, 4 }; // Rsvd, 3, 2, 1

    // Quick validity check
    if ((((unsigned char)hdr[0] & 0xFF) != 0xFF)
        || (((unsigned char)hdr[1] & 0xE0) != 0xE0)   // 3 sync bits
        || (((unsigned char)hdr[1] & 0x18) == 0x08)   // Version rsvd
        || (((unsigned char)hdr[1] & 0x06) == 0x00)   // Layer rsvd
        || (((unsigned char)hdr[2] & 0xF0) == 0xF0)   // Bitrate rsvd
        ) return 0;

    // Data to be extracted from the header
    uint8_t   ver = (hdr[1] & 0x18) >> 3;   // Version index
    uint8_t   lyr = (hdr[1] & 0x06) >> 1;   // Layer index
    uint8_t   pad = (hdr[2] & 0x02) >> 1;   // Padding? 0/1
    uint8_t   brx = (hdr[2] & 0xf0) >> 4;   // Bitrate index
    uint8_t   srx = (hdr[2] & 0x0c) >> 2;   // SampRate index

    // added for FSBext to skip invalid frames
    //printf("MP3debug %d %d %d %d %d\n", ver, lyr, pad, brx, srx);
    //if((ver != 3) || (lyr != 1)) return 0;

    // Lookup real values of these fields
    uint32_t  bitrate = mpeg_bitrates[ver][lyr][brx] * 1000;
    uint32_t  samprate = mpeg_srates[ver][srx];
    uint16_t  samples = mpeg_frame_samples[ver][lyr];
    uint8_t   slot_size = mpeg_slot_size[lyr];

    // In-between calculations
    float     bps = (float)samples / 8.0;
    float     fsize = ((bps * (float)bitrate) / (float)samprate)
        + ((pad) ? slot_size : 0);

    // Frame sizes are truncated integers
    return (uint16_t)fsize;
}

int mywav_writehead(luaL_Buffer* b, mywav_fmtchunk* fmt, uint32_t data_size, uint8_t* more, int morelen)
{
    mywav_chunk *chunk = (mywav_chunk*)luaL_prepbuffsize(b,sizeof(mywav_chunk));
    SDL_memcpy(chunk->id, "RIFF", 4);
    chunk->size =
        4 +
        sizeof(mywav_chunk) +
        sizeof(mywav_fmtchunk) +
        morelen +
        sizeof(mywav_chunk) +
        data_size;
    luaL_addsize(b, sizeof(mywav_chunk));

    luaL_addlstring(b, "WAVE", 4);

    chunk = (mywav_chunk*)luaL_prepbuffsize(b, sizeof(mywav_chunk));
    SDL_memcpy(chunk->id, "fmt ", 4);
    chunk->size = sizeof(mywav_fmtchunk) + morelen;
    luaL_addsize(b, sizeof(mywav_chunk));

    luaL_addlstring(b, (char*)fmt, sizeof(mywav_fmtchunk));
    if (morelen)
        luaL_addlstring(b, (char*)more, morelen);

    chunk = (mywav_chunk*)luaL_prepbuffsize(b, sizeof(mywav_chunk));
    SDL_memcpy(chunk->id, "data", 4);
    chunk->size = data_size;
    luaL_addsize(b, sizeof(mywav_chunk));
    return(0);
}



int FSB_Create(lua_State* L, Uint8* data, size_t len)
{
    FSOUND_FSB_HEADER_FSB4* head = (FSOUND_FSB_HEADER_FSB4*)data;
    if (head->id != '4BSF' )
        return 0;
    int num = head->numsamples;

    FSB_UserData* ud = (FSB_UserData*)lua_newuserdata(L, sizeof(FSB_UserData));
    SDL_memset(ud, 0, sizeof(FSB_UserData));
    ud->num  = head->numsamples;
    ud->mode = head->mode;
    ud->data = (Uint8*)SDL_malloc(len);
    ud->len  = (int)len;
    SDL_memcpy(ud->data, data, len);
    data = ud->data;
    luaL_setmetatable(L, "xyq_fsb");
    
    data += sizeof(FSOUND_FSB_HEADER_FSB4);
    ud->info = (FSOUND_FSB_SAMPLE_HEADER_3_1*)data;
    ud->list = (Uint32*)SDL_malloc(num * sizeof(Uint32));
    lua_createtable(L, num, 0);
    int offset = sizeof(FSOUND_FSB_HEADER_FSB4)+head->shdrsize;
    for (int i = 0; i < num; i++)
    {
        ud->list[i] = offset;
        offset += ud->info[i].lengthcompressedbytes;
        lua_pushinteger(L,i+1);
        lua_setfield(L, -2, ud->info[i].name);
    }
    return 2;
}

static int FSB_Get(lua_State* L)
{
    FSB_UserData* ud = (FSB_UserData*)luaL_checkudata(L, 1, "xyq_fsb");
    Uint32 i = (Uint32)luaL_checkinteger(L, 2) - 1;
    if (i >= ud->num)
        return luaL_error(L, "id error!");

    FSOUND_FSB_SAMPLE_HEADER_3_1* info = &ud->info[i];
    char* data = (char*)ud->data + ud->list[i];
    int pcm_endian = ud->mode & 0x08,
        aligned = ud->mode & 0x40,
        codec = FMOD_SOUND_FORMAT_PCM16,
        bits = 16,
        mode = info->mode,
        chans = info->numchannels,
        freq = info->deffreq,
        size = info->lengthcompressedbytes;

    if (mode & FSOUND_DELTA) codec = FMOD_SOUND_FORMAT_MPEG;
    if (mode & FSOUND_STEREO)chans = 2;
    if (mode & FSOUND_8BITS) bits = 8;
    
    if (codec == FMOD_SOUND_FORMAT_PCM16)
    {
        luaL_Buffer b;
        mywav_fmtchunk  fmt;

        fmt.wFormatTag = 0x0001;
        fmt.wChannels = chans;
        fmt.dwSamplesPerSec = freq;
        fmt.wBitsPerSample = bits;
        fmt.wBlockAlign = (fmt.wBitsPerSample / 8) * fmt.wChannels;
        fmt.dwAvgBytesPerSec = fmt.dwSamplesPerSec * fmt.wBlockAlign;

        luaL_buffinit(L, &b);
        mywav_writehead(&b, &fmt, size, NULL, 0);
        luaL_addlstring(&b, data, size);
        luaL_pushresult(&b);
        return 1;
    }else if(codec == FMOD_SOUND_FORMAT_MPEG) {
        luaL_Buffer b;
        int len = size;
        int t, n, frame_chans;

        frame_chans = (chans & 1) ? chans : (chans / 2);
        luaL_buffinit(L, &b);
        for (int frame = 0; len > 0; frame++) {
            //if (myfr(fd, hdr, 3) != 3) break;    //goto quit;
            len -= 3;
            t = 0;
            while (len > 0) {
                t = mpg_get_frame_size(data); 
                if (t) 
                    break;
                len--;
                data++;
            }
            if (len < 0) break;

            t -= 3;
            if ((len - t) < 0) break;

#define MP3_CHANS_DOWNMIX   (chans <= 2) || !(frame % frame_chans)

            if (MP3_CHANS_DOWNMIX) {
                luaL_addlstring(&b, data, 3);
                data += 3;
            }

            if (t > 0) {
                len -= t;
                
                if (MP3_CHANS_DOWNMIX) {
                    luaL_addlstring(&b, data, t);
                    data += t;
                }
            }

            if (mode & FSOUND_MULTICHANNEL) {//£¿
                //for (n = myftell(fd); n & 0xf; n++) {
                //    myfgetc(fd);
                //}
                printf("FSOUND_MULTICHANNEL\n");
            }
        }
        luaL_pushresult(&b);
        return 1;
    }

    return 0;
}

static int FSB_NEW(lua_State* L)
{
    size_t len;
    Uint8* data = (Uint8*)luaL_checklstring(L, 1, &len);
    return FSB_Create(L, data, len);
}

static int FSB_GC(lua_State* L)
{
    FSB_UserData* ud = (FSB_UserData*)luaL_checkudata(L, 1, "xyq_fsb");
    SDL_free(ud->data);
    SDL_free(ud->list);
    return 0;
}

static const luaL_Reg tcp_funs[] = {
    {"__gc",FSB_GC},
    {"__close",FSB_GC},
    {"Get",FSB_Get},

    {NULL,NULL},
};

LUAMOD_API int luaopen_gxyq_fsb(lua_State* L)
{
    luaL_newmetatable(L, "xyq_fsb");
    luaL_setfuncs(L, tcp_funs, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    lua_pushcfunction(L, FSB_NEW);
    return 1;
}