#pragma once
#include "common.h"

class HP_PackServer : public HP_Base,public CTcpServerListener
{
public:
	HP_PackServer(lua_State* L):HP_Base(L){
		m_Server = HP_Create_TcpPackServer(this);
	};

	~HP_PackServer(){
		HP_Destroy_TcpPackServer(m_Server);
	};

	virtual EnHandleResult OnPrepareListen(ITcpServer* pSender, SOCKET soListen);
	virtual EnHandleResult OnAccept(ITcpServer* pSender, CONNID dwConnID, UINT_PTR soClient);
	//virtual EnHandleResult OnHandShake(ITcpServer* pSender, CONNID dwConnID);
	virtual EnHandleResult OnReceive(ITcpServer* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHandleResult OnSend(ITcpServer* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHandleResult OnShutdown(ITcpServer* pSender);
	virtual EnHandleResult OnClose(ITcpServer* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode);

	ITcpPackServer* m_Server;
};