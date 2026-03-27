#include "HP_HttpClient.h"
#include "HP_TcpClient.h"

EnHandleResult HP_HttpClient::OnConnect(ITcpClient* pSender, CONNID dwConnID)
{
	if (GetCallBack("OnConnect"))
	{
		lua_pushinteger(L, dwConnID);
		lua_call(L, 2, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHandleResult HP_HttpClient::OnReceive(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
{
	if (GetCallBack("OnReceive"))
	{
		lua_pushinteger(L, dwConnID);
		lua_pushlstring(L, (const char*)pData, iLength);
		lua_call(L, 3, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHandleResult HP_HttpClient::OnSend(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
{
	if (GetCallBack("OnSend"))
	{
		lua_pushinteger(L, dwConnID);
		//lua_pushlightuserdata(L,(void*)pData);
		lua_pushinteger(L, iLength);
		lua_call(L, 3, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHandleResult HP_HttpClient::OnClose(ITcpClient* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode)
{
	if (GetCallBack("OnClose"))
	{
		lua_pushinteger(L, dwConnID);
		switch (enOperation) {
			CASE(SO_UNKNOWN)
				CASE(SO_ACCEPT)
				CASE(SO_CONNECT)
				CASE(SO_SEND)
				CASE(SO_RECEIVE)
				CASE(SO_CLOSE)
		}
		lua_pushinteger(L, iErrorCode);
		lua_call(L, 4, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHttpParseResult HP_HttpClient::OnMessageBegin(IHttpClient* pSender, CONNID dwConnID)
{
	if (GetCallBack("OnMessageBegin"))
	{
		lua_pushinteger(L, dwConnID);
		lua_call(L, 2, 1);
		return GetResult2();
	}
	return HPR_OK;
}

EnHttpParseResult HP_HttpClient::OnStatusLine(IHttpClient* pSender, CONNID dwConnID, USHORT usStatusCode, LPCSTR lpszDesc)
{
	if (GetCallBack("OnStatusLine"))
	{
		lua_pushinteger(L, dwConnID);
		lua_pushinteger(L, usStatusCode);
		lua_pushstring(L, lpszDesc);
		lua_call(L, 4, 1);
		return GetResult2();
	}
	return HPR_OK;
}

EnHttpParseResult HP_HttpClient::OnHeader(IHttpClient* pSender, CONNID dwConnID, LPCSTR lpszName, LPCSTR lpszValue)
{
	if (GetCallBack("OnHeader"))
	{
		lua_pushinteger(L, dwConnID);
		lua_pushstring(L, lpszName);
		lua_pushstring(L, lpszValue);
		lua_call(L, 4, 1);
		return GetResult2();
	}
	return HPR_OK;
}

EnHttpParseResult HP_HttpClient::OnHeadersComplete(IHttpClient* pSender, CONNID dwConnID)
{
	if (GetCallBack("OnHeadersComplete"))
	{
		lua_pushinteger(L, dwConnID);
		lua_call(L, 2, 1);
		return GetResult2();
	}
	return HPR_OK;
}

EnHttpParseResult HP_HttpClient::OnBody(IHttpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
{
	if (GetCallBack("OnBody"))
	{
		lua_pushinteger(L, dwConnID);
		lua_pushlstring(L, (char*)pData, iLength);
		lua_call(L, 3, 1);
		return GetResult2();
	}
	return HPR_OK;
}

EnHttpParseResult HP_HttpClient::OnChunkHeader(IHttpClient* pSender, CONNID dwConnID, int iLength)
{
	if (GetCallBack("OnChunkHeader"))
	{
		lua_pushinteger(L, dwConnID);
		lua_pushinteger(L, iLength);
		lua_call(L, 3, 1);
		return GetResult2();
	}
	return HPR_OK;
}

EnHttpParseResult HP_HttpClient::OnChunkComplete(IHttpClient* pSender, CONNID dwConnID)
{
	if (GetCallBack("OnChunkComplete"))
	{
		lua_pushinteger(L, dwConnID);
		lua_call(L, 2, 1);
		return GetResult2();
	}
	return HPR_OK;
}

EnHttpParseResult HP_HttpClient::OnMessageComplete(IHttpClient* pSender, CONNID dwConnID)
{
	if (GetCallBack("OnMessageComplete"))
	{
		lua_pushinteger(L, dwConnID);
		lua_call(L, 2, 1);
		return GetResult2();
	}
	return HPR_OK;
}

EnHttpParseResult HP_HttpClient::OnUpgrade(IHttpClient* pSender, CONNID dwConnID, EnHttpUpgradeType enUpgradeType)
{
	if (GetCallBack("OnUpgrade"))
	{
		lua_pushinteger(L, dwConnID);
		lua_pushinteger(L, enUpgradeType);
		lua_call(L, 3, 1);
		return GetResult2();
	}
	return HPR_OK;
}

EnHttpParseResult HP_HttpClient::OnParseError(IHttpClient* pSender, CONNID dwConnID, int iErrorCode, LPCSTR lpszErrorDesc)
{
	if (GetCallBack("OnParseError"))
	{
		lua_pushinteger(L, dwConnID);
		lua_pushinteger(L, iErrorCode);
		lua_pushstring(L, lpszErrorDesc);
		lua_call(L, 4, 1);
		return GetResult2();
	}
	return HPR_OK;
}


static IHttpClient* GetHttpClient(lua_State* L) {
	HP_HttpClient* p = *(HP_HttpClient**)luaL_checkudata(L, 1, "HP_HttpClient");
	return p->m_Client;
}

//IHttpClient
static int SendRequest(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	LPCSTR lpszMethod = luaL_checkstring(L, 2);
	LPCSTR lpszPath = luaL_checkstring(L, 3);
	THeader* lpHeaders = nullptr;
	int iHeaderCount = 0;
	size_t iLength;
	const BYTE* pBody = (const BYTE*)luaL_optlstring(L, 5, nullptr, &iLength);
	if (lua_istable(L, 4)) {
		lua_pushnil(L);  /* start 'next' loop */
		while (lua_next(L, 4)) {  /* for each pair in table */
			iHeaderCount++;
			lua_pop(L, 1);  /* remove value */
		}
		if (iHeaderCount) {
			lpHeaders = new THeader[iHeaderCount];
			iHeaderCount = 0;
			lua_pushnil(L);  /* start 'next' loop */
			while (lua_next(L, 4)) {  /* for each pair in table */
				lpHeaders[iHeaderCount].value = luaL_checkstring(L, -1);
				lpHeaders[iHeaderCount].name = luaL_checkstring(L, -2);
				iHeaderCount++;
				lua_pop(L, 1);  /* remove value */
			}
		}
	}
	lua_pushboolean(L, cli->SendRequest(lpszMethod, lpszPath, lpHeaders, iHeaderCount, pBody, (int)iLength));
	if (lpHeaders)
		delete[]lpHeaders;
	return 1;
}
//SendLocalFile

static int SetUseCookie(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	cli->SetUseCookie(lua_toboolean(L, 2));
	return 0;
}

static int IsUseCookie(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	lua_pushboolean(L, cli->IsUseCookie());
	return 1;
}
//IHttp
//SendWSMessage

static int StartHttp(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	lua_pushboolean(L, cli->StartHttp());
	return 1;
}

static int SendChunkData(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	size_t iLength;
	const BYTE* pData = (const BYTE*)luaL_checklstring(L, 2, &iLength);
	lua_pushboolean(L, cli->SendChunkData(pData, (int)iLength, luaL_optstring(L, 3, nullptr)));
	return 1;
}

static int SetLocalVersion(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	cli->SetLocalVersion((EnHttpVersion)luaL_checkinteger(L, 2));
	return 0;
}

static int GetLocalVersion(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	lua_pushinteger(L, cli->GetLocalVersion());
	return 1;
}

static int IsUpgrade(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	lua_pushboolean(L, cli->IsUpgrade());
	return 1;
}

static int IsKeepAlive(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	lua_pushboolean(L, cli->IsKeepAlive());
	return 1;
}

static int GetVersion(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	lua_pushinteger(L, cli->GetVersion());
	return 1;
}

static int GetContentLength(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	lua_pushinteger(L, cli->GetContentLength());
	return 1;
}

static int GetContentType(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	lua_pushstring(L, cli->GetContentType());
	return 1;
}

static int GetContentEncoding(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	lua_pushstring(L, cli->GetContentEncoding());
	return 1;
}

static int GetTransferEncoding(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	lua_pushstring(L, cli->GetTransferEncoding());
	return 1;
}

static int GetUpgradeType(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	lua_pushinteger(L, cli->GetUpgradeType());
	return 1;
}

static int GetParseErrorCode(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	LPCSTR lpszErrorDesc;
	lua_pushinteger(L, cli->GetParseErrorCode(&lpszErrorDesc));
	lua_pushstring(L, lpszErrorDesc);
	return 2;
}

static int GetStatusCode(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	lua_pushinteger(L, cli->GetStatusCode());
	return 1;
}

static int GetHeader(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	LPCSTR lpszValue;
	if (cli->GetHeader(luaL_checkstring(L, 2), &lpszValue))
		lua_pushstring(L, (const char*)lpszValue);
	else
		lua_pushnil(L);
	return 1;
}

static int GetHeaders(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	LPCSTR lpszName = luaL_checkstring(L, 2);
	DWORD dwCount;
	lua_createtable(L, 0, 0);
	if (cli->GetHeaders(lpszName, nullptr, dwCount)) {
		LPCSTR* lpszValue = new LPCSTR[dwCount];
		cli->GetHeaders(lpszName, lpszValue, dwCount);
		for (DWORD i = 0; i < dwCount; i++)
		{
			lua_pushstring(L, lpszValue[i]);
			lua_seti(L, -2, i + 1);
		}
		delete[]lpszValue;
	}
	return 1;
}

static int GetAllHeaders(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	DWORD dwCount;
	lua_createtable(L, 0, 0);
	if (cli->GetAllHeaders(nullptr, dwCount)) {
		THeader* lpHeaders = new THeader[dwCount];
		cli->GetAllHeaders(lpHeaders, dwCount);
		for (DWORD i = 0; i < dwCount; i++)
		{
			lua_pushstring(L, lpHeaders[i].value);
			lua_setfield(L, -2, lpHeaders[i].name);
		}
		delete[]lpHeaders;
	}
	return 1;
}

static int GetAllHeaderNames(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	DWORD dwCount;
	lua_createtable(L, 0, 0);
	if (cli->GetAllHeaderNames(nullptr, dwCount)) {
		LPCSTR* lpszName = new LPCSTR[dwCount];
		cli->GetAllHeaderNames(lpszName, dwCount);
		for (DWORD i = 0; i < dwCount; i++)
		{
			lua_pushstring(L, lpszName[i]);
			lua_seti(L, -2, i + 1);
		}
		delete[]lpszName;
	}
	return 1;
}

static int GetCookie(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	LPCSTR lpszValue;
	if (cli->GetCookie(luaL_checkstring(L, 2), &lpszValue))
		lua_pushstring(L, (const char*)lpszValue);
	else
		lua_pushnil(L);
	return 1;
}

static int GetAllCookies(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	DWORD dwCount;
	lua_createtable(L, 0, 0);
	if (cli->GetAllCookies(nullptr, dwCount)) {
		TCookie* lpCookies = new TCookie[dwCount];
		cli->GetAllHeaders(lpCookies, dwCount);
		for (DWORD i = 0; i < dwCount; i++)
		{
			lua_pushstring(L, lpCookies[i].value);
			lua_setfield(L, -2, lpCookies[i].name);
		}
		delete[]lpCookies;
	}
	return 1;
}
//GetWSMessageState

static int SetHttpAutoStart(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	cli->SetHttpAutoStart(lua_toboolean(L, 2));
	return 0;
}

static int IsHttpAutoStart(lua_State* L) {
	IHttpClient* cli = GetHttpClient(L);
	lua_pushboolean(L, cli->IsHttpAutoStart());
	return 1;
}

//
static int Creat_HP_HttpClient(lua_State* L) {
	HP_HttpClient* p = new HP_HttpClient(L);
	HP_HttpClient** ud = (HP_HttpClient**)lua_newuserdata(L, sizeof(HP_HttpClient*));
	*ud = p;
	lua_pushlightuserdata(L, (ITcpClient*)p->m_Client);
	lua_setuservalue(L, -2);
	luaL_setmetatable(L, "HP_HttpClient");
	return 1;
}

static int Destroy_HP_HttpClient(lua_State* L) {
	HP_HttpClient* ud = *(HP_HttpClient**)luaL_checkudata(L, 1, "HP_HttpClient");
	ClientStop(L);
	delete ud;
	return 0;
}

extern "C"
#if defined(LUA_BUILD_AS_DLL)
LUAMOD_API
#endif
int luaopen_ghpsocket_httpclient(lua_State * L)
{
	luaL_Reg methods[] = {
		{"SendRequest",SendRequest},
		//{"SendLocalFile",SendLocalFile},
		{"SetUseCookie",SetUseCookie},
		{"IsUseCookie",IsUseCookie},
		//{"SendWSMessage",SendWSMessage},
		{"StartHttp",StartHttp},
		//{"SendChunkData",SendChunkData},
		{"SetLocalVersion",SetLocalVersion},
		{"GetLocalVersion",GetLocalVersion},
		{"IsUpgrade",IsUpgrade},
		{"IsKeepAlive",IsKeepAlive},
		{"GetVersion",GetVersion},
		{"GetContentLength",GetContentLength},
		{"GetContentType",GetContentType},
		{"GetContentEncoding",GetContentEncoding},
		{"GetTransferEncoding",GetTransferEncoding},
		{"GetUpgradeType",GetUpgradeType},
		{"GetParseErrorCode",GetParseErrorCode},
		{"GetStatusCode",GetStatusCode},
		{"GetHeader",GetHeader},
		{"GetHeaders",GetHeaders},
		{"GetAllHeaders",GetAllHeaders},
		{"GetAllHeaderNames",GetAllHeaderNames},
		{"GetCookie",GetCookie},
		{"GetAllCookies",GetAllCookies},
		//{"GetWSMessageState",GetWSMessageState},
		{"SetHttpAutoStart",SetHttpAutoStart},
		{"IsHttpAutoStart",IsHttpAutoStart},

		{"SendSmallFile",ClientSendSmallFile},
		{"SetSocketBufferSize",ClientSetSocketBufferSize},
		{"SetKeepAliveTime",ClientSetKeepAliveTime},
		{"SetKeepAliveInterval",ClientSetKeepAliveInterval},
		{"GetSocketBufferSize",ClientGetSocketBufferSize},
		{"GetKeepAliveTime",ClientGetKeepAliveTime},
		{"GetKeepAliveInterval",ClientGetKeepAliveInterval},
		{"Start",ClientStart},
		{"Stop",ClientStop},
		{"Send",ClientSend},
		//{"SendPackets",SendPackets},
		{"PauseReceive",ClientPauseReceive},
		{"Wait",ClientWait},
		//{"SetExtra",SetExtra},
		//{"GetExtra",GetExtra},
		{"IsSecure",ClientIsSecure},
		{"HasStarted",ClientHasStarted},
		{"GetState",ClientGetState},
		{"GetLastError",ClientGetLastError},
		{"GetLastErrorDesc",ClientGetLastErrorDesc},
		{"GetConnectionID",ClientGetConnectionID},
		{"GetLocalAddress",ClientGetLocalAddress},
		{"GetRemoteHost",ClientGetRemoteHost},
		{"GetPendingDataLength",ClientGetPendingDataLength},
		{"IsPauseReceive",ClientIsPauseReceive},
		{"IsConnected",ClientIsConnected},
		{"SetReuseAddressPolicy",ClientSetReuseAddressPolicy},
		{"SetFreeBufferPoolSize",ClientSetFreeBufferPoolSize},
		{"SetFreeBufferPoolHold",ClientSetFreeBufferPoolHold},
		{"GetReuseAddressPolicy",ClientGetReuseAddressPolicy},
		{"GetFreeBufferPoolSize",ClientGetFreeBufferPoolSize},
		{"GetFreeBufferPoolHold",ClientGetFreeBufferPoolHold},
		{NULL, NULL},
	};
#ifdef _DEBUG
    setvbuf(stdout, NULL, _IONBF, 0);
#endif // DEBUG
	luaL_newmetatable(L, "HP_HttpClient");
	luaL_newlib(L, methods);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, Destroy_HP_HttpClient);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);

	lua_pushcfunction(L, Creat_HP_HttpClient);
	return 1;
}