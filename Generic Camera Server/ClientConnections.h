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
		enum class DataDirection { inbound, outbound } flow;
		void* lp_connection;
		std::string data;
	};

protected:	
	std::shared_ptr<CLogManager> m_pLog = nullptr;

	std::vector<_ConnectionMessage> m_inbound;
	std::vector<_ConnectionMessage> m_outbound;
	std::vector<_ConnectionMessage> m_internal_connection_list;

	std::vector<void*> m_new_connections;
	std::vector<void*> m_disconnected_connections;
	
	std::thread  m_thread;
	std::atomic<ConnectionThreadState> m_thread_status;	
	std::atomic<bool> m_is_running;

	std::mutex m_inbound_lock;
	std::mutex m_outbound_lock;
	std::mutex m_connection_lock;

public:		
	virtual ~CBaseServer() {};
	virtual int Init() { return 0; };
	virtual int Uninit() { return 0; };
	virtual void Run() {};

	virtual void FnConnectionThread() {};

	int AsyncGetMessage(_ConnectionMessage& message);
	int AsyncPutMessage(_ConnectionMessage& message);	

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

	std::vector<_ConnectionMessage> m_outbound_data_buffer;
	std::vector<_ConnectionMessage> m_inbound_data_buffer;
	
public:		
	CWSServer(std::shared_ptr<CLogManager> log);
	~CWSServer();
	
	std::vector<unsigned int> sockets;

	static int callback_client(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);
	
	int Init();
	int Uninit();

	void Run();
	void SignalStop(bool signal);	
	void FnConnectionThread();

	int SendWSData(std::string& datatosend, struct lws * wsi);

	void AsyncAddMessageToQueue(_ConnectionMessage& message);
	bool AsyncGetMessageFromQueue(_ConnectionMessage& message);

	int SendDataToClientIfAvalable(struct lws * wsi);
	int UploadDataToControlIfAvalable(struct lws * wsi, std::string& data);
};