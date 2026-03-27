/*
** LuaFileSystem
** Copyright Kepler Project 2003 - 2020
** (http://keplerproject.github.io/luafilesystem)
*/

/* Define 'chdir' for systems that do not implement it */
#ifdef NO_CHDIR
#define chdir(p)	(-1)
#define chdir_error	"Function 'chdir' not provided by system"
#else
#define chdir_error	strerror(errno)
#endif

#ifdef _WIN32
//#define chdir(p) (_chdir(p))
//#define getcwd(d, s) (_getcwd(d, s))
//#define rmdir(p) (_rmdir(p))
#define LFS_EXPORT __declspec (dllexport)
#ifndef fileno
#define fileno(f) (_fileno(f))
#endif
#else
#define LFS_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

    LUALIB_API int luaopen_lfs(lua_State* L);

#ifdef __cplusplus
}
#endif

#ifdef _WIN32
#include <stdio.h>
#include <wchar.h>
#include <windows.h>

struct utf8_finddata
{
    struct _wfinddata_t data;
    char name[260];
};
#undef MAX_PATH
#define MAX_PATH          1024
int utf8_stat(const char* _FileName, struct _stat64* _Stat);
int utf8_chdir(const char* _Path);
int utf8_mkdir(const char* _Path);
int utf8_rmdir(const char* _Path);
char* utf8_getcwd(char* _DstBuf, size_t _SizeInBytes);
intptr_t utf8_findfirst(const char* _FileName, struct utf8_finddata* _FindData);
int utf8_findnext(intptr_t _FindHandle, struct utf8_finddata* _FindData);
int utf8_utime(const char* _FileName, const struct utimbuf* _Time);

#define STAT_FUNC utf8_stat
#define chdir utf8_chdir
#define getcwd utf8_getcwd
#define lfs_mkdir utf8_mkdir
#define rmdir utf8_rmdir
#define utime utf8_utime

#undef _findfirst
#define _findfirst utf8_findfirst

#undef _findnext
#define _findnext utf8_findnext

BOOLEAN utf8_CreateSymbolicLink(LPCSTR lpSymlinkFileName, LPCSTR lpTargetFileName, DWORD dwFlags);
BOOLEAN utf8_CreateHardLink(LPCSTR lpFileName, LPCSTR lpExistingFileName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
HANDLE utf8_CreateFile(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
DWORD utf8_GetFinalPathNameByHandle(HANDLE hFile, LPSTR lpszFilePath, DWORD cchFilePath, DWORD dwFlags);
BOOL utf8_GetFileAttributesExW(LPCSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation);


#undef CreateSymbolicLink
#define CreateSymbolicLink utf8_CreateSymbolicLink

#undef CreateHardLink
#define CreateHardLink utf8_CreateHardLink

#undef CreateFile
#define CreateFile utf8_CreateFile

#undef GetFinalPathNameByHandle
#define GetFinalPathNameByHandle utf8_GetFinalPathNameByHandle

#undef GetFileAttributesEx
#define GetFileAttributesEx utf8_GetFileAttributesExW


#endif