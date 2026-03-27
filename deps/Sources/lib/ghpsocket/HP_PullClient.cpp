#include "HP_PullClient.h"
#include "HP_TcpClient.h"

EnHandleResult HP_PullClient::OnPrepareConnect(ITcpClient* pSender, CONNID dwConnID, SOCKET socket)
{
    if (GetCallBack("OnPrepareConnect")) {
        lua_pushinteger(L, dwConnID);
        lua_pushinteger(L, socket);
        lua_call(L, 3, 1);
        return GetResult();
    }
    return HR_OK;
}

EnHandleResult HP_PullClient::OnConnect(ITcpClient* pSender, CONNID dwConnID)
{
    if (GetCallBack("OnConnect"))
    {
        lua_pushinteger(L, dwConnID);
        lua_call(L, 2, 1);
        return GetResult();
    }
    return HR_OK;
}

EnHandleResult HP_PullClient::OnReceive(ITcpClient* pSender, CONNID dwConnID, int iLength)
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

EnHandleResult HP_PullClient::OnSend(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
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

EnHandleResult HP_PullClient::OnClose(ITcpClient* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode)
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

static BOOL luaL_optboolean(lua_State* L, int arg, BOOL def) {
    if (lua_isnoneornil(L, arg)) {
        return def;
    }
    return lua_toboolean(L, arg);
}

static ITcpPullClient* GetPullClient(lua_State* L) {
    HP_PullClient* p = *(HP_PullClient**)luaL_checkudata(L, 1, "HP_PullClient");
    return p->m_Client;
}


//ITcpPullClient

static int Fetch(lua_State* L) {
    ITcpPullClient* cli = GetPullClient(L);
    BYTE* buf = (BYTE*)lua_topointer(L, 2);
    int len = (int)luaL_checkinteger(L, 3);
    lua_pushinteger(L, (int)cli->Fetch(buf, len));
    return 1;
}

static int Peek(lua_State* L) {
    ITcpPullClient* cli = GetPullClient(L);
    BYTE* buf = (BYTE*)lua_topointer(L, 2);
    int len = (int)luaL_checkinteger(L, 3);
    lua_pushinteger(L, (int)cli->Peek(buf, len));
    return 1;
}

static int Creat_HP_PullClient(lua_State* L) {
    HP_PullClient* p = new HP_PullClient(L);
    HP_PullClient** ud = (HP_PullClient**)lua_newuserdata(L, sizeof(HP_PullClient*));
    *ud = p;
    lua_pushlightuserdata(L, (ITcpClient*)p->m_Client);
    lua_setuservalue(L, -2);
    luaL_setmetatable(L, "HP_PullClient");
    return 1;
}

static int Destroy_HP_PullClient(lua_State* L) {
    HP_PullClient* ud = *(HP_PullClient**)luaL_checkudata(L, 1, "HP_PullClient");
    ClientStop(L);
    delete ud;
    return 0;
}

extern "C"
#if defined(LUA_BUILD_AS_DLL)
LUAMOD_API
#endif
int luaopen_ghpsocket_pullclient(lua_State * L)
{
    luaL_Reg methods[] = {
        //ITcpPullClient
        {"Fetch",Fetch},
        {"Peek",Peek},
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
    luaL_newmetatable(L, "HP_PullClient");
    luaL_newlib(L, methods);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, Destroy_HP_PullClient);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    lua_pushcfunction(L, Creat_HP_PullClient);
    return 1;
}
