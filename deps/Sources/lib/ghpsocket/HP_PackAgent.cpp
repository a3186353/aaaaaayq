#include "HP_PackAgent.h"

EnHandleResult HP_PackAgent::OnPrepareConnect(ITcpAgent* pSender, CONNID dwConnID, SOCKET socket)
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

EnHandleResult HP_PackAgent::OnConnect(ITcpAgent* pSender, CONNID dwConnID)
{
	if (GetCallBack("OnConnect"))
	{
		lua_pushinteger(L, dwConnID);
		lua_call(L, 2, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHandleResult HP_PackAgent::OnReceive(ITcpAgent* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
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

EnHandleResult HP_PackAgent::OnSend(ITcpAgent* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
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

EnHandleResult HP_PackAgent::OnShutdown(ITcpAgent* pSender)
{
	if (GetCallBack("OnShutdown"))
	{
		lua_call(L, 1, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHandleResult HP_PackAgent::OnClose(ITcpAgent* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode)
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

static BOOL luaL_optboolean(lua_State* L, int arg, BOOL def) 
{
	if (lua_isnoneornil(L, arg)) {
		return def;
	}
	return lua_toboolean(L, arg);
}

static ITcpPackAgent* GetAgent(lua_State* L) 
{
	HP_PackAgent* p = *(HP_PackAgent**)luaL_checkudata(L, 1, "HP_PackAgent");
	return p->m_Agent;
}
//IPackSocket
static int SetMaxPackSize(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	Agent->SetMaxPackSize((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int SetPackHeaderFlag(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	Agent->SetPackHeaderFlag((USHORT)luaL_checkinteger(L, 2));
	return 0;
}

static int GetMaxPackSize(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushinteger(L, Agent->GetMaxPackSize());
	return 1;
}

static int GetPackHeaderFlag(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushinteger(L, Agent->GetPackHeaderFlag());
	return 1;
}
//ITcpAgent
static int SendSmallFile(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushboolean(L, Agent->SendSmallFile((CONNID)luaL_checkinteger(L, 2), luaL_checkstring(L, 3),
		(LPWSABUF)luaL_optlstring(L, 4, nullptr, NULL),
		(LPWSABUF)luaL_optlstring(L, 5, nullptr, NULL)));
	return 1;
}

static int SetSocketBufferSize(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	Agent->SetSocketBufferSize((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int SetKeepAliveTime(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	Agent->SetKeepAliveTime((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int SetKeepAliveInterval(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	Agent->SetKeepAliveInterval((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int GetSocketBufferSize(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushinteger(L, Agent->GetSocketBufferSize());
	return 1;
}

static int GetKeepAliveTime(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushinteger(L, Agent->GetKeepAliveTime());
	return 1;
}

static int GetKeepAliveInterval(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushinteger(L, Agent->GetKeepAliveInterval());
	return 1;
}
//IAgent
static int Start(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	BOOL r = Agent->Start(luaL_optstring(L, 2, nullptr), luaL_optboolean(L, 3, FALSE));
	lua_pushboolean(L, r);
	return 1;
}

static int Connect(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	CONNID ConnID;
	BOOL r = Agent->Connect(luaL_checkstring(L, 2), (USHORT)luaL_checkinteger(L, 3), &ConnID, nullptr, (USHORT)luaL_optinteger(L, 4, 0), luaL_optstring(L, 5, nullptr));
	lua_pushboolean(L, r);
	lua_pushinteger(L, ConnID);
	return 2;
}

static int GetRemoteHost(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	luaL_Buffer b;
	char* p = luaL_buffinitsize(L, &b, LUAL_BUFFERSIZE);
	int len = LUAL_BUFFERSIZE;
	USHORT port;
	if (Agent->GetRemoteHost((CONNID)luaL_checkinteger(L, 2), p, len, port)) {
		luaL_pushresultsize(&b, len - 1);
		lua_pushinteger(L, port);
		return 2;
	}
	return 0;
}

//IComplexSocket
static int Stop(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	SDL_mutex* mutex = *(SDL_mutex**)lua_getextraspace(L);
	SDL_UnlockMutex(mutex);
	BOOL r = Agent->Stop();
	SDL_LockMutex(mutex);
	lua_pushboolean(L, r);
	return 1;
}

static int Send(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	size_t len;
	BYTE* buf = (BYTE*)luaL_checklstring(L, 3, &len);
	lua_pushboolean(L, Agent->Send((CONNID)luaL_checkinteger(L, 2), buf, (int)len, (int)luaL_optinteger(L, 4, 0)));
	return 1;
}

//static int SendPackets(lua_State* L){
//	ITcpPackAgent* Agent = GetAgent(L);
//
//	return 0;
//}

static int PauseReceive(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushboolean(L, Agent->PauseReceive((CONNID)luaL_checkinteger(L, 2), luaL_optboolean(L, 3, TRUE)));
	return 1;
}

static int Disconnect(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushboolean(L, Agent->Disconnect((CONNID)luaL_checkinteger(L, 2), luaL_optboolean(L, 3, TRUE)));
	return 1;
}

static int DisconnectLongConnections(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushboolean(L, Agent->DisconnectLongConnections((DWORD)luaL_checkinteger(L, 2), luaL_optboolean(L, 3, TRUE)));
	return 1;
}

static int DisconnectSilenceConnections(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushboolean(L, Agent->DisconnectSilenceConnections((DWORD)luaL_checkinteger(L, 2), luaL_optboolean(L, 3, TRUE)));
	return 1;
}

static int Wait(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushboolean(L, Agent->Wait((DWORD)luaL_optinteger(L, 2, INFINITE)));
	return 1;
}
//static int SetConnectionExtra(lua_State* L){
//	ITcpPackAgent* Agent = GetAgent(L);
//
//	return 0;
//}
//static int GetConnectionExtra(lua_State* L){
//	ITcpPackAgent* Agent = GetAgent(L);
//
//	return 0;
//}

static int IsSecure(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushboolean(L, Agent->IsSecure());
	return 1;
}

static int HasStarted(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushboolean(L, Agent->HasStarted());
	return 1;
}

static int GetState(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	switch (Agent->GetState()) {
		CASE(SS_STARTING)
			CASE(SS_STARTED)
			CASE(SS_STOPPING)
			CASE(SS_STOPPED)
	}
	return 1;
}

static int GetConnectionCount(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushinteger(L, Agent->GetConnectionCount());
	return 1;
}

static int GetAllConnectionIDs(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	DWORD num = Agent->GetConnectionCount();
	CONNID* id = new CONNID[num];
	lua_createtable(L, num, 0);
	if (Agent->GetAllConnectionIDs(id, num)) {
		for (DWORD i = 0; i < num; i++) {
			lua_pushinteger(L, id[i]);
			lua_seti(L, -2, i + 1);
		}
	}
	delete[]id;
	return 1;
}

static int GetConnectPeriod(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	DWORD v;
	Agent->GetConnectPeriod((CONNID)luaL_checkinteger(L, 2), v);
	lua_pushinteger(L, v);
	return 1;
}

static int GetSilencePeriod(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	DWORD v;
	Agent->GetSilencePeriod((CONNID)luaL_checkinteger(L, 2), v);
	lua_pushinteger(L, v);
	return 1;
}

static int GetLocalAddress(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	luaL_Buffer b;
	char* p = luaL_buffinitsize(L, &b, LUAL_BUFFERSIZE);
	int len = LUAL_BUFFERSIZE;
	USHORT port;
	if (Agent->GetLocalAddress((CONNID)luaL_checkinteger(L, 2), p, len, port)) {
		luaL_pushresultsize(&b, len - 1);
		lua_pushinteger(L, port);
		return 2;
	}
	return 0;
}

static int GetRemoteAddress(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	luaL_Buffer b;
	char* p = luaL_buffinitsize(L, &b, LUAL_BUFFERSIZE);
	int len = LUAL_BUFFERSIZE;
	USHORT port;
	if (Agent->GetRemoteAddress((CONNID)luaL_checkinteger(L, 2), p, len, port)) {
		luaL_pushresultsize(&b, len - 1);
		lua_pushinteger(L, port);
		return 2;
	}
	return 0;
}

static int GetLastError(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	switch (Agent->GetLastError()) {
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

static int GetLastErrorDesc(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushstring(L, Agent->GetLastErrorDesc());
	return 1;
}

static int GetPendingDataLength(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	int v;
	Agent->GetPendingDataLength((CONNID)luaL_checkinteger(L, 2), v);
	lua_pushinteger(L, v);
	return 1;
}

static int IsPauseReceive(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	BOOL v;
	Agent->GetPendingDataLength((CONNID)luaL_checkinteger(L, 2), v);
	lua_pushboolean(L, v);
	return 1;
}

static int IsConnected(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushboolean(L, Agent->IsConnected((CONNID)luaL_checkinteger(L, 2)));
	return 1;
}

static int SetReuseAddressPolicy(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	static const char* const opts[] = { "RAP_NONE", "RAP_ADDR_ONLY", "RAP_ADDR_AND_PORT", NULL };
	Agent->SetReuseAddressPolicy(EnReuseAddressPolicy(luaL_checkoption(L, 2, "RAP_NONE", opts)));
	return 0;
}

static int SetSendPolicy(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	static const char* const opts[] = { "SP_PACK", "SP_SAFE", "SP_DIRECT", NULL };
	Agent->SetSendPolicy(EnSendPolicy(luaL_checkoption(L, 2, "SP_PACK", opts)));
	return 0;
}

static int SetOnSendSyncPolicy(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	static const char* const opts[] = { "OSSP_NONE", "OSSP_CLOSE", "OSSP_RECEIVE", NULL };
	Agent->SetOnSendSyncPolicy(EnOnSendSyncPolicy(luaL_checkoption(L, 2, "OSSP_NONE", opts)));
	return 0;
}

static int SetMaxConnectionCount(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	Agent->SetMaxConnectionCount((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int SetFreeSocketObjLockTime(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	Agent->SetFreeSocketObjLockTime((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int SetFreeSocketObjPool(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	Agent->SetFreeSocketObjPool((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int SetFreeBufferObjPool(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	Agent->SetFreeBufferObjPool((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int SetFreeSocketObjHold(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	Agent->SetFreeSocketObjHold((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int SetFreeBufferObjHold(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	Agent->SetFreeBufferObjHold((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int SetWorkerThreadCount(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	Agent->SetWorkerThreadCount((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int SetMarkSilence(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	Agent->SetMarkSilence(lua_toboolean(L, 2));
	return 0;
}

static int GetReuseAddressPolicy(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	switch (Agent->GetReuseAddressPolicy()) {
		CASE(RAP_NONE)
			CASE(RAP_ADDR_ONLY)
			CASE(RAP_ADDR_AND_PORT)
	}
	return 1;
}

static int GetSendPolicy(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	switch (Agent->GetSendPolicy()) {
		CASE(SP_PACK)
			CASE(SP_SAFE)
			CASE(SP_DIRECT)
	}
	return 1;
}

static int GetOnSendSyncPolicy(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	switch (Agent->GetOnSendSyncPolicy()) {
		CASE(OSSP_NONE)
			CASE(OSSP_CLOSE)
			CASE(OSSP_RECEIVE)
	}
	return 1;
}

static int GetMaxConnectionCount(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushinteger(L, Agent->GetMaxConnectionCount());
	return 1;
}

static int GetFreeSocketObjLockTime(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushinteger(L, Agent->GetFreeSocketObjLockTime());
	return 1;
}

static int GetFreeSocketObjPool(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushinteger(L, Agent->GetFreeSocketObjPool());
	return 1;
}

static int GetFreeBufferObjPool(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushinteger(L, Agent->GetFreeBufferObjPool());
	return 1;
}

static int GetFreeSocketObjHold(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushinteger(L, Agent->GetFreeSocketObjHold());
	return 1;
}

static int GetFreeBufferObjHold(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushinteger(L, Agent->GetFreeBufferObjHold());
	return 1;
}

static int GetWorkerThreadCount(lua_State* L) 
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushinteger(L, Agent->GetWorkerThreadCount());
	return 1;
}

static int IsMarkSilence(lua_State* L)
{
	ITcpPackAgent* Agent = GetAgent(L);
	lua_pushboolean(L, Agent->IsMarkSilence());
	return 1;
}


static int Creat_HP_PackAgent(lua_State* L)
{
	HP_PackAgent* p = new HP_PackAgent(L);
	HP_PackAgent** ud = (HP_PackAgent**)lua_newuserdata(L, sizeof(HP_PackAgent*));
	*ud = p;
	luaL_setmetatable(L, "HP_PackAgent");
	return 1;
}

static int Destroy_HP_PackAgent(lua_State* L)
{
	HP_PackAgent* ud = *(HP_PackAgent**)luaL_checkudata(L, 1, "HP_PackAgent");
	Stop(L);
	delete ud;
	return 0;
}

extern "C"
#if defined(LUA_BUILD_AS_DLL)
LUAMOD_API
#endif
int luaopen_ghpsocket_packagent(lua_State * L)
{
	luaL_Reg methods[] = {
		{"SetMaxPackSize",SetMaxPackSize},
		{"SetPackHeaderFlag",SetPackHeaderFlag},
		{"GetMaxPackSize",GetMaxPackSize},
		{"GetPackHeaderFlag",GetPackHeaderFlag},

		{"SendSmallFile",SendSmallFile},
		{"SetSocketBufferSize",SetSocketBufferSize},
		{"SetKeepAliveTime",SetKeepAliveTime},
		{"SetKeepAliveInterval",SetKeepAliveInterval},
		{"GetSocketBufferSize",GetSocketBufferSize},
		{"GetKeepAliveTime",GetKeepAliveTime},
		{"GetKeepAliveInterval",GetKeepAliveInterval},
		{"Start",Start},
		{"Connect",Connect},
		{"GetRemoteHost",GetRemoteHost},
		{"Stop",Stop},
		{"Send",Send},
		//{"SendPackets",SendPackets},
		{"PauseReceive",PauseReceive},
		{"Disconnect",Disconnect},
		{"DisconnectLongConnections",DisconnectLongConnections},
		{"DisconnectSilenceConnections",DisconnectSilenceConnections},
		{"Wait",Wait},
		//{"SetConnectionExtra",SetConnectionExtra},
		//{"GetConnectionExtra",GetConnectionExtra},
		{"IsSecure",IsSecure},
		{"HasStarted",HasStarted},
		{"GetState",GetState},
		{"GetConnectionCount",GetConnectionCount},
		{"GetAllConnectionIDs",GetAllConnectionIDs},
		{"GetConnectPeriod",GetConnectPeriod},
		{"GetSilencePeriod",GetSilencePeriod},
		{"GetLocalAddress",GetLocalAddress},
		{"GetRemoteAddress",GetRemoteAddress},
		{"GetLastError",GetLastError},
		{"GetLastErrorDesc",GetLastErrorDesc},
		{"GetPendingDataLength",GetPendingDataLength},
		{"IsPauseReceive",IsPauseReceive},
		{"IsConnected",IsConnected},
		{"SetReuseAddressPolicy",SetReuseAddressPolicy},
		{"SetSendPolicy",SetSendPolicy},
		{"SetOnSendSyncPolicy",SetOnSendSyncPolicy},
		{"SetMaxConnectionCount",SetMaxConnectionCount},
		{"SetFreeSocketObjLockTime",SetFreeSocketObjLockTime},
		{"SetFreeSocketObjPool",SetFreeSocketObjPool},
		{"SetFreeBufferObjPool",SetFreeBufferObjPool},
		{"SetFreeSocketObjHold",SetFreeSocketObjHold},
		{"SetFreeBufferObjHold",SetFreeBufferObjHold},
		{"SetWorkerThreadCount",SetWorkerThreadCount},
		{"SetMarkSilence",SetMarkSilence},
		{"GetReuseAddressPolicy",GetReuseAddressPolicy},
		{"GetSendPolicy",GetSendPolicy},
		{"GetOnSendSyncPolicy",GetOnSendSyncPolicy},
		{"GetMaxConnectionCount",GetMaxConnectionCount},
		{"GetFreeSocketObjLockTime",GetFreeSocketObjLockTime},
		{"GetFreeSocketObjPool",GetFreeSocketObjPool},
		{"GetFreeBufferObjPool",GetFreeBufferObjPool},
		{"GetFreeSocketObjHold",GetFreeSocketObjHold},
		{"GetFreeBufferObjHold",GetFreeBufferObjHold},
		{"GetWorkerThreadCount",GetWorkerThreadCount},
		{"IsMarkSilence",IsMarkSilence},
		{NULL, NULL},
	};
	luaL_newmetatable(L, "HP_PackAgent");
	luaL_newlib(L, methods);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, Destroy_HP_PackAgent);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);

	lua_pushcfunction(L, Creat_HP_PackAgent);
	return 1;
}