#include "HP_TcpServer.h"

static BOOL luaL_optboolean(lua_State* L, int arg, BOOL def)
{
    if (lua_isnoneornil(L, arg)) {
        return def;
    }
    return lua_toboolean(L, arg);
}

static ITcpServer* GetTcpServer(lua_State* L)
{
    lua_getuservalue(L, 1);
    ITcpServer* p = (ITcpServer*)lua_topointer(L, -1);
    lua_pop(L, 1);
    return p;
}

//ITcpServer
int ServerSendSmallFile(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushboolean(L, Svr->SendSmallFile((CONNID)luaL_checkinteger(L, 2), luaL_checkstring(L, 3),
        (LPWSABUF)luaL_optlstring(L, 4, nullptr, NULL),
        (LPWSABUF)luaL_optlstring(L, 5, nullptr, NULL)));
    return 1;
}

int ServerSetAcceptSocketCount(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    Svr->SetAcceptSocketCount((DWORD)luaL_checkinteger(L, 2));
    return 0;
}

int ServerSetSocketBufferSize(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    Svr->SetSocketBufferSize((DWORD)luaL_checkinteger(L, 2));
    return 0;
}

int ServerSetSocketListenQueue(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    Svr->SetSocketListenQueue((DWORD)luaL_checkinteger(L, 2));
    return 0;
}

int ServerSetKeepAliveTime(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    Svr->SetKeepAliveTime((DWORD)luaL_checkinteger(L, 2));
    return 0;
}

int ServerSetKeepAliveInterval(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    Svr->SetKeepAliveInterval((DWORD)luaL_checkinteger(L, 2));
    return 0;
}

int ServerGetAcceptSocketCount(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushinteger(L, Svr->GetAcceptSocketCount());
    return 1;
}

int ServerGetSocketBufferSize(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushinteger(L, Svr->GetSocketBufferSize());
    return 1;
}

int ServerGetSocketListenQueue(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushinteger(L, Svr->GetSocketListenQueue());
    return 1;
}

int ServerGetKeepAliveTime(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushinteger(L, Svr->GetKeepAliveTime());
    return 1;
}

int ServerGetKeepAliveInterval(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushinteger(L, Svr->GetKeepAliveInterval());
    return 1;
}

//IServer
int ServerStart(lua_State* L)
{
    const char* ip = luaL_checkstring(L, 2);
    USHORT port = (USHORT)luaL_checkinteger(L, 3);
    ITcpServer* Svr = GetTcpServer(L);
    BOOL r = Svr->Start(ip, port);
    lua_pushboolean(L, r);
    return 1;
}

int ServerGetListenAddress(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    luaL_Buffer b;
    char* p = luaL_buffinitsize(L, &b, LUAL_BUFFERSIZE);
    int len = LUAL_BUFFERSIZE;
    USHORT port;
    if (Svr->GetListenAddress(p, len, port)) {
        luaL_pushresultsize(&b, len - 1);
        lua_pushinteger(L, port);
        return 2;
    }
    return 0;
}

//IComplexSocket
int ServerStop(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    HP_mutex* mutex = *(HP_mutex**)lua_getextraspace(L);
    HP_UnlockMutex(mutex);
    BOOL r = Svr->Stop();
    HP_LockMutex(mutex);
    lua_pushboolean(L, r);
    return 1;
}

int ServerSend(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    size_t len;
    BYTE* buf = (BYTE*)luaL_checklstring(L, 3, &len);
    lua_pushboolean(L, Svr->Send((CONNID)luaL_checkinteger(L, 2), buf, (int)len, (int)luaL_optinteger(L, 4, 0)));
    return 1;
}
//int ServerSendPackets(lua_State* L){
//	ITcpServer* Svr = GetTcpServer(L);
//
//	return 0;
//}
int ServerPauseReceive(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushboolean(L, Svr->PauseReceive((CONNID)luaL_checkinteger(L, 2), luaL_optboolean(L, 3, TRUE)));
    return 1;
}

int ServerDisconnect(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushboolean(L, Svr->Disconnect((CONNID)luaL_checkinteger(L, 2), luaL_optboolean(L, 3, TRUE)));
    return 1;
}

int ServerDisconnectLongConnections(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushboolean(L, Svr->DisconnectLongConnections((DWORD)luaL_checkinteger(L, 2), luaL_optboolean(L, 3, TRUE)));
    return 1;
}

int ServerDisconnectSilenceConnections(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushboolean(L, Svr->DisconnectSilenceConnections((DWORD)luaL_checkinteger(L, 2), luaL_optboolean(L, 3, TRUE)));
    return 1;
}

int ServerWait(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushboolean(L, Svr->Wait((DWORD)luaL_optinteger(L, 2, INFINITE)));
    return 1;
}
//int ServerSetConnectionExtra(lua_State* L){
//	ITcpServer* Svr = GetTcpServer(L);
//
//	return 0;
//}
//int ServerGetConnectionExtra(lua_State* L){
//	ITcpServer* Svr = GetTcpServer(L);
//
//	return 0;
//}
int ServerIsSecure(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushboolean(L, Svr->IsSecure());
    return 1;
}

int ServerHasStarted(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushboolean(L, Svr->HasStarted());
    return 1;
}

int ServerGetState(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    switch (Svr->GetState()) {
        CASE(SS_STARTING)
            CASE(SS_STARTED)
            CASE(SS_STOPPING)
            CASE(SS_STOPPED)
    }
    return 1;
}

int ServerGetConnectionCount(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushinteger(L, Svr->GetConnectionCount());
    return 1;
}

int ServerGetAllConnectionIDs(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    DWORD num = Svr->GetConnectionCount();
    CONNID* id = new CONNID[num];
    lua_createtable(L, num, 0);
    if (Svr->GetAllConnectionIDs(id, num)) {
        for (DWORD i = 0; i < num; i++) {
            lua_pushinteger(L, id[i]);
            lua_seti(L, -2, i + 1);
        }
    }
    delete[]id;
    return 1;
}

int ServerGetConnectPeriod(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    DWORD v;
    Svr->GetConnectPeriod((CONNID)luaL_checkinteger(L, 2), v);
    lua_pushinteger(L, v);
    return 1;
}

int ServerGetSilencePeriod(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    DWORD v;
    Svr->GetSilencePeriod((CONNID)luaL_checkinteger(L, 2), v);
    lua_pushinteger(L, v);
    return 1;
}

int ServerGetLocalAddress(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    luaL_Buffer b;
    char* p = luaL_buffinitsize(L, &b, LUAL_BUFFERSIZE);
    int len = LUAL_BUFFERSIZE;
    USHORT port;
    if (Svr->GetLocalAddress((CONNID)luaL_checkinteger(L, 2), p, len, port)) {
        luaL_pushresultsize(&b, len - 1);
        lua_pushinteger(L, port);
        return 2;
    }
    return 0;
}

int ServerGetRemoteAddress(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    luaL_Buffer b;
    char* p = luaL_buffinitsize(L, &b, LUAL_BUFFERSIZE);
    int len = LUAL_BUFFERSIZE;
    USHORT port;
    if (Svr->GetRemoteAddress((CONNID)luaL_checkinteger(L, 2), p, len, port)) {
        luaL_pushresultsize(&b, len - 1);
        lua_pushinteger(L, port);
        return 2;
    }
    return 0;
}

int ServerGetLastError(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    switch (Svr->GetLastError()) {
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

int ServerGetLastErrorDesc(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushstring(L, Svr->GetLastErrorDesc());
    return 1;
}

int ServerGetPendingDataLength(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    int v;
    Svr->GetPendingDataLength((CONNID)luaL_checkinteger(L, 2), v);
    lua_pushinteger(L, v);
    return 1;
}

int ServerIsPauseReceive(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    BOOL v;
    Svr->GetPendingDataLength((CONNID)luaL_checkinteger(L, 2), v);
    lua_pushboolean(L, v);
    return 1;
}

int ServerIsConnected(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushboolean(L, Svr->IsConnected((CONNID)luaL_checkinteger(L, 2)));
    return 1;
}

int ServerSetReuseAddressPolicy(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    static const char* const opts[] = { "RAP_NONE", "RAP_ADDR_ONLY", "RAP_ADDR_AND_PORT", NULL };
    Svr->SetReuseAddressPolicy(EnReuseAddressPolicy(luaL_checkoption(L, 2, "RAP_NONE", opts)));
    return 0;
}

int ServerSetSendPolicy(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    static const char* const opts[] = { "SP_PACK", "SP_SAFE", "SP_DIRECT", NULL };
    Svr->SetSendPolicy(EnSendPolicy(luaL_checkoption(L, 2, "SP_PACK", opts)));
    return 0;
}

int ServerSetOnSendSyncPolicy(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    static const char* const opts[] = { "OSSP_NONE", "OSSP_CLOSE", "OSSP_RECEIVE", NULL };
    Svr->SetOnSendSyncPolicy(EnOnSendSyncPolicy(luaL_checkoption(L, 2, "OSSP_NONE", opts)));
    return 0;
}

int ServerSetMaxConnectionCount(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    Svr->SetMaxConnectionCount((DWORD)luaL_checkinteger(L, 2));
    return 0;
}

int ServerSetFreeSocketObjLockTime(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    Svr->SetFreeSocketObjLockTime((DWORD)luaL_checkinteger(L, 2));
    return 0;
}

int ServerSetFreeSocketObjPool(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    Svr->SetFreeSocketObjPool((DWORD)luaL_checkinteger(L, 2));
    return 0;
}

int ServerSetFreeBufferObjPool(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    Svr->SetFreeBufferObjPool((DWORD)luaL_checkinteger(L, 2));
    return 0;
}

int ServerSetFreeSocketObjHold(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    Svr->SetFreeSocketObjHold((DWORD)luaL_checkinteger(L, 2));
    return 0;
}

int ServerSetFreeBufferObjHold(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    Svr->SetFreeBufferObjHold((DWORD)luaL_checkinteger(L, 2));
    return 0;
}

int ServerSetWorkerThreadCount(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    Svr->SetWorkerThreadCount((DWORD)luaL_checkinteger(L, 2));
    return 0;
}

int ServerSetMarkSilence(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    Svr->SetMarkSilence(lua_toboolean(L, 2));
    return 0;
}

int ServerGetReuseAddressPolicy(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    switch (Svr->GetReuseAddressPolicy()) {
        CASE(RAP_NONE)
            CASE(RAP_ADDR_ONLY)
            CASE(RAP_ADDR_AND_PORT)
    }
    return 1;
}

int ServerGetSendPolicy(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    switch (Svr->GetSendPolicy()) {
        CASE(SP_PACK)
            CASE(SP_SAFE)
            CASE(SP_DIRECT)
    }
    return 1;
}

int ServerGetOnSendSyncPolicy(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    switch (Svr->GetOnSendSyncPolicy()) {
        CASE(OSSP_NONE)
            CASE(OSSP_CLOSE)
            CASE(OSSP_RECEIVE)
    }
    return 1;
}

int ServerGetMaxConnectionCount(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushinteger(L, Svr->GetMaxConnectionCount());
    return 1;
}

int ServerGetFreeSocketObjLockTime(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushinteger(L, Svr->GetFreeSocketObjLockTime());
    return 1;
}

int ServerGetFreeSocketObjPool(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushinteger(L, Svr->GetFreeSocketObjPool());
    return 1;
}

int ServerGetFreeBufferObjPool(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushinteger(L, Svr->GetFreeBufferObjPool());
    return 1;
}

int ServerGetFreeSocketObjHold(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushinteger(L, Svr->GetFreeSocketObjHold());
    return 1;
}

int ServerGetFreeBufferObjHold(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushinteger(L, Svr->GetFreeBufferObjHold());
    return 1;
}

int ServerGetWorkerThreadCount(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushinteger(L, Svr->GetWorkerThreadCount());
    return 1;
}

int ServerIsMarkSilence(lua_State* L)
{
    ITcpServer* Svr = GetTcpServer(L);
    lua_pushboolean(L, Svr->IsMarkSilence());
    return 1;
}