#include "common.h"
#include "HP_HttpClient.h"

// Simple HTTP file download utility
// Uses HP_HttpClient internally for single-connection downloads

class HP_Download : public HP_Base, public CHttpClientListener
{
public:
	HP_Download(lua_State* L) :HP_Base(L) {
		m_Client = HP_Create_HttpClient(this);
	};

	~HP_Download() {
		HP_Destroy_HttpClient(m_Client);
	};

	virtual EnHandleResult OnConnect(ITcpClient* pSender, CONNID dwConnID) {
		return HR_OK;
	}

	virtual EnHandleResult OnReceive(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength) {
		return HR_OK;
	}

	virtual EnHandleResult OnSend(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength) {
		return HR_OK;
	}

	virtual EnHandleResult OnClose(ITcpClient* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode) {
		if (GetCallBack("OnClose"))
		{
			lua_pushinteger(L, dwConnID);
			lua_pushinteger(L, iErrorCode);
			lua_call(L, 3, 1);
			return GetResult();
		}
		return HR_OK;
	}

	virtual EnHttpParseResult OnMessageBegin(IHttpClient* pSender, CONNID dwConnID) {
		return HPR_OK;
	}

	virtual EnHttpParseResult OnStatusLine(IHttpClient* pSender, CONNID dwConnID, USHORT usStatusCode, LPCSTR lpszDesc) {
		m_StatusCode = usStatusCode;
		return HPR_OK;
	}

	virtual EnHttpParseResult OnHeader(IHttpClient* pSender, CONNID dwConnID, LPCSTR lpszName, LPCSTR lpszValue) {
		return HPR_OK;
	}

	virtual EnHttpParseResult OnHeadersComplete(IHttpClient* pSender, CONNID dwConnID) {
		return HPR_OK;
	}

	virtual EnHttpParseResult OnBody(IHttpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength) {
		if (GetCallBack("OnBody"))
		{
			lua_pushinteger(L, dwConnID);
			lua_pushlstring(L, (char*)pData, iLength);
			lua_call(L, 3, 1);
			return GetResult2();
		}
		return HPR_OK;
	}

	virtual EnHttpParseResult OnChunkHeader(IHttpClient* pSender, CONNID dwConnID, int iLength) {
		return HPR_OK;
	}

	virtual EnHttpParseResult OnChunkComplete(IHttpClient* pSender, CONNID dwConnID) {
		return HPR_OK;
	}

	virtual EnHttpParseResult OnMessageComplete(IHttpClient* pSender, CONNID dwConnID) {
		if (GetCallBack("OnComplete"))
		{
			lua_pushinteger(L, dwConnID);
			lua_pushinteger(L, m_StatusCode);
			lua_call(L, 3, 1);
			return GetResult2();
		}
		return HPR_OK;
	}

	virtual EnHttpParseResult OnUpgrade(IHttpClient* pSender, CONNID dwConnID, EnHttpUpgradeType enUpgradeType) {
		return HPR_OK;
	}

	virtual EnHttpParseResult OnParseError(IHttpClient* pSender, CONNID dwConnID, int iErrorCode, LPCSTR lpszErrorDesc) {
		if (GetCallBack("OnError"))
		{
			lua_pushinteger(L, dwConnID);
			lua_pushinteger(L, iErrorCode);
			lua_pushstring(L, lpszErrorDesc);
			lua_call(L, 4, 1);
			return GetResult2();
		}
		return HPR_OK;
	}

	IHttpClient* m_Client;
	USHORT m_StatusCode = 0;
};

static IHttpClient* GetDownloadClient(lua_State* L) {
	HP_Download* p = *(HP_Download**)luaL_checkudata(L, 1, "HP_Download");
	return p->m_Client;
}

static int Download_Start(lua_State* L) {
	HP_Download* p = *(HP_Download**)luaL_checkudata(L, 1, "HP_Download");
	IHttpClient* cli = p->m_Client;
	LPCSTR lpszHost = luaL_checkstring(L, 2);
	USHORT usPort = (USHORT)luaL_optinteger(L, 3, 80);
	lua_pushboolean(L, cli->Start(lpszHost, usPort, FALSE));
	return 1;
}

static int Download_SendRequest(lua_State* L) {
	IHttpClient* cli = GetDownloadClient(L);
	LPCSTR lpszMethod = luaL_optstring(L, 2, "GET");
	LPCSTR lpszPath = luaL_checkstring(L, 3);
	THeader* lpHeaders = nullptr;
	int iHeaderCount = 0;
	size_t iLength;
	const BYTE* pBody = (const BYTE*)luaL_optlstring(L, 5, nullptr, &iLength);
	if (lua_istable(L, 4)) {
		lua_pushnil(L);
		while (lua_next(L, 4)) {
			iHeaderCount++;
			lua_pop(L, 1);
		}
		if (iHeaderCount) {
			lpHeaders = new THeader[iHeaderCount];
			iHeaderCount = 0;
			lua_pushnil(L);
			while (lua_next(L, 4)) {
				lpHeaders[iHeaderCount].value = luaL_checkstring(L, -1);
				lpHeaders[iHeaderCount].name = luaL_checkstring(L, -2);
				iHeaderCount++;
				lua_pop(L, 1);
			}
		}
	}
	lua_pushboolean(L, cli->SendRequest(lpszMethod, lpszPath, lpHeaders, iHeaderCount, pBody, (int)iLength));
	if (lpHeaders)
		delete[]lpHeaders;
	return 1;
}

static int Download_Stop(lua_State* L) {
	IHttpClient* cli = GetDownloadClient(L);
	lua_pushboolean(L, cli->Stop());
	return 1;
}

static int Download_Wait(lua_State* L) {
	IHttpClient* cli = GetDownloadClient(L);
	lua_pushboolean(L, cli->Wait((DWORD)luaL_optinteger(L, 2, INFINITE)));
	return 1;
}

static int Download_GetStatusCode(lua_State* L) {
	IHttpClient* cli = GetDownloadClient(L);
	lua_pushinteger(L, cli->GetStatusCode());
	return 1;
}

static int Download_GetContentLength(lua_State* L) {
	IHttpClient* cli = GetDownloadClient(L);
	lua_pushinteger(L, cli->GetContentLength());
	return 1;
}

static int Download_HasStarted(lua_State* L) {
	IHttpClient* cli = GetDownloadClient(L);
	lua_pushboolean(L, cli->HasStarted());
	return 1;
}

static int Creat_HP_Download(lua_State* L) {
	HP_Download* p = new HP_Download(L);
	HP_Download** ud = (HP_Download**)lua_newuserdata(L, sizeof(HP_Download*));
	*ud = p;
	lua_pushlightuserdata(L, (ITcpClient*)p->m_Client);
	lua_setuservalue(L, -2);
	luaL_setmetatable(L, "HP_Download");
	return 1;
}

static int Destroy_HP_Download(lua_State* L) {
	HP_Download* ud = *(HP_Download**)luaL_checkudata(L, 1, "HP_Download");
	Download_Stop(L);
	delete ud;
	return 0;
}

extern "C"
#if defined(LUA_BUILD_AS_DLL)
LUAMOD_API
#endif
int luaopen_ghpsocket_download(lua_State * L)
{
	luaL_Reg methods[] = {
		{"Start",Download_Start},
		{"SendRequest",Download_SendRequest},
		{"Stop",Download_Stop},
		{"Wait",Download_Wait},
		{"GetStatusCode",Download_GetStatusCode},
		{"GetContentLength",Download_GetContentLength},
		{"HasStarted",Download_HasStarted},
		{NULL, NULL},
	};
	luaL_newmetatable(L, "HP_Download");
	luaL_newlib(L, methods);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, Destroy_HP_Download);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);

	lua_pushcfunction(L, Creat_HP_Download);
	return 1;
}
