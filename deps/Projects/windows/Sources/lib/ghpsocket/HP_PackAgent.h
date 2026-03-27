#pragma once
#include "common.h"

class HP_PackAgent : public HP_Base,public CTcpAgentListener
{
public:
	HP_PackAgent(lua_State* L):HP_Base(L){
		m_Agent = HP_Create_TcpPackAgent(this);
	};

	~HP_PackAgent(){
		HP_Destroy_TcpPackAgent(m_Agent);
	};

	virtual EnHandleResult OnPrepareConnect(ITcpAgent* pSender, CONNID dwConnID, SOCKET socket);
	virtual EnHandleResult OnConnect(ITcpAgent* pSender, CONNID dwConnID);
	//virtual EnHandleResult OnHandShake(ITcpAgent* pSender, CONNID dwConnID);
	virtual EnHandleResult OnReceive(ITcpAgent* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHandleResult OnSend(ITcpAgent* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHandleResult OnShutdown(ITcpAgent* pSender);
	virtual EnHandleResult OnClose(ITcpAgent* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode);

	ITcpPackAgent*  m_Agent;
};