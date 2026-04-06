#include "sdl_proxy.h"
#include "map.h"

#include <stdlib.h>
#include "../../../Dependencies/SDL_image/external/libwebp-1.3.2/src/webp/decode.h"

/* ---------- 内嵌 stb_image 纯 C JPEG/PNG 解码 ----------
 * 后台线程（SDLTimer）不能调用 IMG_Load_RW，因为 iOS 上它会走
 * ImageIO (ObjC/CoreGraphics)，在无 @autoreleasepool 的线程中
 * 导致内存泄漏、堆损坏和闪退。
 * stb_image 是纯 C 实现，线程安全，零 ObjC 依赖。
 * 使用 STB_IMAGE_STATIC 确保所有函数为 static，不与 SDL_image
 * 内部同名符号冲突。
 */
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_FAILURE_STRINGS
#define STBI_MALLOC(sz)           SDL_malloc(sz)
#define STBI_REALLOC(p,newsz)     SDL_realloc(p,newsz)
#define STBI_FREE(p)              SDL_free(p)
#define STBI_ASSERT(x)            SDL_assert(x)
#include "stb_image.h"

/* ---------- 裸像素解码 WEBP（后台线程安全） ----------
 * 不调用任何 SDL Surface API（SDL_CreateRGBSurfaceWithFormat 内部的
 * SDL_AllocFormat 操作全局格式缓存链表，非线程安全）。
 * 只使用 SDL_malloc 分配裸 ARGB8888 像素缓冲区。
 */
static MAP_RawPixels _webp_raw_decode(const Uint8* data, size_t data_size)
{
    MAP_RawPixels raw = { NULL, 0, 0 };
    int width = 0, height = 0;
    if (!WebPGetInfo(data, data_size, &width, &height))
        return raw;
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
        return raw;

    const int row_bytes = width * 4;
    const size_t rgba_size = (size_t)row_bytes * height;
    uint8_t* rgba = (uint8_t*)SDL_malloc(rgba_size);
    if (!rgba)
        return raw;

    if (!WebPDecodeRGBAInto(data, data_size, rgba, rgba_size, row_bytes))
    {
        SDL_free(rgba);
        return raw;
    }

    Uint32* out = (Uint32*)SDL_malloc((size_t)width * height * sizeof(Uint32));
    if (!out) {
        SDL_free(rgba);
        return raw;
    }

    /* RGBA → ARGB8888 通道交换 */
    for (int y = 0; y < height; y++) {
        const uint8_t* src = rgba + y * row_bytes;
        Uint32* dst = out + y * width;
        for (int x = 0; x < width; x++) {
            dst[x] = ((Uint32)src[3] << 24) | ((Uint32)src[0] << 16)
                   | ((Uint32)src[1] << 8)  |  (Uint32)src[2];
            src += 4;
        }
    }

    SDL_free(rgba);
    raw.pixels = out;
    raw.width = width;
    raw.height = height;
    return raw;
}

/* ---------- 裸像素解码 JPEG/PNG（后台线程安全） ----------
 * 使用 stb_image 解码，输出裸 ARGB8888 像素。
 * 不调用任何 SDL Surface/Format API。
 */
static MAP_RawPixels _stbi_raw_decode(const Uint8* data, size_t data_size)
{
    MAP_RawPixels raw = { NULL, 0, 0 };
    int width = 0, height = 0, channels = 0;

    unsigned char* pixels = stbi_load_from_memory(
        data, (int)data_size, &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0)
    {
        if (pixels) stbi_image_free(pixels);
        return raw;
    }

    Uint32* out = (Uint32*)SDL_malloc((size_t)width * height * sizeof(Uint32));
    if (!out) {
        stbi_image_free(pixels);
        return raw;
    }

    /* RGBA → ARGB8888 通道交换 */
    for (int y = 0; y < height; y++)
    {
        const unsigned char* src = pixels + (size_t)y * width * 4;
        Uint32* dst = out + y * width;
        for (int x = 0; x < width; x++) {
            dst[x] = ((Uint32)src[3] << 24) | ((Uint32)src[0] << 16)
                   | ((Uint32)src[1] << 8)  |  (Uint32)src[2];
            src += 4;
        }
    }
    stbi_image_free(pixels);
    raw.pixels = out;
    raw.width = width;
    raw.height = height;
    return raw;
}

/* ---------- 裸像素→SDL_Surface（仅主线程调用） ----------
 * pixel_format: 由调用方指定（当前统一用 SDL_PIXELFORMAT_ARGB8888） */
static SDL_Surface* _raw_to_surface(MAP_RawPixels* raw, Uint32 pixel_format)
{
    if (!raw || !raw->pixels)
        return NULL;
    SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormat(
        SDL_SWSURFACE, raw->width, raw->height, 32, pixel_format);
    if (!sf) {
        SDL_free(raw->pixels);
        raw->pixels = NULL;
        return NULL;
    }
    /* 逐行拷贝（处理 pitch 对齐差异） */
    const int row_bytes = raw->width * (int)sizeof(Uint32);
    for (int y = 0; y < raw->height; y++) {
        SDL_memcpy(
            (Uint8*)sf->pixels + y * sf->pitch,
            raw->pixels + y * raw->width,
            row_bytes);
    }
    SDL_free(raw->pixels);
    raw->pixels = NULL;
    return sf;
}

/* ---------- 裸像素 Blit（替代 SDL_BlitSurface，后台线程安全） ---------- */
static void _blit_raw_tile(const MAP_RawPixels* tile,
                           Uint32* canvas, int canvas_w, int canvas_h,
                           int dst_x, int dst_y)
{
    if (!tile || !tile->pixels || !canvas) return;
    int src_x = 0, src_y = 0;
    int copy_w = tile->width, copy_h = tile->height;

    if (dst_x < 0) { src_x = -dst_x; copy_w += dst_x; dst_x = 0; }
    if (dst_y < 0) { src_y = -dst_y; copy_h += dst_y; dst_y = 0; }
    if (dst_x + copy_w > canvas_w) copy_w = canvas_w - dst_x;
    if (dst_y + copy_h > canvas_h) copy_h = canvas_h - dst_y;
    if (copy_w <= 0 || copy_h <= 0) return;

    for (int y = 0; y < copy_h; y++) {
        Uint32* d = canvas + (dst_y + y) * canvas_w + dst_x;
        const Uint32* s = tile->pixels + (src_y + y) * tile->width + src_x;
        SDL_memcpy(d, s, (size_t)copy_w * sizeof(Uint32));
    }
}

#if defined(_WIN32)
#define MYGXY_API __declspec(dllexport)
#else
#define MYGXY_API LUAMOD_API
#endif

//申请内存（旧数据不需要保留，free+malloc 比 realloc 少一次无谓拷贝）
static void* _getmem(MAP_Mem* mem, size_t size)
{
    if (mem->size >= size)
        return mem->mem;

    SDL_free(mem->mem);
    mem->mem = SDL_malloc(size);
    if (mem->mem == NULL) {
        SDL_OutOfMemory();
        mem->size = 0;
        return NULL;
    }
    mem->size = size;
    return mem->mem;
}

static void _freemem(MAP_Mem* mem)
{
    if (mem->mem) {
        SDL_free(mem->mem);
        mem->mem = NULL;
        mem->size = 0;
    }
}

typedef struct
{
    SDL_RWops rw;
    Uint8* base;
    size_t size;
    size_t pos;
    int owned;  /* 1: close 时释放 base; 0: 不释放 */
} MAP_MemRW;

static Sint64 SDLCALL MAP_MemRW_Seek(SDL_RWops* context, Sint64 offset, int whence)
{
    MAP_MemRW* m = (MAP_MemRW*)context;
    Sint64 newpos = 0;

    if (whence == RW_SEEK_SET)
        newpos = offset;
    else if (whence == RW_SEEK_CUR)
        newpos = (Sint64)m->pos + offset;
    else if (whence == RW_SEEK_END)
        newpos = (Sint64)m->size + offset;
    else
        return -1;

    if (newpos < 0 || (size_t)newpos > m->size)
        return -1;

    m->pos = (size_t)newpos;
    return newpos;
}

static size_t SDLCALL MAP_MemRW_Read(SDL_RWops* context, void* ptr, size_t size, size_t maxnum)
{
    MAP_MemRW* m = (MAP_MemRW*)context;

    if (!ptr || size == 0 || maxnum == 0)
        return 0;

    if (maxnum > (SIZE_MAX / size))
        return 0;

    size_t want = size * maxnum;
    size_t left = (m->pos < m->size) ? (m->size - m->pos) : 0;
    if (want > left)
        want = left;

    if (want == 0)
        return 0;

    SDL_memcpy(ptr, m->base + m->pos, want);
    m->pos += want;
    return want / size;
}

static size_t SDLCALL MAP_MemRW_Write(SDL_RWops* context, const void* ptr, size_t size, size_t num)
{
    (void)context;
    (void)ptr;
    (void)size;
    (void)num;
    return 0;
}

static Sint64 SDLCALL MAP_MemRW_Size(SDL_RWops* context)
{
    MAP_MemRW* m = (MAP_MemRW*)context;
    return (Sint64)m->size;
}

static int SDLCALL MAP_MemRW_Close(SDL_RWops* context)
{
    MAP_MemRW* m = (MAP_MemRW*)context;
    if (m->owned && m->base)
        SDL_free(m->base);  /* 仅 owned 模式释放数据 */
    SDL_free(m);
    return 0;
}

static SDL_RWops* _MAP_RWFromMem(void* mem, size_t size, int owned)
{
    if (!mem)
        return NULL;

    MAP_MemRW* m = (MAP_MemRW*)SDL_malloc(sizeof(MAP_MemRW));
    if (!m)
        return NULL;

    SDL_memset(m, 0, sizeof(MAP_MemRW));
    m->base = (Uint8*)mem;
    m->size = size;
    m->pos = 0;
    m->owned = owned;

    m->rw.size = MAP_MemRW_Size;
    m->rw.seek = MAP_MemRW_Seek;
    m->rw.read = MAP_MemRW_Read;
    m->rw.write = MAP_MemRW_Write;
    m->rw.close = MAP_MemRW_Close;
    return &m->rw;
}

/* Close 时释放 base 数据 */
static SDL_RWops* MAP_RWFromOwnedMem(void* mem, size_t size)
{
    return _MAP_RWFromMem(mem, size, 1);
}

/* Close 时不释放 base 数据（caller 负责生命周期） */
static SDL_RWops* MAP_RWFromBorrowedMem(void* mem, size_t size)
{
    return _MAP_RWFromMem(mem, size, 0);
}
//恢复普通jpg（云风特殊编码 → 标准 JPEG）
// 处理原理:
//   1. 复制 FFD8 SOI
//   2. 删除 FFA0 自定义标记
//   3. 正确跳过所有标准 Marker（包括 APP0/APP1 等）
//   4. 修改 FFDA(SOS) 长度 0009→000C，追加 00 3F 00
//   5. 熵编码段中的原始 0xFF 转义为 FF00
//   6. 输出标准 FFD9 结尾
static Uint32 _fixjpeg(unsigned char* inbuf, Uint32 insize, unsigned char* outbuf, Uint32 outmax)
{
    if (insize < 4 || outmax < 8)
        return 0;

    unsigned char* ip     = inbuf;
    unsigned char* ip_end = inbuf + insize;
    unsigned char* op     = outbuf;
    unsigned char* op_end = outbuf + outmax;

    /* ---------- 1. SOI (FFD8) ---------- */
    if (ip[0] != 0xFF || ip[1] != 0xD8)
        return 0;
    *op++ = 0xFF; *op++ = 0xD8;
    ip += 2;

    /* ---------- 2. 跳过 FFA0 ---------- */
    if (ip + 2 <= ip_end && ip[0] == 0xFF && ip[1] == 0xA0)
        ip += 2;

    /* ---------- 3. 遍历 Marker ---------- */
    while (ip + 2 <= ip_end && op + 2 <= op_end)
    {
        if (ip[0] != 0xFF)
            break; /* 非 Marker 前缀，数据异常 */

        Uint8 marker = ip[1];
        ip += 2;

        /* EOI */
        if (marker == 0xD9) {
            *op++ = 0xFF; *op++ = 0xD9;
            return (Uint32)(op - outbuf);
        }

        /* SOS (FFDA) — 进入熵编码段 */
        if (marker == 0xDA) {
            if (ip + 2 > ip_end) break;
            Uint16 orig_len = ((Uint16)ip[0] << 8) | ip[1];
            if (orig_len < 2) break;

            /* 写 FFDA + 新长度 000C */
            if (op + 4 > op_end) break;
            *op++ = 0xFF; *op++ = 0xDA;
            *op++ = 0x00; *op++ = 0x0C;
            ip += 2; /* 跳过原始长度 */

            /* 复制 SOS 头剩余字节 */
            Uint16 hdr_remain = orig_len - 2;
            if (ip + hdr_remain > ip_end)
                hdr_remain = (Uint16)(ip_end - ip);
            if (op + hdr_remain > op_end)
                hdr_remain = (Uint16)(op_end - op);
            SDL_memcpy(op, ip, hdr_remain);
            op += hdr_remain;
            ip += hdr_remain;

            /* 追加 00 3F 00 */
            if (op + 3 > op_end) break;
            *op++ = 0x00; *op++ = 0x3F; *op++ = 0x00;

            /* 熵编码段：输入的最后 2 字节是 FFD9，不参与转义 */
            unsigned char* ent_end = ip_end - 2;
            if (ent_end < ip) ent_end = ip;

            while (ip < ent_end && op + 2 <= op_end) {
                if (*ip == 0xFF) {
                    *op++ = 0xFF;
                    *op++ = 0x00; /* byte-stuff */
                    ip++;
                } else {
                    *op++ = *ip++;
                }
            }

            /* 写结束标记 FFD9 */
            if (op + 2 <= op_end) {
                *op++ = 0xFF; *op++ = 0xD9;
            }
            return (Uint32)(op - outbuf);
        }

        /* 所有其他 Marker（FFC0/FFC4/FFDB/FFE0/FFE1…）：读长度，整段复制 */
        if (ip + 2 > ip_end) break;
        Uint16 seg_len = ((Uint16)ip[0] << 8) | ip[1];
        if (seg_len < 2) break;
        if (ip + seg_len > ip_end)
            seg_len = (Uint16)(ip_end - ip);
        if (op + 2 + seg_len > op_end) break;

        *op++ = 0xFF;
        *op++ = marker;
        SDL_memcpy(op, ip, seg_len);
        op += seg_len;
        ip += seg_len;
    }

    /* 异常退出兜底：确保输出至少是合法的 JPEG 空壳 */
    if (op + 2 <= op_end && (Uint32)(op - outbuf) > 2) {
        *op++ = 0xFF; *op++ = 0xD9;
    }
    return (Uint32)(op - outbuf);
}
//解压遮罩数据（带输入/输出边界保护，防止损坏数据导致堆溢出）
static int _lzodecompress(void* in, size_t in_size, void* out, size_t out_size)
{
    unsigned char* op;
    unsigned char* ip;
    unsigned t;
    unsigned char* m_pos;

    unsigned char* op_end;
    unsigned char* ip_end;
    unsigned char* out_base;

    if (in_size == 0 || out_size == 0)
        return -1;

    op = (unsigned char*)out;
    ip = (unsigned char*)in;
    op_end = op + out_size;
    ip_end = ip + in_size;
    out_base = op;

/* 安全宏：检查输入/输出/回溯指针边界 */
#define LZO_CHECK_IP(need) do { if ((size_t)(ip_end - ip) < (size_t)(need)) return -1; } while(0)
#define LZO_CHECK_OP(need) do { if ((size_t)(op_end - op) < (size_t)(need)) return -1; } while(0)
#define LZO_CHECK_MPOS(p)  do { if ((p) < out_base || (p) >= op) return -1; } while(0)

    LZO_CHECK_IP(1);
    if (*ip > 17)
    {
        t = *ip++ - 17;
        if (t < 4)
            goto match_next;
        LZO_CHECK_OP(t);
        LZO_CHECK_IP(t);
        do
            *op++ = *ip++;
        while (--t > 0);
        goto first_literal_run;
    }

    while (1)
    {
        LZO_CHECK_IP(1);
        t = *ip++;
        if (t >= 16)
            goto match;
        if (t == 0)
        {
            LZO_CHECK_IP(1);
            while (*ip == 0)
            {
                t += 255;
                ip++;
                LZO_CHECK_IP(1);
                if (t > out_size) return -1; /* 防止 t 溢出导致超大拷贝 */
            }
            t += 15 + *ip++;
        }

        /* t 为 literal run 长度减3，实际需要复制 t+1 组 4 字节 */
        LZO_CHECK_OP(t + 4);
        LZO_CHECK_IP(t + 4);
        SDL_memcpy(op, ip, 4);
        op += 4;
        ip += 4;
        if (--t > 0)
        {
            if (t >= 4)
            {
                do
                {
                    SDL_memcpy(op, ip, 4);
                    op += 4;
                    ip += 4;
                    t -= 4;
                } while (t >= 4);
                if (t > 0)
                    do
                        *op++ = *ip++;
                while (--t > 0);
            }
            else
                do
                    *op++ = *ip++;
            while (--t > 0);
        }

    first_literal_run:

        LZO_CHECK_IP(1);
        t = *ip++;
        if (t >= 16)
            goto match;

        LZO_CHECK_IP(1);
        m_pos = op - 0x0801;
        m_pos -= t >> 2;
        m_pos -= *ip++ << 2;
        LZO_CHECK_MPOS(m_pos);

        LZO_CHECK_OP(3);
        *op++ = *m_pos++;
        *op++ = *m_pos++;
        *op++ = *m_pos;

        goto match_done;

        while (1)
        {
        match:
            if (t >= 64)
            {
                LZO_CHECK_IP(1);
                m_pos = op - 1;
                m_pos -= (t >> 2) & 7;
                m_pos -= *ip++ << 3;
                t = (t >> 5) - 1;
                LZO_CHECK_MPOS(m_pos);

                goto copy_match;
            }
            else if (t >= 32)
            {
                t &= 31;
                if (t == 0)
                {
                    LZO_CHECK_IP(1);
                    while (*ip == 0)
                    {
                        t += 255;
                        ip++;
                        LZO_CHECK_IP(1);
                        if (t > out_size) return -1;
                    }
                    t += 31 + *ip++;
                }

                m_pos = op - 1;
                LZO_CHECK_IP(2);
                {
                    unsigned short tmp;
                    SDL_memcpy(&tmp, ip, 2);
                    m_pos -= tmp >> 2;
                }
                ip += 2;
                LZO_CHECK_MPOS(m_pos);
            }
            else if (t >= 16)
            {
                m_pos = op;
                m_pos -= (t & 8) << 11;
                t &= 7;
                if (t == 0)
                {
                    LZO_CHECK_IP(1);
                    while (*ip == 0)
                    {
                        t += 255;
                        ip++;
                        LZO_CHECK_IP(1);
                        if (t > out_size) return -1;
                    }
                    t += 7 + *ip++;
                }
                LZO_CHECK_IP(2);
                {
                    unsigned short tmp;
                    SDL_memcpy(&tmp, ip, 2);
                    m_pos -= tmp >> 2;
                }
                ip += 2;
                if (m_pos == op)
                    goto eof_found;
                m_pos -= 0x4000;
                LZO_CHECK_MPOS(m_pos);
            }
            else
            {
                LZO_CHECK_IP(1);
                m_pos = op - 1;
                m_pos -= t >> 2;
                m_pos -= *ip++ << 2;
                LZO_CHECK_MPOS(m_pos);
                LZO_CHECK_OP(2);
                *op++ = *m_pos++;
                *op++ = *m_pos;
                goto match_done;
            }

            if (t >= 6 && (op - m_pos) >= 4)
            {
                LZO_CHECK_OP(t + 2);
                SDL_memcpy(op, m_pos, 4);
                op += 4;
                m_pos += 4;
                t -= 2;
                do
                {
                    SDL_memcpy(op, m_pos, 4);
                    op += 4;
                    m_pos += 4;
                    t -= 4;
                } while (t >= 4);
                if (t > 0)
                    do
                        *op++ = *m_pos++;
                while (--t > 0);
            }
            else
            {
            copy_match:
                LZO_CHECK_OP(t + 2);
                *op++ = *m_pos++;
                *op++ = *m_pos++;
                do
                    *op++ = *m_pos++;
                while (--t > 0);
            }

        match_done:

            t = ip[-2] & 3;
            if (t == 0)
                break;

        match_next:
            LZO_CHECK_OP(t);
            LZO_CHECK_IP(t);
            do
                *op++ = *ip++;
            while (--t > 0);
            LZO_CHECK_IP(1);
            t = *ip++;
        }
    }

eof_found:
#undef LZO_CHECK_IP
#undef LZO_CHECK_OP
#undef LZO_CHECK_MPOS
    return (int)(op - (Uint8*)out);
}
//取地表（tmem: 临时缓冲区，传 NULL 使用 ud->mem；rw: 文件句柄）
// ★ 0x9527 不缓存模式：不读/写 ud->map[id].sf，避免 Timer 线程竞态
// ★ out_raw: 后台线程模式（tmem != NULL）时，输出裸像素到此指针。
//   主线程（out_raw == NULL）仍返回 SDL_Surface*。
static SDL_Surface* _getmapsf(MAP_UserData* ud, Uint32 id, MAP_Mem* tmem, SDL_RWops* rw, MAP_RawPixels* out_raw)
{
    MAP_Mem* m = tmem ? tmem : ud->mem;
    int no_cache = (ud->mode == 0x9527);
    int is_async = (tmem != NULL && out_raw != NULL);

    if (id >= ud->mapnum)
        return 0;

    /* 仅主线程+缓存模式使用缓存的 sf */
    if (!is_async && !no_cache && ud->map[id].sf)
        return ud->map[id].sf;

    Uint32 masknum;//遮罩数量
    SDL_Surface* sf = NULL;
    MAP_RawPixels raw = { NULL, 0, 0 };

    if (SDL_RWseek(rw, ud->maplist[id], RW_SEEK_SET) == -1 ||
        SDL_RWread(rw, &masknum, sizeof(Uint32), 1) != 1)//附近遮罩数量
        return 0;

    if (masknum > 65535)
        return 0;

    if (masknum > 0 && ud->flag == MAP_FLAG_M10 &&
        SDL_RWseek(rw, sizeof(Uint32) * masknum, RW_SEEK_CUR) == -1)
        return 0;

    MAP_BlockInfo info = { 0, 0 };

    void* mem0 = NULL, * mem1 = NULL;
    int loop = 1;
    while (loop)
    {
        if (SDL_RWread(rw, &info, sizeof(MAP_BlockInfo), 1) != 1)
            return 0;

        switch (info.flag)
        {
        case MAP_BLOCK_JPG2: //梦幻普通JPG
        case MAP_BLOCK_PNG1: //梦幻
        case MAP_BLOCK_WEBP: //梦幻
        case MAP_BLOCK_0PNG: //大话
        {
            if (!(mem0 = _getmem(&m[0], info.size)))
                return 0;
            if (SDL_RWread(rw, mem0, sizeof(Uint8), info.size) != info.size)
                return 0;

            loop = 0;
            break;
        }
        case MAP_BLOCK_JPEG:
        {
            if (ud->flag == MAP_FLAG_M10) {
                if (!(mem0 = _getmem(&m[0], info.size)))
                    return 0;
                if (SDL_RWread(rw, mem0, sizeof(Uint8), info.size) != info.size)
                    return 0;

                if (((Uint16*)mem0)[1] == 0xA0FF) { //云风格式
                    Uint32 outmax = info.size * 2 + 256; // 安全余量
                    if (outmax < 153600) outmax = 153600;
                    if (!(mem1 = _getmem(&m[1], outmax))) {
                        return 0;
                    }
                    info.size = _fixjpeg(mem0, info.size, mem1, outmax);
                    if (info.size == 0)
                        return 0; /* fixjpeg 解码失败，跳过该图块 */
                    mem0 = mem1;
                }//大话普通
            }
            else {//MAPX 
                if (!(mem0 = _getmem(&m[0], ud->jpeh.size + info.size)))
                    return 0;

                if (SDL_RWread(rw, (char*)mem0 + ud->jpeh.size, sizeof(Uint8), info.size) != info.size)
                    return 0;

                SDL_memcpy(mem0, ud->jpeh.mem, ud->jpeh.size);

                ujImage img = ujCreate();
                ujDecode(img, mem0, (int)ud->jpeh.size + info.size, 1);
                if (ujIsValid(img)) {
                    info.size = ujGetImageSize(img);
                    if ((mem1 = _getmem(&m[1], info.size)) && ujGetImage(img, mem1)) {
                        int w = ujGetWidth(img);
                        int h = ujGetHeight(img);
                        if (w > 320) w = 320;
                        if (h > 240) h = 240;

                        /* 直接输出到裸像素缓冲区（不调用 SDL_CreateRGBSurfaceWithFormat） */
                        Uint32* px = (Uint32*)SDL_calloc((size_t)320 * 240, sizeof(Uint32));
                        if (px) {
                            Uint8* src = (Uint8*)mem1;
                            if (!ujIsColor(img)) {
                                for (int y = 0; y < h; y++) {
                                    Uint32* row = px + y * 320;
                                    for (int x = 0; x < w; x++) {
                                        Uint8 g = src[y * w + x];
                                        row[x] = 0xFF000000u | ((Uint32)g << 16) | ((Uint32)g << 8) | g;
                                    }
                                }
                            } else {
                                for (int y = 0; y < h; y++) {
                                    Uint32* row = px + y * 320;
                                    Uint8* srow = src + y * w * 3;
                                    for (int x = 0; x < w; x++) {
                                        row[x] = 0xFF000000u | ((Uint32)srow[0] << 16) | ((Uint32)srow[1] << 8) | srow[2];
                                        srow += 3;
                                    }
                                }
                            }
                            raw.pixels = px;
                            raw.width = 320;
                            raw.height = 240;
                        }
                    }
                    ujFree(img);
                }
                mem0 = NULL;
            }
            loop = 0;
            break;

        }
        case 0://结束
        {
            if (SDL_RWseek(rw, info.size, RW_SEEK_CUR) == -1)
                return 0;
            info.size = 0;
            loop = 0;
            break;
        }
        default: //跳过
            if (SDL_RWseek(rw, info.size, RW_SEEK_CUR) == -1)
                return 0;
            break;
        }
    }

    if (mem0 && info.size) {
        /* 单图块大小上限防护（防止损坏数据导致超大分配） */
        if (info.size > 16 * 1024 * 1024)
            return 0;

        /* 1. WEBP 裸像素解码（libwebp 纯 C，无 SDL API） */
        if (!raw.pixels && info.size >= 12) {
            const Uint8* hdr = (const Uint8*)mem0;
            if (hdr[0]=='R' && hdr[1]=='I' && hdr[2]=='F' && hdr[3]=='F' &&
                hdr[8]=='W' && hdr[9]=='E' && hdr[10]=='B' && hdr[11]=='P')
            {
                raw = _webp_raw_decode(hdr, info.size);
            }
        }

        /* 2. JPEG/PNG 裸像素解码（stb_image 纯 C，无 SDL API） */
        if (!raw.pixels) {
            raw = _stbi_raw_decode((const Uint8*)mem0, info.size);
        }

        if (is_async) {
            /* ---- 后台线程：返回裸像素，零 SDL Surface API 调用 ----
             * SDL_CreateRGBSurfaceWithFormat 内部的 SDL_AllocFormat()
             * 操作全局格式缓存链表（无锁），与主线程竞态导致堆损坏。
             * 后台线程只产出裸 ARGB8888 像素，主线程消费时再创建 Surface。 */
            *out_raw = raw;
            return NULL;
        }

        /* ---- 主线程路径 ---- */
        if (raw.pixels) {
            sf = _raw_to_surface(&raw, SDL_PIXELFORMAT_ARGB8888);
        } else {
            /* 主线程最终 fallback：使用 IMG_Load_RW */
            SDL_RWops* src = SDL_RWFromMem(mem0, (int)info.size);
            sf = IMG_Load_RW(src, SDL_TRUE);
        }

        /* 格式转换（仅主线程安全） */
        if (sf && sf->format->format != SDL_PIXELFORMAT_ARGB8888) {
            SDL_Surface* nsf = SDL_ConvertSurfaceFormat(sf, SDL_PIXELFORMAT_ARGB8888, SDL_SWSURFACE);
            SDL_FreeSurface(sf);
            sf = nsf;
        }

        /* 仅主线程缓存模式写入 ud->map[id].sf */
        if (!no_cache) {
            ud->map[id].sf = sf;
        }
    }
    else if (is_async && raw.pixels) {
        /* MAPX ujImage 路径已产出 raw，返回给后台线程 */
        *out_raw = raw;
        return NULL;
    }

    return sf;
}

static int _getmaskinfo(MAP_UserData* ud, Uint32 id, MAP_MaskInfo* info, SDL_RWops* rw)
{
    Sint64 cur = SDL_RWtell(rw);
    int ok = 0;
    if (cur < 0)
        cur = 0;
    if (SDL_RWseek(rw, info->offset, RW_SEEK_SET) == -1)
        goto end;
    if (ud->flag == MAP_FLAG_M10) {
        Uint32 raw_size = 0;
        Uint32 mode = 0;

        SDL_memset(&info->rect, 0, sizeof(SDL_Rect));
        info->size = 0;
        info->mode = 0;
        info->head = 20;

        if (SDL_RWread(rw, (void*)&info->rect, sizeof(SDL_Rect), 1) != 1)
            goto end;
        if (SDL_RWread(rw, (void*)&raw_size, sizeof(Uint32), 1) != 1)
            goto end;

        if (SDL_RWread(rw, (void*)&mode, sizeof(Uint32), 1) == 1) {
            if (mode <= 16)
                info->mode = mode;
        }

        info->size = raw_size;
        ok = 1;
    }
    else {
        if (SDL_RWread(rw, (void*)&info->size, sizeof(Uint32), 1) != 1)
            goto end;
        if (SDL_RWread(rw, (void*)&info->rect, sizeof(SDL_Rect), 1) != 1)
            goto end;

        info->size -= 16;
        info->head = 20;
        info->mode = 0;
        int x = (id % ud->colnum) * 320;
        int y = (id / ud->colnum) * 240;
        info->rect.x += x;
        info->rect.y += y;
        ok = 1;
    }
end:
    SDL_RWseek(rw, cur, RW_SEEK_SET);
    return ok;
}
//取遮罩信息
static int _getmasksinfo(MAP_UserData* ud, Uint32 id, MAP_MaskInfo** mask, Uint32* num, SDL_RWops* rw)
{
    if (!mask || !num)
        return 0;
    *mask = NULL;
    *num = 0;

    if (id >= ud->mapnum)
        return 0;

    Uint32 masknum;//遮罩数量
    MAP_MaskInfo* masklist = NULL;

    if (SDL_RWseek(rw, ud->maplist[id], RW_SEEK_SET) == -1 ||
        SDL_RWread(rw, &masknum, sizeof(Uint32), 1) != 1)//附近遮罩ID
        return 0;

    if (masknum == 0 || masknum > 65535)
        return 0;

    masklist = (MAP_MaskInfo*)SDL_malloc(masknum * sizeof(MAP_MaskInfo));
    if (!masklist)
        return 0;

    if (ud->flag == MAP_FLAG_M10) {
        Uint32* maskid = (Uint32*)SDL_malloc(masknum * sizeof(Uint32));
        if (!maskid) {
            SDL_free(masklist);
            return 0;
        }
        if (SDL_RWread(rw, maskid, sizeof(Uint32), masknum) != masknum) {
            SDL_free(masklist);
            SDL_free(maskid);
            return 0;
        }
        Uint32 valid = 0;
        for (Uint32 i = 0; i < masknum; i++)
        {
            if (maskid[i] < ud->masknum)
            {
                MAP_MaskInfo tmp;
                SDL_memset(&tmp, 0, sizeof(MAP_MaskInfo));
                tmp.offset = ud->masklist[maskid[i]];
                if (_getmaskinfo(ud, id, &tmp, rw))
                    masklist[valid++] = tmp;
            }
        }
        SDL_free(maskid);
        if (valid == 0) {
            SDL_free(masklist);
            return 0;
        }
        *mask = masklist;
        *num = valid;
        return 1;
    }

    MAP_BlockInfo info = { 0, 0 };

    int loop = 1;
    Uint32 i = 0;
    while (loop)
    {
        if (SDL_RWread(rw, &info, sizeof(MAP_BlockInfo), 1) != 1) {
            SDL_free(masklist);
            return 0;
        }
        switch (info.flag)
        {
        case MAP_BLOCK_MASK://MAPX 
        {
            if (i < masknum) {
                masklist[i].offset = (Uint32)SDL_RWtell(rw) - sizeof(Uint32);
                _getmaskinfo(ud, id, &masklist[i], rw);
                i++;
            }

            if (SDL_RWseek(rw, info.size, RW_SEEK_CUR) == -1) {
                SDL_free(masklist);
                return 0;
            }
            break;
        }
        case 0://结束
        {
            if (SDL_RWseek(rw, info.size, RW_SEEK_CUR) == -1) {
                SDL_free(masklist);
                return 0;
            }
            info.size = 0;
            loop = 0;
            break;
        }
        default: //跳过
            if (SDL_RWseek(rw, info.size, RW_SEEK_CUR) == -1) {
                SDL_free(masklist);
                return 0;
            }
            break;
        }
    }

    *mask = masklist;
    *num = i;
    return 1;
}
//取遮罩透明数据（tmem: 临时缓冲区，传 NULL 使用 ud->mem）
static Uint8* _getmaskdata(MAP_UserData* ud, Uint32 id, MASK_Data* mask, MAP_Mem* tmem, SDL_RWops* rw)
{
    MAP_Mem* m = tmem ? tmem : ud->mem;
    Uint32 width, height, size;

    _getmaskinfo(ud, id, &mask->info, rw);

    width = mask->info.rect.w;
    height = mask->info.rect.h;
    size = mask->info.size;

    /* 遮罩尺寸合理性校验：防止损坏元数据导致超大分配或溢出 */
    if (width == 0 || height == 0 || width > 8192 || height > 8192)
        return 0;
    if (size == 0 || size > 16 * 1024 * 1024)  /* 单个遮罩压缩数据不应超过 16MB */
        return 0;

    void* mem0, * mem1;
    int len = ((width + 3) >> 2) * height;// 4对齐>>2等于除以4
    if (len <= 0)
        return 0;
    if (!(mem1 = _getmem(&m[1], len)))
        return 0;

    if (!(mem0 = _getmem(&m[0], size)))
        return 0;

    int ok = 0;
    Uint32 head_try[3] = { 20, 24, 24 };
    Uint32 size_try[3] = { size, size, size > 4 ? (size - 4) : 0 };

    for (int i = 0; i < 3 && !ok; i++)
    {
        Uint32 h = head_try[i];
        Uint32 s = size_try[i];
        if (s == 0)
            continue;
        if (SDL_RWseek(rw, mask->info.offset + h, RW_SEEK_SET) == -1)
            continue;
        if (SDL_RWread(rw, mem0, sizeof(Uint8), s) != s)
            continue;
        if (_lzodecompress(mem0, (size_t)s, mem1, (size_t)len) == len)
            ok = 1;
    }

    if (!ok)
        return 0;

    Uint8* data = (Uint8*)mem1;
    Uint8* dedata = (Uint8*)SDL_malloc(width * height);
    if (!dedata)
        return 0;
    Uint8* alpha = dedata;
    int  bitidx = 0;

    for (Uint32 y = 0; y < height; y++) {
        for (Uint32 x = 0; x < width; x++) {
            *alpha++ = (*data >> bitidx) & 3; //取当前字节中值，2bit

            bitidx += 2; //每两位代表一像素
            if (bitidx == 8) {
                bitidx = 0;
                data++;
            }
        }
        if (bitidx != 0) { //数据4对齐，有剩余就跳过。
            bitidx = 0;
            data++;
        }
    }

    return dedata;
}
//取遮罩（tmem: 临时缓冲区，传 NULL 使用 ud->mem）
static int _getmasksf(MAP_UserData* ud, Uint32 id, MASK_Data* mask, MAP_Mem* tmem, SDL_RWops* rw)
{
    Uint8* alpha = _getmaskdata(ud, id, mask, tmem, rw);
    SDL_Rect* rect = &mask->info.rect;

    if (!alpha)
        return 0;

    int is_async = (tmem != NULL);

    if (is_async) {
        /* ---- 后台线程：纯内存操作，零 SDL Surface API 调用 ---- */
        Uint32* canvas = (Uint32*)SDL_calloc((size_t)rect->w * rect->h, sizeof(Uint32));
        if (!canvas) {
            SDL_free(alpha);
            return 0;
        }

        /* 从地表图块拼合（手动 memcpy 替代 SDL_BlitSurface） */
        Uint32 mapid = (rect->x / 320) + (rect->y / 240) * ud->colnum;
        int sfx = -(rect->x % 320);
        int sfy = -(rect->y % 240);
        Uint32 curid = mapid;

        for (int y = sfy; y < rect->h; y += 240) {
            for (int x = sfx; x < rect->w; x += 320) {
                MAP_RawPixels tile = { NULL, 0, 0 };
                _getmapsf(ud, curid++, tmem, rw, &tile);
                if (tile.pixels) {
                    _blit_raw_tile(&tile, canvas, rect->w, rect->h, x, y);
                    SDL_free(tile.pixels);
                }
            }
            mapid += ud->colnum;
            curid = mapid;
        }

        /* 填充透明通道 */
        Uint8* palpha = alpha;
        for (int y = 0; y < rect->h; y++) {
            Uint32* row = canvas + y * rect->w;
            for (int x = 0; x < rect->w; x++) {
                Uint8 code = *palpha++;
                Uint8 out_a = code;
                if (code == 2)
                    out_a = 255;
                else if (code == 3)
                    out_a = 150;

                Uint32 px = row[x];
                px = (px & 0xFFFFFFFCu) | (code & 3u);
                px = (px & 0x00FFFFFFu) | ((Uint32)out_a << 24);
                row[x] = px;
            }
        }

        SDL_free(alpha);
        mask->raw.pixels = canvas;
        mask->raw.width = rect->w;
        mask->raw.height = rect->h;
        return 1;
    }

    /* ---- 主线程路径（原逻辑不变） ---- */
    SDL_Surface* msf = SDL_CreateRGBSurfaceWithFormat(SDL_SWSURFACE, rect->w, rect->h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!msf) {
        SDL_free(alpha);
        return 0;
    }
    //从地表扣图
    Uint32 mapid = (rect->x / 320) + (rect->y / 240) * ud->colnum;
    int sfx = -(rect->x % 320);
    int sfy = -(rect->y % 240);
    Uint32 curid = mapid;

    for (int y = sfy; y < msf->h; y += 240) {
        for (int x = sfx; x < msf->w; x += 320) {
            SDL_Surface* sf = _getmapsf(ud, curid++, NULL, rw, NULL);
            SDL_Rect xy = { x,y };
            if (sf) {
                SDL_BlitSurface(sf, NULL, msf, &xy);
                if (ud->mode == 0x9527)
                    SDL_FreeSurface(sf);
            }
        }
        mapid += ud->colnum;
        curid = mapid;
    }

    //填充透明通道
    Uint8* pixels = (Uint8*)msf->pixels;
    Uint8* palpha = alpha;
    for (int y = 0; y < msf->h; y++) {
        Uint32* row = (Uint32*)pixels;
        for (int x = 0; x < msf->w; x++) {
            Uint8 code = *palpha++;
            Uint8 out_a = code;
            if (code == 2)
                out_a = 255;
            else if (code == 3)
                out_a = 150;

            Uint32 px = row[x];
            px = (px & 0xFFFFFFFCu) | (code & 3u);
            px = (px & 0x00FFFFFFu) | ((Uint32)out_a << 24);
            row[x] = px;
        }
        pixels += msf->pitch;
    }

    SDL_free(alpha);
    mask->sf = msf;
    return 1;
}

//载入线程（使用 time->mem 独立缓冲区 + 独立 RWops，不复用 ud->mem/ud->file）
static Uint32 SDLCALL TimerCallback(Uint32 interval, void* param)
{
    TIME_Data* time = (TIME_Data*)param;
    MAP_UserData* ud = time->ud;

    /* ---- 1. 短暂加锁：检查 closing，读取 filebuf 信息 ---- */
    SDL_LockMutex(ud->mutex);
    if (ud->closing || !ud->filebuf)
    {
        if (time->type == TIME_TYPE_MAP)
        {
            MAP_Data* map = (MAP_Data*)time->data;
            map->loading = 0;
        }
        else if (time->type == TIME_TYPE_MAPFULL)
        {
            MAPFULL_Data* fm = (MAPFULL_Data*)time->data;
            if (fm && fm->map) fm->map->loading = 0;
        }
        ud->active_tasks--;
        SDL_CondSignal(ud->cond);
        _freemem(&time->mem[0]);
        _freemem(&time->mem[1]);
        SDL_ListAdd(&ud->list, param);
        SDL_UnlockMutex(ud->mutex);
        return 0;
    }
    void* fb = ud->filebuf;
    size_t fb_size = ud->filebuf_size;
    SDL_UnlockMutex(ud->mutex);

    /* ---- 2. 创建独立只读 RWops（无锁，无竞争） ---- */
    SDL_RWops* task_rw = SDL_RWFromConstMem(fb, (int)fb_size);
    if (!task_rw) {
        SDL_LockMutex(ud->mutex);
        if (time->type == TIME_TYPE_MAP) {
            ((MAP_Data*)time->data)->loading = 0;
        } else if (time->type == TIME_TYPE_MAPFULL) {
            MAPFULL_Data* fm = (MAPFULL_Data*)time->data;
            if (fm && fm->map) fm->map->loading = 0;
        }
        ud->active_tasks--;
        SDL_CondSignal(ud->cond);
        _freemem(&time->mem[0]);
        _freemem(&time->mem[1]);
        SDL_ListAdd(&ud->list, param);
        SDL_UnlockMutex(ud->mutex);
        return 0;
    }

    /* ---- 3. 无锁执行 I/O + 解码（后台线程：只产出裸像素） ---- */
    /* ★ 所有解码结果写入 time->result_*（私有），不写 map->（共享）
     *   消除主线程/Timer线程间的数据竞争（iOS nanov2堆损坏根因） */
    if (time->type == TIME_TYPE_MAP || time->type == TIME_TYPE_MAPFULL)
    {
        MAPFULL_Data* fm = NULL;
        if (time->type == TIME_TYPE_MAPFULL) {
            fm = (MAPFULL_Data*)time->data;
        }

        /* 解码地表 → 裸像素（不创建 SDL_Surface） */
        _getmapsf(ud, time->id, time->mem, task_rw, &time->result_raw);
        _getmasksinfo(ud, time->id, &time->result_mask, &time->result_masknum, task_rw);
        
        if (fm && time->result_masknum > 0 && time->result_mask)
        {
            fm->masknum = time->result_masknum;
            fm->mask_raws = (MAP_RawPixels*)SDL_calloc(time->result_masknum, sizeof(MAP_RawPixels));
            if (fm->mask_raws) {
                for (Uint32 i = 0; i < time->result_masknum; i++) {
                    MASK_Data mdata;
                    SDL_memset(&mdata, 0, sizeof(MASK_Data));
                    mdata.id = time->id;
                    mdata.info = time->result_mask[i];
                    
                    _getmasksf(ud, time->id, &mdata, time->mem, task_rw);
                        
                    fm->mask_raws[i] = mdata.raw;
                }
            }
        }
    }
    else if (time->type == TIME_TYPE_MASK)
    {
        MASK_Data* mask = (MASK_Data*)time->data;

        _getmasksf(ud, mask->id, mask, time->mem, task_rw);
    }

    SDL_RWclose(task_rw);

    /* ---- 4. 短暂加锁：更新共享状态 ---- */
    _freemem(&time->mem[0]);
    _freemem(&time->mem[1]);
    SDL_LockMutex(ud->mutex);
    /* ★ loading=0 必须在 ListAdd 之后、mutex 保护内
     *   否则主线程可能在结果入队前就发起新 Timer，导致竞态 */
    if (time->type == TIME_TYPE_MAP) {
        ((MAP_Data*)time->data)->loading = 0;
    } else if (time->type == TIME_TYPE_MAPFULL) {
        MAPFULL_Data* fm = (MAPFULL_Data*)time->data;
        if (fm && fm->map) fm->map->loading = 0;
    }
    SDL_ListAdd(&ud->list, param);
    ud->active_tasks--;
    SDL_CondSignal(ud->cond);
    SDL_UnlockMutex(ud->mutex);
    return 0;
}

static void MAP_DrainPendingNoCallback(lua_State* L, MAP_UserData* ud)
{
    SDL_LockMutex(ud->mutex);
    SDL_ListNode* node = ud->list;
    ud->list = NULL;
    SDL_UnlockMutex(ud->mutex);

    while (node)
    {
        SDL_ListNode* next = node->next;
        TIME_Data* time = (TIME_Data*)node->data;
        SDL_free(node);

        if (time)
        {
            if (time->cb_ref != LUA_NOREF && time->cb_ref != LUA_REFNIL)
                luaL_unref(L, LUA_REGISTRYINDEX, time->cb_ref);

            if (time->type == TIME_TYPE_MAP || time->type == TIME_TYPE_MAPFULL)
            {
                MAP_RawPixels* mask_raws = NULL;
                Uint32 saved_masknum = 0;
                if (time->type == TIME_TYPE_MAPFULL) {
                    MAPFULL_Data* fm = (MAPFULL_Data*)time->data;
                    if (fm) {
                        mask_raws = fm->mask_raws;
                        saved_masknum = fm->masknum;
                    }
                }
                
                /* 释放 Timer 线程产出的裸像素（存在 time->result_* 中） */
                if (time->result_raw.pixels) {
                    SDL_free(time->result_raw.pixels);
                    time->result_raw.pixels = NULL;
                }
                if (time->result_mask) {
                    SDL_free(time->result_mask);
                    time->result_mask = NULL;
                }
                if (mask_raws)
                {
                    for (Uint32 i = 0; i < saved_masknum; i++) {
                        if (mask_raws[i].pixels) SDL_free(mask_raws[i].pixels);
                    }
                    SDL_free(mask_raws);
                }
                if (time->type == TIME_TYPE_MAPFULL) SDL_free(time->data);
            }
            else if (time->type == TIME_TYPE_MASK)
            {
                MASK_Data* mask = (MASK_Data*)time->data;
                if (mask)
                {
                    /* 释放后台线程产出的裸像素 */
                    if (mask->raw.pixels)
                        SDL_free(mask->raw.pixels);
                    SDL_free(mask);
                }
            }

            _freemem(&time->mem[0]);
            _freemem(&time->mem[1]);
            SDL_free(time);
        }

        node = next;
    }
}

static int LUA_Run(lua_State* L)
{
    MAP_UserData* ud = (MAP_UserData*)luaL_checkudata(L, 1, MAP_NAME);

    SDL_LockMutex(ud->mutex);
    SDL_ListNode* node = ud->list;
    ud->list = NULL;
    SDL_UnlockMutex(ud->mutex);

    while (node)
    {
        SDL_ListNode* next = node->next;
        TIME_Data* time = (TIME_Data*)node->data;
        SDL_free(node);

        lua_rawgeti(L, LUA_REGISTRYINDEX, time->cb_ref);
        luaL_unref(L, LUA_REGISTRYINDEX, time->cb_ref);

        if (time->type == TIME_TYPE_MAP || time->type == TIME_TYPE_MAPFULL)
        {
            MAP_Data* map = NULL;
            MAPFULL_Data* fm = NULL;
            if (time->type == TIME_TYPE_MAPFULL) {
                fm = (MAPFULL_Data*)time->data;
                map = fm->map;
            } else {
                map = (MAP_Data*)time->data;
            }
            Uint32 id = time->id;

            /* ★ 主线程：裸像素→SDL_Surface（ARGB8888）
             *   从 time->result_* 读取（Timer线程私有输出），不从 map-> 读取 */
            SDL_Surface* map_sf = _raw_to_surface(&time->result_raw, SDL_PIXELFORMAT_ARGB8888);

            if (!map_sf)
            {
                if (lua_isfunction(L, -1)) {
                    lua_pushnil(L);
                    lua_pushnil(L);
                    lua_pcall(L, 2, 0, 0);
                } else {
                    lua_pop(L, 1);
                }
            }
            else
            {
                {
                    SDL_Surface** sf = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
                    *sf = map_sf;
                    luaL_setmetatable(L, "SDL_Surface");
                }

                lua_createtable(L, time->result_masknum, 0);
                for (Uint32 i = 0; i < time->result_masknum; i++)
                {
                    MAP_MaskInfo* info = &time->result_mask[i];
                    lua_createtable(L, 0, 8);
                    lua_pushinteger(L, id);
                    lua_setfield(L, -2, "id");
                    lua_pushinteger(L, info->offset);
                    lua_setfield(L, -2, "offset");
                    lua_pushinteger(L, info->mode);
                    lua_setfield(L, -2, "mode");
                    lua_pushinteger(L, info->rect.x);
                    lua_setfield(L, -2, "x");
                    lua_pushinteger(L, info->rect.y);
                    lua_setfield(L, -2, "y");
                    lua_pushinteger(L, info->rect.w);
                    lua_setfield(L, -2, "w");
                    lua_pushinteger(L, info->rect.h);
                    lua_setfield(L, -2, "h");
                    
                    /* ★ 遮罩用 ARGB8888（带 alpha 通道，半透明混合） */
                    if (fm && fm->mask_raws && fm->mask_raws[i].pixels) {
                        SDL_Surface* mask_sf = _raw_to_surface(&fm->mask_raws[i], SDL_PIXELFORMAT_ARGB8888);
                        if (mask_sf) {
                            SDL_Surface** msf = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
                            *msf = mask_sf;
                            luaL_setmetatable(L, "SDL_Surface");
                            lua_setfield(L, -2, "sf");
                        }
                    }
                    
                    lua_seti(L, -2, i + 1);
                }

                lua_call(L, 2, 0);
            }

            /* 释放 Timer 线程产出的遮罩信息 */
            if (time->result_mask) {
                SDL_free(time->result_mask);
                time->result_mask = NULL;
            }
            if (fm) {
                if (fm->mask_raws) {
                    for (Uint32 i = 0; i < fm->masknum; i++) {
                         if (fm->mask_raws[i].pixels) SDL_free(fm->mask_raws[i].pixels);
                    }
                    SDL_free(fm->mask_raws);
                }
                SDL_free(fm);
            }
            SDL_free(time);
        }
        else if (time->type == TIME_TYPE_MASK)
        {
            MASK_Data* mask = (MASK_Data*)time->data;

            /* ★ 主线程：遮罩裸像素→SDL_Surface（ARGB8888 带 alpha） */
            SDL_Surface* mask_sf = _raw_to_surface(&mask->raw, SDL_PIXELFORMAT_ARGB8888);

            if (!mask_sf)
            {
                if (lua_isfunction(L, -1)) {
                    lua_pushnil(L);
                    lua_pcall(L, 1, 0, 0);
                } else {
                    lua_pop(L, 1);
                }
            }
            else
            {
                SDL_Surface** sf = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
                *sf = mask_sf;
                luaL_setmetatable(L, "SDL_Surface");
                lua_call(L, 1, 0);
            }
            SDL_free(mask);
            SDL_free(time);
        }
        else
        {
            lua_pop(L, 1);
            SDL_free(time);
        }

        node = next;
    }

    return 0;
}

static int LUA_GetResult(lua_State* L)
{
    return LUA_Run(L);
}

static int LUA_GetMap(lua_State* L)
{
    MAP_UserData* ud = (MAP_UserData*)luaL_checkudata(L, 1, MAP_NAME);
    Uint32 id = (Uint32)luaL_checkinteger(L, 2); //从0开始
    int has_cb = lua_isfunction(L, 3);

    if (id >= ud->mapnum)
        return luaL_error(L, "map id error!");

    MAP_Data* map = &ud->map[id];

    if (has_cb)
    {
        SDL_LockMutex(ud->mutex);
        if (ud->closing || map->loading)
        {
            SDL_UnlockMutex(ud->mutex);
            return 0;
        }
        map->loading = 1;
        ud->active_tasks++;
        SDL_UnlockMutex(ud->mutex);

        TIME_Data* time = (TIME_Data*)SDL_calloc(1, sizeof(TIME_Data));
        time->type = TIME_TYPE_MAP;
        time->ud = ud;
        time->data = (void*)map;
        time->id = id;
        lua_pushvalue(L, 3);
        time->cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        SDL_AddTimer(0, TimerCallback, (void*)time);
    }
    else {
        SDL_Surface* out_sf = NULL;

        SDL_LockMutex(ud->mutex);
        if (!ud->closing && ud->file)
        {
            if (!map->sf)
                map->sf = _getmapsf(ud, id, NULL, ud->file, NULL);

            if (map->sf)
            {
                if (ud->mode == 0x9527)
                {
                    out_sf = map->sf;
                    map->sf = NULL;
                }
                else
                {
                    out_sf = SDL_DuplicateSurface(map->sf);
                }
            }
        }
        SDL_UnlockMutex(ud->mutex);

        if (!out_sf)
            return 0;

        {
            SDL_Surface** sf = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
            *sf = out_sf;
            luaL_setmetatable(L, "SDL_Surface");
        }

        return 1;
    }
    return 0;
}

static int LUA_GetMapFull(lua_State* L)
{
    MAP_UserData* ud = (MAP_UserData*)luaL_checkudata(L, 1, MAP_NAME);
    Uint32 id = (Uint32)luaL_checkinteger(L, 2); //从0开始
    int has_cb = lua_isfunction(L, 3);

    if (id >= ud->mapnum)
        return luaL_error(L, "map id error!");

    MAP_Data* map = &ud->map[id];

    if (has_cb)
    {
        SDL_LockMutex(ud->mutex);
        if (ud->closing || map->loading)
        {
            SDL_UnlockMutex(ud->mutex);
            return 0;
        }
        map->loading = 1;
        ud->active_tasks++;
        SDL_UnlockMutex(ud->mutex);

        TIME_Data* time = (TIME_Data*)SDL_calloc(1, sizeof(TIME_Data));
        time->type = TIME_TYPE_MAPFULL;
        time->ud = ud;
        time->id = id;
        
        MAPFULL_Data* fm = (MAPFULL_Data*)SDL_calloc(1, sizeof(MAPFULL_Data));
        fm->map = map;
        time->data = (void*)fm;
        
        lua_pushvalue(L, 3);
        time->cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        SDL_AddTimer(0, TimerCallback, (void*)time);
        return 0;
    }
    
    return LUA_GetMap(L);
}

static int LUA_GetMapInfo(lua_State* L)
{
    MAP_UserData* ud = (MAP_UserData*)luaL_checkudata(L, 1, MAP_NAME);
    Uint32 id = (Uint32)luaL_checkinteger(L, 2); //从0开始
    int has_cb = lua_isfunction(L, 3);

    if (id >= ud->mapnum)
        return luaL_error(L, "map id error!");

    MAP_Data* map = &ud->map[id];

    if (has_cb)
    {
        SDL_LockMutex(ud->mutex);
        if (ud->closing || map->loading)
        {
            SDL_UnlockMutex(ud->mutex);
            return 0;
        }
        map->loading = 1;
        ud->active_tasks++;
        SDL_UnlockMutex(ud->mutex);

        TIME_Data* time = (TIME_Data*)SDL_calloc(1, sizeof(TIME_Data));
        time->type = TIME_TYPE_MAP;
        time->ud = ud;
        time->data = (void*)map;
        time->id = id;
        lua_pushvalue(L, 3);
        time->cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        SDL_AddTimer(0, TimerCallback, (void*)time);
        return 0;
    }

    SDL_Surface* out_sf = NULL;
    Uint32 num = 0;
    MAP_MaskInfo* mask = NULL;

    SDL_LockMutex(ud->mutex);
    if (!ud->closing && ud->file)
    {
        if (!map->sf)
            map->sf = _getmapsf(ud, id, NULL, ud->file, NULL);

        if (map->sf)
        {
            if (ud->mode == 0x9527)
            {
                out_sf = map->sf;
                map->sf = NULL;
            }
            else
            {
                out_sf = SDL_DuplicateSurface(map->sf);
            }
        }

        _getmasksinfo(ud, id, &mask, &num, ud->file);
    }
    SDL_UnlockMutex(ud->mutex);

    if (!out_sf)
    {
        if (num && mask)
            SDL_free(mask);
        return 0;
    }

    {
        SDL_Surface** sf = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *sf = out_sf;
        luaL_setmetatable(L, "SDL_Surface");
    }

    lua_createtable(L, num, 0);
    for (Uint32 i = 0; i < num; i++)
    {
        MAP_MaskInfo* info = &mask[i];
        lua_createtable(L, 0, 7);
        lua_pushinteger(L, id);
        lua_setfield(L, -2, "id");
        lua_pushinteger(L, info->offset);
        lua_setfield(L, -2, "offset");
        lua_pushinteger(L, info->mode);
        lua_setfield(L, -2, "mode");
        lua_pushinteger(L, info->rect.x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, info->rect.y);
        lua_setfield(L, -2, "y");
        lua_pushinteger(L, info->rect.w);
        lua_setfield(L, -2, "w");
        lua_pushinteger(L, info->rect.h);
        lua_setfield(L, -2, "h");
        lua_seti(L, -2, i + 1);
    }

    if (num && mask)
        SDL_free(mask);
    return 2;
}

static int LUA_GetMaskInfo(lua_State* L)
{
    MAP_UserData* ud = (MAP_UserData*)luaL_checkudata(L, 1, MAP_NAME);
    Uint32 id = (Uint32)luaL_checkinteger(L, 2); //从0开始

    Uint32 num = 0;
    MAP_MaskInfo* mask = NULL;

    SDL_LockMutex(ud->mutex);
    if (!ud->closing && ud->file)
        _getmasksinfo(ud, id, &mask, &num, ud->file);
    SDL_UnlockMutex(ud->mutex);

    lua_createtable(L, num, 0);
    for (Uint32 i = 0; i < num; i++)
    {
        MAP_MaskInfo* info = &mask[i];
        lua_createtable(L, 0, 7);
        lua_pushinteger(L, id);
        lua_setfield(L, -2, "id");
        lua_pushinteger(L, info->offset);
        lua_setfield(L, -2, "offset");
        lua_pushinteger(L, info->mode);
        lua_setfield(L, -2, "mode");
        lua_pushinteger(L, info->rect.x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, info->rect.y);
        lua_setfield(L, -2, "y");
        lua_pushinteger(L, info->rect.w);
        lua_setfield(L, -2, "w");
        lua_pushinteger(L, info->rect.h);
        lua_setfield(L, -2, "h");
        lua_seti(L, -2, i + 1);
    }

    if (num && mask)
        SDL_free(mask);
    return 1;
}

static int LUA_GetMask(lua_State* L)
{
    MAP_UserData* ud = (MAP_UserData*)luaL_checkudata(L, 1, MAP_NAME);
    int has_cb = lua_isfunction(L, 3);

    lua_getfield(L, 2, "id");
    Uint32 id = (Uint32)luaL_checkinteger(L, -1);
    lua_getfield(L, 2, "offset");
    Uint32 offset = (Uint32)luaL_checkinteger(L, -1);

    if (has_cb)
    {
        SDL_LockMutex(ud->mutex);
        if (ud->closing)
        {
            SDL_UnlockMutex(ud->mutex);
            return 0;
        }
        ud->active_tasks++;
        SDL_UnlockMutex(ud->mutex);

        MASK_Data* mask = (MASK_Data*)SDL_calloc(1, sizeof(MASK_Data));
        if (!mask)
        {
            SDL_LockMutex(ud->mutex);
            ud->active_tasks--;
            SDL_CondSignal(ud->cond);
            SDL_UnlockMutex(ud->mutex);
            return 0;
        }
        mask->id = id;
        mask->info.offset = offset;

        TIME_Data* time = (TIME_Data*)SDL_calloc(1, sizeof(TIME_Data));
        if (!time)
        {
            SDL_free(mask);
            SDL_LockMutex(ud->mutex);
            ud->active_tasks--;
            SDL_CondSignal(ud->cond);
            SDL_UnlockMutex(ud->mutex);
            return 0;
        }
        time->type = TIME_TYPE_MASK;
        time->ud = ud;
        time->data = (void*)mask;
        time->id = 0;
        lua_pushvalue(L, 3);
        time->cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        SDL_AddTimer(0, TimerCallback, (void*)time);
    }
    else {
        MASK_Data mask;
        SDL_memset(&mask, 0, sizeof(MASK_Data));
        mask.info.offset = offset;

        SDL_LockMutex(ud->mutex);
        if (!ud->closing && ud->file)
        {
            _getmasksf(ud, id, &mask, NULL, ud->file);
        }
        SDL_UnlockMutex(ud->mutex);

        if (!mask.sf)
            return 0;

        SDL_Surface** sf = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *sf = mask.sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

//读障碍
static int LUA_GetCell(lua_State* L)
{
    MAP_UserData* ud = (MAP_UserData*)luaL_checkudata(L, 1, MAP_NAME);

    SDL_LockMutex(ud->mutex);
    if (ud->closing || !ud->file)
    {
        SDL_UnlockMutex(ud->mutex);
        return luaL_error(L, "cell read error!");
    }

    int line_width = (ud->colnum - 1) * 16; //一行格子  (列-1)*16
    int line_size = ud->colnum * 176;       //一行块大小(320/20)*(240/20)=192
    Uint32 masknum;

    MAP_BlockInfo info;
    int loop = 1, x, y;
    int celllen = ud->rownum * ud->colnum * 192;
    void* mem = _getmem(&ud->mem[0], celllen);
    Uint8* cell = (Uint8*)mem;
    Uint8* m192 = (Uint8*)_getmem(&ud->mem[1], 192);
    if (!cell || !m192)
    {
        SDL_UnlockMutex(ud->mutex);
        return luaL_error(L, "getmem error!");
    }

    Uint32 n = 0, h, l;
    for (h = 0; h < ud->rownum; h++)
    {
        for (l = 0; l < ud->colnum; l++)
        {
            if (SDL_RWseek(ud->file, ud->maplist[n++], RW_SEEK_SET) == -1 ||
                SDL_RWread(ud->file, &masknum, sizeof(Uint32), 1) != 1)//附近遮罩
                goto readerr;

            if (ud->flag == MAP_FLAG_M10 && masknum > 0 &&
                SDL_RWseek(ud->file, masknum * sizeof(Uint32), RW_SEEK_CUR) == -1)
                goto readerr;
            loop = 1;
            while (loop)
            {
                if (SDL_RWread(ud->file, &info, sizeof(MAP_BlockInfo), 1) != 1)
                    goto readerr;

                switch (info.flag)
                {
                case MAP_BLOCK_CELL:
                {
                    Uint8* rtemp = m192;
                    Uint8* wtemp = cell;
                    if (SDL_RWread(ud->file, (void*)m192, sizeof(Uint8), info.size) != info.size || info.size != 192)
                        goto readerr;
                    for (y = 0; y < 12; y++) {
                        for (x = 0; x < 16; x++) {
                            *wtemp++ = *rtemp == 2 ? 0 : *rtemp;
                            rtemp++;
                        }
                        wtemp += line_width;
                    }
                    loop = 0;
                    break;
                }
                case 0:
                    loop = 0;
                    break;
                default:
                    if (SDL_RWseek(ud->file, info.size, RW_SEEK_CUR) == -1)
                        goto readerr;
                    break;
                }
            }
            cell += 16; //下一列
        }
        cell += line_size; //下一行
    }
    lua_pushlstring(L, mem, celllen);
    SDL_UnlockMutex(ud->mutex);
    return 1;
readerr:
    SDL_UnlockMutex(ud->mutex);
    return luaL_error(L, "cell read error!");
}

//读二进制
static int LUA_GetBlock(lua_State* L)
{
    MAP_UserData* ud = (MAP_UserData*)luaL_checkudata(L, 1, MAP_NAME);
    Uint32 id = (Uint32)luaL_checkinteger(L, 2); //从0开始

    SDL_LockMutex(ud->mutex);
    if (ud->closing || !ud->file)
    {
        SDL_UnlockMutex(ud->mutex);
        return luaL_error(L, "read error!");
    }

    if (id >= ud->mapnum)
    {
        SDL_UnlockMutex(ud->mutex);
        return luaL_error(L, "map id error!");
    }

    Uint32 masknum;

    if (SDL_RWseek(ud->file, ud->maplist[id], RW_SEEK_SET) == -1 ||
        SDL_RWread(ud->file, &masknum, sizeof(Uint32), 1) != 1)//附近遮罩ID
    {
        SDL_UnlockMutex(ud->mutex);
        return luaL_error(L, "read error!");
    }

    lua_createtable(L, 0, 0);
    int rettab = lua_gettop(L);
    lua_createtable(L, masknum, 0);
    int masktab = lua_gettop(L);

    if (ud->flag == MAP_FLAG_M10 && masknum > 0) {
        Uint32* maskid;
        if (!(maskid = (Uint32*)_getmem(&ud->mem[0], sizeof(Uint32) * masknum)))
        {
            SDL_UnlockMutex(ud->mutex);
            return luaL_error(L, "getmem error!");
        }

        if (SDL_RWread(ud->file, maskid, sizeof(Uint32), masknum) != masknum)
        {
            SDL_UnlockMutex(ud->mutex);
            return luaL_error(L, "read error!");
        }

        for (Uint32 i = 0; i < masknum; i++)
        {
            lua_pushinteger(L, maskid[i]);
            lua_seti(L, masktab, i + 1);
        }

    }

    MAP_BlockInfo info = { 0, 0 };

    void* mem0 = NULL;

    while (1)
    {
        if (SDL_RWread(ud->file, &info, sizeof(MAP_BlockInfo), 1) != 1)
        {
            SDL_UnlockMutex(ud->mutex);
            return luaL_error(L, "read error!");
        }

        switch (info.flag)
        {
        case 0:
        { //结束
            if (SDL_RWseek(ud->file, info.size, RW_SEEK_CUR) == -1)
            {
                SDL_UnlockMutex(ud->mutex);
                return luaL_error(L, "read error!");
            }

            lua_setfield(L, rettab, "MASK");
            SDL_UnlockMutex(ud->mutex);
            return 1;
        }
        case MAP_BLOCK_MASK:
        {
            if (!(mem0 = _getmem(&ud->mem[0], info.size)))
            {
                SDL_UnlockMutex(ud->mutex);
                return luaL_error(L, "read error!");
            }
            if (SDL_RWread(ud->file, mem0, sizeof(Uint8), info.size) != info.size)
            {
                SDL_UnlockMutex(ud->mutex);
                return luaL_error(L, "read error!");
            }

            lua_pushlstring(L, (char*)mem0, info.size);
            lua_seti(L, masktab, masknum--);

            break;
        }
        default:
            if (!(mem0 = _getmem(&ud->mem[0], info.size)))
            {
                SDL_UnlockMutex(ud->mutex);
                return luaL_error(L, "read error!");
            }
            if (SDL_RWread(ud->file, mem0, sizeof(Uint8), info.size) != info.size)
            {
                SDL_UnlockMutex(ud->mutex);
                return luaL_error(L, "read error!");
            }

            lua_pushlstring(L, (char*)&info.flag, sizeof(Uint32));
            lua_pushlstring(L, (char*)mem0, info.size);
            lua_settable(L, rettab);
            break;
        }
    }
    SDL_UnlockMutex(ud->mutex);
    return 1;
}

static int MAP_NEW(lua_State* L)
{
    size_t in_len = 0;
    const char* in = luaL_checklstring(L, 1, &in_len);
    lua_Integer want = luaL_optinteger(L, 2, (lua_Integer)in_len);
    if (want <= 0 || (size_t)want > in_len)
        want = (lua_Integer)in_len;

    if (want > 0x7fffffff)
        return luaL_error(L, "open map data failed");

    MAP_Header head;

    void* buf = SDL_malloc((size_t)want);
    if (!buf)
        return luaL_error(L, "open map data failed");
    SDL_memcpy(buf, in, (size_t)want);

    SDL_RWops* rw = MAP_RWFromBorrowedMem(buf, (size_t)want);
    if (rw == NULL)
    {
        SDL_free(buf);
        return luaL_error(L, "open map data failed");
    }

    if (SDL_RWread(rw, (void*)&head, sizeof(MAP_Header), 1) != 1)
    {
        if (rw->close)
            rw->close(rw);
        SDL_free(buf);
        return luaL_error(L, "open map data failed");
    }

    if (head.flag != MAP_FLAG_M10 && head.flag != MAP_FLAG_MAPX)
    {
        if (rw->close)
            rw->close(rw);
        SDL_free(buf);
        return luaL_error(L, "Unsupported map!");
    }

    MAP_UserData* ud = (MAP_UserData*)lua_newuserdata(L, sizeof(MAP_UserData));
    luaL_setmetatable(L, MAP_NAME);
    SDL_memset(ud, 0, sizeof(MAP_UserData));
    ud->file = rw;
    ud->filebuf = buf;
    ud->filebuf_size = (size_t)want;
    ud->flag = head.flag;
    ud->rownum = (Uint32)SDL_ceil(head.height / 240.0); //行数
    ud->colnum = (Uint32)SDL_ceil(head.width / 320.0);  //列数
    ud->mapnum = ud->rownum * ud->colnum;
    ud->mutex = SDL_CreateMutex();
    ud->cond = SDL_CreateCond();
    if (!ud->mutex)
        goto openerr;
    //地图部分
    ud->maplist = (Uint32*)SDL_malloc(ud->mapnum * sizeof(Uint32)); //地表偏移
    ud->map = (MAP_Data*)SDL_calloc(ud->mapnum, sizeof(MAP_Data));  //缓存
    if (!ud->maplist || !ud->map)
        goto openerr;

    if (SDL_RWread(rw, ud->maplist, sizeof(Uint32), ud->mapnum) != ud->mapnum)
        goto openerr;

    //遮罩部分
    if (head.flag == MAP_FLAG_M10) {
        Uint32 maskoffset;
        if (SDL_RWread(rw, &maskoffset, sizeof(Uint32), 1) != 1)
            goto openerr;
        if (SDL_RWseek(rw, maskoffset, RW_SEEK_SET) == -1 ||
            SDL_RWread(rw, &ud->masknum, sizeof(Uint32), 1) != 1)
            goto openerr;

        if (ud->masknum > 0) {
            ud->masklist = (Uint32*)SDL_malloc(ud->masknum * sizeof(Uint32));  //遮罩偏移
            if (SDL_RWread(rw, ud->masklist, sizeof(Uint32), ud->masknum) != ud->masknum)
                goto openerr;
        }

        lua_createtable(L, 0, 7);
        lua_pushlstring(L, (char*)&head.flag, sizeof(Uint32));
        lua_setfield(L, -2, "flag");
        lua_pushinteger(L, head.width);
        lua_setfield(L, -2, "width");
        lua_pushinteger(L, head.height);
        lua_setfield(L, -2, "height");
        lua_pushinteger(L, ud->colnum);
        lua_setfield(L, -2, "colnum");
        lua_pushinteger(L, ud->rownum);
        lua_setfield(L, -2, "rownum");
        lua_pushinteger(L, ud->mapnum);
        lua_setfield(L, -2, "mapnum");
        lua_pushinteger(L, ud->masknum);
        lua_setfield(L, -2, "masknum");

        return 2; //ud,table
    }
    else {
        SDL_RWseek(rw, sizeof(Uint32), RW_SEEK_CUR);//文件大小
        MAP_BlockInfo info;
        void* mem = NULL;
        if (SDL_RWread(ud->file, &info, sizeof(MAP_BlockInfo), 1) != 1)
            goto openerr;
        if (!(mem = _getmem(&ud->jpeh, info.size)))
            goto openerr;
        if (SDL_RWread(ud->file, mem, sizeof(Uint8), info.size) != info.size)
            goto openerr;

        lua_createtable(L, 0, 7);
        lua_pushlstring(L, (char*)&head.flag, sizeof(Uint32));
        lua_setfield(L, -2, "flag");
        lua_pushinteger(L, head.width);
        lua_setfield(L, -2, "width");
        lua_pushinteger(L, head.height);
        lua_setfield(L, -2, "height");
        lua_pushinteger(L, ud->colnum);
        lua_setfield(L, -2, "colnum");//列数
        lua_pushinteger(L, ud->rownum);
        lua_setfield(L, -2, "rownum");//行数
        lua_pushinteger(L, ud->mapnum);
        lua_setfield(L, -2, "mapnum");
        lua_pushinteger(L, ud->masknum);
        lua_setfield(L, -2, "masknum");

        return 2; //ud,table
    }

openerr:
    if (ud->mutex)
    {
        SDL_DestroyMutex(ud->mutex);
        ud->mutex = NULL;
    }
    if (ud->cond)
    {
        SDL_DestroyCond(ud->cond);
        ud->cond = NULL;
    }
    if (ud->map)
    {
        SDL_free(ud->map);
        ud->map = NULL;
    }
    if (ud->maplist)
    {
        SDL_free(ud->maplist);
        ud->maplist = NULL;
    }
    if (ud->masklist)
    {
        SDL_free(ud->masklist);
        ud->masklist = NULL;
    }
    if (ud->jpeh.mem)
    {
        SDL_free(ud->jpeh.mem);
        ud->jpeh.mem = NULL;
        ud->jpeh.size = 0;
    }

    if (ud->mem[0].mem)
    {
        SDL_free(ud->mem[0].mem);
        ud->mem[0].mem = NULL;
        ud->mem[0].size = 0;
    }

    if (ud->mem[1].mem)
    {
        SDL_free(ud->mem[1].mem);
        ud->mem[1].mem = NULL;
        ud->mem[1].size = 0;
    }

    if (rw && rw->close)
        rw->close(rw);
    ud->file = NULL;

    if (ud->filebuf)
    {
        SDL_free(ud->filebuf);
        ud->filebuf = NULL;
        ud->filebuf_size = 0;
    }
    return luaL_error(L, "open map data failed");
}

static int LUA_Clear(lua_State* L)
{
    MAP_UserData* ud = (MAP_UserData*)luaL_checkudata(L, 1, MAP_NAME);

    SDL_LockMutex(ud->mutex);

    // 等待所有 Timer 线程异步任务完成，避免它们仍在读写 mem[] 和 map[]
    while (ud->active_tasks > 0) {
        SDL_CondWait(ud->cond, ud->mutex);
    }

    // 排空已完成但未消费的回调结果
    SDL_ListNode* node = ud->list;
    ud->list = NULL;

    // 释放地表缓存
    for (Uint32 n = 0; n < ud->mapnum; n++) {
        if (ud->map[n].sf)
            SDL_FreeSurface(ud->map[n].sf);
        if (ud->map[n].mask)
            SDL_free(ud->map[n].mask);
    }
    SDL_memset(ud->map, 0, ud->mapnum * sizeof(MAP_Data));

    // 释放临时缓冲区
    if (ud->mem[0].mem) {
        SDL_free(ud->mem[0].mem);
        ud->mem[0].mem = NULL;
        ud->mem[0].size = 0;
    }

    if (ud->mem[1].mem) {
        SDL_free(ud->mem[1].mem);
        ud->mem[1].mem = NULL;
        ud->mem[1].size = 0;
    }

    SDL_UnlockMutex(ud->mutex);

    // 在锁外释放排空的回调资源（含 Lua ref 和 mask 数据）
    while (node)
    {
        SDL_ListNode* next = node->next;
        TIME_Data* time = (TIME_Data*)node->data;
        SDL_free(node);

        if (time)
        {
            if (time->cb_ref != LUA_NOREF && time->cb_ref != LUA_REFNIL)
                luaL_unref(L, LUA_REGISTRYINDEX, time->cb_ref);

            if (time->type == TIME_TYPE_MAP)
            {
                /* 释放 Timer 线程产出的结果 */
                if (time->result_raw.pixels) {
                    SDL_free(time->result_raw.pixels);
                    time->result_raw.pixels = NULL;
                }
                if (time->result_mask) {
                    SDL_free(time->result_mask);
                    time->result_mask = NULL;
                }
            }
            else if (time->type == TIME_TYPE_MASK)
            {
                MASK_Data* mask = (MASK_Data*)time->data;
                if (mask) {
                    if (mask->raw.pixels)
                        SDL_free(mask->raw.pixels);
                    SDL_free(mask);
                }
            }
            else if (time->type == TIME_TYPE_MAPFULL)
            {
                /* 释放 Timer 线程产出的结果 */
                if (time->result_raw.pixels) {
                    SDL_free(time->result_raw.pixels);
                    time->result_raw.pixels = NULL;
                }
                if (time->result_mask) {
                    SDL_free(time->result_mask);
                    time->result_mask = NULL;
                }
                MAPFULL_Data* fm = (MAPFULL_Data*)time->data;
                if (fm) {
                    if (fm->mask_raws) {
                        Uint32 masknum = fm->masknum;
                        for (Uint32 i = 0; i < masknum; i++) {
                            if (fm->mask_raws[i].pixels) SDL_free(fm->mask_raws[i].pixels);
                        }
                        SDL_free(fm->mask_raws);
                    }
                    SDL_free(fm);
                }
            }
            _freemem(&time->mem[0]);
            _freemem(&time->mem[1]);
            SDL_free(time);
        }
        node = next;
    }

    return 0;
}

static int LUA_GC(lua_State* L)
{
    MAP_UserData* ud = (MAP_UserData*)luaL_checkudata(L, 1, MAP_NAME);

    if (!ud->mutex)
        return 0;

    /* 1. 设置 closing 标志 + 等待所有异步任务完成（它们仍需读 filebuf） */
    SDL_LockMutex(ud->mutex);
    ud->closing = 1;
    while (ud->active_tasks > 0) {
        SDL_CondWait(ud->cond, ud->mutex);
    }
    SDL_UnlockMutex(ud->mutex);


    /* 3. 所有异步任务已完成，安全关闭 rw */
    SDL_LockMutex(ud->mutex);
    SDL_RWops* rw = ud->file;
    ud->file = NULL;
    SDL_UnlockMutex(ud->mutex);

    if (rw && rw->close)
        rw->close(rw);

    MAP_DrainPendingNoCallback(L, ud);

    SDL_LockMutex(ud->mutex);
    if (ud->map)
    {
        for (Uint32 n = 0; n < ud->mapnum; n++)
        {
            if (ud->map[n].sf)
                SDL_FreeSurface(ud->map[n].sf);

            if (ud->map[n].mask)
                SDL_free(ud->map[n].mask);
        }

        SDL_free(ud->map);
        ud->map = NULL;
    }

    if (ud->maplist)
    {
        SDL_free(ud->maplist);
        ud->maplist = NULL;
    }

    if (ud->masklist)
    {
        SDL_free(ud->masklist);
        ud->masklist = NULL;
    }

    if (ud->jpeh.mem)
    {
        SDL_free(ud->jpeh.mem);
        ud->jpeh.mem = NULL;
        ud->jpeh.size = 0;
    }

    if (ud->mem[0].mem)
    {
        SDL_free(ud->mem[0].mem);
        ud->mem[0].mem = NULL;
        ud->mem[0].size = 0;
    }

    if (ud->mem[1].mem)
    {
        SDL_free(ud->mem[1].mem);
        ud->mem[1].mem = NULL;
        ud->mem[1].size = 0;
    }

    if (ud->filebuf)
    {
        SDL_free(ud->filebuf);
        ud->filebuf = NULL;
        ud->filebuf_size = 0;
    }

    SDL_UnlockMutex(ud->mutex);

    SDL_DestroyMutex(ud->mutex);
    ud->mutex = NULL;
    if (ud->cond) {
        SDL_DestroyCond(ud->cond);
        ud->cond = NULL;
    }
    return 0;
}

static int LUA_SetMode(lua_State* L)
{
    MAP_UserData* ud = (MAP_UserData*)luaL_checkudata(L, 1, MAP_NAME);
    ud->mode = (Uint32)luaL_checkinteger(L, 2);
    return 0;
}

MYGXY_API int luaopen_mygxy_map(lua_State* L)
{
    const luaL_Reg funcs[] = {
        {"__gc", LUA_GC},
        {"__close", LUA_GC},
        {"GetMap", LUA_GetMap},
    {"GetMapFull", LUA_GetMapFull},
        {"GetMaskInfo", LUA_GetMaskInfo},
        {"GetMask", LUA_GetMask},
        {"GetResult", LUA_GetResult},
        {"GetCell", LUA_GetCell},
        {"GetBlock", LUA_GetBlock},
        {"Clear", LUA_Clear},
        {"SetMode", LUA_SetMode},
        {"Run", LUA_Run},
        {NULL, NULL},
    };
#ifdef _DEBUG
    setvbuf(stdout, NULL, _IONBF, 0);
#endif // DEBUG
    luaL_newmetatable(L, MAP_NAME);
    luaL_setfuncs(L, funcs, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    lua_pushcfunction(L, MAP_NEW);
    return 1;
}