#pragma once
#include "common.h"

class HP_PullClient : public HP_Base, public CTcpPullClientListener
{
public:
	HP_PullClient(lua_State* L) :HP_Base(L) {
		m_Client = HP_Create_TcpPullClient(this);
	};

	~HP_PullClient() {
		HP_Destroy_TcpPullClient(m_Client);
	};

	virtual EnHandleResult OnPrepareConnect(ITcpClient* pSender, CONNID dwConnID, SOCKET socket);
	virtual EnHandleResult OnConnect(ITcpClient* pSender, CONNID dwConnID);
	//virtual EnHandleResult OnHandShake(ITcpClient* pSender, CONNID dwConnID);
	virtual EnHandleResult OnReceive(ITcpClient* pSender, CONNID dwConnID, int iLength);
	virtual EnHandleResult OnSend(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHandleResult OnClose(ITcpClient* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode);

	ITcpPullClient* m_Client;
};
