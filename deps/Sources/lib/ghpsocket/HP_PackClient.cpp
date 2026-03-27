#include "HP_TcpClient.h"
#include "HP_PackClient.h"


EnHandleResult HP_PackClient::OnPrepareConnect(ITcpClient* pSender, CONNID dwConnID, SOCKET socket)
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

EnHandleResult HP_PackClient::OnConnect(ITcpClient* pSender, CONNID dwConnID)
{
	if (GetCallBack("OnConnect"))
	{
		lua_pushinteger(L, dwConnID);
		lua_call(L, 2, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHandleResult HP_PackClient::OnReceive(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
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

EnHandleResult HP_PackClient::OnSend(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
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

EnHandleResult HP_PackClient::OnClose(ITcpClient* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode)
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


static ITcpPackClient* GetPackClient(lua_State* L) {
	HP_PackClient* p = *(HP_PackClient**)luaL_checkudata(L, 1, "HP_PackClient");
	return p->m_Client;
}

//ITcpPackClient
static int SetMaxPackSize(lua_State* L) {
	ITcpPackClient* cli = GetPackClient(L);
	cli->SetMaxPackSize((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int SetPackHeaderFlag(lua_State* L) {
	ITcpPackClient* cli = GetPackClient(L);
	cli->SetPackHeaderFlag((USHORT)luaL_checkinteger(L, 2));
	return 0;
}

static int GetMaxPackSize(lua_State* L) {
	ITcpPackClient* cli = GetPackClient(L);
	lua_pushinteger(L, cli->GetMaxPackSize());
	return 1;
}

static int GetPackHeaderFlag(lua_State* L) {
	ITcpPackClient* cli = GetPackClient(L);
	lua_pushinteger(L, cli->GetPackHeaderFlag());
	return 1;
}


static int Creat_HP_PackClient(lua_State* L) {
	HP_PackClient* p = new HP_PackClient(L);
	HP_PackClient** ud = (HP_PackClient**)lua_newuserdata(L, sizeof(HP_PackClient*));
	*ud = p;
	lua_pushlightuserdata(L, (ITcpClient*)p->m_Client);
	lua_setuservalue(L, -2);
	luaL_setmetatable(L, "HP_PackClient");
	return 1;
}

static int Destroy_HP_PackClient(lua_State* L) {
	HP_PackClient* ud = *(HP_PackClient**)luaL_checkudata(L, 1, "HP_PackClient");
	ClientStop(L);
	delete ud;
	return 0;
}

extern "C"
#if defined(LUA_BUILD_AS_DLL)
LUAMOD_API
#endif
int luaopen_ghpsocket_packclient(lua_State * L)
{
	luaL_Reg methods[] = {
		{"SetMaxPackSize",SetMaxPackSize},
		{"SetPackHeaderFlag",SetPackHeaderFlag},
		{"GetMaxPackSize",GetMaxPackSize},
		{"GetPackHeaderFlag",GetPackHeaderFlag},
		//ITcpClient
		{"SendSmallFile",ClientSendSmallFile},
		{"SetSocketBufferSize",ClientSetSocketBufferSize},
		{"SetKeepAliveTime",ClientSetKeepAliveTime},
		{"SetKeepAliveInterval",ClientSetKeepAliveInterval},
		{"GetSocketBufferSize",ClientGetSocketBufferSize},
		{"GetKeepAliveTime",ClientGetKeepAliveTime},
		{"GetKeepAliveInterval",ClientGetKeepAliveInterval},
		//IClient
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
	luaL_newmetatable(L, "HP_PackClient");
	luaL_newlib(L, methods);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, Destroy_HP_PackClient);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);

	lua_pushcfunction(L, Creat_HP_PackClient);
	return 1;
}