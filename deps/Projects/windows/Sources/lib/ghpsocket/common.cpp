#include "common.h"

static int GetHPSocketVersion(lua_State* L) {
	lua_pushinteger(L, HP_GetHPSocketVersion());
	return 1;
}

static int GetSocketErrorDesc(lua_State* L) {
	EnSocketError enCode = (EnSocketError)luaL_checkinteger(L, 1);
	lua_pushstring(L, HP_GetSocketErrorDesc(enCode));
	return 1;
}

static int GetLastError(lua_State* L) {
	lua_pushinteger(L, SYS_GetLastError());
	return 1;
}
#if defined(_WIN32)
static int WSAGetLastError(lua_State* L) {
	lua_pushinteger(L, SYS_WSAGetLastError());
	return 1;
}
#endif//_WIN32
//
//static int SetSocketOption(lua_State* L){
//	SOCKET sock = luaL_checkinteger(L,1);
//	int level = luaL_checkinteger(L,2);
//	int name = luaL_checkinteger(L,3);
//	LPVOID val = luaL_checkinteger(L,1);
//	int len = luaL_checkinteger(L,5);
//	lua_pushinteger(L,SYS_SetSocketOption());
//	return 1;
//}
//
//static int GetSocketOption(lua_State* L){
//	SOCKET sock = luaL_checkinteger(L,1);
//	int level = luaL_checkinteger(L,2);
//	int name = luaL_checkinteger(L,3);
//	LPVOID val = luaL_checkinteger(L,1);
//	int len = luaL_checkinteger(L,5);
//	lua_pushinteger(L,SYS_GetSocketOption());
//	return 1;
//}
//
//SYS_IoctlSocket
//SYS_WSAIoctl

//SYS_SSO_NoDelay
//SYS_SSO_DontLinger
//SYS_SSO_Linger
//SYS_SSO_RecvBuffSize
//SYS_SSO_SendBuffSize
//SYS_SSO_RecvTimeOut
//SYS_SSO_SendTimeOut
//SYS_SSO_ReuseAddress
//SYS_SSO_ExclusiveAddressUse
static int GetSocketLocalAddress(lua_State* L) {
	SOCKET sock = (SOCKET)luaL_checkinteger(L, 1);
	TCHAR lpszAddress[16] = { 0 };//INET_ADDRSTRLEN
	int iAddressLen = 16;
	USHORT usPort;

	SYS_GetSocketLocalAddress(sock, lpszAddress, iAddressLen, usPort);
	lua_pushstring(L, lpszAddress);
	lua_pushinteger(L, usPort);
	return 2;
}

static int GetSocketRemoteAddress(lua_State* L) {
	SOCKET sock = (SOCKET)luaL_checkinteger(L, 1);
	TCHAR lpszAddress[16] = { 0 };//INET_ADDRSTRLEN
	int iAddressLen = 16;
	USHORT usPort;

	SYS_GetSocketRemoteAddress(sock, lpszAddress, iAddressLen, usPort);
	lua_pushstring(L, lpszAddress);
	lua_pushinteger(L, usPort);
	return 2;
}

static int EnumHostIPAddresses(lua_State* L) {
	LPCTSTR lpszHost = luaL_optstring(L, 1, "");
	EnIPAddrType enType = (EnIPAddrType)luaL_optinteger(L, 2, IPT_IPV4);
	LPTIPAddr* lppIPAddr = nullptr;
	int iIPAddrCount = 0;
	SYS_EnumHostIPAddresses(lpszHost, enType, &lppIPAddr, iIPAddrCount);
	lua_createtable(L, iIPAddrCount, 0);
	for (int n = 0; n < iIPAddrCount; n++)
	{
		lua_pushstring(L, lppIPAddr[n]->address);
		lua_seti(L, -2, n + 1);
	}
	SYS_FreeHostIPAddresses(lppIPAddr);
	return 1;
}

static int IsIPAddress(lua_State* L) {
	LPCTSTR lpszAddress = luaL_checkstring(L, 1);
	lua_pushboolean(L, SYS_IsIPAddress(lpszAddress));
	return 1;
}

static int GetIPAddress(lua_State* L) {
	LPCTSTR lpszHost = luaL_checkstring(L, 1);
	TCHAR lpszIP[46] = { 0 };//INET6_ADDRSTRLEN
	int iIPLenth = 46;
	EnIPAddrType enType;

	SYS_GetIPAddress(lpszHost, lpszIP, iIPLenth, enType);
	lua_pushstring(L, lpszIP);
	lua_pushinteger(L, enType);
	return 2;
}
//SYS_NToH64
//SYS_HToN64
//SYS_SwapEndian16
//SYS_SwapEndian32
//SYS_IsLittleEndian

//SYS_CodePageToUnicode
//SYS_UnicodeToCodePage
//SYS_GbkToUnicode
//SYS_UnicodeToGbk
//SYS_Utf8ToUnicode
//SYS_UnicodeToUtf8
//SYS_GbkToUtf8
//SYS_Utf8ToGbk

//SYS_Compress
//SYS_CompressEx
//SYS_Uncompress
//SYS_UncompressEx
//SYS_GuessCompressBound

//SYS_GZipCompress
//SYS_GZipUncompress
//SYS_GZipGuessUncompressBound

//SYS_GuessBase64EncodeBound
//SYS_GuessBase64DecodeBound
//SYS_Base64Encode
//SYS_Base64Decode

//SYS_GuessUrlEncodeBound
//SYS_GuessUrlDecodeBound
//SYS_UrlEncode
//SYS_UrlDecode
extern "C"
#if defined(LUA_BUILD_AS_DLL)
LUAMOD_API
#endif
int luaopen_ghpsocket_hpsocket(lua_State * L)
{
	luaL_Reg methods[] = {
		{"GetHPSocketVersion",GetHPSocketVersion},
		{"GetSocketErrorDesc",GetSocketErrorDesc},
		{"GetLastError",GetLastError},

		{"GetSocketLocalAddress",GetSocketLocalAddress},
		{"GetSocketRemoteAddress",GetSocketRemoteAddress},
		{"EnumHostIPAddresses",EnumHostIPAddresses},
		{"IsIPAddress",IsIPAddress},
		{"GetIPAddress",GetIPAddress},

		{NULL, NULL},
	};
	luaL_newlib(L, methods);

	return 1;
}