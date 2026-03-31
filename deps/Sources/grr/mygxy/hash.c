#include "lua_proxy.h"
#include "sdl_proxy.h"
#include "lua_proxy.h"
#include "sdl_proxy.h"
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define MYGXY_API __declspec(dllexport)
#else
#define MYGXY_API LUAMOD_API
#endif

#ifdef _WIN32
#include <Windows.h>
#define DELAYIMP_INSECURE_WRITABLE_HOOKS
#include <delayimp.h>

typedef BOOL(WINAPI* gxy_SetDefaultDllDirectories_t)(DWORD);
typedef DLL_DIRECTORY_COOKIE(WINAPI* gxy_AddDllDirectory_t)(PCWSTR);

static HMODULE gxy_get_self_module_handle(void)
{
    HMODULE self = NULL;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&gxy_get_self_module_handle,
            &self))
        return NULL;
    return self;
}

static int gxy_dir_exists_w(const WCHAR* path)
{
    if (!path || !path[0])
        return 0;
    DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return 0;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static int gxy_get_self_dir_w(WCHAR outDir[MAX_PATH])
{
    if (!outDir)
        return 0;

    HMODULE self = gxy_get_self_module_handle();
    if (!self)
        return 0;

    DWORD n = GetModuleFileNameW(self, outDir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return 0;

    WCHAR* slash = wcsrchr(outDir, L'\\');
    if (!slash)
        return 0;
    *slash = L'\0';
    return 1;
}

static int gxy_parent_dir_w(const WCHAR* dir, WCHAR outDir[MAX_PATH])
{
    if (!dir || !dir[0] || !outDir)
        return 0;
    wcsncpy(outDir, dir, MAX_PATH - 1);
    outDir[MAX_PATH - 1] = L'\0';

    WCHAR* slash = wcsrchr(outDir, L'\\');
    if (!slash)
        return 0;
    *slash = L'\0';
    return outDir[0] != L'\0';
}

static void gxy_try_add_dll_dir(HMODULE kernel32, gxy_AddDllDirectory_t addDllDirectory, const WCHAR* dir)
{
    if (!kernel32 || !addDllDirectory || !dir)
        return;
    if (!gxy_dir_exists_w(dir))
        return;
    addDllDirectory(dir);
}

static void gxy_setup_dll_search_path(void)
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32)
        return;

    gxy_SetDefaultDllDirectories_t setDefaultDllDirectories =
        (gxy_SetDefaultDllDirectories_t)GetProcAddress(kernel32, "SetDefaultDllDirectories");
    gxy_AddDllDirectory_t addDllDirectory =
        (gxy_AddDllDirectory_t)GetProcAddress(kernel32, "AddDllDirectory");

    WCHAR selfDir[MAX_PATH];
    if (!gxy_get_self_dir_w(selfDir))
        return;

    WCHAR parentDir[MAX_PATH];
    parentDir[0] = L'\0';
    gxy_parent_dir_w(selfDir, parentDir);

    WCHAR parentLibDir[MAX_PATH];
    parentLibDir[0] = L'\0';
    if (parentDir[0])
        _snwprintf(parentLibDir, MAX_PATH, L"%s\\lib", parentDir);

    WCHAR selfLibDir[MAX_PATH];
    selfLibDir[0] = L'\0';
    _snwprintf(selfLibDir, MAX_PATH, L"%s\\lib", selfDir);

    if (setDefaultDllDirectories && addDllDirectory)
    {
        setDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
        gxy_try_add_dll_dir(kernel32, addDllDirectory, selfDir);
        gxy_try_add_dll_dir(kernel32, addDllDirectory, selfLibDir);
        gxy_try_add_dll_dir(kernel32, addDllDirectory, parentDir);
        gxy_try_add_dll_dir(kernel32, addDllDirectory, parentLibDir);
        return;
    }

    SetDllDirectoryW(selfDir);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
    (void)hinstDLL;
    (void)lpReserved;
    if (fdwReason == DLL_PROCESS_ATTACH)
        gxy_setup_dll_search_path();
    return TRUE;
}

static HMODULE gxy_load_library_from_self_dir(const char* dllName)
{
    HMODULE self = NULL;
    if (!dllName)
        return NULL;
    if (strchr(dllName, '\\') || strchr(dllName, '/'))
        return NULL;

    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&gxy_load_library_from_self_dir,
            &self))
        return NULL;

    char selfPath[MAX_PATH];
    DWORD n = GetModuleFileNameA(self, selfPath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return NULL;

    char* slash = strrchr(selfPath, '\\');
    if (!slash)
        return NULL;
    *slash = '\0';

    char candidate[MAX_PATH];
    int wrote = snprintf(candidate, MAX_PATH, "%s\\%s", selfPath, dllName);
    if (wrote > 0 && wrote < MAX_PATH)
    {
        HMODULE h = LoadLibraryA(candidate);
        if (h)
            return h;
    }

    char* parent = strrchr(selfPath, '\\');
    if (!parent)
        return NULL;
    *parent = '\0';

    wrote = snprintf(candidate, MAX_PATH, "%s\\%s", selfPath, dllName);
    if (wrote > 0 && wrote < MAX_PATH)
        return LoadLibraryA(candidate);
    return NULL;
}

static FARPROC WINAPI gxy_delay_load_failure_hook(unsigned dliNotify, PDelayLoadInfo pdli)
{
    if (dliNotify == dliFailLoadLib && pdli && pdli->szDll)
    {
        HMODULE h = gxy_load_library_from_self_dir(pdli->szDll);
        return (FARPROC)h;
    }
    return NULL;
}

PfnDliHook __pfnDliFailureHook2 = gxy_delay_load_failure_hook;
#endif

static void string_adjust(const char* path, char* p)
{
    int i;
    strncpy(p, path, 260);

    for (i = 0; p[i]; i++) {
        if (p[i] >= 'A' && p[i] <= 'Z') p[i] += 'a' - 'A';
        else if (p[i] == '/') p[i] = '\\';
    }
}

static unsigned int g_tohash(const char* path)
{
    unsigned int m[70];

    //x86 - 32 bits - Registers
    unsigned int eax, ebx, ecx, edx, edi, esi;
    unsigned long long  num = 0;

    unsigned int v;
    unsigned int i;
    if (!path)
        return 0;
    string_adjust(path, (char*)m);

    for (i = 0; i < 256 / 4 && m[i]; i++);

    m[i++] = 0x9BE74448; //
    m[i++] = 0x66F42C48; //

    v = 0xF4FA8928; //

    edi = 0x7758B42B;
    esi = 0x37A8470E; //

    for (ecx = 0; ecx < i; ecx++)
    {
        ebx = 0x267B0B11; //
        v = (v << 1) | (v >> 0x1F);
        ebx ^= v;
        eax = m[ecx];
        esi ^= eax;
        edi ^= eax;
        edx = ebx;
        edx += edi;
        edx |= 0x02040801; // 
        edx &= 0xBFEF7FDF;//
        num = edx;
        num *= esi;
        eax = (unsigned int)num;
        edx = (unsigned int)(num >> 0x20);
        if (edx != 0)
            eax++;
        num = eax;
        num += edx;
        eax = (unsigned int)num;
        if (((unsigned int)(num >> 0x20)) != 0)
            eax++;
        edx = ebx;
        edx += esi;
        edx |= 0x00804021; //
        edx &= 0x7DFEFBFF; //
        esi = eax;
        num = edi;
        num *= edx;
        eax = (unsigned int)num;
        edx = (unsigned int)(num >> 0x20);
        num = edx;
        num += edx;
        edx = (unsigned int)num;
        if (((unsigned int)(num >> 0x20)) != 0)
            eax++;
        num = eax;
        num += edx;
        eax = (unsigned int)num;
        if (((unsigned int)(num >> 0x20)) != 0)
            eax += 2;
        edi = eax;
    }
    esi ^= edi;
    v = esi;
    return v;
}

static int lua_tohash(lua_State* L)
{
    lua_pushinteger(L, g_tohash(luaL_checkstring(L, 1)));
    return 1;
}

static int lua_mygxy_call(lua_State* L)
{
    lua_settop(L, 1);
    return 1;
}

MYGXY_API int luaopen_mygxy_hash(lua_State* L)
{
    lua_pushcfunction(L, lua_tohash);
    return 1;
}

int luaopen_mygxy_tcp(lua_State* L);
int luaopen_mygxy_map(lua_State* L);
int luaopen_mygxy_wdf(lua_State* L);
int luaopen_mygxy_fsb(lua_State* L);
int luaopen_mygxy_wpk(lua_State* L);
MYGXY_API int luaopen_mygxy(lua_State* L)
{
    init_lua_proxy();
    init_sdl_proxy();

    lua_createtable(L, 0, 6);

    lua_pushcfunction(L, luaopen_mygxy_tcp);
    lua_call(L, 0, 1);
    lua_setfield(L, -2, "Tcp");

    lua_pushcfunction(L, luaopen_mygxy_wdf);
    lua_call(L, 0, 1);
    lua_setfield(L, -2, "Wdf");

    lua_pushcfunction(L, luaopen_mygxy_wpk);
    lua_call(L, 0, 1);
    lua_setfield(L, -2, "Wpk");

    lua_pushcfunction(L, luaopen_mygxy_map);
    lua_call(L, 0, 1);
    lua_setfield(L, -2, "Map");

    lua_pushcfunction(L, luaopen_mygxy_fsb);
    lua_call(L, 0, 1);
    lua_setfield(L, -2, "Fsb");

    lua_pushcfunction(L, luaopen_mygxy_hash);
    lua_call(L, 0, 1);
    lua_setfield(L, -2, "Hash");

    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, lua_mygxy_call);
    lua_setfield(L, -2, "__call");
    lua_setmetatable(L, -2);

    return 1;
}
