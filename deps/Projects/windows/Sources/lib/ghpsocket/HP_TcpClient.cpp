#include "HP_TcpClient.h"

static BOOL luaL_optboolean(lua_State* L, int arg, BOOL def) 
{
	if (lua_isnoneornil(L, arg)) {
		return def;
	}
	return lua_toboolean(L, arg);
}

static ITcpClient* GetTcpClient(lua_State* L) 
{
	lua_getuservalue(L, 1);
	ITcpClient* p = (ITcpClient*)lua_topointer(L, -1);
	lua_pop(L, 1);
	return p;
}

//ITcpClient
int ClientSendSmallFile(lua_State* L)
{
	ITcpClient* cli = GetTcpClient(L);
	lua_pushboolean(L, cli->SendSmallFile(luaL_checkstring(L, 2),
		(LPWSABUF)luaL_optlstring(L, 3, nullptr, NULL),
		(LPWSABUF)luaL_optlstring(L, 4, nullptr, NULL)));
	return 1;
}

int ClientSetSocketBufferSize(lua_State* L)
{
	ITcpClient* cli = GetTcpClient(L);
	cli->SetSocketBufferSize((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

int ClientSetKeepAliveTime(lua_State* L) 
{
	ITcpClient* cli = GetTcpClient(L);
	cli->SetKeepAliveTime((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

int ClientSetKeepAliveInterval(lua_State* L)
{
	ITcpClient* cli = GetTcpClient(L);
	cli->SetKeepAliveInterval((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

int ClientGetSocketBufferSize(lua_State* L)
{
	ITcpClient* cli = GetTcpClient(L);
	lua_pushinteger(L, cli->GetSocketBufferSize());
	return 1;
}

int ClientGetKeepAliveTime(lua_State* L)
{
	ITcpClient* cli = GetTcpClient(L);
	lua_pushinteger(L, cli->GetKeepAliveTime());
	return 1;
}

int ClientGetKeepAliveInterval(lua_State* L) 
{
	ITcpClient* cli = GetTcpClient(L);
	lua_pushinteger(L, cli->GetKeepAliveInterval());
	return 1;
}

//IClient
int ClientStart(lua_State* L) 
{
	ITcpClient* cli = GetTcpClient(L);
	BOOL r = cli->Start(luaL_checkstring(L, 2), (USHORT)luaL_checkinteger(L, 3), luaL_optboolean(L, 4, FALSE),
		luaL_optstring(L, 5, nullptr), (USHORT)luaL_optinteger(L, 6, 0U));
	lua_pushboolean(L, r);
	return 1;
}

int ClientStop(lua_State* L) 
{
	ITcpClient* cli = GetTcpClient(L);
	HP_mutex* mutex = *(HP_mutex**)lua_getextraspace(L);
	HP_UnlockMutex(mutex);
	BOOL r = cli->Stop();
	HP_LockMutex(mutex);
	lua_pushboolean(L, r);
	return 1;
}

int ClientSend(lua_State* L) 
{
	ITcpClient* cli = GetTcpClient(L);
	size_t len;
	BYTE* buf = (BYTE*)luaL_checklstring(L, 2, &len);
	lua_pushboolean(L, cli->Send(buf, (int)len, (int)luaL_optinteger(L, 3, 0)));
	return 1;
}
//int ClientSendPackets(lua_State* L){
//	ITcpClient* cli = GetTcpClient(L);
//
//	return 0;
//}
int ClientPauseReceive(lua_State* L) 
{
	ITcpClient* cli = GetTcpClient(L);
	lua_pushboolean(L, cli->PauseReceive());
	return 1;
}

int ClientWait(lua_State* L)
{
	ITcpClient* cli = GetTcpClient(L);
	lua_pushboolean(L, cli->Wait((DWORD)luaL_optinteger(L, 2, INFINITE)));
	return 1;
}
//int ClientSetExtra(lua_State* L){
//	ITcpClient* cli = GetTcpClient(L);
//
//	return 0;
//}
//int ClientGetExtra(lua_State* L){
//	ITcpClient* cli = GetTcpClient(L);
//
//	return 0;
//}
int ClientIsSecure(lua_State* L)
{
	ITcpClient* cli = GetTcpClient(L);
	lua_pushboolean(L, cli->IsSecure());
	return 1;
}

int ClientHasStarted(lua_State* L)
{
	ITcpClient* cli = GetTcpClient(L);
	lua_pushboolean(L, cli->HasStarted());
	return 1;
}

int ClientGetState(lua_State* L) 
{
	ITcpClient* cli = GetTcpClient(L);
	switch (cli->GetState()) {
		CASE(SS_STARTING)
			CASE(SS_STARTED)
			CASE(SS_STOPPING)
			CASE(SS_STOPPED)
	}
	return 1;
}

int ClientGetLastError(lua_State* L)
{
	ITcpClient* cli = GetTcpClient(L);
	switch (cli->GetLastError()) {
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

int ClientGetLastErrorDesc(lua_State* L)
{
	ITcpClient* cli = GetTcpClient(L);
	lua_pushstring(L, cli->GetLastErrorDesc());
	return 1;
}

int ClientGetConnectionID(lua_State* L) 
{
	ITcpClient* cli = GetTcpClient(L);
	lua_pushinteger(L, cli->GetConnectionID());
	return 1;
}

int ClientGetLocalAddress(lua_State* L)
{
	ITcpClient* cli = GetTcpClient(L);
	luaL_Buffer b;
	char* p = luaL_buffinitsize(L, &b, LUAL_BUFFERSIZE);
	int len = LUAL_BUFFERSIZE;
	USHORT port;
	if (cli->GetLocalAddress(p, len, port)) {
		luaL_pushresultsize(&b, len - 1);
		lua_pushinteger(L, port);
		return 2;
	}
	return 0;
}

int ClientGetRemoteHost(lua_State* L) 
{
	ITcpClient* cli = GetTcpClient(L);
	luaL_Buffer b;
	char* p = luaL_buffinitsize(L, &b, LUAL_BUFFERSIZE);
	int len = LUAL_BUFFERSIZE;
	USHORT port;
	if (cli->GetRemoteHost(p, len, port)) {
		luaL_pushresultsize(&b, len - 1);
		lua_pushinteger(L, port);
		return 2;
	}
	return 0;
}

int ClientGetPendingDataLength(lua_State* L)
{
	ITcpClient* cli = GetTcpClient(L);
	int v;
	cli->GetPendingDataLength(v);
	lua_pushinteger(L, v);
	return 1;
}

int ClientIsPauseReceive(lua_State* L) 
{
	ITcpClient* cli = GetTcpClient(L);
	BOOL v;
	cli->IsPauseReceive(v);
	lua_pushboolean(L, v);
	return 1;
}

int ClientIsConnected(lua_State* L)
{
	ITcpClient* cli = GetTcpClient(L);
	lua_pushboolean(L, cli->IsConnected());
	return 1;
}

int ClientSetReuseAddressPolicy(lua_State* L)
{
	ITcpClient* cli = GetTcpClient(L);
	static const char* const opts[] = { "RAP_NONE", "RAP_ADDR_ONLY", "RAP_ADDR_AND_PORT", NULL };
	//static const int optsnum[] = {RAP_NONE, RAP_ADDR_ONLY, RAP_ADDR_AND_PORT,};
	cli->SetReuseAddressPolicy(EnReuseAddressPolicy(luaL_checkoption(L, 2, "RAP_NONE", opts)));
	return 0;
}

int ClientSetFreeBufferPoolSize(lua_State* L)
{
	ITcpClient* cli = GetTcpClient(L);
	cli->SetFreeBufferPoolSize((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

int ClientSetFreeBufferPoolHold(lua_State* L) 
{
	ITcpClient* cli = GetTcpClient(L);
	cli->SetFreeBufferPoolHold((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

int ClientGetReuseAddressPolicy(lua_State* L) 
{
	ITcpClient* cli = GetTcpClient(L);
	switch (cli->GetReuseAddressPolicy()) {
		CASE(RAP_NONE)
			CASE(RAP_ADDR_ONLY)
			CASE(RAP_ADDR_AND_PORT)
	}
	return 1;
}

int ClientGetFreeBufferPoolSize(lua_State* L) 
{
	ITcpClient* cli = GetTcpClient(L);
	lua_pushinteger(L, cli->GetFreeBufferPoolSize());
	return 1;
}

int ClientGetFreeBufferPoolHold(lua_State* L)
{
	ITcpClient* cli = GetTcpClient(L);
	lua_pushinteger(L, cli->GetFreeBufferPoolHold());
	return 1;
}
