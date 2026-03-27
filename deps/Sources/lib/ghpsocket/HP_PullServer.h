#pragma once
#include "common.h"

class HP_PullServer : public HP_Base, public CTcpPullServerListener
{
public:
	HP_PullServer(lua_State* L) :HP_Base(L) {
		m_Server = HP_Create_TcpPullServer(this);
	};

	~HP_PullServer() {
		HP_Destroy_TcpPullServer(m_Server);
	};

	virtual EnHandleResult OnPrepareListen(ITcpServer* pSender, SOCKET soListen);
	virtual EnHandleResult OnAccept(ITcpServer* pSender, CONNID dwConnID, UINT_PTR soClient);
	//virtual EnHandleResult OnHandShake(ITcpServer* pSender, CONNID dwConnID);
	virtual EnHandleResult OnReceive(ITcpServer* pSender, CONNID dwConnID, int iLength);
	virtual EnHandleResult OnSend(ITcpServer* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHandleResult OnShutdown(ITcpServer* pSender);
	virtual EnHandleResult OnClose(ITcpServer* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode);

	ITcpPullServer* m_Server;
};