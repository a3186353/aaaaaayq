#pragma once
#include "common.h"

class HP_HttpClient : public HP_Base,public CHttpClientListener
{
public:
	HP_HttpClient(lua_State* L):HP_Base(L){
		m_Client = HP_Create_HttpClient(this);
	};

	~HP_HttpClient(){
		HP_Destroy_HttpClient(m_Client);
	};

	virtual EnHandleResult OnSend(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHandleResult OnReceive(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHandleResult OnClose(ITcpClient* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode);
	virtual EnHandleResult OnConnect(ITcpClient* pSender, CONNID dwConnID);
	//virtual EnHandleResult OnHandShake(ITcpClient* pSender, CONNID dwConnID);

	virtual EnHttpParseResult OnMessageBegin(IHttpClient* pSender, CONNID dwConnID);
	virtual EnHttpParseResult OnStatusLine(IHttpClient* pSender, CONNID dwConnID, USHORT usStatusCode, LPCSTR lpszDesc);
	virtual EnHttpParseResult OnHeader(IHttpClient* pSender, CONNID dwConnID, LPCSTR lpszName, LPCSTR lpszValue);
	virtual EnHttpParseResult OnHeadersComplete(IHttpClient* pSender, CONNID dwConnID);
	virtual EnHttpParseResult OnBody(IHttpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHttpParseResult OnChunkHeader(IHttpClient* pSender, CONNID dwConnID, int iLength);
	virtual EnHttpParseResult OnChunkComplete(IHttpClient* pSender, CONNID dwConnID);
	virtual EnHttpParseResult OnMessageComplete(IHttpClient* pSender, CONNID dwConnID);
	virtual EnHttpParseResult OnUpgrade(IHttpClient* pSender, CONNID dwConnID, EnHttpUpgradeType enUpgradeType);
	virtual EnHttpParseResult OnParseError(IHttpClient* pSender, CONNID dwConnID, int iErrorCode, LPCSTR lpszErrorDesc);

	//virtual EnHandleResult OnWSMessageHeader(IHttpClient* pSender, CONNID dwConnID, BOOL bFinal, BYTE iReserved, BYTE iOperationCode, const BYTE lpszMask[4], ULONGLONG ullBodyLen);
	//virtual EnHandleResult OnWSMessageBody(IHttpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	//virtual EnHandleResult OnWSMessageComplete(IHttpClient* pSender, CONNID dwConnID);

	IHttpClient* m_Client;
};