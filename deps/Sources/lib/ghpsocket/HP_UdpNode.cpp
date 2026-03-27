#include "HP_UdpNode.h"

EnHandleResult HP_UdpNode::OnPrepareListen(IUdpNode* pSender, SOCKET soListen)
{
	if (GetCallBack("OnPrepareListen"))
	{
		lua_pushinteger(L, soListen);
		lua_call(L, 2, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHandleResult HP_UdpNode::OnSend(IUdpNode* pSender, LPCTSTR lpszRemoteAddress, USHORT usRemotePort, const BYTE* pData, int iLength)
{
	if (GetCallBack("OnSend"))
	{
		lua_pushstring(L, lpszRemoteAddress);
		lua_pushinteger(L, usRemotePort);
		//lua_pushlightuserdata(L,(void *)pData);
		lua_pushinteger(L, iLength);
		lua_call(L, 4, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHandleResult HP_UdpNode::OnReceive(IUdpNode* pSender, LPCTSTR lpszRemoteAddress, USHORT usRemotePort, const BYTE* pData, int iLength)
{
	if (GetCallBack("OnReceive"))
	{
		lua_pushstring(L, lpszRemoteAddress);
		lua_pushinteger(L, usRemotePort);
		lua_pushlstring(L, (const char*)pData, iLength);
		lua_call(L, 4, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHandleResult HP_UdpNode::OnError(IUdpNode* pSender, EnSocketOperation enOperation, int iErrorCode, LPCTSTR lpszRemoteAddress, USHORT usRemotePort, const BYTE* pBuffer, int iLength)
{
	if (GetCallBack("OnError"))
	{
		switch (enOperation) {
			CASE(SO_UNKNOWN)
				CASE(SO_ACCEPT)
				CASE(SO_CONNECT)
				CASE(SO_SEND)
				CASE(SO_RECEIVE)
				CASE(SO_CLOSE)
		}
		lua_pushinteger(L, iErrorCode);
		lua_pushstring(L, lpszRemoteAddress);
		lua_pushinteger(L, usRemotePort);
		lua_pushlstring(L, (const char*)pBuffer, iLength);

		lua_call(L, 6, 1);
		return GetResult();
	}
	return HR_OK;
}

EnHandleResult HP_UdpNode::OnShutdown(IUdpNode* pSender)
{
	if (GetCallBack("OnShutdown"))
	{
		lua_call(L, 1, 1);
		return GetResult();
	}
	return HR_OK;
}


static IUdpNode* GetUdpNode(lua_State* L) {
	HP_UdpNode* p = *(HP_UdpNode**)luaL_checkudata(L, 1, "HP_UdpNode");
	return p->m_UdpNode;
}

static int Start(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	static const char* const opts[] = { "CM_UNICAST", "CM_MULTICAST", "CM_BROADCAST", NULL };
	Udp->SetReuseAddressPolicy(RAP_ADDR_AND_PORT);
	BOOL r = Udp->Start(
		luaL_optstring(L, 2, nullptr),
		(USHORT)luaL_optinteger(L, 3, 0),
		EnCastMode(luaL_checkoption(L, 4, "CM_BROADCAST", opts) - 1),
		luaL_optstring(L, 5, "255.255.255.255"));
	lua_pushboolean(L, r);
	return 1;
}

static int Stop(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	SDL_mutex* mutex = *(SDL_mutex**)lua_getextraspace(L);
	SDL_UnlockMutex(mutex);
	BOOL r = Udp->Stop();
	SDL_LockMutex(mutex);
	lua_pushboolean(L, r);
	return 1;
}

static int Send(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	size_t len;
	const BYTE* buf = (BYTE*)luaL_checklstring(L, 4, &len);
	lua_pushboolean(L, Udp->Send(luaL_checkstring(L, 2), (USHORT)luaL_checkinteger(L, 3), buf, (int)len, (int)luaL_optinteger(L, 5, 0)));
	return 1;
}
//static int SendPackets(lua_State* L){
//	IUdpNode* Udp = GetUdpNode(L);
//	return 1;
//}
static int SendCast(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	size_t len;
	const BYTE* buf = (BYTE*)luaL_checklstring(L, 2, &len);
	lua_pushboolean(L, Udp->SendCast(buf, (int)len, (int)luaL_optinteger(L, 3, 0)));
	return 1;
}
//static int SendCastPackets(lua_State* L){
//	IUdpNode* Udp = GetUdpNode(L);
//	return 1;
//}
static int Wait(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	lua_pushboolean(L, Udp->Wait((DWORD)luaL_optinteger(L, 2, INFINITE)));
	return 1;
}
//static int SetExtra(lua_State* L){
//	IUdpNode* Udp = GetUdpNode(L);
//	return 1;
//}
//static int GetExtra(lua_State* L){
//	IUdpNode* Udp = GetUdpNode(L);
//	return 1;
//}

static int HasStarted(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	lua_pushboolean(L, Udp->HasStarted());
	return 1;
}

static int GetState(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	switch (Udp->GetState()) {
		CASE(SS_STARTING)
			CASE(SS_STARTED)
			CASE(SS_STOPPING)
			CASE(SS_STOPPED)
	}
	return 1;
}

static int GetLastError(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	switch (Udp->GetLastError()) {
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
	IUdpNode* Udp = GetUdpNode(L);
	lua_pushstring(L, Udp->GetLastErrorDesc());
	return 1;
}

static int GetLocalAddress(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	luaL_Buffer b;
	char* p = luaL_buffinitsize(L, &b, LUAL_BUFFERSIZE);
	int len = LUAL_BUFFERSIZE;
	USHORT port;
	if (Udp->GetLocalAddress(p, len, port)) {
		luaL_pushresultsize(&b, len - 1);
		lua_pushinteger(L, port);
		return 2;
	}
	return 0;
}

static int GetCastAddress(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	luaL_Buffer b;
	char* p = luaL_buffinitsize(L, &b, LUAL_BUFFERSIZE);
	int len = LUAL_BUFFERSIZE;
	USHORT port;
	if (Udp->GetCastAddress(p, len, port)) {
		luaL_pushresultsize(&b, len - 1);
		lua_pushinteger(L, port);
		return 2;
	}
	return 0;
}

static int GetCastMode(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	switch (Udp->GetCastMode()) {
		CASE(CM_UNICAST)
			CASE(CM_MULTICAST)
			CASE(CM_BROADCAST)
	}
	return 1;
}

static int GetPendingDataLength(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	int v;
	Udp->GetPendingDataLength(v);
	lua_pushinteger(L, v);
	return 1;
}

static int SetMaxDatagramSize(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	Udp->SetMaxDatagramSize((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int GetMaxDatagramSize(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	lua_pushinteger(L, Udp->GetMaxDatagramSize());
	return 1;
}

static int SetMultiCastTtl(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	Udp->SetMultiCastTtl((int)luaL_checkinteger(L, 2));
	return 0;
}

static int GetMultiCastTtl(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	lua_pushinteger(L, Udp->GetMultiCastTtl());
	return 1;
}

static int SetMultiCastLoop(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	Udp->SetMultiCastLoop(lua_toboolean(L, 2));
	return 0;
}

static int IsMultiCastLoop(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	lua_pushboolean(L, Udp->IsMultiCastLoop());
	return 1;
}

static int SetReuseAddressPolicy(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	static const char* const opts[] = { "RAP_NONE", "RAP_ADDR_ONLY", "RAP_ADDR_AND_PORT", NULL };
	Udp->SetReuseAddressPolicy(EnReuseAddressPolicy(luaL_checkoption(L, 2, "RAP_NONE", opts)));
	return 0;
}

static int SetWorkerThreadCount(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	Udp->SetWorkerThreadCount((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int SetPostReceiveCount(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	Udp->SetPostReceiveCount((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int SetFreeBufferPoolSize(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	Udp->SetFreeBufferPoolSize((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int SetFreeBufferPoolHold(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	Udp->SetFreeBufferPoolHold((DWORD)luaL_checkinteger(L, 2));
	return 0;
}

static int GetReuseAddressPolicy(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	switch (Udp->GetReuseAddressPolicy()) {
		CASE(RAP_NONE)
			CASE(RAP_ADDR_ONLY)
			CASE(RAP_ADDR_AND_PORT)
	}
	return 1;
}

static int GetWorkerThreadCount(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	lua_pushinteger(L, Udp->GetWorkerThreadCount());
	return 1;
}

static int GetPostReceiveCount(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	lua_pushinteger(L, Udp->GetPostReceiveCount());
	return 1;
}

static int GetFreeBufferPoolSize(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	lua_pushinteger(L, Udp->GetFreeBufferPoolSize());
	return 1;
}

static int GetFreeBufferPoolHold(lua_State* L) {
	IUdpNode* Udp = GetUdpNode(L);
	lua_pushinteger(L, Udp->GetFreeBufferPoolHold());
	return 1;
}

static int Creat_HP_UdpNode(lua_State* L) {
	HP_UdpNode* p = new HP_UdpNode(L);
	HP_UdpNode** ud = (HP_UdpNode**)lua_newuserdata(L, sizeof(HP_UdpNode*));
	*ud = p;
	luaL_setmetatable(L, "HP_UdpNode");
	return 1;
}

static int Destroy_HP_UdpNode(lua_State* L) {
	HP_UdpNode* ud = *(HP_UdpNode**)luaL_checkudata(L, 1, "HP_UdpNode");
	Stop(L);
	delete ud;
	return 0;
}

extern "C"
#if defined(LUA_BUILD_AS_DLL)
LUAMOD_API
#endif
int luaopen_ghpsocket_udpnode(lua_State * L)
{
	luaL_Reg methods[] = {
		{"Start",Start},
		{"Stop",Stop},
		{"Send",Send},
		//{"SendPackets",SendPackets},
		{"SendCast",SendCast},
		//{"SendCastPackets",SendCastPackets},
		{"Wait",Wait},
		//{"SetExtra",SetExtra},
		//{"GetExtra",GetExtra},
		{"HasStarted",HasStarted},
		{"GetState",GetState},
		{"GetLastError",GetLastError},
		{"GetLastErrorDesc",GetLastErrorDesc},
		{"GetLocalAddress",GetLocalAddress},
		{"GetCastAddress",GetCastAddress},
		{"GetCastMode",GetCastMode},
		{"GetPendingDataLength",GetPendingDataLength},
		{"SetMaxDatagramSize",SetMaxDatagramSize},
		{"GetMaxDatagramSize",GetMaxDatagramSize},
		{"SetMultiCastTtl",SetMultiCastTtl},
		{"GetMultiCastTtl",GetMultiCastTtl},
		{"SetMultiCastLoop",SetMultiCastLoop},
		{"IsMultiCastLoop",IsMultiCastLoop},
		{"SetReuseAddressPolicy",SetReuseAddressPolicy},
		{"SetWorkerThreadCount",SetWorkerThreadCount},
		{"SetPostReceiveCount",SetPostReceiveCount},
		{"SetFreeBufferPoolSize",SetFreeBufferPoolSize},
		{"SetFreeBufferPoolHold",SetFreeBufferPoolHold},
		{"GetReuseAddressPolicy",GetReuseAddressPolicy},
		{"GetWorkerThreadCount",GetWorkerThreadCount},
		{"GetPostReceiveCount",GetPostReceiveCount},
		{"GetFreeBufferPoolSize",GetFreeBufferPoolSize},
		{"GetFreeBufferPoolHold",GetFreeBufferPoolHold},
		{NULL, NULL},
	};
	luaL_newmetatable(L, "HP_UdpNode");
	luaL_newlib(L, methods);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, Destroy_HP_UdpNode);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);

	lua_pushcfunction(L, Creat_HP_UdpNode);
	return 1;
}