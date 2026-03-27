#include "HP_HttpAgent.h"

EnHandleResult HP_HttpAgent::OnPrepareConnect(ITcpAgent* pSender, CONNID dwConnID, SOCKET socket)
{
	if (GetCallBack("OnPrepareConnect"))
	{
		lua_pushinteger(L, dwConnID);
		lua_pushinteger(L, socket);
		lua_call(L, 3, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHandleResult HP_HttpAgent::OnConnect(ITcpAgent* pSender, CONNID dwConnID)
{
	if (GetCallBack("OnConnect"))
	{
		lua_pushinteger(L, dwConnID);
		lua_call(L, 2, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHandleResult HP_HttpAgent::OnReceive(ITcpAgent* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
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

EnHandleResult HP_HttpAgent::OnSend(ITcpAgent* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
{
	if (GetCallBack("OnSend"))
	{
		lua_pushinteger(L, dwConnID);
		lua_pushinteger(L, iLength);
		lua_call(L, 3, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHandleResult HP_HttpAgent::OnShutdown(ITcpAgent* pSender)
{
	if (GetCallBack("OnShutdown"))
	{
		lua_call(L, 1, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHandleResult HP_HttpAgent::OnClose(ITcpAgent* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode)
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

EnHttpParseResult HP_HttpAgent::OnMessageBegin(IHttpAgent* pSender, CONNID dwConnID)
{
	if (GetCallBack("OnMessageBegin"))
	{
		lua_pushinteger(L, dwConnID);
		lua_call(L, 2, 1);
		return GetResult2();
	}
	return HPR_OK;
}

EnHttpParseResult HP_HttpAgent::OnRequestLine(IHttpAgent* pSender, CONNID dwConnID, LPCSTR lpszMethod, LPCSTR lpszUrl)
{
	if (GetCallBack("OnRequestLine"))
	{
		lua_pushinteger(L, dwConnID);
		lua_pushstring(L, lpszMethod);
		lua_pushstring(L, lpszUrl);
		lua_call(L, 4, 1);
		return GetResult2();
	}
	return HPR_OK;
}

EnHttpParseResult HP_HttpAgent::OnStatusLine(IHttpAgent* pSender, CONNID dwConnID, USHORT usStatusCode, LPCSTR lpszDesc)
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

EnHttpParseResult HP_HttpAgent::OnHeader(IHttpAgent* pSender, CONNID dwConnID, LPCSTR lpszName, LPCSTR lpszValue)
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

EnHttpParseResult HP_HttpAgent::OnHeadersComplete(IHttpAgent* pSender, CONNID dwConnID)
{
	if (GetCallBack("OnHeadersComplete"))
	{
		lua_pushinteger(L, dwConnID);
		lua_call(L, 2, 1);
		return GetResult2();
	}
	return HPR_OK;
}

EnHttpParseResult HP_HttpAgent::OnBody(IHttpAgent* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
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

EnHttpParseResult HP_HttpAgent::OnChunkHeader(IHttpAgent* pSender, CONNID dwConnID, int iLength)
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

EnHttpParseResult HP_HttpAgent::OnChunkComplete(IHttpAgent* pSender, CONNID dwConnID)
{
	if (GetCallBack("OnChunkComplete"))
	{
		lua_pushinteger(L, dwConnID);
		lua_call(L, 2, 1);
		return GetResult2();
	}
	return HPR_OK;
}

EnHttpParseResult HP_HttpAgent::OnMessageComplete(IHttpAgent* pSender, CONNID dwConnID)
{
	if (GetCallBack("OnMessageComplete"))
	{
		lua_pushinteger(L, dwConnID);
		lua_call(L, 2, 1);
		return GetResult2();
	}
	return HPR_OK;
}

EnHttpParseResult HP_HttpAgent::OnUpgrade(IHttpAgent* pSender, CONNID dwConnID, EnHttpUpgradeType enUpgradeType)
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

EnHttpParseResult HP_HttpAgent::OnParseError(IHttpAgent* pSender, CONNID dwConnID, int iErrorCode, LPCSTR lpszErrorDesc)
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

static IHttpAgent* GetHttpAgent(lua_State* L) {
	HP_HttpAgent* p = *(HP_HttpAgent**)luaL_checkudata(L, 1, "HP_HttpAgent");
	return p->m_Agent;
}

static BOOL luaL_optboolean(lua_State* L, int arg, BOOL def) 
{
	if (lua_isnoneornil(L, arg)) {
		return def;
	}
	return lua_toboolean(L, arg);
}

// IHttpAgent methods
static int SendRequest(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	CONNID dwConnID = (CONNID)luaL_checkinteger(L, 2);
	LPCSTR lpszMethod = luaL_checkstring(L, 3);
	LPCSTR lpszPath = luaL_checkstring(L, 4);
	THeader* lpHeaders = nullptr;
	int iHeaderCount = 0;
	size_t iLength;
	const BYTE* pBody = (const BYTE*)luaL_optlstring(L, 6, nullptr, &iLength);
	if (lua_istable(L, 5)) {
		lua_pushnil(L);
		while (lua_next(L, 5)) {
			iHeaderCount++;
			lua_pop(L, 1);
		}
		if (iHeaderCount) {
			lpHeaders = new THeader[iHeaderCount];
			iHeaderCount = 0;
			lua_pushnil(L);
			while (lua_next(L, 5)) {
				lpHeaders[iHeaderCount].value = luaL_checkstring(L, -1);
				lpHeaders[iHeaderCount].name = luaL_checkstring(L, -2);
				iHeaderCount++;
				lua_pop(L, 1);
			}
		}
	}
	lua_pushboolean(L, agent->SendRequest(dwConnID, lpszMethod, lpszPath, lpHeaders, iHeaderCount, pBody, (int)iLength));
	if (lpHeaders)
		delete[]lpHeaders;
	return 1;
}

static int Start(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	lua_pushboolean(L, agent->Start(luaL_optstring(L, 2, nullptr), luaL_optboolean(L, 3, FALSE)));
	return 1;
}

static int Connect(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	CONNID ConnID;
	BOOL r = agent->Connect(luaL_checkstring(L, 2), (USHORT)luaL_checkinteger(L, 3), &ConnID, nullptr, (USHORT)luaL_optinteger(L, 4, 0), luaL_optstring(L, 5, nullptr));
	lua_pushboolean(L, r);
	lua_pushinteger(L, ConnID);
	return 2;
}

static int Stop(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	SDL_mutex* mutex = *(SDL_mutex**)lua_getextraspace(L);
	SDL_UnlockMutex(mutex);
	BOOL r = agent->Stop();
	SDL_LockMutex(mutex);
	lua_pushboolean(L, r);
	return 1;
}

static int Send(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	size_t len;
	BYTE* buf = (BYTE*)luaL_checklstring(L, 3, &len);
	lua_pushboolean(L, agent->Send((CONNID)luaL_checkinteger(L, 2), buf, (int)len, (int)luaL_optinteger(L, 4, 0)));
	return 1;
}

static int Disconnect(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	lua_pushboolean(L, agent->Disconnect((CONNID)luaL_checkinteger(L, 2), luaL_optboolean(L, 3, TRUE)));
	return 1;
}

static int Wait(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	lua_pushboolean(L, agent->Wait((DWORD)luaL_optinteger(L, 2, INFINITE)));
	return 1;
}

static int HasStarted(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	lua_pushboolean(L, agent->HasStarted());
	return 1;
}

static int GetState(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	switch (agent->GetState()) {
		CASE(SS_STARTING)
			CASE(SS_STARTED)
			CASE(SS_STOPPING)
			CASE(SS_STOPPED)
	}
	return 1;
}

static int GetConnectionCount(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	lua_pushinteger(L, agent->GetConnectionCount());
	return 1;
}

static int GetStatusCode(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	lua_pushinteger(L, agent->GetStatusCode((CONNID)luaL_checkinteger(L, 2)));
	return 1;
}

static int GetContentLength(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	lua_pushinteger(L, agent->GetContentLength((CONNID)luaL_checkinteger(L, 2)));
	return 1;
}

static int GetContentType(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	lua_pushstring(L, agent->GetContentType((CONNID)luaL_checkinteger(L, 2)));
	return 1;
}

static int GetHeader(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	LPCSTR lpszValue;
	if (agent->GetHeader((CONNID)luaL_checkinteger(L, 2), luaL_checkstring(L, 3), &lpszValue))
		lua_pushstring(L, lpszValue);
	else
		lua_pushnil(L);
	return 1;
}

static int GetAllHeaders(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	CONNID dwConnID = (CONNID)luaL_checkinteger(L, 2);
	DWORD dwCount;
	lua_createtable(L, 0, 0);
	if (agent->GetAllHeaders(dwConnID, nullptr, dwCount)) {
		THeader* lpHeaders = new THeader[dwCount];
		agent->GetAllHeaders(dwConnID, lpHeaders, dwCount);
		for (DWORD i = 0; i < dwCount; i++)
		{
			lua_pushstring(L, lpHeaders[i].value);
			lua_setfield(L, -2, lpHeaders[i].name);
		}
		delete[]lpHeaders;
	}
	return 1;
}

static int IsKeepAlive(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	lua_pushboolean(L, agent->IsKeepAlive((CONNID)luaL_checkinteger(L, 2)));
	return 1;
}

static int SetUseCookie(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	agent->SetUseCookie(lua_toboolean(L, 2));
	return 0;
}

static int IsUseCookie(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	lua_pushboolean(L, agent->IsUseCookie());
	return 1;
}

static int IsConnected(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	lua_pushboolean(L, agent->IsConnected((CONNID)luaL_checkinteger(L, 2)));
	return 1;
}

static int GetLastError(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	switch (agent->GetLastError()) {
		CASE(SE_OK)
			CASE(SE_ILLEGAL_STATE)
			CASE(SE_INVALID_PARAM)
			CASE(SE_SOCKET_CREATE)
			CASE(SE_SOCKET_BIND)
			CASE(SE_SOCKET_PREPARE)
			CASE(SE_SOCKET_LISTEN)
			CASE(SE_CP_CREATE)
			CASE(SE_WORKER_THREAD_CREATE)
			CASE(SE_DETECT_THREAD_CREATE)
			CASE(SE_SOCKE_ATTACH_TO_CP)
			CASE(SE_CONNECT_SERVER)
			CASE(SE_NETWORK)
			CASE(SE_DATA_PROC)
			CASE(SE_DATA_SEND)
			CASE(SE_SSL_ENV_NOT_READY)
	}
	return 1;
}

static int GetLastErrorDesc(lua_State* L) {
	IHttpAgent* agent = GetHttpAgent(L);
	lua_pushstring(L, agent->GetLastErrorDesc());
	return 1;
}

static int Creat_HP_HttpAgent(lua_State* L) {
	HP_HttpAgent* p = new HP_HttpAgent(L);
	HP_HttpAgent** ud = (HP_HttpAgent**)lua_newuserdata(L, sizeof(HP_HttpAgent*));
	*ud = p;
	luaL_setmetatable(L, "HP_HttpAgent");
	return 1;
}

static int Destroy_HP_HttpAgent(lua_State* L) {
	HP_HttpAgent* ud = *(HP_HttpAgent**)luaL_checkudata(L, 1, "HP_HttpAgent");
	Stop(L);
	delete ud;
	return 0;
}

extern "C"
#if defined(LUA_BUILD_AS_DLL)
LUAMOD_API
#endif
int luaopen_ghpsocket_httpagent(lua_State * L)
{
	luaL_Reg methods[] = {
		{"SendRequest",SendRequest},
		{"SetUseCookie",SetUseCookie},
		{"IsUseCookie",IsUseCookie},
		{"Start",Start},
		{"Connect",Connect},
		{"Stop",Stop},
		{"Send",Send},
		{"Disconnect",Disconnect},
		{"Wait",Wait},
		{"HasStarted",HasStarted},
		{"GetState",GetState},
		{"GetConnectionCount",GetConnectionCount},
		{"GetStatusCode",GetStatusCode},
		{"GetContentLength",GetContentLength},
		{"GetContentType",GetContentType},
		{"GetHeader",GetHeader},
		{"GetAllHeaders",GetAllHeaders},
		{"IsKeepAlive",IsKeepAlive},
		{"IsConnected",IsConnected},
		{"GetLastError",GetLastError},
		{"GetLastErrorDesc",GetLastErrorDesc},
		{NULL, NULL},
	};
	luaL_newmetatable(L, "HP_HttpAgent");
	luaL_newlib(L, methods);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, Destroy_HP_HttpAgent);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);

	lua_pushcfunction(L, Creat_HP_HttpAgent);
	return 1;
}
