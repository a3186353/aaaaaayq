#pragma once
#include "common.h"
class HP_PullAgent : public HP_Base, public CTcpPullAgentListener
{
public:
	HP_PullAgent(lua_State* L) :HP_Base(L) {
		m_Agent = HP_Create_TcpPullAgent(this);
	};

	~HP_PullAgent() {
		HP_Destroy_TcpPullAgent(m_Agent);
	};

	virtual EnHandleResult OnPrepareConnect(ITcpAgent* pSender, CONNID dwConnID, SOCKET socket);
	virtual EnHandleResult OnConnect(ITcpAgent* pSender, CONNID dwConnID);
	//virtual EnHandleResult OnHandShake(ITcpAgent* pSender, CONNID dwConnID);
	virtual EnHandleResult OnReceive(ITcpAgent* pSender, CONNID dwConnID, int iLength);
	virtual EnHandleResult OnReceive(ITcpAgent* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHandleResult OnSend(ITcpAgent* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHandleResult OnShutdown(ITcpAgent* pSender);
	virtual EnHandleResult OnClose(ITcpAgent* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode);

	ITcpPullAgent* m_Agent;
};
