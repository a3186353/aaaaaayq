#include "gge.h"
#include "SDL_video.h"

static int LUA_GetNumVideoDrivers(lua_State* L)
{
    lua_pushinteger(L, SDL_GetNumVideoDrivers());
    return 1;
}

static int LUA_GetVideoDriver(lua_State* L)
{
    int index = (int)luaL_optinteger(L, 1, 0);
    lua_pushstring(L, SDL_GetVideoDriver(index));
    return 1;
}

static int LUA_VideoInit(lua_State* L)
{
    const char* driver_name = luaL_optstring(L, 1, NULL);
    lua_pushboolean(L, SDL_VideoInit(driver_name) == 0);
    return 1;
}

static int LUA_VideoQuit(lua_State* L)
{
    SDL_VideoQuit();
    return 0;
}

static int LUA_GetCurrentVideoDriver(lua_State* L)
{
    lua_pushstring(L, SDL_GetCurrentVideoDriver());
    return 1;
}

static int LUA_GetNumVideoDisplays(lua_State* L)
{
    lua_pushinteger(L, SDL_GetNumVideoDisplays());
    return 1;
}

static int LUA_GetDisplayName(lua_State* L)
{
    int index = (int)luaL_optinteger(L, 1, 0);
    lua_pushstring(L, SDL_GetDisplayName(index));
    return 1;
}

static int LUA_GetDisplayBounds(lua_State* L)
{
    int index = (int)luaL_optinteger(L, 1, 0);

    SDL_Rect rect;
    if (SDL_GetDisplayBounds(index, &rect) == 0)
    {
        lua_pushinteger(L, rect.x);
        lua_pushinteger(L, rect.y);
        lua_pushinteger(L, rect.w);
        lua_pushinteger(L, rect.h);
        return 4;
    }
    return 0;
}

static int LUA_GetDisplayUsableBounds(lua_State* L)
{
    int index = (int)luaL_optinteger(L, 1, 0);
    SDL_Rect rect;
    if (SDL_GetDisplayUsableBounds(index, &rect) == 0)
    {
        lua_pushinteger(L, rect.x);
        lua_pushinteger(L, rect.y);
        lua_pushinteger(L, rect.w);
        lua_pushinteger(L, rect.h);
        return 4;
    }
    return 0;
}

static int LUA_GetDisplayDPI(lua_State* L)
{
    int index = (int)luaL_optinteger(L, 1, 0);
    float ddpi, hdpi, vdpi;

    if (SDL_GetDisplayDPI(index, &ddpi, &hdpi, &vdpi) == 0)
    {
        lua_pushnumber(L, ddpi);
        lua_pushnumber(L, hdpi);
        lua_pushnumber(L, vdpi);
        return 3;
    }
    return 0;
}

static int LUA_GetDisplayOrientation(lua_State* L)
{
    int index = (int)luaL_optinteger(L, 1, 0);
    lua_pushinteger(L, SDL_GetDisplayOrientation(index));
    return 1;
}

static int LUA_GetNumDisplayModes(lua_State* L)
{
    int index = (int)luaL_optinteger(L, 1, 0);
    lua_pushinteger(L, SDL_GetNumDisplayModes(index));
    return 1;
}

static int LUA_GetDisplayMode(lua_State* L)
{
    int display_index = (int)luaL_checkinteger(L, 1);
    int mode_index = (int)luaL_checkinteger(L, 2);
    SDL_DisplayMode mode;
    if (SDL_GetDisplayMode(display_index, mode_index, &mode) == 0)
    {
        lua_createtable(L, 0, 5);
        lua_pushinteger(L, mode.format);
        lua_setfield(L, -2, "format");
        lua_pushinteger(L, mode.h);
        lua_setfield(L, -2, "h");
        lua_pushinteger(L, mode.w);
        lua_setfield(L, -2, "w");
        lua_pushinteger(L, mode.refresh_rate);
        lua_setfield(L, -2, "refresh_rate");
        lua_pushlightuserdata(L, mode.driverdata);
        lua_setfield(L, -2, "driverdata");
        return 1;
    }
    return 0;
}

static int LUA_GetDesktopDisplayMode(lua_State* L)
{
    int index = (int)luaL_optinteger(L, 1, 0);
    SDL_DisplayMode mode;

    if (SDL_GetDesktopDisplayMode(index, &mode) == 0)
    {
        lua_createtable(L, 0, 5);
        lua_pushinteger(L, mode.format);
        lua_setfield(L, -2, "format");
        lua_pushinteger(L, mode.h);
        lua_setfield(L, -2, "h");
        lua_pushinteger(L, mode.w);
        lua_setfield(L, -2, "w");
        lua_pushinteger(L, mode.refresh_rate);
        lua_setfield(L, -2, "refresh_rate");
        lua_pushlightuserdata(L, mode.driverdata);
        lua_setfield(L, -2, "driverdata");
        return 1;
    }
    return 0;
}

static int LUA_GetCurrentDisplayMode(lua_State* L)
{
    int index = (int)luaL_optinteger(L, 1, 0);
    SDL_DisplayMode mode;

    if (SDL_GetCurrentDisplayMode(index, &mode) == 0)
    {
        lua_createtable(L, 0, 5);
        lua_pushinteger(L, mode.format);
        lua_setfield(L, -2, "format");
        lua_pushinteger(L, mode.h);
        lua_setfield(L, -2, "h");
        lua_pushinteger(L, mode.w);
        lua_setfield(L, -2, "w");
        lua_pushinteger(L, mode.refresh_rate);
        lua_setfield(L, -2, "refresh_rate");
        lua_pushlightuserdata(L, mode.driverdata);
        lua_setfield(L, -2, "driverdata");
        return 1;
    }
    return 0;
}

static int LUA_GetClosestDisplayMode(lua_State* L)
{
    SDL_DisplayMode mode, closest;
    int displayIndex = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_getfield(L, 2, "h");
    mode.h = (int)luaL_checkinteger(L, -1);
    lua_getfield(L, 2, "w");
    mode.w = (int)luaL_checkinteger(L, -1);
    lua_getfield(L, 2, "format");
    mode.format = (int)luaL_checkinteger(L, -1);
    lua_getfield(L, 2, "refresh_rate");
    mode.refresh_rate = (int)luaL_checkinteger(L, -1);
    lua_pop(L, 4);

    if (SDL_GetClosestDisplayMode(displayIndex, &mode, &closest))
    {
        lua_createtable(L, 0, 5);
        lua_pushinteger(L, closest.format);
        lua_setfield(L, -2, "format");
        lua_pushinteger(L, closest.h);
        lua_setfield(L, -2, "h");
        lua_pushinteger(L, closest.w);
        lua_setfield(L, -2, "w");
        lua_pushinteger(L, closest.refresh_rate);
        lua_setfield(L, -2, "refresh_rate");
        lua_pushlightuserdata(L, closest.driverdata);
        lua_setfield(L, -2, "driverdata");
        return 1;
    }
    return 0;
}

static int LUA_GetWindowDisplayIndex(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    lua_pushinteger(L, SDL_GetWindowDisplayIndex(win));
    return 1;
}

static int LUA_SetWindowDisplayMode(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    SDL_DisplayMode mode;

    luaL_checktype(L, 2, LUA_TTABLE);
    lua_getfield(L, 2, "h");
    mode.h = (int)luaL_checkinteger(L, -1);
    lua_getfield(L, 2, "w");
    mode.w = (int)luaL_checkinteger(L, -1);
    lua_getfield(L, 2, "format");
    mode.format = (int)luaL_checkinteger(L, -1);
    lua_getfield(L, 2, "refresh_rate");
    mode.refresh_rate = (int)luaL_checkinteger(L, -1);
    lua_pop(L, 4);
    mode.driverdata = NULL;

    lua_pushboolean(L, SDL_SetWindowDisplayMode(win, &mode) == 0);
    return 1;
}

static int LUA_GetWindowDisplayMode(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    SDL_DisplayMode mode;

    if (SDL_GetWindowDisplayMode(win, &mode) == 0)
    {
        lua_createtable(L, 0, 5);
        lua_pushinteger(L, mode.format);
        lua_setfield(L, -2, "format");
        lua_pushinteger(L, mode.h);
        lua_setfield(L, -2, "h");
        lua_pushinteger(L, mode.w);
        lua_setfield(L, -2, "w");
        lua_pushinteger(L, mode.refresh_rate);
        lua_setfield(L, -2, "refresh_rate");
        lua_pushlightuserdata(L, mode.driverdata);
        lua_setfield(L, -2, "driverdata");
        return 1;
    }
    return 0;
}

static int LUA_GetWindowPixelFormat(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    lua_pushinteger(L, SDL_GetWindowPixelFormat(win));
    return 1;
}

static int LUA_CreateWindow(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    int x = (int)luaL_optinteger(L, 2, SDL_WINDOWPOS_CENTERED);
    int y = (int)luaL_optinteger(L, 3, SDL_WINDOWPOS_CENTERED);
    int width = (int)luaL_checkinteger(L, 4);
    int height = (int)luaL_checkinteger(L, 5);
    int flags = (int)luaL_checkinteger(L, 6);

    SDL_Window* win = SDL_CreateWindow(name, x, y, width, height, flags);
    if (win)
    {
        SDL_Window** ud = (SDL_Window**)lua_newuserdata(L, sizeof(SDL_Window*));
        *ud = win;
        lua_pushinteger(L, 1);
        lua_setuservalue(L, -2);
        luaL_setmetatable(L, "SDL_Window");
        return 1;
    }
    return 0;
}
//TODO
//SDL_CreateWindowFrom()
static int LUA_GetWindowID(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    lua_pushinteger(L, SDL_GetWindowID(win));
    return 1;
}

static int LUA_GetWindowFromID(lua_State* L)
{
    int id = (int)luaL_checkinteger(L, 1);
    SDL_Window* win = SDL_GetWindowFromID(id);
    if (win)
    {
        SDL_Window** ud = (SDL_Window**)lua_newuserdata(L, sizeof(SDL_Window*));
        *ud = win;
        luaL_setmetatable(L, "SDL_Window");
        return 1;
    }
    return 0;
}

static int LUA_GetWindowFlags(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    lua_pushinteger(L, SDL_GetWindowFlags(win));
    return 1;
}

static int LUA_SetWindowTitle(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    const char* title = luaL_checkstring(L, 2);

    SDL_SetWindowTitle(win, title);
    return 0;
}

static int LUA_GetWindowTitle(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    lua_pushstring(L, SDL_GetWindowTitle(win));
    return 1;
}

static int LUA_SetWindowIcon(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    SDL_Surface* sf = *(SDL_Surface**)luaL_checkudata(L, 2, "SDL_Surface");

    SDL_SetWindowIcon(win, sf);
    return 0;
}
//TODO:
//SDL_SetWindowData()
//SDL_GetWindowData()
static int LUA_SetWindowPosition(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    int x = (int)luaL_optinteger(L, 2, SDL_WINDOWPOS_CENTERED);
    int y = (int)luaL_optinteger(L, 3, SDL_WINDOWPOS_CENTERED);

    SDL_SetWindowPosition(win, x, y);
    return 0;
}

static int LUA_GetWindowPosition(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    int x, y;

    SDL_GetWindowPosition(win, &x, &y);

    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    return 2;
}

static int LUA_SetWindowSize(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    int w = (int)luaL_checkinteger(L, 2);
    int h = (int)luaL_checkinteger(L, 3);

    SDL_SetWindowSize(win, w, h);
    return 0;
}

static int LUA_GetWindowSize(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    int w, h;

    SDL_GetWindowSize(win, &w, &h);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 2;
}

static int LUA_GetWindowBordersSize(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    int top, left, bottom, right;

    SDL_GetWindowBordersSize(win, &top, &left, &bottom, &right);

    lua_pushinteger(L, top);
    lua_pushinteger(L, left);
    lua_pushinteger(L, bottom);
    lua_pushinteger(L, right);
    return 4;
}

static int LUA_SetWindowMinimumSize(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    int maxw = (int)luaL_checkinteger(L, 2);
    int maxh = (int)luaL_checkinteger(L, 3);

    SDL_SetWindowMinimumSize(win, maxw, maxh);
    return 0;
}

static int LUA_GetWindowMinimumSize(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    int w, h;

    SDL_GetWindowMinimumSize(win, &w, &h);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 2;
}

static int LUA_SetWindowMaximumSize(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    int maxw = (int)luaL_checkinteger(L, 2);
    int maxh = (int)luaL_checkinteger(L, 3);

    SDL_SetWindowMaximumSize(win, maxw, maxh);
    return 0;
}

static int LUA_GetWindowMaximumSize(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    int w, h;

    SDL_GetWindowMaximumSize(win, &w, &h);

    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 2;
}

static int LUA_SetWindowBordered(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    SDL_bool bordered = (SDL_bool)lua_toboolean(L, 2);

    SDL_SetWindowBordered(win, bordered);
    return 0;
}

int LUA_SetWindowResizable(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    SDL_bool resizable = (SDL_bool)lua_toboolean(L, 2);

    SDL_SetWindowResizable(win, resizable);
    return 0;
}

int LUA_SetWindowAlwaysOnTop(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    SDL_bool on_top = (SDL_bool)lua_toboolean(L, 2);

    SDL_SetWindowAlwaysOnTop(win, on_top);
    return 0;
}

static int LUA_ShowWindow(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    SDL_ShowWindow(win);
    return 0;
}

static int LUA_HideWindow(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");

    SDL_HideWindow(win);
    return 0;
}

static int LUA_RaiseWindow(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");

    SDL_RaiseWindow(win);
    return 0;
}

static int LUA_MaximizeWindow(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");

    SDL_MaximizeWindow(win);
    return 0;
}

static int LUA_MinimizeWindow(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");

    SDL_MinimizeWindow(win);
    return 0;
}

static int LUA_RestoreWindow(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");

    SDL_RestoreWindow(win);
    return 0;
}

static int LUA_SetWindowFullscreen(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    Uint32 flags = (int)luaL_optinteger(L, 2, SDL_WINDOW_FULLSCREEN);

    lua_pushboolean(L, SDL_SetWindowFullscreen(win, flags) == 0);
    return 1;
}

static int LUA_GetWindowSurface(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    SDL_Surface* sf = SDL_GetWindowSurface(win);

    if (sf)
    {
        SDL_Surface** ud = (SDL_Surface**)lua_newuserdata(L, sizeof(SDL_Surface*));
        *ud = sf;
        luaL_setmetatable(L, "SDL_Surface");
        return 1;
    }
    return 0;
}

static int LUA_UpdateWindowSurface(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");

    lua_pushboolean(L, SDL_UpdateWindowSurface(win) == 0);
    return 1;
}

static int LUA_UpdateWindowSurfaceRects(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    luaL_checktype(L, 2, LUA_TTABLE);
    int num = (int)lua_rawlen(L, 2);
    SDL_Rect* rects = SDL_calloc(num, sizeof(SDL_Rect));

    for (int i = 0; i < num; i++)
    {
        lua_geti(L, 2, i + 1);
        rects[i] = *(SDL_Rect*)luaL_checkudata(L, -1, "SDL_Rect");
    }

    lua_pushboolean(L, SDL_UpdateWindowSurfaceRects(win, rects, num) == 0);
    SDL_free(rects);
    return 1;
}

static int LUA_SetWindowGrab(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    SDL_bool grabbed = lua_toboolean(L, 2);

    SDL_SetWindowGrab(win, grabbed);
    return 0;
}

static int LUA_GetWindowGrab(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");

    lua_pushboolean(L, SDL_GetWindowGrab(win));
    return 1;
}

static int LUA_GetGrabbedWindow(lua_State* L)
{
    SDL_Window* win = SDL_GetGrabbedWindow();
    if (win)
    {
        SDL_Window** ud = (SDL_Window**)lua_newuserdata(L, sizeof(SDL_Window*));
        *ud = win;
        luaL_setmetatable(L, "SDL_Window");
        return 1;
    }

    return 0;
}

static int LUA_SetWindowBrightness(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    float brightness = (float)luaL_checknumber(L, 2);

    lua_pushboolean(L, SDL_SetWindowBrightness(win, brightness) == 0);
    return 1;
}

static int LUA_GetWindowBrightness(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    lua_pushnumber(L, SDL_GetWindowBrightness(win));
    return 1;
}

static int LUA_SetWindowOpacity(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    float opacity = (float)luaL_checknumber(L, 2);

    lua_pushboolean(L, SDL_SetWindowOpacity(win, opacity) == 0);
    return 1;
}

static int LUA_GetWindowOpacity(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    float opacity;

    if (SDL_GetWindowOpacity(win, &opacity) == 0)
    {
        lua_pushnumber(L, opacity);
        return 1;
    }

    return 0;
}

static int LUA_SetWindowModalFor(lua_State* L)
{
    SDL_Window* mw = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    SDL_Window* pw = *(SDL_Window**)luaL_checkudata(L, 2, "SDL_Window");

    lua_pushboolean(L, SDL_SetWindowModalFor(mw, pw) == 0);
    return 1;
}

static int LUA_SetWindowInputFocus(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");

    lua_pushboolean(L, SDL_SetWindowInputFocus(win) == 0);
    return 1;
}

//static int LUA_SetWindowGammaRamp(lua_State *L)
//{
//    //SDL_Window * win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
//    //Uint16 red[256];
//    //Uint16 green[256];
//    //Uint16 blue[256];
//
//    //TODO
//
//    //if (SDL_SetWindowGammaRamp(win, red, green, blue) == 0){
//    //  lua_pushboolean(L,1);
//    //  return 1;
//    //}
//
//    return 0;
//}
//
//static int LUA_GetWindowGammaRamp(lua_State *L)
//{
//    //SDL_Window * win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
//    //Uint16 red[256];
//    //Uint16 green[256];
//    //Uint16 blue[256];
//    //TODO
//    //if (SDL_GetWindowGammaRamp(win, red, green, blue) == 0){
//    //  lua_createtable(L, 3, 3);
//    //  //?
//    //  return 1;
//    //}
//    return 0;
//}
//TODO

//typedef struct {
//    lua_State* L;   // The Lua state the callback is in
//    int ref;    // The registry index of the function to call
//} CallbackData;
//
//static SDL_HitTestResult
//hitTestCallback(SDL_Window* win, const SDL_Point* area, CallbackData* cd)
//{
//    SDL_HitTestResult ht;
//    lua_State* L = cd->L;
//    int st = lua_gettop(L);
//    //SDL_Window ** ud;
//    lua_geti(L, LUA_REGISTRYINDEX, cd->ref);
//
//    //ud = (SDL_Window**)lua_newuserdata(L, sizeof (SDL_Window*));
//    //*ud = win;
//    //luaL_setmetatable(L, "SDL_Window");
//    lua_pushinteger(L, area->x);
//    lua_pushinteger(L, area->y);
//    lua_pcall(cd->L, 2, 1, 0);
//
//    ht = luaL_optinteger(L, -1, SDL_HITTEST_NORMAL);
//    lua_settop(L, st);
//
//    return ht;
//}
//
//static int LUA_SetWindowHitTest(lua_State* L)
//{
//    //SDL_Window * win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
//    //CallbackData *cd;
//    //if (lua_isfunction(L,2)) {
//
//    //  if (lua_getuservalue(L, 1)== LUA_TUSERDATA){
//    //      cd = lua_touserdata(L, -1);
//    //      luaL_unref(L, LUA_REGISTRYINDEX, cd->ref);
//    //  }else{
//    //      cd = lua_newuserdata(L, sizeof(CallbackData));
//    //      lua_setuservalue(L, 1);
//    //      cd->L = lua_newthread(L);
//    //  }
//    //
//    //  cd->ref = luaL_ref(L, LUA_REGISTRYINDEX);
//
//    //  if (SDL_SetWindowHitTest(win, hitTestCallback, cd) == 0){
//    //      lua_pushboolean(L,1);
//    //      return 1;
//    //  }
//    //}else {
//    //  if (lua_getuservalue(L, 1)== LUA_TUSERDATA){
//    //      cd = lua_touserdata(L, -1);
//    //      luaL_unref(cd->L, LUA_REGISTRYINDEX, cd->ref);
//    //  }
//    //  if (SDL_SetWindowHitTest(win, NULL, NULL) == 0){
//    //      lua_pushboolean(L,1);
//    //      return 1;
//    //  }
//    //}
//
//    return 0;
//}
static int LUA_FlashWindow(lua_State* L)
{
    SDL_Window* win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    SDL_FlashOperation operation = (SDL_FlashOperation)luaL_optinteger(L, 2, 2);
    lua_pushboolean(L, SDL_FlashWindow(win, operation));
    return 0;
}

static int LUA_DestroyWindow(lua_State* L)
{
    SDL_Window** win = (SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    lua_getuservalue(L, 1);
    if (*win && lua_tointeger(L, -1))
    {
        SDL_DestroyWindow(*win);
        *win = NULL;
    }
    return 0;
}

static int LUA_IsScreenSaverEnabled(lua_State* L)
{
    lua_pushboolean(L, SDL_IsScreenSaverEnabled());
    return 1;
}

static int LUA_EnableScreenSaver(lua_State* L)
{
    SDL_EnableScreenSaver();
    return 0;
}

static int LUA_DisableScreenSaver(lua_State* L)
{
    SDL_DisableScreenSaver();
    return 0;
}
//TODO
//SDL_GL_LoadLibrary()
//SDL_GL_GetProcAddress()
//SDL_GL_UnloadLibrary
//static int LUA_GL_ExtensionSupported(lua_State *L)
//{
//    const char* extension = luaL_checkstring(L, 1);
//    lua_pushboolean(L,SDL_GL_ExtensionSupported(extension));
//    return 1;
//}
//
//static int LUA_GL_ResetAttributes(lua_State *L)
//{
//    SDL_GL_ResetAttributes();
//    return 0;
//}
//
//static int LUA_GL_SetAttribute(lua_State *L)
//{
//    SDL_GLattr attr = (SDL_GLattr)luaL_checkinteger(L, 1);
//    int value   = (int)luaL_checkinteger(L, 2);
//
//    lua_pushboolean(L,SDL_GL_SetAttribute(attr, value)== 0);
//    return 1;
//}
//
//static int LUA_GL_GetAttribute(lua_State *L)
//{
//    SDL_GLattr attr = (SDL_GLattr)luaL_checkinteger(L, 1);
//    int value;
//
//    if (SDL_GL_GetAttribute(attr, &value) == 0){
//        lua_pushinteger(L,value);
//        return 1;
//    }
//    return 0;
//}
//
//static int LUA_GL_CreateContext(lua_State *L)
//{
//    SDL_Window * win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
//    SDL_GLContext ctx = SDL_GL_CreateContext(win);
//
//    SDL_GLContext *ud = (SDL_GLContext*)lua_newuserdata(L, sizeof (SDL_GLContext));
//    *ud = ctx;
//    luaL_setmetatable(L, "SDL_GLContext");
//    return 1;
//}
//
//static int LUA_GL_MakeCurrent(lua_State *L)
//{
//    SDL_Window * win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
//    SDL_GLContext ctx = *(SDL_GLContext*)luaL_checkudata(L, 2, "SDL_GLContext");
//
//    lua_pushboolean(L,SDL_GL_MakeCurrent(win, ctx) == 0);
//    return 1;
//}
//
//static int LUA_GL_GetCurrentWindow(lua_State *L)
//{
//    SDL_Window *win = SDL_GL_GetCurrentWindow();
//    SDL_Window **ud = (SDL_Window**)lua_newuserdata(L, sizeof (SDL_Window*));
//    *ud = win;
//    luaL_setmetatable(L, "SDL_Window");
//    return 1;
//}
//
//static int LUA_GL_GetCurrentContext(lua_State *L)
//{
//    SDL_GLContext ctx = SDL_GL_GetCurrentContext();
//    SDL_GLContext *ud = (SDL_GLContext*)lua_newuserdata(L, sizeof (SDL_GLContext));
//    *ud = ctx;
//    luaL_setmetatable(L, "SDL_GLContext");
//    return 1;
//}
//
//static int LUA_GL_GetDrawableSize(lua_State *L)
//{
//    SDL_Window * win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
//    int width, height;
//
//    SDL_GL_GetDrawableSize(win, &width, &height);
//
//    lua_pushinteger(L,width);
//    lua_pushinteger(L,height);
//    return 2;
//}
//
//static int LUA_GL_SetSwapInterval(lua_State *L)
//{
//    int interval = (int)luaL_optinteger(L, 1, -1);
//
//    lua_pushboolean(L,SDL_GL_SetSwapInterval(interval) == 0);
//    return 1;
//}
//
//static int LUA_GL_GetSwapInterval(lua_State *L)
//{
//    lua_pushinteger(L,SDL_GL_GetSwapInterval());
//    return 1;
//}
//
//static int LUA_GL_SwapWindow(lua_State *L)
//{
//    SDL_Window * win = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
//    SDL_GL_SwapWindow(win);
//
//    return 0;
//}
//
//static int LUA_GL_DeleteContext(lua_State *L)
//{
//    SDL_GLContext ctx = *(SDL_GLContext*)luaL_checkudata(L, 1, "SDL_GLContext");
//
//    SDL_GL_DeleteContext(ctx);
//    return 0;
//}

static const luaL_Reg window_funcs[] = {
    {"__index", NULL},
    {"GetWindowDisplayIndex", LUA_GetWindowDisplayIndex},
    {"SetWindowDisplayMode", LUA_SetWindowDisplayMode},
    {"GetWindowDisplayMode", LUA_GetWindowDisplayMode},
    {"GetWindowPixelFormat", LUA_GetWindowPixelFormat},
    {"GetWindowID", LUA_GetWindowID},
    {"GetWindowFlags", LUA_GetWindowFlags},
    {"SetWindowTitle", LUA_SetWindowTitle},
    {"GetWindowTitle", LUA_GetWindowTitle},
    {"SetWindowIcon", LUA_SetWindowIcon},
    //SetWindowData
    //GetWindowData
    {"SetWindowPosition", LUA_SetWindowPosition},
    {"GetWindowPosition", LUA_GetWindowPosition},
    {"SetWindowSize", LUA_SetWindowSize},
    {"GetWindowSize", LUA_GetWindowSize},
    {"GetWindowBordersSize", LUA_GetWindowBordersSize},
    {"SetWindowMinimumSize", LUA_SetWindowMinimumSize},
    {"GetWindowMinimumSize", LUA_GetWindowMinimumSize},
    {"SetWindowMaximumSize", LUA_SetWindowMaximumSize},
    {"GetWindowMaximumSize", LUA_GetWindowMaximumSize},
    {"SetWindowBordered", LUA_SetWindowBordered},
    {"SetWindowResizable", LUA_SetWindowResizable},
    {"SetWindowAlwaysOnTop", LUA_SetWindowAlwaysOnTop},
    {"ShowWindow", LUA_ShowWindow},
    {"HideWindow", LUA_HideWindow},
    {"RaiseWindow", LUA_RaiseWindow},
    {"MaximizeWindow", LUA_MaximizeWindow},
    {"MinimizeWindow", LUA_MinimizeWindow},
    {"RestoreWindow", LUA_RestoreWindow},
    {"SetWindowFullscreen", LUA_SetWindowFullscreen},
    {"GetWindowSurface", LUA_GetWindowSurface},
    {"UpdateWindowSurface", LUA_UpdateWindowSurface},
    {"UpdateWindowSurfaceRects", LUA_UpdateWindowSurfaceRects},
    {"SetWindowGrab", LUA_SetWindowGrab},
    {"GetWindowGrab", LUA_GetWindowGrab},
    {"SetWindowBrightness", LUA_SetWindowBrightness},
    {"GetWindowBrightness", LUA_GetWindowBrightness},
    {"SetWindowOpacity", LUA_SetWindowOpacity},
    {"GetWindowOpacity", LUA_GetWindowOpacity},
    {"SetWindowModalFor", LUA_SetWindowModalFor},
    {"SetWindowInputFocus", LUA_SetWindowInputFocus},
    //{"SetWindowGammaRamp", LUA_SetWindowGammaRamp},
    //{"GetWindowGammaRamp", LUA_GetWindowGammaRamp},
    //{"SetWindowHitTest", LUA_SetWindowHitTest},
    {"FlashWindow", LUA_FlashWindow},
    {"DestroyWindow", LUA_DestroyWindow},

    //{ "GL_CreateContext", LUA_GL_CreateContext},
    //{ "GL_GetDrawableSize", LUA_GL_GetDrawableSize},
    //{ "GL_MakeCurrent", LUA_GL_MakeCurrent},
    //{ "GL_SwapWindow", LUA_GL_SwapWindow},

    {NULL, NULL} };

static const luaL_Reg sdl_funcs[] = {
    {"GetNumVideoDrivers", LUA_GetNumVideoDrivers},
    {"GetVideoDriver", LUA_GetVideoDriver},
    {"VideoInit", LUA_VideoInit},
    {"VideoQuit", LUA_VideoQuit},
    {"GetCurrentVideoDriver", LUA_GetCurrentVideoDriver},
    {"GetNumVideoDisplays", LUA_GetNumVideoDisplays},
    {"GetDisplayName", LUA_GetDisplayName},
    {"GetDisplayBounds", LUA_GetDisplayBounds},
    {"GetDisplayUsableBounds", LUA_GetDisplayUsableBounds},
    {"GetDisplayDPI", LUA_GetDisplayDPI},
    {"GetDisplayOrientation", LUA_GetDisplayOrientation},
    {"GetNumDisplayModes", LUA_GetNumDisplayModes},
    {"GetDisplayMode", LUA_GetDisplayMode},
    {"GetDesktopDisplayMode", LUA_GetDesktopDisplayMode},
    {"GetCurrentDisplayMode", LUA_GetCurrentDisplayMode},
    {"GetClosestDisplayMode", LUA_GetClosestDisplayMode},

    {"CreateWindow", LUA_CreateWindow},
    //CreateWindowFrom
    {"GetWindowFromID", LUA_GetWindowFromID},
    {"GetGrabbedWindow", LUA_GetGrabbedWindow},
    {"IsScreenSaverEnabled", LUA_IsScreenSaverEnabled},
    {"EnableScreenSaver", LUA_EnableScreenSaver},
    {"DisableScreenSaver", LUA_DisableScreenSaver},

    //SDL_GL_LoadLibrary

    //{"GL_ExtensionSupported", LUA_GL_ExtensionSupported},
    //{"GL_GetAttribute", LUA_GL_GetAttribute},
    //{"GL_GetCurrentContext", LUA_GL_GetCurrentContext},
    //{"GL_GetCurrentWindow", LUA_GL_GetCurrentWindow},
    //{"GL_GetSwapInterval", LUA_GL_GetSwapInterval},
    //{"GL_ResetAttributes", LUA_GL_ResetAttributes},
    //{"GL_SetAttribute", LUA_GL_SetAttribute},
    //{"GL_SetSwapInterval", LUA_GL_SetSwapInterval},

    {NULL, NULL} };

int bind_video(lua_State* L)
{
    luaL_getmetatable(L, "SDL_Window");
    luaL_setfuncs(L, window_funcs, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}