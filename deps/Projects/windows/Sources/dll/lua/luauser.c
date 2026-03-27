#include "luauser.h"

void UTF8ToUnicode(const char* UTF8, WCHAR* UNI) {
    int len = MultiByteToWideChar(CP_UTF8, 0, UTF8, -1, NULL, 0);
    MultiByteToWideChar(CP_UTF8, 0, UTF8, -1, UNI, len);
}

void UnicodeToUTF8(const WCHAR* UNI, char* UTF8) {
    int len = WideCharToMultiByte(CP_UTF8, 0, UNI, -1, NULL, 0, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, UNI, -1, UTF8, len, NULL, NULL);
}

FILE* utf8_fopen(const char* _FileName, const char* _Mode) {
    WCHAR FileName[MAX_PATH];
    memset(FileName, 0, sizeof(FileName));
    UTF8ToUnicode(_FileName, FileName);
    WCHAR Mode[MAX_PATH];
    memset(Mode, 0, sizeof(Mode));
    UTF8ToUnicode(_Mode, Mode);
    return _wfopen(FileName, Mode);
}

FILE* utf8_popen(const char* _FileName, const char* _Mode) {
    WCHAR FileName[MAX_PATH];
    memset(FileName, 0, sizeof(FileName));
    UTF8ToUnicode(_FileName, FileName);
    WCHAR Mode[MAX_PATH];
    memset(Mode, 0, sizeof(Mode));
    UTF8ToUnicode(_Mode, Mode);
    return _wpopen(FileName, Mode);
}

FILE* utf8_freopen(const char* _FileName, const char* _Mode, FILE* _OldStream) {
    WCHAR FileName[MAX_PATH];
    memset(FileName, 0, sizeof(FileName));
    UTF8ToUnicode(_FileName, FileName);
    WCHAR Mode[MAX_PATH];
    memset(Mode, 0, sizeof(Mode));
    UTF8ToUnicode(_Mode, Mode);
    return _wfreopen(FileName, Mode, _OldStream);
}

int utf8_system(const char* _Command) {
    WCHAR cmd[4096];
    memset(cmd, 0, sizeof(cmd));
    UTF8ToUnicode(_Command, cmd);
    return _wsystem(cmd);
}

static char utf8[4096];
const char* utf8_getenv(const char* _VarName) {
    WCHAR name[MAX_PATH];
    memset(name, 0, sizeof(name));
    UTF8ToUnicode(_VarName, name);
    WCHAR* uni = _wgetenv(name);
    if (uni) {
        UnicodeToUTF8(uni, utf8);
        return utf8;
    }
    return NULL;
}

int utf8_remove(const char* _FileName) {
    WCHAR FileName[MAX_PATH];
    memset(FileName, 0, sizeof(FileName));
    UTF8ToUnicode(_FileName, FileName);
    return _wremove(FileName);
}

int utf8_rename(char const* _OldFileName, char const* _NewFileName) {
    WCHAR OldFileName[MAX_PATH];
    memset(OldFileName, 0, sizeof(OldFileName));
    UTF8ToUnicode(_OldFileName, OldFileName);
    WCHAR NewFileName[MAX_PATH];
    memset(NewFileName, 0, sizeof(NewFileName));
    UTF8ToUnicode(_NewFileName, NewFileName);
    return _wrename(OldFileName, NewFileName);
}

DWORD utf8_GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize)
{
    WCHAR buff[MAX_PATH];
    DWORD n = GetModuleFileNameW(hModule, buff, sizeof(buff) / sizeof(WCHAR));
    if (n <= nSize)
    {
        UnicodeToUTF8(buff, lpFilename);
        return n;
    }
    return 0;
}

HMODULE utf8_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    WCHAR FileName[MAX_PATH];
    memset(FileName, 0, sizeof(FileName));
    UTF8ToUnicode(lpLibFileName, FileName);
    return LoadLibraryExW(FileName, hFile, dwFlags);
}

DWORD utf8_FormatMessageA(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId, DWORD dwLanguageId, LPSTR lpBuffer, DWORD nSize, va_list* Arguments)
{
    WCHAR Buffer[MAX_PATH];
    DWORD r = FormatMessageW(dwFlags, lpSource, dwMessageId, dwLanguageId, Buffer, MAX_PATH, Arguments);
    UnicodeToUTF8(Buffer, lpBuffer);
    return r;
}