#include "HP_TcpServer.h"
#include "HP_PackServer.h"

EnHandleResult HP_PackServer::OnPrepareListen(ITcpServer* pSender, SOCKET soListen)
{
	if (GetCallBack("OnPrepareListen"))
	{
		lua_pushinteger(L, soListen);
		lua_call(L, 2, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHandleResult HP_PackServer::OnAccept(ITcpServer* pSender, CONNID dwConnID, UINT_PTR soClient)
{
	if (GetCallBack("OnAccept"))
	{
		lua_pushinteger(L, dwConnID);
		lua_pushinteger(L, soClient);
		lua_call(L, 3, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHandleResult HP_PackServer::OnReceive(ITcpServer* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
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

EnHandleResult HP_PackServer::OnSend(ITcpServer* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
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

EnHandleResult HP_PackServer::OnShutdown(ITcpServer* pSender)
{
	if (GetCallBack("OnShutdown"))
	{
		lua_call(L, 1, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHandleResult HP_PackServer::OnClose(ITcpServer* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode)
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


static ITcpPackServer* GetPackServer(lua_State* L) 
{
	HP_PackServer* p = *(HP_PackServer**)luaL_checkudata(L, 1, "HP_PackServer");
	return p->m_Server;
}

//IPackSocket
static int SetMaxPackSize(lua_State* L) 
{
	ITcpPackServer* Svr = GetPackServer(L);
	Svr->SetMaxPackSize((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int SetPackHeaderFlag(lua_State* L) 
{
	ITcpPackServer* Svr = GetPackServer(L);
	Svr->SetPackHeaderFlag((USHORT)luaL_checkinteger(L, 2));
	return 0;
}

static int GetMaxPackSize(lua_State* L) 
{
	ITcpPackServer* Svr = GetPackServer(L);
	lua_pushinteger(L, Svr->GetMaxPackSize());
	return 1;
}

static int GetPackHeaderFlag(lua_State* L) 
{
	ITcpPackServer* Svr = GetPackServer(L);
	lua_pushinteger(L, Svr->GetPackHeaderFlag());
	return 1;
}



static int Creat_HP_PackServer(lua_State* L)
{
	HP_PackServer* p = new HP_PackServer(L);
	HP_PackServer** ud = (HP_PackServer**)lua_newuserdata(L, sizeof(HP_PackServer*));
	*ud = p;
	lua_pushlightuserdata(L, (ITcpServer*)p->m_Server);
	lua_setuservalue(L, -2);
	luaL_setmetatable(L, "HP_PackServer");
	return 1;
}

static int Destroy_HP_PackServer(lua_State* L)
{
	HP_PackServer* ud = *(HP_PackServer**)luaL_checkudata(L, 1, "HP_PackServer");
	ServerStop(L);
	delete ud;
	return 0;
}

extern "C"
#if defined(LUA_BUILD_AS_DLL)
LUAMOD_API
#endif
int luaopen_ghpsocket_packserver(lua_State * L)
{
	luaL_Reg methods[] = {
		{"SetMaxPackSize",SetMaxPackSize},
		{"SetPackHeaderFlag",SetPackHeaderFlag},
		{"GetMaxPackSize",GetMaxPackSize},
		{"GetPackHeaderFlag",GetPackHeaderFlag},
		{"SendSmallFile",ServerSendSmallFile},

		{"SetAcceptSocketCount",ServerSetAcceptSocketCount},
		{"SetSocketBufferSize",ServerSetSocketBufferSize},
		{"SetSocketListenQueue",ServerSetSocketListenQueue},
		{"SetKeepAliveTime",ServerSetKeepAliveTime},
		{"SetKeepAliveInterval",ServerSetKeepAliveInterval},
		{"GetAcceptSocketCount",ServerGetAcceptSocketCount},
		{"GetSocketBufferSize",ServerGetSocketBufferSize},
		{"GetSocketListenQueue",ServerGetSocketListenQueue},
		{"GetKeepAliveTime",ServerGetKeepAliveTime},
		{"GetKeepAliveInterval",ServerGetKeepAliveInterval},
		{"Start",ServerStart},
		{"GetListenAddress",ServerGetListenAddress},
		{"Stop",ServerStop},
		{"Send",ServerSend},
		//{"SendPackets",SendPackets},
		{"PauseReceive",ServerPauseReceive},
		{"Disconnect",ServerDisconnect},
		{"DisconnectLongConnections",ServerDisconnectLongConnections},
		{"DisconnectSilenceConnections",ServerDisconnectSilenceConnections},
		{"Wait",ServerWait},
		//{"SetConnectionExtra",SetConnectionExtra},
		//{"GetConnectionExtra",GetConnectionExtra},
		{"IsSecure",ServerIsSecure},
		{"HasStarted",ServerHasStarted},
		{"GetState",ServerGetState},
		{"GetConnectionCount",ServerGetConnectionCount},
		{"GetAllConnectionIDs",ServerGetAllConnectionIDs},
		{"GetConnectPeriod",ServerGetConnectPeriod},
		{"GetSilencePeriod",ServerGetSilencePeriod},
		{"GetLocalAddress",ServerGetLocalAddress},
		{"GetRemoteAddress",ServerGetRemoteAddress},
		{"GetLastError",ServerGetLastError},
		{"GetLastErrorDesc",ServerGetLastErrorDesc},
		{"GetPendingDataLength",ServerGetPendingDataLength},
		{"IsPauseReceive",ServerIsPauseReceive},
		{"IsConnected",ServerIsConnected},
		{"SetReuseAddressPolicy",ServerSetReuseAddressPolicy},
		{"SetSendPolicy",ServerSetSendPolicy},
		{"SetOnSendSyncPolicy",ServerSetOnSendSyncPolicy},
		{"SetMaxConnectionCount",ServerSetMaxConnectionCount},
		{"SetFreeSocketObjLockTime",ServerSetFreeSocketObjLockTime},
		{"SetFreeSocketObjPool",ServerSetFreeSocketObjPool},
		{"SetFreeBufferObjPool",ServerSetFreeBufferObjPool},
		{"SetFreeSocketObjHold",ServerSetFreeSocketObjHold},
		{"SetFreeBufferObjHold",ServerSetFreeBufferObjHold},
		{"SetWorkerThreadCount",ServerSetWorkerThreadCount},
		{"SetMarkSilence",ServerSetMarkSilence},
		{"GetReuseAddressPolicy",ServerGetReuseAddressPolicy},
		{"GetSendPolicy",ServerGetSendPolicy},
		{"GetOnSendSyncPolicy",ServerGetOnSendSyncPolicy},
		{"GetMaxConnectionCount",ServerGetMaxConnectionCount},
		{"GetFreeSocketObjLockTime",ServerGetFreeSocketObjLockTime},
		{"GetFreeSocketObjPool",ServerGetFreeSocketObjPool},
		{"GetFreeBufferObjPool",ServerGetFreeBufferObjPool},
		{"GetFreeSocketObjHold",ServerGetFreeSocketObjHold},
		{"GetFreeBufferObjHold",ServerGetFreeBufferObjHold},
		{"GetWorkerThreadCount",ServerGetWorkerThreadCount},
		{"IsMarkSilence",ServerIsMarkSilence},
		{NULL, NULL},
	};
	luaL_newmetatable(L, "HP_PackServer");
	luaL_newlib(L, methods);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, Destroy_HP_PackServer);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);

	lua_pushcfunction(L, Creat_HP_PackServer);
	return 1;
}