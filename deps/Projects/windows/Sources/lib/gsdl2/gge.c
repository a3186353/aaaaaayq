#include "gge.h"
#include <SDL_image.h>
#include <SDL_syswm.h>

//把图像的alpha提取到8位索引
SDL_Surface* GGE_SurfaceAlphaToSurface(SDL_Surface* sf, int addpal)
{
    SDL_Surface* nsf = SDL_CreateRGBSurfaceWithFormat(0, sf->w, sf->h, 8, SDL_PIXELFORMAT_INDEX8);
    SDL_BlendMode blendMode;
    SDL_GetSurfaceBlendMode(sf, &blendMode);

    if (blendMode & SDL_BLENDMODE_BLEND)
    {
        Uint8 r, g, b, a;
        Uint8* rp = (Uint8*)sf->pixels;
        Uint8* wp = (Uint8*)nsf->pixels;
        int bpp = sf->format->BytesPerPixel;

        for (int h = 0; h < sf->h; h++)
        {
            Uint8* lrp = rp;
            Uint8* lwp = wp;
            for (int w = 0; w < sf->w; w++)
            {
                switch (bpp)
                {
                case 2:
                {
                    SDL_GetRGBA(*(Uint16*)lrp, sf->format, &r, &g, &b, &a);
                    *lwp = a; //FIXME 未还原
                    break;
                }
                case 4:
                {
                    SDL_GetRGBA(*(Uint32*)lrp, sf->format, &r, &g, &b, &a);
                    *lwp = a;
                    break;
                }
                }
                lrp += bpp;
                lwp++;
            }
            rp += sf->pitch;
            wp += nsf->pitch;
        }
        if (addpal)
        {
            SDL_Palette* pal = SDL_AllocPalette(256);
            for (int n = 0; n < pal->ncolors; n++)
            {
                pal->colors[n].r = n;
                pal->colors[n].g = n;
                pal->colors[n].b = n;
                pal->colors[n].a = 255;
            }
            SDL_SetSurfacePalette(nsf, pal);
            SDL_FreePalette(pal);
        }
    }
    return nsf;
}
//Texture取透明
int GGE_GetTextureAlpha(lua_State* L)
{
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 1, "SDL_Texture");
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    GGE_Texture* ud = (GGE_Texture*)SDL_GetTextureUserData(tex);
    if (ud && ud->sf)
    {
        SDL_Surface* sf = ud->sf;
        if (x >= 0 && y >= 0 && x < sf->w && y < sf->h)
        {
            if (sf->format->format == SDL_PIXELFORMAT_INDEX8)
            {
                Uint8* pixels = (Uint8*)sf->pixels;
                lua_pushinteger(L, pixels[y * sf->pitch + x]);
                return 1;
            }
            Uint8 r, g, b, a;
            int bpp = sf->format->BytesPerPixel;
            Uint8* pixels = ((Uint8*)sf->pixels) + y * sf->pitch + x * bpp;

            switch (bpp)
            {
            case 2:
            {
                SDL_GetRGBA(*(Uint16*)pixels, sf->format, &r, &g, &b, &a);
                lua_pushinteger(L, a);
                return 1;
            }
            case 4:
            {
                SDL_GetRGBA(*(Uint32*)pixels, sf->format, &r, &g, &b, &a);
                lua_pushinteger(L, a);
                return 1;
            }
            }
        }
    }
    lua_pushinteger(L, 0);
    return 1;
}
//Texture取像素
int GGE_GetTexturePixels(lua_State* L)
{
    SDL_Texture* tex = *(SDL_Texture**)luaL_checkudata(L, 1, "SDL_Texture");
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    GGE_Texture* ud = (GGE_Texture*)SDL_GetTextureUserData(tex);
    if (ud && ud->sf)
    {
        SDL_Surface* sf = ud->sf;
        if (x >= 0 && y >= 0 && x < sf->w && y < sf->h)
        {
            Uint8 r, g, b, a;
            int bpp = sf->format->BytesPerPixel;
            Uint8* pixels = ((Uint8*)sf->pixels) + y * sf->pitch + x * bpp;

            switch (bpp)
            {
            case 2:
            {
                SDL_GetRGBA(*(Uint16*)pixels, sf->format, &r, &g, &b, &a);
                lua_pushinteger(L, a);
                lua_pushinteger(L, r);
                lua_pushinteger(L, g);
                lua_pushinteger(L, b);
                return 4;
            }
            case 4:
            {
                SDL_GetRGBA(*(Uint32*)pixels, sf->format, &r, &g, &b, &a);
                lua_pushinteger(L, a);
                lua_pushinteger(L, r);
                lua_pushinteger(L, g);
                lua_pushinteger(L, b);
                return 4;
            }
            }
        }
    }
    lua_pushinteger(L, 0);
    lua_pushinteger(L, 0);
    lua_pushinteger(L, 0);
    lua_pushinteger(L, 0);
    return 4;
}
//Surface灰度
int GGE_SurfaceToGrayscale(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    Uint8 r, g, b, a, gray;

    Uint8* pixels = (Uint8*)sf->pixels;
    switch (sf->format->BitsPerPixel)
    {
    case 16:
    {
        for (int h = 0; h < sf->h; h++)
        {
            Uint16* pixels16 = (Uint16*)pixels;
            for (int w = 0; w < sf->w; w++)
            {
                SDL_GetRGBA(*pixels16, sf->format, &r, &g, &b, &a);
                gray = (r + (g << 1) + b) >> 2;
                *pixels16++ = SDL_MapRGBA(sf->format, gray, gray, gray, a);
            }
            pixels += sf->pitch;
        }
        break;
    }
    case 32:
    {
        for (int h = 0; h < sf->h; h++)
        {
            Uint32* pixels32 = (Uint32*)pixels;
            for (int w = 0; w < sf->w; w++)
            {
                SDL_GetRGBA(*pixels32, sf->format, &r, &g, &b, &a);
                gray = (r + (g << 1) + b) >> 2;
                *pixels32++ = SDL_MapRGBA(sf->format, gray, gray, gray, a);
            }
            pixels += sf->pitch;
        }
        break;
    }
    }

    return 0;
}

int GGE_GetSurfacePixel(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);

    if (x >= 0 && y >= 0 && x < sf->w && y < sf->h)
    {
        Uint8 r, g, b, a;
        int bpp = sf->format->BytesPerPixel;
        Uint8* p = (Uint8*)sf->pixels + y * sf->pitch + x * bpp;

        switch (bpp)
        {
        case 1:
        {
            SDL_Color* color = &sf->format->palette->colors[*p];
            lua_pushinteger(L, color->r);
            lua_pushinteger(L, color->g);
            lua_pushinteger(L, color->b);
            lua_pushinteger(L, color->a);
            return 4;
        }
        case 2:
        {
            SDL_GetRGBA(*(Uint16*)p, sf->format, &r, &g, &b, &a);

            lua_pushinteger(L, r);
            lua_pushinteger(L, g);
            lua_pushinteger(L, b);
            lua_pushinteger(L, a);
            return 4;
        }
        case 4:
        {
            SDL_GetRGBA(*(Uint32*)p, sf->format, &r, &g, &b, &a);

            lua_pushinteger(L, r);
            lua_pushinteger(L, g);
            lua_pushinteger(L, b);
            lua_pushinteger(L, a);
            return 4;
        }
        }
    }
    lua_pushinteger(L, 0);
    lua_pushinteger(L, 0);
    lua_pushinteger(L, 0);
    lua_pushinteger(L, 0);
    return 4;
}

int GGE_SetSurfacePixel(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    int r = (int)luaL_optinteger(L, 4, 0);
    int g = (int)luaL_optinteger(L, 5, 0);
    int b = (int)luaL_optinteger(L, 6, 0);
    int a = (int)luaL_optinteger(L, 7, 0);
    if (x >= 0 && y >= 0 && x < sf->w && y < sf->h)
    {
        int bpp = sf->format->BytesPerPixel;
        Uint8* p = (Uint8*)sf->pixels + y * sf->pitch + x * bpp;
        switch (bpp)
        {
        case 2:
            *(Uint16*)p = SDL_MapRGBA(sf->format, r, g, b, a);
            break;
        case 4:
            *(Uint32*)p = SDL_MapRGBA(sf->format, r, g, b, a);
            break;
        }
    }
    return 0;
}

//聊天窗口
#ifdef _WIN32
#include <Windows.h>
int GGE_SetParent(lua_State* L)
{
    SDL_Window* Child = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    SDL_Window* Parent = *(SDL_Window**)luaL_checkudata(L, 2, "SDL_Window");
    SDL_SysWMinfo cinfo;
    SDL_SysWMinfo pinfo;
    LONG style, r;

    SDL_VERSION(&cinfo.version);
    SDL_VERSION(&pinfo.version);
    SDL_GetWindowWMInfo(Child, &cinfo);
    SDL_GetWindowWMInfo(Parent, &pinfo);

    r = SetWindowLongPtr(cinfo.info.win.window, GWLP_HWNDPARENT, (LONG_PTR)pinfo.info.win.window); //64位

    style = GetWindowLong(cinfo.info.win.window, GWL_STYLE);
    style &= ~(WS_MAXIMIZEBOX); //最大化
    style &= ~(WS_MINIMIZEBOX); //最小化
    style &= ~(WS_SYSMENU);     //菜单
    SetWindowLong(cinfo.info.win.window, GWL_STYLE, style);

    return 0;
}

static const luaL_Reg window_funcs[] = {
    {"SetParent", GGE_SetParent},
    {NULL, NULL}
};
#endif

static const luaL_Reg texture_funcs[] = {
    {"GetTextureAlpha", GGE_GetTextureAlpha},
    {"GetTexturePixels", GGE_GetTexturePixels},

    {NULL, NULL}
};

static const luaL_Reg surface_funcs[] = {
    {"SurfaceToGrayscale", GGE_SurfaceToGrayscale},
    {"GetSurfacePixel", GGE_GetSurfacePixel},
    {"SetSurfacePixel", GGE_SetSurfacePixel},
    {NULL, NULL}
};

static const luaL_Reg sdl_funcs[] = {

    {NULL, NULL}
};

int bind_gge(lua_State* L)
{
#ifdef _WIN32
    luaL_getmetatable(L, "SDL_Window");
    luaL_setfuncs(L, window_funcs, 0);
    lua_pop(L, 1);
#endif
    luaL_getmetatable(L, "SDL_Surface");
    luaL_setfuncs(L, surface_funcs, 0);
    lua_pop(L, 1);

    luaL_getmetatable(L, "SDL_Texture");
    luaL_setfuncs(L, texture_funcs, 0);
    lua_pop(L, 1);

    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}