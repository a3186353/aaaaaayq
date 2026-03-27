#include "HP_TcpServer.h"
#include "HP_PullServer.h"


EnHandleResult HP_PullServer::OnPrepareListen(ITcpServer* pSender, SOCKET soListen)
{
    if (GetCallBack("OnPrepareListen"))
    {
        lua_pushinteger(L, soListen);
        lua_call(L, 2, 1);
        return GetResult();
    }
    return HR_OK;
}

EnHandleResult HP_PullServer::OnAccept(ITcpServer* pSender, CONNID dwConnID, UINT_PTR soClient)
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

EnHandleResult HP_PullServer::OnReceive(ITcpServer* pSender, CONNID dwConnID, int iLength)
{
    if (GetCallBack("OnReceive"))
    {
        lua_pushinteger(L, dwConnID);
        lua_pushinteger(L, iLength);
        lua_call(L, 3, 1);
        return GetResult();
    }
    return HR_OK;
}

EnHandleResult HP_PullServer::OnSend(ITcpServer* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
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

EnHandleResult HP_PullServer::OnShutdown(ITcpServer* pSender)
{
    if (GetCallBack("OnShutdown"))
    {
        lua_call(L, 1, 1);
        return GetResult();
    }
    return HR_OK;
}

EnHandleResult HP_PullServer::OnClose(ITcpServer* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode)
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


static ITcpPullServer* GetPullServer(lua_State* L)
{
    HP_PullServer* p = *(HP_PullServer**)luaL_checkudata(L, 1, "HP_PullServer");
    return p->m_Server;
}

//ITcpPullServer

static int Fetch(lua_State* L)
{
    ITcpPullServer* Svr = GetPullServer(L);
    CONNID dwConnID = (CONNID)luaL_checkinteger(L, 2);
    BYTE* buf = (BYTE*)lua_topointer(L, 3);
    int len = (int)luaL_checkinteger(L, 4);
    EnFetchResult r = Svr->Fetch(dwConnID, buf, len);
    lua_pushinteger(L, r);
    return 1;
}

static int Peek(lua_State* L)
{
    ITcpPullServer* Svr = GetPullServer(L);
    CONNID dwConnID = (CONNID)luaL_checkinteger(L, 2);
    BYTE* buf = (BYTE*)lua_topointer(L, 3);
    int len = (int)luaL_checkinteger(L, 4);
    EnFetchResult r = Svr->Peek(dwConnID, buf, len);
    lua_pushinteger(L, r);
    return 1;
}


static int Creat_HP_PullServer(lua_State* L)
{
    HP_PullServer* p = new HP_PullServer(L);
    HP_PullServer** ud = (HP_PullServer**)lua_newuserdata(L, sizeof(HP_PullServer*));
    *ud = p;
    lua_pushlightuserdata(L, (ITcpServer*)p->m_Server);
    lua_setuservalue(L, -2);
    luaL_setmetatable(L, "HP_PullServer");
    return 1;
}

static int Destroy_HP_PullServer(lua_State* L)
{
    HP_PullServer* ud = *(HP_PullServer**)luaL_checkudata(L, 1, "HP_PullServer");
    ServerStop(L);
    delete ud;
    return 0;
}

extern "C"
#if defined(LUA_BUILD_AS_DLL)
LUAMOD_API
#endif
int luaopen_ghpsocket_pullserver(lua_State * L)
{
    luaL_Reg methods[] = {
        //ITcpPullServer
        {"Fetch",Fetch},
        {"Peek",Peek},
        //ITcpServer
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
        //IServer
        {"Start",ServerStart},
        {"GetListenAddress",ServerGetListenAddress},
        {"Stop",ServerStop},
        {"Send",ServerSend},
        //{"SendPullets",SendPullets},
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
    luaL_newmetatable(L, "HP_PullServer");
    luaL_newlib(L, methods);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, Destroy_HP_PullServer);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    lua_pushcfunction(L, Creat_HP_PullServer);
    return 1;
}