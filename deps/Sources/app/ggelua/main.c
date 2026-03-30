#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "SDL_stdinc.h"
#include "SDL_mutex.h"
#include "SDL_messagebox.h"
#include "SDL_rwops.h"
#include "SDL_main.h"
#include "SDL_log.h"
#include "SDL_system.h"
#include "SDL_platform.h"
#ifdef _WIN32
#include "windows.h"
#endif

int luaopen_ggelua(lua_State* L);
typedef struct _PACKINFO
{
    Uint32 signal;
    Uint32 coresize; //ggelua.lua
    Uint32 packsize;
} PACKINFO;

static int GGE_LoadScript(lua_State* L)
{
    size_t len;
    const char* path = lua_tolstring(L, 1, &len);
    PACKINFO info = { 0, 0, 0 };

    SDL_RWops* rw = SDL_RWFromFile(path, "rb");

    if (rw)
    {
        if (SDL_RWseek(rw, -(int)sizeof(PACKINFO), RW_SEEK_END) == -1)
        { //移动到文件尾
            SDL_FreeRW(rw);
            return 0;
        }
        if (SDL_RWread(rw, &info, sizeof(PACKINFO), 1) == 0)
        { //包信息
            SDL_FreeRW(rw);
            return 0;
        }
    }

    if (info.signal == 0x20454747)
    { //GGE\x20
        if (SDL_RWseek(rw, -(int)(sizeof(PACKINFO) + info.coresize + info.packsize), RW_SEEK_END) == -1)
        {
            SDL_FreeRW(rw);
            return 0;
        }
        void* ggecore = SDL_malloc(info.coresize);
        SDL_RWread(rw, ggecore, info.coresize, 1);

        void* ggepack = SDL_malloc(info.packsize);
        SDL_RWread(rw, ggepack, info.packsize, 1);

        lua_pushlstring(L, ggecore, info.coresize);
        lua_setfield(L, LUA_REGISTRYINDEX, "ggecore");
        SDL_free(ggecore);

        lua_pushlstring(L, ggepack, info.packsize);
        lua_setfield(L, LUA_REGISTRYINDEX, "ggepack");
        SDL_free(ggepack);
    }
    else
    {
#if defined(_WIN32)
        char lpath[260];
        for (size_t i = 0; i < len; i++) //到小写
            lpath[i] = SDL_tolower(path[i]);
        lpath[len] = 0;
        if (GetConsoleWindow() != NULL)
            path = luaL_gsub(L, lpath, "ggeluac.exe", "ggelua.lua");
        else
            path = luaL_gsub(L, lpath, "ggelua.exe", "ggelua.lua");

        void* ggecore = SDL_LoadFile(path, (size_t*)&info.coresize);
        if (ggecore)
        {
            lua_pushlstring(L, ggecore, info.coresize);
            lua_setfield(L, LUA_REGISTRYINDEX, "ggecore");
            SDL_free(ggecore);
        }
#else
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "error", "no ggescript", NULL);
#endif
    }
    SDL_FreeRW(rw);
    lua_pushboolean(L, info.signal != 0x20454747);
    return 1;
}

int SDL_main(int argc, char** argv)
{
#ifdef _WIN32
    CONSOLE_FONT_INFOEX info = { 0 }; // 以下设置字体
    info.cbSize = sizeof(info);
    info.dwFontSize.Y = 16;
    info.FontWeight = FW_NORMAL;

    SDL_wcslcpy(info.FaceName, L"Consolas", LF_FACESIZE);
    SetCurrentConsoleFontEx(GetStdHandle(STD_OUTPUT_HANDLE), FALSE, &info);
    SetConsoleOutputCP(65001);
    SDL_SetMemoryFunctions(malloc, calloc, realloc, free);
    SDL_RegisterApp("GGELUA", 0, NULL);

#endif
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    lua_pushlightuserdata(L, argv);
    lua_setfield(L, LUA_REGISTRYINDEX, "_argv");

    lua_pushinteger(L, argc);
    lua_setfield(L, LUA_REGISTRYINDEX, "_argc");

    luaL_getsubtable(L, LUA_REGISTRYINDEX, "_ggelua");
    lua_createtable(L, argc, 0);

    for (int i = 1; i < argc; i++)
    {
        lua_pushstring(L, argv[i]);
        lua_seti(L, -2, i);
    }
    lua_setfield(L, -2, "arg"); //gge.arg

#ifdef _WIN32
    lua_pushboolean(L, GetConsoleWindow() != NULL);
#else
    lua_pushboolean(L, 0);
#endif
    lua_setfield(L, -2, "isconsole"); //gge.isconsole

    lua_pushcfunction(L, GGE_LoadScript); //读取脚本
#if defined(_WIN32)
    lua_pushstring(L, argv[0]);
#else
    lua_pushstring(L, "ggelua.com"); //手机脚本包名
#endif
    lua_call(L, 1, 1);
    int isdebug = lua_toboolean(L, -1);
    lua_setfield(L, -2, "isdebug"); //gge.isdebug

    lua_pushstring(L, "main");
    lua_setfield(L, -2, "entry"); //gge.entry
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "ismain"); //gge.ismain
    lua_pushstring(L, SDL_GetPlatform());
    lua_setfield(L, -2, "platform"); //gge.platform
    lua_pushstring(L, "22.06.10");
    lua_setfield(L, -2, "version");
    lua_setglobal(L, "gge");

    lua_getfield(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE); //package.preload
    lua_pushcfunction(L, luaopen_ggelua);
    lua_setfield(L, -2, "ggelua");
    lua_pop(L, 1);

    SDL_mutex** extra = (SDL_mutex**)lua_getextraspace(L); //线程锁
    SDL_mutex* mutex = SDL_CreateMutex();
    *extra = mutex;
    int status = LUA_OK;

    if (lua_getfield(L, LUA_REGISTRYINDEX, "ggecore") == LUA_TSTRING)
    {
        size_t coresize;
        const char* ggecore = lua_tolstring(L, -1, &coresize);
        lua_pop(L, 1);
        SDL_LockMutex(mutex); //ggelua.delay解锁

        if (luaL_loadbuffer(L, ggecore, coresize, "ggelua.lua") == LUA_OK)
        {
            lua_pushstring(L, "main"); //入口脚本
            lua_pushboolean(L, 0);     //是否虚拟机
            status = lua_pcall(L, 2, 0, 0);
            if (status == LUA_OK)
            {
                status = LUA_OK;
                if (lua_getglobal(L, "main") == LUA_TFUNCTION) //loop
                    status = lua_pcall(L, 0, 0, 0);
            }
        }
    }
    else
    {
        status = 'GGE';
        lua_pushstring(L, "no script");
    }

    if (status != LUA_OK)
    {
        const char* msg = lua_tostring(L, -1);
        luaL_traceback(L, L, msg, 1);
        msg = lua_tostring(L, -1);
        SDL_Log("%s", msg);
        if (!isdebug)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "error", msg, NULL);
    }

    SDL_DestroyMutex(mutex);
    lua_close(L);
#ifdef _WIN32
    SDL_UnregisterApp();
#endif
    return -status;
}