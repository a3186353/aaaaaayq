#include "gge.h"
#include "SDL_blendmode.h"

static int LUA_ComposeCustomBlendMode(lua_State *L)
{
    SDL_BlendFactor srcColorFactor = (SDL_BlendFactor)luaL_checkinteger(L, 1);
    SDL_BlendFactor dstColorFactor = (SDL_BlendFactor)luaL_checkinteger(L, 2);
    SDL_BlendOperation colorOperation = (SDL_BlendOperation)luaL_checkinteger(L, 3);
    SDL_BlendFactor srcAlphaFactor = (SDL_BlendFactor)luaL_checkinteger(L, 4);
    SDL_BlendFactor dstAlphaFactor = (SDL_BlendFactor)luaL_checkinteger(L, 5);
    SDL_BlendOperation alphaOperation = (SDL_BlendOperation)luaL_checkinteger(L, 6);
    SDL_BlendMode r = SDL_ComposeCustomBlendMode(
        srcColorFactor, dstColorFactor,
        colorOperation, srcAlphaFactor,
        dstAlphaFactor, alphaOperation);
    lua_pushinteger(L, r);
    return 1;
}

static const luaL_Reg sdl_funcs[] = {
    {"ComposeCustomBlendMode", LUA_ComposeCustomBlendMode},
    {NULL, NULL}};

int bind_blendmode(lua_State *L)
{
    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}