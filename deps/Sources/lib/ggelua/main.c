#include "luaopen.h"
#include "SDL_clipboard.h"
#include "SDL_filesystem.h"
#include "SDL_messagebox.h"
#include "SDL_mutex.h"
#include "SDL_platform.h"
#include "SDL_rect.h"
#include "SDL_stdinc.h"
#include "SDL_thread.h"
#include "SDL_timer.h"
#include "SDL_log.h"
#include "SDL_version.h"

#ifdef __WIN32__
#include <Windows.h>
#else
#include "gbk_conv.h"
#endif

static int LUA_GBK2UTF8(lua_State *L)
{
    size_t inlen;
    char *str = (char *)luaL_checklstring(L, 1, &inlen);
#ifdef __WIN32__
    int len = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0);
    WCHAR *temp = (WCHAR *)SDL_malloc(len * sizeof(WCHAR));
    MultiByteToWideChar(CP_ACP, 0, str, -1, temp, len); //Unicode

    luaL_Buffer b;
    len = WideCharToMultiByte(CP_UTF8, 0, temp, -1, NULL, 0, NULL, 0);
    char *buf = luaL_buffinitsize(L, &b, len);
    len = WideCharToMultiByte(CP_UTF8, 0, temp, -1, buf, len, NULL, 0);

    SDL_free(temp);
    luaL_pushresultsize(&b, len - 1);
    return 1;
#else
    /* 使用内嵌 GBK 码表转换（不依赖系统 iconv） */
    luaL_Buffer b;
    size_t out_size = inlen * 3 + 1;
    char *buf = luaL_buffinitsize(L, &b, out_size);
    size_t written = gbk_to_utf8(str, inlen, buf, out_size);
    if (written > 0) {
        luaL_pushresultsize(&b, written);
        return 1;
    }
#endif
    return 0;
}

static int LUA_UTF82GBK(lua_State *L)
{
    size_t inlen;
    char *str = (char *)luaL_checklstring(L, 1, &inlen);
#ifdef __WIN32__
    int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
    WCHAR *temp = (WCHAR *)SDL_malloc(len * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, str, -1, temp, len);

    luaL_Buffer b;
    len = WideCharToMultiByte(CP_ACP, 0, temp, -1, NULL, 0, NULL, 0);
    char *buf = luaL_buffinitsize(L, &b, len);
    len = WideCharToMultiByte(CP_ACP, 0, temp, -1, buf, len, NULL, 0);

    SDL_free(temp);
    luaL_pushresultsize(&b, len - 1);
    return 1;
#else
    /* 使用内嵌 GBK 码表转换（不依赖系统 iconv） */
    luaL_Buffer b;
    size_t out_size = inlen * 2 + 1;
    char *buf = luaL_buffinitsize(L, &b, out_size);
    size_t written = utf8_to_gbk(str, inlen, buf, out_size);
    if (written > 0) {
        luaL_pushresultsize(&b, written);
        return 1;
    }
#endif
    return 0;
}
#ifdef __WIN32__
static int LUA_GetRunPath(lua_State *L)
{
    lua_pushcfunction(L, LUA_GBK2UTF8);
    luaL_Buffer b;
    char *buf = luaL_buffinitsize(L, &b, 256);
    DWORD n = GetModuleFileName(NULL, buf, 256);
    buf[n] = 0;
    luaL_pushresultsize(&b, strrchr(buf, '\\') - buf); /* close buffer */

    lua_call(L, 1, 1);
    return 1;
}

static int LUA_GetCurPath(lua_State *L)
{
    lua_pushcfunction(L, LUA_GBK2UTF8);
    luaL_Buffer b;
    char *buf = luaL_buffinitsize(L, &b, 256);
    DWORD n = GetCurrentDirectory(LUAL_BUFFERSIZE, buf);
    luaL_pushresultsize(&b, n); /* close buffer */

    lua_call(L, 1, 1);
    return 1;
}

static int LUA_SetCurPath(lua_State *L)
{
    lua_pushcfunction(L, LUA_UTF82GBK);
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1);
    const char *str = luaL_checkstring(L, -1);

    lua_pushboolean(L, SetCurrentDirectory(str));
    return 1;
}
#endif

typedef struct _THREADINFO
{
    const char *ggecore;
    const char *ggepack;
    size_t coresize;
    size_t packsize;
    const char *entry;
    int isdebug;
    int argc;
    char **argv;
} THREADINFO;

static int LUA_StateThread(void *data)
{
    THREADINFO *info = (THREADINFO *)data;
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    if (info->ggepack)
    {
        lua_pushlstring(L, info->ggepack, info->packsize);
        lua_setfield(L, LUA_REGISTRYINDEX, "ggepack");
    }
    luaL_getsubtable(L, LUA_REGISTRYINDEX, "_ggelua");
    lua_createtable(L, info->argc, 0);
    for (int i = 1; i < info->argc; i++)
    {
        lua_pushstring(L, info->argv[i]);
        lua_seti(L, -2, i);
    }
    lua_setfield(L, -2, "arg"); //gge.arg
#ifdef __WIN32__
    lua_pushboolean(L, GetConsoleWindow() != NULL);
#else
    lua_pushboolean(L, 0);
#endif
    lua_setfield(L, -2, "isconsole"); //gge.isconsole
    lua_pushboolean(L, info->isdebug);
    lua_setfield(L, -2, "isdebug"); //gge.isdebug
    lua_pushstring(L, info->entry);
    lua_setfield(L, -2, "entry"); //gge.entry
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "ismain"); //gge.ismain
    lua_pushstring(L, SDL_GetPlatform());
    lua_setfield(L, -2, "platform"); //gge.platform
    lua_setglobal(L, "gge");

    lua_getfield(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE); //package.preload
    lua_pushcfunction(L, luaopen_ggelua);
    lua_setfield(L, -2, "ggelua");
    lua_pop(L, 1);

    SDL_mutex **extra = (SDL_mutex **)lua_getextraspace(L);
    SDL_mutex *mutex = SDL_CreateMutex();
    *extra = mutex;

    size_t coresize = info->coresize;
    const char *ggecore = info->ggecore;
    int haserror = 1;
    SDL_LockMutex(mutex);
    if (luaL_loadbuffer(L, ggecore, coresize, "ggelua.lua") == LUA_OK)
    {
        lua_pushstring(L, info->entry); //入口脚本
        lua_pushboolean(L, 1);          //是否虚拟机

        if (lua_pcall(L, 2, 0, 0) == LUA_OK)
        {
            haserror = 0;
            if (lua_getglobal(L, "main") == LUA_TFUNCTION) //loop
                haserror = lua_pcall(L, 0, 0, 0);
        }
    }
    if (haserror)
    {
        const char *msg = lua_tostring(L, -1);
        SDL_Log("%s", msg);
        if (!info->isdebug)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "error", msg, NULL);
    }
    SDL_DestroyMutex(mutex);
    lua_close(L);
    SDL_free(info);
    return 0;
}
//主要用于服务端创建
static int LUA_NewState(lua_State *L)
{
    if (lua_getfield(L, LUA_REGISTRYINDEX, "ggecore") == LUA_TSTRING)
    {
        THREADINFO *info = (THREADINFO *)SDL_malloc(sizeof(THREADINFO));
        lua_getfield(L, LUA_REGISTRYINDEX, "_argc");
        info->argc = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, LUA_REGISTRYINDEX, "_argv");
        info->argv = (char **)lua_topointer(L, -1);
        lua_pop(L, 1);
        info->entry = luaL_checkstring(L, 1);
        info->ggecore = lua_tolstring(L, -1, &info->coresize);
        if (lua_getfield(L, LUA_REGISTRYINDEX, "ggepack") == LUA_TSTRING)
        {
            info->isdebug = 0;
            info->ggepack = lua_tolstring(L, -1, &info->packsize);
        }
        else
        {
            info->isdebug = 1;
            info->ggepack = NULL;
        }
        lua_pop(L, 1);

        SDL_Thread *t = SDL_CreateThread(LUA_StateThread, NULL, (void *)info);
        lua_pushboolean(L, t != NULL);
        if (!t)
            SDL_free(info);
        return 1;
    }
    return 0;
}

static int LUA_Delay(lua_State *L)
{
    Uint32 n = (Uint32)lua_tointeger(L, 1);
    SDL_mutex *mutex = *(SDL_mutex **)lua_getextraspace(L);
    SDL_UnlockMutex(mutex);
    SDL_Delay(n);
    SDL_LockMutex(mutex);
    return 0;
}

static int LUA_GetTicks(lua_State* L)
{
    lua_pushnumber(L, SDL_GetTicks64()/1000.0);
    return 1;
}

static int LUA_Exit(lua_State *L)
{
    SDL_mutex **extra = (SDL_mutex **)lua_getextraspace(L);
    SDL_mutex *mutex = *extra;
    *extra = NULL;
    SDL_UnlockMutex(mutex);
    SDL_Delay(10);
    return 0;
}

static int LUA_SetClipboardText(lua_State *L)
{
    const char *text = luaL_checkstring(L, 1);
    lua_pushboolean(L, SDL_SetClipboardText(text) == 0);
    return 1;
}

static int LUA_GetClipboardText(lua_State *L)
{
    char *str = SDL_GetClipboardText();
    lua_pushstring(L, str);
    SDL_free(str);
    return 1;
}

static int LUA_HasClipboardText(lua_State *L)
{
    lua_pushboolean(L, SDL_HasClipboardText());
    return 1;
}

static int LUA_GetBasePath(lua_State *L)
{
    char *str = SDL_GetBasePath();
    lua_pushstring(L, str);
    SDL_free(str);
    return 1;
}
//PC %AppData%\Roaming\GGELUA
//ANDROID /data/data/com.GGELUA.game/files
static int LUA_GetPrefPath(lua_State *L)
{
#ifdef __WIN32__
    const char *application = luaL_checkstring(L, 1);
#else
    const char *application = NULL;
#endif
    char *str = SDL_GetPrefPath("GGELUA", application);

    lua_pushstring(L, str);
    SDL_free(str);
    return 1;
}

static int LUA_MessageBox(lua_State *L)
{
    const char *message = lua_tostring(L, 1);
    const char *title = luaL_optstring(L, 2, "");
    Uint32 flags = (int)luaL_optinteger(L, 3, 0);

    lua_pushboolean(L, SDL_ShowSimpleMessageBox(flags, title, message, NULL) == 0);
    return 1;
}

static int LUA_GetPlatform(lua_State *L)
{
    lua_pushstring(L, SDL_GetPlatform());
    return 1;
}

static int LUA_GetSDLVersion(lua_State *L)
{
    SDL_version ver;
    SDL_GetVersion(&ver);
    lua_pushfstring(L, "SDL %d.%d.%d", ver.major, ver.minor, ver.patch);
    return 1;
}

static int LUA_GetLUAVersion(lua_State *L)
{
    lua_pushfstring(L, "Lua %s.%s.%s", LUA_VERSION_MAJOR, LUA_VERSION_MINOR, LUA_VERSION_RELEASE);
    return 1;
}

static int LUA_Log(lua_State *L)
{
    SDL_Log(lua_tostring(L, 1));
    return 0;
}

static int LUA_Warn(lua_State* L)
{
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,lua_tostring(L, 1));
    return 0;
}

static const luaL_Reg fun_list[] = {
    {"gbktoutf8", LUA_GBK2UTF8},
    {"utf8togbk", LUA_UTF82GBK},
#ifdef __WIN32__
    {"getcurpath", LUA_GetCurPath},
    {"setcurpath", LUA_SetCurPath},
    {"getrunpath", LUA_GetRunPath},
#endif
    {"newstate", LUA_NewState},

    {"delay", LUA_Delay},
    {"getticks", LUA_GetTicks},
    {"exit", LUA_Exit},
    {"messagebox", LUA_MessageBox},

    {"setclipboardtext", LUA_SetClipboardText},
    {"getclipboardtext", LUA_GetClipboardText},
    {"hasclipboardtext", LUA_HasClipboardText},

    {"getbasepath", LUA_GetBasePath},
    {"getprefpath", LUA_GetPrefPath},
    {"getplatform", LUA_GetPlatform},
    {"getsdlversion", LUA_GetSDLVersion},
    {"getluaversion", LUA_GetLUAVersion},

    {"hash", luaopen_tohash},
    {"log", LUA_Log},
    {"warn", LUA_Warn},

    {NULL, NULL}};

static const luaL_Reg lib_list[] = {
    {"zlib", luaopen_zlib},
    {"md5", luaopen_md5},
    {"base64", luaopen_base64},
    {"cjson", luaopen_cjson},
    {"cjson.safe", luaopen_cjson_safe},
    {"cmsgpack", luaopen_cmsgpack},
    {"cmsgpack.safe", luaopen_cmsgpack_safe},
    {"lfs", luaopen_lfs},
    {"cprint", luaopen_cprint},
    {"uuid", luaopen_uuid},
    {"nanoid", luaopen_nanoid},
    {"aes", luaopen_aes},
    {"physfs", luaopen_physfs},
    {"ghv.TcpClient", luaopen_ghv_TcpClient},
    {"ghv.TcpServer", luaopen_ghv_TcpServer},
    {"ghv.HttpRequests", luaopen_ghv_HttpRequests},
    {"ghv.download", luaopen_ghv_download},
    {"ghv.WebSocketServer", luaopen_ghv_WebSocketServer},
    {"gge.core", luaopen_ggecore},
    {NULL, NULL},
};

int luaopen_ggelua(lua_State *L)
{
    //内置模块
    lua_getfield(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE); //package.preload
    luaL_setfuncs(L, lib_list, 0);
    lua_pop(L, 1); // remove PRELOAD

    //全局gge.?
    luaL_getsubtable(L, LUA_REGISTRYINDEX, "_ggelua");
    luaL_setfuncs(L, fun_list, 0);

    lua_pushcfunction(L, luaopen_ggescript);
    lua_getfield(L, LUA_REGISTRYINDEX, "ggepack");
    lua_call(L, 1, 1);

    lua_setfield(L, -2, "script");
    return 1; //fun_list
}
