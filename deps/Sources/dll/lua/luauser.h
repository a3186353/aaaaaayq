#ifndef gge_h
#define gge_h
#if defined(_WIN32)

#include <stdio.h>
#include <wchar.h>
#include <windows.h>

#undef LUA_IDSIZE
#define LUA_IDSIZE 260

#undef MAX_PATH
#define MAX_PATH 1024

FILE* utf8_fopen(char const* _FileName, char const* _Mode);
FILE* utf8_popen(char const* _FileName, char const* _Mode);
FILE* utf8_freopen(const char* _FileName, const char* _Mode, FILE* _OldStream);
int utf8_system(const char* _Command);
const char* utf8_getenv(char const* _VarName);
int utf8_remove(const char* _FileName);
int utf8_rename(char const* _OldFileName, char const* _NewFileName);
DWORD utf8_GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize);
HMODULE utf8_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
DWORD utf8_FormatMessageA(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId, DWORD dwLanguageId, LPSTR lpBuffer, DWORD nSize, va_list* Arguments);

#define fopen	utf8_fopen
#define _popen	utf8_popen
#define freopen utf8_freopen
#define system utf8_system//execute
#define getenv utf8_getenv
#define remove utf8_remove
#define rename utf8_rename
#define GetModuleFileNameA utf8_GetModuleFileNameA
#define LoadLibraryExA utf8_LoadLibraryExA
#define FormatMessageA utf8_FormatMessageA
#endif
#endif