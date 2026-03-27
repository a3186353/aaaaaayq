#pragma once
#include "common.h"

int ClientSendSmallFile(lua_State* L);
int ClientSetSocketBufferSize(lua_State* L);
int ClientSetKeepAliveTime(lua_State* L);
int ClientSetKeepAliveInterval(lua_State* L);
int ClientGetSocketBufferSize(lua_State* L);
int ClientGetKeepAliveTime(lua_State* L);
int ClientGetKeepAliveInterval(lua_State* L);
int ClientStart(lua_State* L);
int ClientStop(lua_State* L);
int ClientSend(lua_State* L);
//int ClientSendPackets(lua_State* L){;
int ClientPauseReceive(lua_State* L);
int ClientWait(lua_State* L);
//int ClientSetExtra(lua_State* L){;
//int ClientGetExtra(lua_State* L){;
int ClientIsSecure(lua_State* L);
int ClientHasStarted(lua_State* L);
int ClientGetState(lua_State* L);
int ClientGetLastError(lua_State* L);
int ClientGetLastErrorDesc(lua_State* L);
int ClientGetConnectionID(lua_State* L);
int ClientGetLocalAddress(lua_State* L);
int ClientGetRemoteHost(lua_State* L);
int ClientGetPendingDataLength(lua_State* L);
int ClientIsPauseReceive(lua_State* L);
int ClientIsConnected(lua_State* L);
int ClientSetReuseAddressPolicy(lua_State* L);
int ClientSetFreeBufferPoolSize(lua_State* L);
int ClientSetFreeBufferPoolHold(lua_State* L);
int ClientGetReuseAddressPolicy(lua_State* L);
int ClientGetFreeBufferPoolSize(lua_State* L);
int ClientGetFreeBufferPoolHold(lua_State* L);