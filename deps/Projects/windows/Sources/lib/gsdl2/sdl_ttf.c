#include "gge.h"
#include "SDL_ttf.h"
#include "SDL_FontCache.h"

static int LUA_TTF_Linked_Version(lua_State* L)
{
    const SDL_version* ver = TTF_Linked_Version();
    lua_pushfstring(L, "SDL_TTF %d.%d.%d", ver->major, ver->minor, ver->patch);
    return 1;
}
//TTF_ByteSwappedUNICODE
static int LUA_TTF_Init(lua_State* L)
{
    lua_pushboolean(L, TTF_Init() == 0);
    return 1;
}

static int LUA_TTF_OpenFont(lua_State* L)
{
    const char* file = luaL_checkstring(L, 1);
    int size = (int)luaL_optinteger(L, 2, 16);
    TTF_Font* font = TTF_OpenFont(file, size);

    if (font)
    {
        TTF_Font** ud = (TTF_Font**)lua_newuserdata(L, sizeof(TTF_Font*));
        *ud = font;
        luaL_setmetatable(L, "TTF_Font");
        return 1;
    }
    return 0;
}

static int LUA_TTF_OpenFontIndex(lua_State* L)
{
    const char* file = luaL_checkstring(L, 1);
    int size = (int)luaL_optinteger(L, 2, 16);
    long index = (int)luaL_optinteger(L, 3, 0);
    TTF_Font* font = TTF_OpenFontIndex(file, size, index);
    if (font)
    {
        TTF_Font** ud = (TTF_Font**)lua_newuserdata(L, sizeof(TTF_Font*));
        *ud = font;
        luaL_setmetatable(L, "TTF_Font");
        return 1;
    }
    return 0;
}

static int LUA_TTF_OpenFontRW(lua_State* L)
{
    SDL_RWops* ops = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    int size = (int)luaL_optinteger(L, 2, 16);
    TTF_Font* font = TTF_OpenFontRW(ops, 0, size);
    if (font)
    {
        TTF_Font** ud = (TTF_Font**)lua_newuserdata(L, sizeof(TTF_Font*));
        *ud = font;
        luaL_setmetatable(L, "TTF_Font");
        return 1;
    }
    return 0;
}

static int LUA_TTF_OpenFontIndexRW(lua_State* L)
{
    SDL_RWops* ops = *(SDL_RWops**)luaL_checkudata(L, 1, "SDL_RWops");
    int size = (int)luaL_optinteger(L, 2, 16);
    long index = (int)luaL_optinteger(L, 3, 0);
    TTF_Font* font = TTF_OpenFontIndexRW(ops, 0, size, index);
    TTF_Font** ud;
    if (font)
    {
        ud = (TTF_Font**)lua_newuserdata(L, sizeof(TTF_Font*));
        *ud = font;
        luaL_setmetatable(L, "TTF_Font");
        return 1;
    }
    return 0;
}
//TTF_OpenFontDPI
static int LUA_TTF_SetFontSize(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    int size = (int)luaL_checkinteger(L, 2);
    lua_pushboolean(L, TTF_SetFontSize(font, size) == 0);

    return 1;
}
//TTF_SetFontSizeDPI
static int LUA_TTF_GetFontStyle(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");

    lua_pushinteger(L, TTF_GetFontStyle(font));
    return 1;
}

static int LUA_TTF_SetFontStyle(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    int style = (int)luaL_checkinteger(L, 2);

    TTF_SetFontStyle(font, style);
    return 0;
}

static int LUA_TTF_GetFontOutline(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");

    lua_pushinteger(L, TTF_GetFontOutline(font));
    return 1;
}

static int LUA_TTF_SetFontOutline(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    int outline = (int)luaL_checkinteger(L, 2);

    TTF_SetFontOutline(font, outline);
    return 0;
}

static int LUA_TTF_GetFontHinting(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");

    lua_pushinteger(L, TTF_GetFontHinting(font));
    return 1;
}

static int LUA_TTF_SetFontHinting(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    int hinting = (int)luaL_checkinteger(L, 2);

    TTF_SetFontHinting(font, hinting);
    return 0;
}

static int LUA_TTF_GetFontWrappedAlign(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");

    lua_pushinteger(L, TTF_GetFontWrappedAlign(font));
    return 1;
}

static int LUA_TTF_SetFontWrappedAlign(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    int align = (int)luaL_checkinteger(L, 2);

    TTF_SetFontWrappedAlign(font, align);
    return 0;
}

static int LUA_TTF_FontHeight(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");

    lua_pushinteger(L, TTF_FontHeight(font));
    return 1;
}

static int LUA_TTF_FontAscent(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");

    lua_pushinteger(L, TTF_FontAscent(font));
    return 1;
}

static int LUA_TTF_FontDescent(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");

    lua_pushinteger(L, TTF_FontAscent(font));
    return 1;
}

static int LUA_TTF_FontLineSkip(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");

    lua_pushinteger(L, TTF_FontLineSkip(font));
    return 1;
}

static int LUA_TTF_GetFontKerning(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");

    lua_pushinteger(L, TTF_GetFontKerning(font));
    return 1;
}

static int LUA_TTF_SetFontKerning(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    int allowed = (int)luaL_checkinteger(L, 2);

    TTF_SetFontKerning(font, allowed);
    return 0;
}

static int LUA_TTF_FontFaces(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");

    lua_pushinteger(L, TTF_FontFaces(font));
    return 1;
}

static int LUA_TTF_FontFaceIsFixedWidth(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");

    lua_pushinteger(L, TTF_FontFaceIsFixedWidth(font));
    return 1;
}

static int LUA_TTF_FontFaceFamilyName(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");

    lua_pushstring(L, TTF_FontFaceFamilyName(font));
    return 1;
}

static int LUA_TTF_FontFaceStyleName(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");

    lua_pushstring(L, TTF_FontFaceStyleName(font));
    return 1;
}

static int LUA_TTF_GlyphIsProvided(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    int ch = (int)luaL_checkinteger(L, 2);
    lua_pushinteger(L, TTF_GlyphIsProvided(font, ch));
    return 1;
}
//TTF_GlyphIsProvided32
static int LUA_TTF_GlyphMetrics(lua_State* L)
{
    TTF_Font* f = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    int ch = (int)luaL_checkinteger(L, 2);
    int minx, maxx, miny, maxy, advance;

    if (TTF_GlyphMetrics(f, ch, &minx, &maxx, &miny, &maxy, &advance) == 0)
    {
        lua_pushinteger(L, minx);
        lua_pushinteger(L, maxx);
        lua_pushinteger(L, miny);
        lua_pushinteger(L, maxy);
        lua_pushinteger(L, advance);

        return 5;
    }
    return 0;
}

static int LUA_TTF_SizeUTF8(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    const char* text = luaL_checkstring(L, 2);
    int w, h;
    if (TTF_SizeUTF8(font, text, &w, &h) == 0)
    {
        lua_pushinteger(L, w);
        lua_pushinteger(L, h);
        return 2;
    }
    return 0;
}

static int LUA_TTF_MeasureUTF8(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    const char* text = luaL_checkstring(L, 2);
    int measure_width = (int)luaL_checkinteger(L, 3);
    int extent, count;
    if (TTF_MeasureUTF8(font, text, measure_width, &extent, &count) == 0)
    {
        lua_pushinteger(L, extent);
        lua_pushinteger(L, count);
        return 2;
    }
    return 0;
}


static int LUA_TTF_RenderUTF8_Solid(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    const char* text = luaL_checkstring(L, 2);
    SDL_Color fg;
    SDL_Surface* sf;

    fg.r = (Uint8)luaL_optinteger(L, 3, 255);
    fg.g = (Uint8)luaL_optinteger(L, 4, 255);
    fg.b = (Uint8)luaL_optinteger(L, 5, 255);
    fg.a = (Uint8)luaL_optinteger(L, 6, 255);
    sf = TTF_RenderUTF8_Solid(font, text, fg);
    if (sf)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_TTF_RenderUTF8_Solid_Wrapped(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    const char* text = luaL_checkstring(L, 2);
    Uint32 wrapLength = (int)luaL_checkinteger(L, 7);
    SDL_Color fg;
    SDL_Surface* sf;

    fg.r = (Uint8)luaL_optinteger(L, 3, 255);
    fg.g = (Uint8)luaL_optinteger(L, 4, 255);
    fg.b = (Uint8)luaL_optinteger(L, 5, 255);
    fg.a = (Uint8)luaL_optinteger(L, 6, 255);
    sf = TTF_RenderUTF8_Solid_Wrapped(font, text, fg, wrapLength);
    if (sf)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

//static int LUA_TTF_RenderGlyph_Solid(lua_State *L)
//{
//  TTF_Font * f = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
//  Uint16 ch = luaL_checkinteger(L,2);
//  SDL_Color fg = toSDL_Color(luaL_optinteger(L,3,0xFFFFFFFF));
//  SDL_Surface * r = TTF_RenderGlyph_Solid(f,ch,fg);
//  SDL_Surface **ud;
//
//  if (r){
//      ud = (SDL_Surface**)lua_newuserdata(L, sizeof (SDL_Surface*));
//      *ud = r;
//      luaL_setmetatable(L, "SDL_Surface");
//      return 1;
//  }
//  return 0;
//}
//TTF_RenderGlyph32_Solid
//static int LUA_TTF_RenderText_Shaded(lua_State *L)
//{
//  TTF_Font * f = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
//  const char *text = luaL_checkstring(L,2);
//  SDL_Color fg = toSDL_Color(luaL_optinteger(L,3,0xFFFFFFFF));
//  SDL_Color bg = toSDL_Color(luaL_optinteger(L,4,0xFFFFFFFF));
//  SDL_Surface * r = TTF_RenderText_Shaded(f,text,fg,bg);
//  SDL_Surface **ud;
//  if (r){
//      ud = (SDL_Surface**)lua_newuserdata(L, sizeof (SDL_Surface*));
//      *ud = r;
//      luaL_setmetatable(L, "SDL_Surface");
//      return 1;
//  }
//  return 0;
//}
static int LUA_TTF_RenderUTF8_LCD(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    const char* text = luaL_checkstring(L, 2);
    SDL_Color fg, bg;
    SDL_Surface* sf;
    fg.r = (Uint8)luaL_optinteger(L, 3, 255);
    fg.g = (Uint8)luaL_optinteger(L, 4, 255);
    fg.b = (Uint8)luaL_optinteger(L, 5, 255);
    fg.a = (Uint8)luaL_optinteger(L, 6, 255);

    bg.r = (Uint8)luaL_optinteger(L, 7, 0);
    bg.g = (Uint8)luaL_optinteger(L, 8, 0);
    bg.b = (Uint8)luaL_optinteger(L, 9, 0);
    bg.a = (Uint8)luaL_optinteger(L, 10, 0);

    sf = TTF_RenderUTF8_LCD(font, text, fg, bg);
    if (sf)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_TTF_RenderUTF8_Shaded(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    const char* text = luaL_checkstring(L, 2);
    SDL_Color fg, bg;
    SDL_Surface* sf;
    fg.r = (Uint8)luaL_optinteger(L, 3, 255);
    fg.g = (Uint8)luaL_optinteger(L, 4, 255);
    fg.b = (Uint8)luaL_optinteger(L, 5, 255);
    fg.a = (Uint8)luaL_optinteger(L, 6, 255);

    bg.r = (Uint8)luaL_optinteger(L, 7, 0);
    bg.g = (Uint8)luaL_optinteger(L, 8, 0);
    bg.b = (Uint8)luaL_optinteger(L, 9, 0);
    bg.a = (Uint8)luaL_optinteger(L, 10, 0);

    sf = TTF_RenderUTF8_Shaded(font, text, fg, bg);
    if (sf)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_TTF_RenderUTF8_Shaded_Wrapped(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    const char* text = luaL_checkstring(L, 2);
    Uint32 wrapLength = (int)luaL_checkinteger(L, 11);
    SDL_Color fg, bg;
    SDL_Surface* sf;

    fg.r = (Uint8)luaL_optinteger(L, 3, 255);
    fg.g = (Uint8)luaL_optinteger(L, 4, 255);
    fg.b = (Uint8)luaL_optinteger(L, 5, 255);
    fg.a = (Uint8)luaL_optinteger(L, 6, 255);

    bg.r = (Uint8)luaL_optinteger(L, 7, 0);
    bg.g = (Uint8)luaL_optinteger(L, 8, 0);
    bg.b = (Uint8)luaL_optinteger(L, 9, 0);
    bg.a = (Uint8)luaL_optinteger(L, 10, 0);
    sf = TTF_RenderUTF8_Shaded_Wrapped(font, text, fg, bg, wrapLength);
    if (sf)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

//static int LUA_TTF_RenderGlyph_Shaded(lua_State *L)
//{
//  TTF_Font * f = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
//  Uint16 ch = luaL_checkinteger(L,2);
//  SDL_Color fg = toSDL_Color(luaL_optinteger(L,3,0xFFFFFFFF));
//  SDL_Color bg = toSDL_Color(luaL_checkinteger(L,4));
//  SDL_Surface * r = TTF_RenderGlyph_Shaded(f,ch,fg,bg);
//  SDL_Surface **ud;
//  if (r){
//      ud = (SDL_Surface**)lua_newuserdata(L, sizeof (SDL_Surface*));
//      *ud = r;
//      luaL_setmetatable(L, "SDL_Surface");
//      return 1;
//  }
//  return 0;
//}

static int LUA_TTF_RenderUTF8_Blended(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    const char* text = luaL_checkstring(L, 2);
    SDL_Color fg;
    SDL_Surface* sf;
    fg.r = (Uint8)luaL_optinteger(L, 3, 255);
    fg.g = (Uint8)luaL_optinteger(L, 4, 255);
    fg.b = (Uint8)luaL_optinteger(L, 5, 255);
    fg.a = (Uint8)luaL_optinteger(L, 6, 255);
    sf = TTF_RenderUTF8_Blended(font, text, fg);
    if (sf)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_TTF_RenderUTF8_Blended_Wrapped(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    const char* text = luaL_checkstring(L, 2);
    Uint32 wrapLength = (int)luaL_checkinteger(L, 7);
    SDL_Color fg;
    SDL_Surface* sf;
    fg.r = (Uint8)luaL_optinteger(L, 3, 255);
    fg.g = (Uint8)luaL_optinteger(L, 4, 255);
    fg.b = (Uint8)luaL_optinteger(L, 5, 255);
    fg.a = (Uint8)luaL_optinteger(L, 6, 255);
    sf = TTF_RenderUTF8_Blended_Wrapped(font, text, fg, wrapLength);
    if (sf)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

//static int LUA_TTF_RenderGlyph_Blended(lua_State *L)
//{
//  TTF_Font * f = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
//  Uint16 ch = luaL_checkinteger(L,2);
//  SDL_Color fg = toSDL_Color(luaL_optinteger(L,3,0xFFFFFFFF));
//
//  SDL_Surface * r = TTF_RenderGlyph_Blended(f,ch,fg);
//  SDL_Surface **ud;
//  if (r){
//      ud = (SDL_Surface**)lua_newuserdata(L, sizeof (SDL_Surface*));
//      *ud = r;
//      luaL_setmetatable(L, "SDL_Surface");
//      return 1;
//  }
//  return 0;
//}
static int LUA_TTF_CloseFont(lua_State* L)
{
    TTF_Font** font = (TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    if (*font)
    {
        TTF_CloseFont(*font);
        *font = NULL;
    }
    return 0;
}

static int LUA_TTF_Quit(lua_State* L)
{
    TTF_Quit();
    return 0;
}

static int LUA_TTF_WasInit(lua_State* L)
{
    TTF_WasInit();
    return 0;
}

//static int LUA_TTF_GetFontKerningSize(lua_State *L)
//{
//  TTF_Font * f = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
//  int prev_index = luaL_checkinteger(L,2);
//  int index = luaL_checkinteger(L,3);
//  lua_pushinteger(L,TTF_GetFontKerningSize(f,prev_index,index));
//  return 1;
//}
//
//static int LUA_TTF_GetFontKerningSizeGlyphs(lua_State *L)
//{
//  TTF_Font * f = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
//  int previous_ch = luaL_checkinteger(L,2);
//  int ch = luaL_checkinteger(L,3);
//  lua_pushinteger(L,TTF_GetFontKerningSizeGlyphs(f,previous_ch,ch));
//  return 1;
//}
//TTF_GetFontKerningSizeGlyphs32

static int LUA_FC_CreateFont(lua_State* L)
{
    TTF_Font* font = *(TTF_Font**)luaL_checkudata(L, 1, "TTF_Font");
    SDL_Renderer* rd = *(SDL_Renderer**)luaL_checkudata(L, 2, "SDL_Renderer");
    FC_Font* fc = FC_CreateFont();
    SDL_Color color = { 255, 255, 255, 255 };

    if (FC_LoadFontFromTTF(fc, rd, font, color))
    {
        FC_Font** ud = (FC_Font**)lua_newuserdata(L, sizeof(FC_Font*));
        *ud = fc;
        luaL_setmetatable(L, "FC_Font");
        return 1;
    }
    return 0;
}

static int LUA_FC_ClearFont(lua_State* L)
{
    FC_Font* fc = *(FC_Font**)luaL_checkudata(L, 1, "FC_Font");
    FC_ClearFont(fc);
    return 0;
}

static int LUA_FC_FreeFont(lua_State* L)
{
    FC_Font** fc = (FC_Font**)luaL_checkudata(L, 1, "FC_Font");
    if (*fc)
    {
        FC_FreeFont(*fc);
        *fc = NULL;
    }
    return 0;
}

static int LUA_FC_Draw(lua_State* L)
{
    FC_Font* fc = *(FC_Font**)luaL_checkudata(L, 1, "FC_Font");
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    const char* text = luaL_checkstring(L, 4);
    FC_Draw(fc, FC_GetRenderer(fc), x, y, text);
    return 0;
}

// FC_DrawColor(fc, x, y, r, g, b, a, text)
static int LUA_FC_DrawColor(lua_State* L)
{
    FC_Font* fc = *(FC_Font**)luaL_checkudata(L, 1, "FC_Font");
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    SDL_Color color;
    color.r = (Uint8)luaL_checkinteger(L, 4);
    color.g = (Uint8)luaL_checkinteger(L, 5);
    color.b = (Uint8)luaL_checkinteger(L, 6);
    color.a = (Uint8)luaL_checkinteger(L, 7);
    const char* text = luaL_checkstring(L, 8);
    FC_DrawColor(fc, FC_GetRenderer(fc), x, y, color, text);
    return 0;
}

// FC_DrawScale(fc, x, y, sx, sy, text)
static int LUA_FC_DrawScale(lua_State* L)
{
    FC_Font* fc = *(FC_Font**)luaL_checkudata(L, 1, "FC_Font");
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    FC_Scale scale;
    scale.x = (float)luaL_checknumber(L, 4);
    scale.y = (float)luaL_checknumber(L, 5);
    const char* text = luaL_checkstring(L, 6);
    FC_DrawScale(fc, FC_GetRenderer(fc), x, y, scale, text);
    return 0;
}

// FC_DrawEffect(fc, x, y, align, sx, sy, r, g, b, a, text)
static int LUA_FC_DrawEffect(lua_State* L)
{
    FC_Font* fc = *(FC_Font**)luaL_checkudata(L, 1, "FC_Font");
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    FC_Effect effect;
    effect.alignment = (FC_AlignEnum)luaL_checkinteger(L, 4);
    effect.scale.x = (float)luaL_checknumber(L, 5);
    effect.scale.y = (float)luaL_checknumber(L, 6);
    effect.color.r = (Uint8)luaL_checkinteger(L, 7);
    effect.color.g = (Uint8)luaL_checkinteger(L, 8);
    effect.color.b = (Uint8)luaL_checkinteger(L, 9);
    effect.color.a = (Uint8)luaL_checkinteger(L, 10);
    const char* text = luaL_checkstring(L, 11);
    FC_DrawEffect(fc, FC_GetRenderer(fc), x, y, effect, text);
    return 0;
}

// FC_DrawColumn(fc, x, y, width, text)
static int LUA_FC_DrawColumn(lua_State* L)
{
    FC_Font* fc = *(FC_Font**)luaL_checkudata(L, 1, "FC_Font");
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    Uint16 width = (Uint16)luaL_checkinteger(L, 4);
    const char* text = luaL_checkstring(L, 5);
    FC_DrawColumn(fc, FC_GetRenderer(fc), x, y, width, text);
    return 0;
}

// FC_DrawColumnColor(fc, x, y, width, r, g, b, a, text)
static int LUA_FC_DrawColumnColor(lua_State* L)
{
    FC_Font* fc = *(FC_Font**)luaL_checkudata(L, 1, "FC_Font");
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    Uint16 width = (Uint16)luaL_checkinteger(L, 4);
    SDL_Color color;
    color.r = (Uint8)luaL_checkinteger(L, 5);
    color.g = (Uint8)luaL_checkinteger(L, 6);
    color.b = (Uint8)luaL_checkinteger(L, 7);
    color.a = (Uint8)luaL_checkinteger(L, 8);
    const char* text = luaL_checkstring(L, 9);
    FC_DrawColumnColor(fc, FC_GetRenderer(fc), x, y, width, color, text);
    return 0;
}

// FC_SetDefaultColor(fc, r, g, b, a)
static int LUA_FC_SetDefaultColor(lua_State* L)
{
    FC_Font* fc = *(FC_Font**)luaL_checkudata(L, 1, "FC_Font");
    SDL_Color color;
    color.r = (Uint8)luaL_checkinteger(L, 2);
    color.g = (Uint8)luaL_checkinteger(L, 3);
    color.b = (Uint8)luaL_checkinteger(L, 4);
    color.a = (Uint8)luaL_checkinteger(L, 5);
    FC_SetDefaultColor(fc, color);
    return 0;
}

// FC_GetDefaultColor(fc) -> r, g, b, a
static int LUA_FC_GetDefaultColor(lua_State* L)
{
    FC_Font* fc = *(FC_Font**)luaL_checkudata(L, 1, "FC_Font");
    SDL_Color color = FC_GetDefaultColor(fc);
    lua_pushinteger(L, color.r);
    lua_pushinteger(L, color.g);
    lua_pushinteger(L, color.b);
    lua_pushinteger(L, color.a);
    return 4;
}

// FC_GetWidth(fc, text) -> w
static int LUA_FC_GetWidth(lua_State* L)
{
    FC_Font* fc = *(FC_Font**)luaL_checkudata(L, 1, "FC_Font");
    const char* text = luaL_checkstring(L, 2);
    lua_pushinteger(L, FC_GetWidth(fc, text));
    return 1;
}

// FC_GetHeight(fc, text) -> h
static int LUA_FC_GetHeight(lua_State* L)
{
    FC_Font* fc = *(FC_Font**)luaL_checkudata(L, 1, "FC_Font");
    const char* text = luaL_checkstring(L, 2);
    lua_pushinteger(L, FC_GetHeight(fc, text));
    return 1;
}

// FC_GetLineHeight(fc) -> h
static int LUA_FC_GetLineHeight(lua_State* L)
{
    FC_Font* fc = *(FC_Font**)luaL_checkudata(L, 1, "FC_Font");
    lua_pushinteger(L, FC_GetLineHeight(fc));
    return 1;
}

// FC_GetAscent(fc, text) -> ascent
static int LUA_FC_GetAscent(lua_State* L)
{
    FC_Font* fc = *(FC_Font**)luaL_checkudata(L, 1, "FC_Font");
    const char* text = luaL_checkstring(L, 2);
    lua_pushinteger(L, FC_GetAscent(fc, text));
    return 1;
}

// FC_SetSpacing(fc, spacing)
static int LUA_FC_SetSpacing(lua_State* L)
{
    FC_Font* fc = *(FC_Font**)luaL_checkudata(L, 1, "FC_Font");
    int spacing = (int)luaL_checkinteger(L, 2);
    FC_SetSpacing(fc, spacing);
    return 0;
}

// FC_SetLineSpacing(fc, spacing)
static int LUA_FC_SetLineSpacing(lua_State* L)
{
    FC_Font* fc = *(FC_Font**)luaL_checkudata(L, 1, "FC_Font");
    int spacing = (int)luaL_checkinteger(L, 2);
    FC_SetLineSpacing(fc, spacing);
    return 0;
}

// FC_SetFilterMode(fc, mode)  -- 0=NEAREST, 1=LINEAR
static int LUA_FC_SetFilterMode(lua_State* L)
{
    FC_Font* fc = *(FC_Font**)luaL_checkudata(L, 1, "FC_Font");
    int mode = (int)luaL_checkinteger(L, 2);
    FC_SetFilterMode(fc, (FC_FilterEnum)mode);
    return 0;
}

static const luaL_Reg font_funcs[] = {
    {"__index", NULL},
    {"__gc", LUA_TTF_CloseFont},
    {"SetFontSize", LUA_TTF_SetFontSize},
    //{"SetFontSizeDPI", LUA_TTF_SetFontSizeDPI},

    {"GetFontStyle", LUA_TTF_GetFontStyle},
    {"SetFontStyle", LUA_TTF_SetFontStyle},

    {"GetFontOutline", LUA_TTF_GetFontOutline},
    {"SetFontOutline", LUA_TTF_SetFontOutline},

    {"GetFontHinting", LUA_TTF_GetFontHinting},
    {"SetFontHinting", LUA_TTF_SetFontHinting},

    {"FontHeight", LUA_TTF_FontHeight},
    {"FontAscent", LUA_TTF_FontAscent},
    {"FontDescent", LUA_TTF_FontDescent},
    {"FontLineSkip", LUA_TTF_FontLineSkip},

    {"GetFontKerning", LUA_TTF_GetFontKerning},
    {"SetFontKerning", LUA_TTF_SetFontKerning},

    {"FontFaces", LUA_TTF_FontFaces},

    {"FontFaceIsFixedWidth", LUA_TTF_FontFaceIsFixedWidth},
    {"FontFaceFamilyName", LUA_TTF_FontFaceFamilyName},
    {"FontFaceStyleName", LUA_TTF_FontFaceStyleName},

    {"GlyphIsProvided", LUA_TTF_GlyphIsProvided},
    //{"GlyphIsProvided32", LUA_TTF_GlyphIsProvided32},

    {"GlyphMetrics", LUA_TTF_GlyphMetrics},
    //{"GlyphMetrics32", LUA_TTF_GlyphMetrics32},

    {"SizeUTF8", LUA_TTF_SizeUTF8},
    {"MeasureUTF8", LUA_TTF_MeasureUTF8},

    {"RenderUTF8_Solid", LUA_TTF_RenderUTF8_Solid},
    {"RenderUTF8_Solid_Wrapped", LUA_TTF_RenderUTF8_Solid_Wrapped},
    //{"RenderGlyph_Solid", LUA_TTF_RenderGlyph_Solid},
    //{"RenderGlyph32_Solid", LUA_TTF_RenderGlyph32_Solid},

    {"GetFontWrappedAlign", LUA_TTF_GetFontWrappedAlign},
    {"SetFontWrappedAlign", LUA_TTF_SetFontWrappedAlign},
    {"RenderUTF8_LCD", LUA_TTF_RenderUTF8_LCD},
    {"RenderUTF8_Shaded", LUA_TTF_RenderUTF8_Shaded},
    {"RenderUTF8_Shaded_Wrapped", LUA_TTF_RenderUTF8_Shaded_Wrapped},

    //{"RenderGlyph_Shaded", LUA_TTF_RenderGlyph_Shaded},
    //{"RenderGlyph32_Shaded", LUA_TTF_RenderGlyph32_Shaded},

    {"RenderUTF8_Blended", LUA_TTF_RenderUTF8_Blended},
    {"RenderUTF8_Blended_Wrapped", LUA_TTF_RenderUTF8_Blended_Wrapped},

    //{"RenderGlyph_Blended", LUA_TTF_RenderGlyph_Blended},
    //{"RenderGlyph32_Blended", LUA_TTF_RenderGlyph32_Blended},

    //{"GetFontKerningSize", LUA_TTF_GetFontKerningSize},
    //{"GetFontKerningSizeGlyphs", LUA_TTF_GetFontKerningSizeGlyphs},
    //{"GetFontKerningSizeGlyphs32",LUA_TTF_GetFontKerningSizeGlyphs32},
    {"CreateFontCache", LUA_FC_CreateFont},

    {NULL, NULL} };

static const luaL_Reg ttf_funcs[] = {
    {"Init", LUA_TTF_Init},
    {"OpenFont", LUA_TTF_OpenFont},
    {"OpenFontIndex", LUA_TTF_OpenFontIndex},
    {"OpenFontRW", LUA_TTF_OpenFontRW},
    {"OpenFontIndexRW", LUA_TTF_OpenFontIndexRW},

    //{"OpenFontDPI", LUA_TTF_OpenFontDPI},

    {"Quit", LUA_TTF_Quit},
    {"WasInit", LUA_TTF_WasInit},
    //Harfbuzz
    //{"SetDirection", LUA_TTF_SetDirection},
    //{"SetScript", LUA_TTF_SetScript},
    {NULL, NULL} };

static const luaL_Reg fc_funcs[] = {
    {"__index", NULL},
    {"__gc", LUA_FC_FreeFont},
    {"Draw", LUA_FC_Draw},
    {"DrawColor", LUA_FC_DrawColor},
    {"DrawScale", LUA_FC_DrawScale},
    {"DrawEffect", LUA_FC_DrawEffect},
    {"DrawColumn", LUA_FC_DrawColumn},
    {"DrawColumnColor", LUA_FC_DrawColumnColor},
    {"SetDefaultColor", LUA_FC_SetDefaultColor},
    {"GetDefaultColor", LUA_FC_GetDefaultColor},
    {"GetWidth", LUA_FC_GetWidth},
    {"GetHeight", LUA_FC_GetHeight},
    {"GetLineHeight", LUA_FC_GetLineHeight},
    {"GetAscent", LUA_FC_GetAscent},
    {"SetSpacing", LUA_FC_SetSpacing},
    {"SetLineSpacing", LUA_FC_SetLineSpacing},
    {"SetFilterMode", LUA_FC_SetFilterMode},
    {NULL, NULL} };

LUALIB_API int luaopen_gsdl2_ttf(lua_State* L)
{
    luaL_newmetatable(L, "TTF_Font");
    luaL_setfuncs(L, font_funcs, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    luaL_newmetatable(L, "FC_Font");
    luaL_setfuncs(L, fc_funcs, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    luaL_newlib(L, ttf_funcs);
    return 1;
}