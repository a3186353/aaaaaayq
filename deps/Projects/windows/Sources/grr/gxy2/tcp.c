#include "tcp.h"

static Uint32 RGB565to888(Uint16 color, Uint8 Alpha)
{

    Uint32 r = (color >> 11) & 0x1f;
    Uint32 g = (color >> 5) & 0x3f;
    Uint32 b = (color) & 0x1f;

    Uint32 A = Alpha << 24;
    Uint32 R = ((r << 3) | (r >> 2)) << 16;
    Uint32 G = ((g << 2) | (g >> 4)) << 8;
    Uint32 B = (b << 3) | (b >> 2);

    return A | R | G | B;
}

static Uint32 RGB565to888_Pal(Uint16 color16, Uint32 R1, Uint32 G1, Uint32 B1, Uint32 R2, Uint32 G2, Uint32 B2, Uint32 R3, Uint32 G3, Uint32 B3)
{

    Uint32 r = (color16 >> 11) & 0x1f;
    Uint32 g = (color16 >> 5) & 0x3f;
    Uint32 b = (color16) & 0x1f;

    Uint32 r2 = r * R1 + g * R2 + b * R3;
    Uint32 g2 = r * G1 + g * G2 + b * G3;
    Uint32 b2 = r * B1 + g * B2 + b * B3;
    Uint32 A, R, G, B;
    r = r2 >> 8;
    g = g2 >> 8;
    b = b2 >> 8;

    r = r > 0x1f ? 0x1f : r;
    g = g > 0x3f ? 0x3f : g;
    b = b > 0x1f ? 0x1f : b;

    A = 255 << 24;
    R = ((r << 3) | (r >> 2)) << 16;
    G = ((g << 2) | (g >> 4)) << 8;
    B = (b << 3) | (b >> 2);

    return A | R | G | B;
}

static int TCP_GetPS(lua_State* L, TCP_UserData* ud, Uint32 id)
{
    Uint8* data = ud->data + ud->splist[id];
    SP_INFO* info = (SP_INFO*)data;
    Uint32* line, * pal, * wdata, * wdata2;

    Uint8* rdata;
    Uint32 linelen, linepos, Color, h;
    Uint8 style, alpha, Repeat;
    if (!info->width || !info->height) //如果宽高为0
        return 0;
    data += sizeof(SP_INFO);
    line = (Uint32*)data; //压缩像素行
    data = ud->data + ud->splist[id];
    pal = ud->pal; //颜色板

    SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormat(SDL_SWSURFACE, info->width, info->height, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_SetSurfaceBlendMode(sf, SDL_BLENDMODE_BLEND);
    wdata = (Uint32*)sf->pixels;
    wdata2 = wdata;

    linelen = info->width; //行总长度
    linepos = 0;           //记录行长度
    style = 0;
    alpha = 0;  // Alpha
    Repeat = 0; // 重复次数
    Color = 0;  //重复颜色

    for (h = 0; h < info->height; h++)
    {
        wdata = wdata2 + linepos;
        linepos += linelen;     //写地址
        rdata = data + line[h]; //读地址
        if (!*rdata)
        { //法术隔行处理
            if (h > 0 && *(data + line[h - 1]))
                SDL_memcpy(wdata, wdata - linelen, linelen * 4);
        }
        else
        {
            while (*rdata)
            {                                 // {00000000} 表示像素行结束，如有剩余像素用透明色代替
                style = (*rdata & 0xC0) >> 6; // 取字节的前两个比特
                switch (style)
                {
                case 0: // {00******}
                    if (*rdata & 0x20)
                    {                                     // {001*****} 表示带有Alpha通道的单个像素
                        alpha = ((*rdata++) & 0x1F) << 3; // 0x1f=(11111) 获得Alpha通道的值
                        //alpha = (alpha<<3)|(alpha>>2);
                        *wdata++ = (pal[*rdata++] & 0xFFFFFF) | (alpha << 24); //合成透明
                    }
                    else
                    {                               // {000*****} 表示重复n次带有Alpha通道的像素
                        Repeat = (*rdata++) & 0x1F; // 获得重复的次数
                        alpha = (*rdata++) << 3;    // 获得Alpha通道值
                        //alpha = (alpha<<3)|(alpha>>2);
                        Color = (pal[*rdata++] & 0xFFFFFF) | (alpha << 24); //合成透明
                        while (Repeat)
                        {
                            *wdata++ = Color; //循环固定颜色
                            Repeat--;
                        }
                    }
                    break;
                case 1:                         // {01******} 表示不带Alpha通道不重复的n个像素组成的数据段
                    Repeat = (*rdata++) & 0x3F; // 获得数据组中的长度
                    while (Repeat)
                    {
                        *wdata++ = pal[*rdata++]; //循环指定颜色
                        Repeat--;
                    }
                    break;
                case 2:                         // {10******} 表示重复n次像素
                    Repeat = (*rdata++) & 0x3F; // 获得重复的次数
                    Color = pal[*rdata++];
                    while (Repeat)
                    {
                        *wdata++ = Color;
                        Repeat--;
                    }
                    break;
                case 3:                         // {11******} 表示跳过n个像素，跳过的像素用透明色代替
                    Repeat = (*rdata++) & 0x3f; // 获得重复次数
                    if (!Repeat)
                    { //非常规处理
                        //printf("%d,%d,%X,%X\n",*rdata,rdata[1],rdata[2],rdata[3]);//ud->FrameList[id]+line[h]
                        if ((wdata[-1] & 0xFFFFFF) == 0 && h > 0) //黑点
                            wdata[-1] = wdata[-(int)linelen];
                        else
                            wdata[-1] = wdata[-1] | 0xFF000000; //边缘
                        rdata += 2;
                        break;
                    }
                    wdata += Repeat; //跳过
                    break;
                }
            }
        }
    }

    SDL_Surface** sfud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
    *sfud = sf;
    luaL_setmetatable(L, "SDL_Surface");

    lua_createtable(L, 0, 4);
    lua_pushinteger(L, info->height);
    lua_setfield(L, -2, "height");
    lua_pushinteger(L, info->width);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, info->x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, info->y);
    lua_setfield(L, -2, "y");
    return 2;
}

static int TCP_GetPR(lua_State* L, TCP_UserData* ud, Uint32 id)
{
    RP_INFO* info = &ud->rpinfo[id];
    SDL_RWops* src = SDL_RWFromMem(ud->data + ud->rplist[id].offset, ud->rplist[id].len);

    SDL_Surface* sf = IMG_Load_RW(src, SDL_TRUE);
    if (sf)
    {
        SDL_Surface** sfud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        SDL_Surface* nsf = SDL_ConvertSurfaceFormat(sf, SDL_PIXELFORMAT_ARGB8888, SDL_SWSURFACE);
        SDL_FreeSurface(sf);
        *sfud = nsf;
        luaL_setmetatable(L, "SDL_Surface");

        lua_createtable(L, 0, 4);
        lua_pushnumber(L, info->height);
        lua_setfield(L, -2, "height");
        lua_pushnumber(L, info->width);
        lua_setfield(L, -2, "width");
        lua_pushnumber(L, info->x);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, info->y);
        lua_setfield(L, -2, "y");
        return 2;
    }
    return 0;
}

static int TCP_GetFrame(lua_State* L)
{
    TCP_UserData* ud = (TCP_UserData*)luaL_checkudata(L, 1, TCP_NAME);
    Uint32 i = (Uint32)luaL_checkinteger(L, 2) - 1;

    if (i >= ud->number)
        return luaL_error(L, "id error!");
    if (ud->splist && ud->splist[i])
    { //空帧
        return TCP_GetPS(L, ud, i);
    }
    else if (ud->rplist && ud->rplist[i].len)
    {
        return TCP_GetPR(L, ud, i);
    }
    return 0;
}

static int TCP_SetPP(lua_State* L)
{
    TCP_UserData* ud = (TCP_UserData*)luaL_checkudata(L, 1, TCP_NAME);
    if (ud->splist)
    {
        int s = (int)luaL_checkinteger(L, 2);
        int e = (int)luaL_checkinteger(L, 3);
        Uint32 r1 = (Uint32)luaL_checkinteger(L, 4);
        Uint32 g1 = (Uint32)luaL_checkinteger(L, 5);
        Uint32 b1 = (Uint32)luaL_checkinteger(L, 6);
        Uint32 r2 = (Uint32)luaL_checkinteger(L, 7);
        Uint32 g2 = (Uint32)luaL_checkinteger(L, 8);
        Uint32 b2 = (Uint32)luaL_checkinteger(L, 9);
        Uint32 r3 = (Uint32)luaL_checkinteger(L, 10);
        Uint32 g3 = (Uint32)luaL_checkinteger(L, 11);
        Uint32 b3 = (Uint32)luaL_checkinteger(L, 12);
        Uint8* data;
        SP_HEAD* head;
        Uint16* pal16;
        int i;
        if (s < 0 || e > 256)
            return 0;
        data = ud->data;
        head = (SP_HEAD*)data;
        data += head->len + 4;
        pal16 = (Uint16*)data;

        for (i = s; i < e; i++)
        {
            ud->pal[i] = RGB565to888_Pal(pal16[i], r1, g1, b1, r2, g2, b2, r3, g3, b3);
        }
        lua_pushboolean(L, 1);
        return 1;
    }
    return 0;
}

static int TCP_GetPal(lua_State* L)
{
    TCP_UserData* ud = (TCP_UserData*)luaL_checkudata(L, 1, TCP_NAME);
    lua_pushlstring(L, (char*)ud->pal, 1024);
    return 1;
}

static int TCP_SetPal(lua_State* L)
{
    TCP_UserData* ud = (TCP_UserData*)luaL_checkudata(L, 1, TCP_NAME);
    size_t len;
    const char* pal = luaL_checklstring(L, 2, &len);
    if (len == 1024)
        SDL_memcpy(ud->pal, pal, len);
    return 0;
}

static int TCP_NEW(lua_State* L)
{
    size_t len;
    Uint8* data = (Uint8*)luaL_checklstring(L, 1, &len);
    return TCP_Create(L, data, len);
}

static int TCP_GC(lua_State* L)
{
    TCP_UserData* ud = (TCP_UserData*)luaL_checkudata(L, 1, TCP_NAME);
    SDL_free(ud->data);
    return 0;
}

int TCP_Create(lua_State* L, Uint8* data, size_t len)
{
    SP_HEAD* head = (SP_HEAD*)data;

    if (head->flag != 'PS' && head->flag != 'PR')
        return 0;
    TCP_UserData* ud = (TCP_UserData*)lua_newuserdata(L, sizeof(TCP_UserData));
    SDL_memset(ud, 0, sizeof(TCP_UserData));
    ud->data = (Uint8*)SDL_malloc(len);
    ud->len = (int)len;
    SDL_memcpy(ud->data, data, len);
    data = ud->data;
    luaL_setmetatable(L, TCP_NAME);

    lua_createtable(L, 0, 6);
    if (head->flag == 'PS')
    {
        data += 16;
        int n = head->len - 12;
        lua_createtable(L, 0, n);

        for (int i = 0; i < n; i++)
        {
            lua_pushinteger(L, *data++);
            lua_seti(L, -2, i + 1);
        }

        lua_setfield(L, -2, "dts");

        Uint16* pal16 = (Uint16*)data;
        for (Uint32 i = 0; i < 256; i++)
        {
            ud->pal[i] = RGB565to888(pal16[i], 255);
        }
        data += 512;

        ud->splist = (Uint32*)data;
        ud->number = head->group * head->frame;
        for (Uint32 i = 0; i < ud->number; i++)
        {
            if (ud->splist[i])                    //空帧
                ud->splist[i] += (head->len + 4); //偏移
        }
        lua_pushinteger(L, head->group);
        lua_setfield(L, -2, "group");
        lua_pushinteger(L, head->frame);
        lua_setfield(L, -2, "frame");
        lua_pushinteger(L, head->width);
        lua_setfield(L, -2, "width");
        lua_pushinteger(L, head->height);
        lua_setfield(L, -2, "height");
        lua_pushinteger(L, head->x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, head->y);
        lua_setfield(L, -2, "y");
    }
    else if (head->flag == 'PR')
    {
        RP_HEAD* rphead = (RP_HEAD*)data;
        data += sizeof(RP_HEAD);
        ud->rpinfo = (RP_INFO*)data;
        data += rphead->number * sizeof(RP_INFO) + 4; //4未知
        ud->rplist = (RP_LIST*)data;                 //偏移
        ud->number = rphead->number;

        lua_pushinteger(L, rphead->group);
        lua_setfield(L, -2, "group");
        lua_pushinteger(L, rphead->frame);
        lua_setfield(L, -2, "frame");
        lua_pushinteger(L, rphead->width);
        lua_setfield(L, -2, "width");
        lua_pushinteger(L, rphead->height);
        lua_setfield(L, -2, "height");
        lua_pushinteger(L, rphead->x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, rphead->y);
        lua_setfield(L, -2, "y");
    }
    return 2;
}

LUAMOD_API int luaopen_gxy2_tcp(lua_State* L)
{
    const luaL_Reg funcs[] = {
        {"__gc", TCP_GC},
        {"__close", TCP_GC},
        {"GetFrame", TCP_GetFrame},
        {"SetPP", TCP_SetPP},
        {"GetPal", TCP_GetPal},
        {"SetPal", TCP_SetPal},

        {NULL, NULL},
    };

    luaL_newmetatable(L, TCP_NAME);
    luaL_setfuncs(L, funcs, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    lua_pushcfunction(L, TCP_NEW);
    return 1;
}