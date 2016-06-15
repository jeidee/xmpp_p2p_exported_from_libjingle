
#include "talk/base/thread.h"
#include "talk/xmpp/constants.h"
#include "talk/xmpp/xmppclientsettings.h"
#include "talk/xmpp/xmppengine.h"
#include "talk/xmpp/xmppthread.h"
#include "talk/base/ssladapter.h"
#include "talk/xmpp/xmpppump.h"
#include "talk/xmpp/xmppauth.h"
#include "talk/xmpp/presencestatus.h"
#include "talk/xmpp/presencereceivetask.h"
#include "talk/xmpp/presenceouttask.h"
#include "talk/xmpp/jingleinfotask.h"
#include "talk/xmpp/xmpptask.h"
#include "talk/base/logging.h"
#include "talk/base/ssladapter.h"

#include <cstdio>
#include <iostream>
#include <string>

using namespace std;


const buzz::StaticQName QN_SVPN = { "svpn:webrtc", "data" };

#ifdef WIN32
#include <Windows.h>
#include <OleAuto.h>
char* UTF8ToANSI(const char *pszCode)
{
	BSTR    bstrWide;
	char*   pszAnsi;
	int     nLength;

	nLength = MultiByteToWideChar(CP_UTF8, 0, pszCode, lstrlen(pszCode) + 1, NULL, NULL);
	bstrWide = SysAllocStringLen(NULL, nLength);

	MultiByteToWideChar(CP_UTF8, 0, pszCode, lstrlen(pszCode) + 1, bstrWide, nLength);

	nLength = WideCharToMultiByte(CP_ACP, 0, bstrWide, -1, NULL, 0, NULL, NULL);
	pszAnsi = new char[nLength];

	WideCharToMultiByte(CP_ACP, 0, bstrWide, -1, pszAnsi, nLength, NULL, NULL);
	SysFreeString(bstrWide);

	return pszAnsi;
}

char* ANSIToUTF8(const char * pszCode)
{
	int		nLength, nLength2;
	BSTR	bstrCode;
	char*	pszUTFCode = NULL;

	nLength = MultiByteToWideChar(CP_ACP, 0, pszCode, lstrlen(pszCode), NULL, NULL);
	bstrCode = SysAllocStringLen(NULL, nLength);
	MultiByteToWideChar(CP_ACP, 0, pszCode, lstrlen(pszCode), bstrCode, nLength);

	nLength2 = WideCharToMultiByte(CP_UTF8, 0, bstrCode, -1, pszUTFCode, 0, NULL, NULL);
	pszUTFCode = (char*)malloc(nLength2 + 1);
	WideCharToMultiByte(CP_UTF8, 0, bstrCode, -1, pszUTFCode, nLength2, NULL, NULL);

	return pszUTFCode;
}
#else
// iconv 사용해서 구현할 것
char* UTF8ToANSI(const char *pszCode)
{
	return pszCode;
}

char* ANSIToUTF8(const char * pszCode)
{
	return pszCode;
}
#endif

class SjingleLog : public sigslot::has_slots<> {
public:
	void OnInputLogging(const char* data, int len);
	void OnOutputLogging(const char* data, int len);
};

class SvpnTask : public buzz::XmppTask {
public:
	explicit SvpnTask(buzz::XmppTaskParentInterface* parent)
		: XmppTask(parent, buzz::XmppEngine::HL_SINGLE) {}

	void SendRequest(const buzz::Jid &to, const std::string &data);

protected:
	virtual int ProcessStart();
	virtual bool HandleStanza(const buzz::XmlElement* stanza);

};

class PeerDiscovery : public sigslot::has_slots<> {
public:
	explicit PeerDiscovery(buzz::XmppClient *client);

private:
	buzz::XmppClient* client_;
	buzz::PresenceStatus my_status_;
	buzz::PresenceReceiveTask presence_receive_;
	buzz::PresenceOutTask presence_out_;
	buzz::JingleInfoTask jingle_info_;
	SvpnTask svpn_task_;
	void OnPresenceUpdate(const buzz::PresenceStatus &status);
	void OnJingleInfo(const std::string &relay_token,
		const std::vector<std::string> &relay_addresses,
		const std::vector<talk_base::SocketAddress>
		&stun_addresses);
	void OnSignOn();
	void OnStateChange(buzz::XmppEngine::State state);
};

void SjingleLog::OnInputLogging(const char* data, int len) {
	string log(data, len);
	cout << "[RECV] " << log << endl;
}

void SjingleLog::OnOutputLogging(const char* data, int len) {
	string log(data, len);
	cout << "[SEND] " << log << endl;
}

void SvpnTask::SendRequest(const buzz::Jid &to, const std::string &data) {
	talk_base::scoped_ptr<buzz::XmlElement> get(
		MakeIq("get", to, task_id()));
	buzz::XmlElement* element = new buzz::XmlElement(QN_SVPN);
	element->SetBodyText(data);
	get->AddElement(element);
	SendStanza(get.get());
	//QueueStanza
}

int SvpnTask::ProcessStart() {
	// Call NextStanza
	std::cout << "PROCESS START" << std::endl;

	return STATE_BLOCKED;
}

bool SvpnTask::HandleStanza(const buzz::XmlElement* stanza) {
	std::cout << "HANDLE STANZA" << std::endl;
	std::cout << UTF8ToANSI(stanza->Str().c_str()) << std::endl;

	if (!MatchRequestIq(stanza, "get", QN_SVPN)) {
		return false;
	}
	std::cout << "MATCH" << std::endl;
	return true;
	//QueueStanza
}

PeerDiscovery::PeerDiscovery(buzz::XmppClient *client) :
	client_(client),
	presence_receive_(client),
	presence_out_(client),
	jingle_info_(client),
	svpn_task_(client) {
	my_status_.set_jid(client->jid());
	my_status_.set_available(true);
	my_status_.set_show(buzz::PresenceStatus::SHOW_ONLINE);
	my_status_.set_priority(0);

	client_->SignalStateChange.connect(this, &PeerDiscovery::OnStateChange);
	presence_receive_.PresenceUpdate.connect(this,
		&PeerDiscovery::OnPresenceUpdate);
	jingle_info_.SignalJingleInfo.connect(this, &PeerDiscovery::OnJingleInfo);
}

void PeerDiscovery::OnPresenceUpdate(const buzz::PresenceStatus &status) {
	cout << "UPDATE " << status.jid().Str() << endl;
	std::string data("yo mamma");
	svpn_task_.SendRequest(status.jid(), data);
}

void PeerDiscovery::OnJingleInfo(const std::string &relay_token,
	const std::vector<std::string> &relay_addresses,
	const std::vector<talk_base::SocketAddress> &stun_addresses) {
	cout << "relay token " << relay_token << endl;
	for (std::vector<std::string>::const_iterator it = relay_addresses.begin();
		it != relay_addresses.end(); ++it) {
		cout << "relay address " << *it << endl;
	}
	for (std::vector<talk_base::SocketAddress>::const_iterator
		it = stun_addresses.begin(); it != stun_addresses.end(); ++it) {
		cout << "socket address " << (*it).ToString() << endl;
	}
}

void PeerDiscovery::OnSignOn() {
	presence_receive_.Start();
	presence_out_.Send(my_status_);
	presence_out_.Start();
	jingle_info_.RefreshJingleInfoNow();
	jingle_info_.Start();
	svpn_task_.Start();
}

void PeerDiscovery::OnStateChange(buzz::XmppEngine::State state) {
	switch (state) {
	case buzz::XmppEngine::STATE_START:
		cout << "START" << endl;
		break;
	case buzz::XmppEngine::STATE_OPENING:
		cout << "OPENING" << endl;
		break;
	case buzz::XmppEngine::STATE_OPEN:
		cout << "OPEN" << endl;
		OnSignOn();
		break;
	case buzz::XmppEngine::STATE_CLOSED:
		cout << "CLOSED" << endl;
		break;
	}
}

int main(int argc, char **argv) {
	
	talk_base::LogMessage::LogToDebug(talk_base::LS_INFO);
	talk_base::InitializeSSL();

	SjingleLog log;
	buzz::XmppPump pump;
	pump.client()->SignalLogInput.connect(&log, &SjingleLog::OnInputLogging);
	pump.client()->SignalLogOutput.connect(&log,&SjingleLog::OnOutputLogging);

	std::cout << "User Name: ";
	std::string username;
	std::getline(std::cin, username);

	std::cout << "Password: ";
	std::string password;
	std::getline(std::cin, password);

	talk_base::InsecureCryptStringImpl pass;
	pass.password() = password;

	buzz::Jid jid(username);
	buzz::XmppClientSettings xcs;
	xcs.set_user(jid.node());
	xcs.set_host(jid.domain());
	xcs.set_resource("sjingle");
	xcs.set_use_tls(buzz::TLS_REQUIRED);
	xcs.set_pass(talk_base::CryptString(pass));
	xcs.set_server(talk_base::SocketAddress("10.5.23.33", 5223));

	pump.DoLogin(xcs, new buzz::XmppSocket(buzz::TLS_REQUIRED), 0);
	//new buzz::XmppAuth());
	PeerDiscovery discovery(pump.client());
	talk_base::Thread::Current()->Run();
	return 0;
}