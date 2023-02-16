#pragma once
#include <libwebsockets.h>

#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

#include "LogManager.h"

#define WS_PORT 7681
#define WS_MAX_MSGLEN 1048576

class CBaseServer {
public:
	enum class ConnectionThreadState { idle, run, exiting, exit };	
	struct _ConnectionMessage {
		enum class DataDirection { inbound, outbound, none } flow = DataDirection::none;
		void* lp_connection = nullptr;
		std::string data = "";
		_ConnectionMessage() = default;		
		_ConnectionMessage(DataDirection flow_, void* conn_, std::string data_):flow(flow_), lp_connection(conn_), data(std::move(data_))
		{};
	};	
	CBaseServer() {
		m_pLog = CLogManager::getInstance();
	}
protected:	
	
	//std::vector<_ConnectionMessage> m_inbound;
	std::list<_ConnectionMessage> m_inbound;
	//std::vector<_ConnectionMessage> m_outbound;
	std::list<_ConnectionMessage> m_outbound;
	//std::vector<_ConnectionMessage> m_internal_connection_list;
	std::list<_ConnectionMessage> m_internal_connection_list;

	//std::vector<void*> m_new_connections;
	std::list<void*> m_new_connections;
	//std::vector<void*> m_disconnected_connections;
	std::list<void*> m_disconnected_connections;
	
	
	std::thread  m_thread;
	
	std::atomic<bool> m_is_running;

	std::mutex m_inbound_lock;
	std::mutex m_outbound_lock;
	std::mutex m_connection_lock;

	
	CLogManager* m_pLog = nullptr;
	virtual int Init() { return 0; };
	virtual int Uninit() { return 0; };

public:		
	virtual ~CBaseServer() {};
	
	virtual int Run() { return 0; };

	virtual void FnConnectionThread() {};

	int AsyncGetMessage(_ConnectionMessage& message);
	int AsyncPutMessage(const _ConnectionMessage& message);	

	bool AsyncGetNewConnection(void** lp_connection);
	bool AsyncGetDisconnectedConnection(void** lp_connection);

	void AsyncAddNewConnection(void* p);
	void AsyncAddDisconnectedConnection(void* p);

	virtual unsigned int AsyncGetNewConnection() { return 0; };
};

class CWSServer : public CBaseServer {

private:
	static struct lws_protocols m_protocols[];

	struct lws_context_creation_info m_info;
	struct lws_context* m_pcontext;	

	//std::vector<_ConnectionMessage> m_outbound_data_buffer;
	std::list<_ConnectionMessage> m_outbound_data_buffer;
	//std::vector<_ConnectionMessage> m_inbound_data_buffer;
	std::list<_ConnectionMessage> m_inbound_data_buffer;
	
protected:
	int Init();
	int Uninit();

public:		
	~CWSServer();
	
	std::vector<unsigned int> sockets;

	static int callback_client(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);
	
	int Run();

	void SignalStop(bool signal);	
	void FnConnectionThread();

	int SendWSData(const std::string& datatosend, struct lws * wsi);

	void AsyncAddMessageToQueue(const _ConnectionMessage& message);
	bool AsyncGetMessageFromQueue(_ConnectionMessage& message);

	int SendDataToClientIfAvalable(struct lws * wsi);
	int UploadDataToControlIfAvalable(struct lws * wsi, std::string& data);
};