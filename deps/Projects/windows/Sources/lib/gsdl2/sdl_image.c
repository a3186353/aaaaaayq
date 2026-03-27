#include "gge.h"
#include <SDL_image.h>

static int LUA_IMG_Linked_Version(lua_State* L)
{
    const SDL_version* ver = IMG_Linked_Version();
    lua_pushfstring(L, "SDL_IMG %d.%d.%d", ver->major, ver->minor, ver->patch);
    return 1;
}

static int LUA_IMG_Init(lua_State* L)
{
    int flags = (int)luaL_optinteger(L, 1, IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP);
    lua_pushboolean(L, IMG_Init(flags) == flags);
    return 1;
}

static int LUA_IMG_Quit(lua_State* L)
{
    IMG_Quit();
    return 0;
}

static int LUA_IMG_LoadTyped_RW(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    const char* type = luaL_checkstring(L, 2);
    SDL_Surface* r = IMG_LoadTyped_RW(rw, SDL_FALSE, type);
    if (r)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = r;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_IMG_Load(lua_State* L)
{
    const char* file = luaL_checkstring(L, 1);
    Uint32 key = (Uint32)luaL_optinteger(L, 2, 0);

    SDL_Surface* sf = IMG_Load(file);
    if (sf)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_IMG_LoadARGB8888(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    Uint32 key = (Uint32)luaL_optinteger(L, 2, 0);

    SDL_Surface* sf = IMG_Load(path);
    if (sf)
    {
        if (sf->format->format != SDL_PIXELFORMAT_ARGB8888)
        {
            SDL_Surface* temp = SDL_ConvertSurfaceFormat(sf, SDL_PIXELFORMAT_ARGB8888, SDL_SWSURFACE);
            if (temp)
            {
                if (key != 0)
                    SDL_SetColorKey(temp, SDL_TRUE, key);
                SDL_FreeSurface(sf);
                sf = temp;
            }
        }
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_IMG_Load_RW(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    Uint32 key = (Uint32)luaL_optinteger(L, 2, 0);

    SDL_Surface* sf = IMG_Load_RW(rw, SDL_FALSE);
    if (sf)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_IMG_LoadARGB8888_RW(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    Uint32 key = (Uint32)luaL_optinteger(L, 2, 0);

    SDL_Surface* sf = IMG_Load_RW(rw, SDL_FALSE);
    if (sf)
    {
        if (sf->format->format != SDL_PIXELFORMAT_ARGB8888)
        {
            SDL_Surface* temp = SDL_ConvertSurfaceFormat(sf, SDL_PIXELFORMAT_ARGB8888, SDL_SWSURFACE);
            if (temp)
            {
                if (key != 0)
                    SDL_SetColorKey(temp, SDL_TRUE, key);
                SDL_FreeSurface(sf);
                sf = temp;
            }
        }
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_IMG_LoadTexture(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    const char* file = luaL_checkstring(L, 2);
    int access = (int)luaL_optinteger(L, 3, SDL_TEXTUREACCESS_STATIC);

    if (access == SDL_TEXTUREACCESS_STATIC)
    {
        SDL_Surface* sf = IMG_Load(file);
        if (sf)
        {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(rd, sf);
            if (tex)
            {
                GGE_Texture* gt = (GGE_Texture*)SDL_calloc(1, sizeof(GGE_Texture));
                SDL_Texture** ud = (SDL_Texture**)lua_newuserdata(L, sizeof(SDL_Texture*));

                *ud = tex;
                gt->refcount = 1;
                gt->sf = GGE_SurfaceAlphaToSurface(sf, 0);
                SDL_SetTextureUserData(tex, gt);
                luaL_setmetatable(L, "SDL_Texture");
                SDL_FreeSurface(sf);
                return 1;
            }
            SDL_FreeSurface(sf);
        }
    }
    else if (access == SDL_TEXTUREACCESS_STREAMING)
    {
        SDL_Surface* sf = IMG_Load(file);
        if (sf)
        {
            SDL_Texture* tex = SDL_CreateTexture(rd, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, sf->w, sf->h);

            if (tex)
            {
                void* pixels = NULL;
                int pitch;
                if (SDL_LockTexture(tex, NULL, &pixels, &pitch) == 0)
                {
                    SDL_ConvertPixels(sf->w, sf->h, sf->format->format, sf->pixels, sf->pitch,
                        SDL_PIXELFORMAT_ARGB8888, pixels, pitch);
                    SDL_UnlockTexture(tex);
                }
                GGE_Texture* gt = (GGE_Texture*)SDL_calloc(1, sizeof(GGE_Texture));
                SDL_Texture** ud = (SDL_Texture**)lua_newuserdata(L, sizeof(SDL_Texture*));

                *ud = tex;
                gt->refcount = 1;
                gt->sf = sf;
                SDL_SetTextureUserData(tex, gt);
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                luaL_setmetatable(L, "SDL_Texture");
                return 1;
            }
        }
    }

    return 0;
}

static int LUA_IMG_LoadTexture_RW(lua_State* L)
{
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 2, "SDL_RWops");
    int access = (int)luaL_optinteger(L, 3, SDL_TEXTUREACCESS_STATIC);

    SDL_Surface* sf = IMG_Load_RW(rw, SDL_FALSE);
    if (!sf)
        sf = IMG_LoadTGA_RW(rw); //IMG_Load_RW »áºöÂÔTGA
    if (!sf)
        return 0;

    if (access == SDL_TEXTUREACCESS_STATIC)
    {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(rd, sf);
        if (tex)
        {
            GGE_Texture* gt = (GGE_Texture*)SDL_calloc(1, sizeof(GGE_Texture));
            SDL_Texture** ud = (SDL_Texture**)lua_newuserdata(L, sizeof(SDL_Texture*));

            *ud = tex;
            gt->refcount = 1;
            gt->sf = GGE_SurfaceAlphaToSurface(sf, 0);
            SDL_SetTextureUserData(tex, gt);
            luaL_setmetatable(L, "SDL_Texture");
            SDL_FreeSurface(sf);
            return 1;
        }
        SDL_FreeSurface(sf);

    }
    else if (access == SDL_TEXTUREACCESS_STREAMING)
    {
        SDL_Texture* tex = SDL_CreateTexture(rd, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, sf->w, sf->h);

        if (tex)
        {
            void* pixels = NULL;
            int pitch;
            if (SDL_LockTexture(tex, NULL, &pixels, &pitch) == 0)
            {
                SDL_ConvertPixels(sf->w, sf->h, sf->format->format, sf->pixels, sf->pitch,
                    SDL_PIXELFORMAT_ARGB8888, pixels, pitch);
                SDL_UnlockTexture(tex);
            }
            GGE_Texture* gt = (GGE_Texture*)SDL_calloc(1, sizeof(GGE_Texture));
            SDL_Texture** ud = (SDL_Texture**)lua_newuserdata(L, sizeof(SDL_Texture*));

            *ud = tex;
            gt->refcount = 1;
            gt->sf = sf;
            SDL_SetTextureUserData(tex, gt);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            luaL_setmetatable(L, "SDL_Texture");
            return 1;
        }
        SDL_FreeSurface(sf);
    }

    return 0;
}

//static int  LUA_IMG_LoadTextureTyped_RW(lua_State* L)
//{
//	SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 1, "SDL_Renderer");
//	SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 2, "SDL_RWops");
//	const char* type = luaL_checkstring(L, 3);
//	SDL_Texture* tex = IMG_LoadTextureTyped_RW(rd, rw, SDL_FALSE, type);
//	if (tex) {

//		return 1;
//	}
//	return 0;
//}

static int LUA_IMG_SavePNG(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    const char* file = luaL_checkstring(L, 2);
    lua_pushboolean(L, IMG_SavePNG(sf, file));

    return 1;
}

static int LUA_IMG_SavePNG_RW(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 2, "SDL_RWops");

    lua_pushboolean(L, IMG_SavePNG_RW(sf, rw, SDL_FALSE));
    return 1;
}

static int LUA_IMG_SaveJPG(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    const char* file = luaL_checkstring(L, 2);
    int quality = (int)luaL_optinteger(L, 3, 90);
    lua_pushboolean(L, IMG_SaveJPG(sf, file, quality));
    return 1;
}

static int LUA_IMG_SaveJPG_RW(lua_State* L)
{
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 1, "SDL_Surface");
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 2, "SDL_RWops");
    int quality = (int)luaL_optinteger(L, 3, 90);
    lua_pushboolean(L, IMG_SaveJPG_RW(sf, rw, SDL_FALSE, quality));
    return 1;
}

static int LUA_IMG_LoadAnimation(lua_State* L)
{
    const char* file = luaL_checkstring(L, 1);
    IMG_Animation* ani = IMG_LoadAnimation(file);

    if (ani)
    {
        lua_createtable(L, 0, 4);
        lua_pushinteger(L, ani->w);
        lua_setfield(L, -2, "w");
        lua_pushinteger(L, ani->h);
        lua_setfield(L, -2, "h");
        lua_createtable(L, ani->count, 0); //delays
        lua_createtable(L, ani->count, 0); //frames
        for (int n = 0; n < ani->count; n++)
        {
            SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
            ani->frames[n]->refcount++;
            *ud = ani->frames[n];
            luaL_setmetatable(L, "SDL_Surface");
            lua_seti(L, -2, n + 1);
            lua_pushinteger(L, ani->delays[n]);
            lua_seti(L, -3, n + 1);
        }
        lua_setfield(L, -3, "frames");
        lua_setfield(L, -2, "delays");
        IMG_FreeAnimation(ani);
        return 1;
    }
    return 0;
}

static int LUA_IMG_LoadAnimation_RW(lua_State* L)
{
    SDL_RWops* rw = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    IMG_Animation* ani = IMG_LoadAnimation_RW(rw, SDL_FALSE);

    if (ani)
    {
        lua_createtable(L, 0, 4);
        lua_pushinteger(L, ani->w);
        lua_setfield(L, -2, "w");
        lua_pushinteger(L, ani->h);
        lua_setfield(L, -2, "h");
        lua_createtable(L, ani->count, 0); //delays
        lua_createtable(L, ani->count, 0); //frames
        for (int n = 0; n < ani->count; n++)
        {
            SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
            ani->frames[n]->refcount++;
            *ud = ani->frames[n];
            luaL_setmetatable(L, "SDL_Surface");
            lua_seti(L, -2, n + 1);
            lua_pushinteger(L, ani->delays[n]);
            lua_seti(L, -3, n + 1);
        }
        lua_setfield(L, -3, "frames");
        lua_setfield(L, -2, "delays");
        IMG_FreeAnimation(ani);
        return 1;
    }
    return 0;
}

static const luaL_Reg renderer_funcs[] = {
    {"LoadTexture", LUA_IMG_LoadTexture},
    {"LoadTexture_RW", LUA_IMG_LoadTexture_RW},
    //{"LoadTextureTyped_RW", LUA_IMG_LoadTextureTyped_RW},
    {NULL, NULL}
};

static const luaL_Reg surface_funcs[] = {
    {"SavePNG", LUA_IMG_SavePNG},
    {"SavePNG_RW", LUA_IMG_SavePNG_RW},
    {"SaveJPG", LUA_IMG_SaveJPG},
    {"SaveJPG_RW", LUA_IMG_SaveJPG_RW},
    {NULL, NULL}
};

static const luaL_Reg image_funcs[] = {
    {"Init", LUA_IMG_Init},
    {"Quit", LUA_IMG_Quit},
    {"Load", LUA_IMG_Load},
    {"LoadARGB8888", LUA_IMG_LoadARGB8888},
    {"LoadARGB8888_RW", LUA_IMG_LoadARGB8888_RW},
    {"Load_RW", LUA_IMG_Load_RW},
    {"LoadTyped_RW", LUA_IMG_LoadTyped_RW},
    {"LoadAnimation", LUA_IMG_LoadAnimation},
    {"LoadAnimation_RW", LUA_IMG_LoadAnimation_RW},
    {NULL, NULL}
};


LUALIB_API int luaopen_gsdl2_image(lua_State* L)
{
    luaL_getmetatable(L, "SDL_Renderer");
    luaL_setfuncs(L, renderer_funcs, 0);
    lua_pop(L, 1);

    luaL_getmetatable(L, "SDL_Surface");
    luaL_setfuncs(L, surface_funcs, 0);
    lua_pop(L, 1);

    luaL_newlib(L, image_funcs);
    return 1;
}
