#pragma once
#include "common.h"

class HP_PackClient : public HP_Base,public CTcpClientListener
{
public:
	HP_PackClient(lua_State* L):HP_Base(L){
		m_Client = HP_Create_TcpPackClient(this);
	};

	~HP_PackClient(){
		HP_Destroy_TcpPackClient(m_Client);
	};

	virtual EnHandleResult OnPrepareConnect(ITcpClient* pSender, CONNID dwConnID, SOCKET socket);
	virtual EnHandleResult OnConnect	(ITcpClient* pSender, CONNID dwConnID);
	//virtual EnHandleResult OnHandShake(ITcpClient* pSender, CONNID dwConnID);
	virtual EnHandleResult OnReceive	(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHandleResult OnSend		(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHandleResult OnClose		(ITcpClient* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode);

	ITcpPackClient* m_Client;
};