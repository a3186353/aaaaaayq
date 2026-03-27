#pragma once
#include "common.h"

class HP_UdpNode : public HP_Base,public CUdpNodeListener
{
public:
	HP_UdpNode(lua_State* L):HP_Base(L){
		m_UdpNode = HP_Create_UdpNode(this);
	};

	~HP_UdpNode(){
		HP_Destroy_UdpNode(m_UdpNode);
	};

	virtual EnHandleResult OnPrepareListen(IUdpNode* pSender, SOCKET soListen);
	virtual EnHandleResult OnSend(IUdpNode* pSender, LPCTSTR lpszRemoteAddress, USHORT usRemotePort, const BYTE* pData, int iLength);
	virtual EnHandleResult OnReceive(IUdpNode* pSender, LPCTSTR lpszRemoteAddress, USHORT usRemotePort, const BYTE* pData, int iLength);
	virtual EnHandleResult OnError(IUdpNode* pSender, EnSocketOperation enOperation, int iErrorCode, LPCTSTR lpszRemoteAddress, USHORT usRemotePort, const BYTE* pBuffer, int iLength);
	virtual EnHandleResult OnShutdown(IUdpNode* pSender);

	IUdpNode* m_UdpNode;
};