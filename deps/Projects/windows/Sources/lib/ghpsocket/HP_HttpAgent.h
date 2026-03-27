#pragma once
#include "common.h"

class HP_HttpAgent : public HP_Base, public CHttpAgentListener
{
public:
	HP_HttpAgent(lua_State* L) :HP_Base(L) {
		m_Agent = HP_Create_HttpAgent(this);
	};

	~HP_HttpAgent() {
		HP_Destroy_HttpAgent(m_Agent);
	};

	virtual EnHandleResult OnPrepareConnect(ITcpAgent* pSender, CONNID dwConnID, SOCKET socket);
	virtual EnHandleResult OnConnect(ITcpAgent* pSender, CONNID dwConnID);
	virtual EnHandleResult OnReceive(ITcpAgent* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHandleResult OnSend(ITcpAgent* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHandleResult OnShutdown(ITcpAgent* pSender);
	virtual EnHandleResult OnClose(ITcpAgent* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode);

	virtual EnHttpParseResult OnMessageBegin(IHttpAgent* pSender, CONNID dwConnID);
	virtual EnHttpParseResult OnRequestLine(IHttpAgent* pSender, CONNID dwConnID, LPCSTR lpszMethod, LPCSTR lpszUrl);
	virtual EnHttpParseResult OnStatusLine(IHttpAgent* pSender, CONNID dwConnID, USHORT usStatusCode, LPCSTR lpszDesc);
	virtual EnHttpParseResult OnHeader(IHttpAgent* pSender, CONNID dwConnID, LPCSTR lpszName, LPCSTR lpszValue);
	virtual EnHttpParseResult OnHeadersComplete(IHttpAgent* pSender, CONNID dwConnID);
	virtual EnHttpParseResult OnBody(IHttpAgent* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHttpParseResult OnChunkHeader(IHttpAgent* pSender, CONNID dwConnID, int iLength);
	virtual EnHttpParseResult OnChunkComplete(IHttpAgent* pSender, CONNID dwConnID);
	virtual EnHttpParseResult OnMessageComplete(IHttpAgent* pSender, CONNID dwConnID);
	virtual EnHttpParseResult OnUpgrade(IHttpAgent* pSender, CONNID dwConnID, EnHttpUpgradeType enUpgradeType);
	virtual EnHttpParseResult OnParseError(IHttpAgent* pSender, CONNID dwConnID, int iErrorCode, LPCSTR lpszErrorDesc);

	IHttpAgent* m_Agent;
};
