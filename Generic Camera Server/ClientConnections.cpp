#include "pch.h"
#include "ClientConnections.h"

#include <iostream>

#include <rapidjson/document.h>

CWSServer* g_pWSServer = nullptr;

int CBaseServer::AsyncGetMessage(_ConnectionMessage& message) {
	int ret = -1;
	try {
		std::lock_guard<std::mutex> guard_l(m_outbound_lock);
		if (m_inbound.size() > 0) {
			bool isFound = false;
			int index = 0;
			std::list<_ConnectionMessage>::iterator it;
			
			for (it = m_inbound.begin(); it != m_inbound.end(); ++it) {
				if (message.lp_connection == it->lp_connection) {
					isFound = true;
					break;
				}
			}
			if (isFound) {
				message = *it;
				m_inbound.erase(it);
				ret = 0;
			}
		}
	}
	catch (...) {
		//log something
	}
	return ret;
}

int CBaseServer::AsyncPutMessage(const _ConnectionMessage& message) {
	int ret = 0;
	
	try {
		std::lock_guard<std::mutex> guard_l(m_outbound_lock);
		m_outbound.emplace_back(message);
	}
	catch (...) {
		//log something
	}
	return ret;
}

bool CBaseServer::AsyncGetNewConnection(void** lp_connection) {
	bool ret = false;
	
	try {
		std::lock_guard<std::mutex> guard_l(m_connection_lock);
		if (m_new_connections.size() > 0) {
			*lp_connection = *m_new_connections.begin();
			m_new_connections.erase(m_new_connections.begin());
			ret = true;
		}

	}
	catch (...) {
		//log something
	}
	return ret;
}

bool CBaseServer::AsyncGetDisconnectedConnection(void** lp_connection) {
	bool ret = false;
	
	try {
		std::lock_guard<std::mutex> guard_l(m_connection_lock);
		if (m_disconnected_connections.size() > 0) {
			*lp_connection = &*m_disconnected_connections.begin();
			m_disconnected_connections.erase(m_disconnected_connections.begin());
			ret = true;
		}

	}
	catch (...) {
		//log something
	}
	return ret;
}

void CBaseServer::AsyncAddNewConnection(void* p) {	
	
	try {
		std::lock_guard<std::mutex> guard_l(m_connection_lock);
		m_new_connections.push_back(p);
	}
	catch (...) {
		//log something
	}
	m_internal_connection_list.emplace_back(_ConnectionMessage::DataDirection::none, p, "");
}

void CBaseServer::AsyncAddDisconnectedConnection(void* p) {
	
	try {
		std::lock_guard<std::mutex> guard_l(m_connection_lock);
		m_disconnected_connections.push_back(p);
	}
	catch (...) {
		//log something
	}
	
	for (auto it= m_internal_connection_list.begin();it!=m_internal_connection_list.end(); ++it) {
		if (it->lp_connection == p) {
			m_internal_connection_list.erase(it);
			break;
		}
	}
}


struct lws_protocols CWSServer::m_protocols[] = {
	{ "http", lws_callback_http_dummy, 0, 0 },
	{
		"client-protocol",
		callback_client,		
		sizeof(void*),
		WS_MAX_MSGLEN,
	},
	{ NULL, NULL, 0, 0 } /* terminator */
};

static const struct lws_protocol_vhost_options pvo_mime = {
NULL,				/* "next" pvo linked-list */
NULL,				/* "child" pvo linked-list */
".mp4",				/* file suffix to match */
"video/mp4"		/* mimetype to use */
};

static const struct lws_http_mount mount = {
	/* .mount_next */		NULL,		/* linked-list "next" */
	/* .mountpoint */		"/",		/* mountpoint URL */
	/* .origin */			"./html",  /* serve from dir */
	/* .def */			"index.html",	/* default filename */
	/* .protocol */			NULL,
	/* .cgienv */			NULL,
	/* .extra_mimetypes */		&pvo_mime,
	///* .extra_mimetypes */		NULL,
	/* .interpret */		NULL,
	/* .cgi_timeout */		0,
	/* .cache_max_age */		0,
	/* .auth_mask */		0,
	/* .cache_reusable */		0,
	/* .cache_revalidate */		0,
	/* .cache_intermediaries */	0,
	/* .origin_protocol */		LWSMPRO_FILE,	/* files in a dir */
	/* .mountpoint_len */		1,		/* char count */
	/* .basic_auth_login_file */	NULL,
};

CWSServer::~CWSServer() {
	SignalStop(false);
	if (m_thread.joinable())
		m_thread.join();
}

int CWSServer::callback_client(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
	switch (reason) {
	case LWS_CALLBACK_PROTOCOL_INIT:
		break;
	case LWS_CALLBACK_ESTABLISHED:
		{
			g_pWSServer->AsyncAddNewConnection(wsi);			
		}
		break;
	case LWS_CALLBACK_CLOSED:
		{
			g_pWSServer->AsyncAddDisconnectedConnection(wsi);
		}
		break;
	case LWS_CALLBACK_SERVER_WRITEABLE:
		{
			g_pWSServer->SendDataToClientIfAvalable(wsi);
		}
		break;
	case LWS_CALLBACK_RECEIVE:
		{
			std::string buffstring = (const char*)in;
			buffstring = buffstring.substr(0, len);
			
			g_pWSServer->UploadDataToControlIfAvalable(wsi, buffstring);
		}
		break;
	default:
		break;
	}
	return 0;
}

int CWSServer::Init() {
	
	m_is_running.store(true, std::memory_order_relaxed);

	memset(&m_info, 0, sizeof(m_info));
	m_info.port = WS_PORT;
	m_info.mounts = &mount;	
	m_info.protocols = m_protocols;

	m_info.vhost_name = "localhost";
	m_info.ws_ping_pong_interval = 5;	
	g_pWSServer = this;

	m_info.ssl_cert_filepath = NULL;
	m_info.ssl_private_key_filepath = NULL;

	m_info.gid = -1;
	m_info.uid = -1;

	m_pcontext = lws_create_context(&m_info);
	if (m_pcontext) {
		return 0;
	}
	return -1;
}

int CWSServer::Run() {
	try {
		m_thread = std::thread(&CWSServer::FnConnectionThread, this);
	}
	catch (...) {
		return -1;
	}
	return 0;
}

void CWSServer::FnConnectionThread() {
	
	if (Init() == 0) {
		m_pLog->AsyncWrite("Websocket Server is running", true, true);
		
		while(m_is_running.load(std::memory_order_relaxed)){
			if (lws_service(m_pcontext, 5) >= 0) {
				_ConnectionMessage message;
				if (AsyncGetMessageFromQueue(message)) {
					m_outbound_data_buffer.emplace_back(message);
					lws_callback_on_writable((struct lws*)message.lp_connection);
				}
			}
			else {
				break;
			}
		}
		Uninit();				
		m_pLog->AsyncWrite("Websocket Server is exiting ...", true, true);
		
	}	
}

void CWSServer::SignalStop(bool signal) {
	m_is_running.store(signal, std::memory_order_relaxed);
}

int CWSServer::Uninit() {
	lws_context_destroy(m_pcontext);
	return 0;
}

int CWSServer::SendWSData(const std::string& datatosend, struct lws * wsi)
{
	int chunk_size = (int)datatosend.size();

	size_t padded_size = chunk_size + LWS_PRE;
	char * sBuff = (char *)malloc(padded_size);
	memset((void*)sBuff, '\0', padded_size);
	char * content_start = sBuff + LWS_PRE;

	memcpy(content_start, datatosend.c_str(), chunk_size);
	int iSent = lws_write(wsi, (unsigned char *)content_start, chunk_size, LWS_WRITE_TEXT);
	free(sBuff);
	return iSent;
}

void CWSServer::AsyncAddMessageToQueue(const _ConnectionMessage& message) {
	m_inbound_lock.lock();
	m_inbound.emplace_back(message);
	m_inbound_lock.unlock();
}

bool CWSServer::AsyncGetMessageFromQueue(_ConnectionMessage& message) {
	bool ret = false;	
	
	try {
		std::lock_guard<std::mutex> guard_l(m_outbound_lock);
		
		if (m_outbound.size() > 0) {
			message.lp_connection = m_outbound.begin()->lp_connection;
			message.data = m_outbound.begin()->data;
			message.flow = m_outbound.begin()->flow;
			m_outbound.erase(m_outbound.begin());
			ret = true;
		}

	}
	catch (...) {
		//log something ...
	}
	return ret;
}

int CWSServer::SendDataToClientIfAvalable(struct lws * wsi) {
	bool bIsFound = false;
	std::string buffer_to_send;
	
	if (m_outbound_data_buffer.size() > 0) {
		std::list<_ConnectionMessage>::iterator it;
		for (it = m_outbound_data_buffer.begin(); it != m_outbound_data_buffer.end();++it) {
			if (wsi == (struct lws*)it->lp_connection) {
				bIsFound = true;
				break;
			}
		}
		if (bIsFound) {
			buffer_to_send = it->data;
			m_outbound_data_buffer.erase(it);
		}
	}
	for (auto& x : m_internal_connection_list) {
		if (x.lp_connection == (void*)wsi) {
			buffer_to_send = buffer_to_send + x.data;
			x.data = "";
		}
	}

	if (buffer_to_send != "") {
		SendWSData(buffer_to_send, wsi);
	}
	return -1;
}

int CWSServer::UploadDataToControlIfAvalable(struct lws * wsi, std::string& data) {
	bool bIsFound = false;
	
	m_pLog->AsyncWrite(data.c_str(), true, false);
	
	std::list<_ConnectionMessage>::iterator it;
	if (m_inbound_data_buffer.size() > 0) {
		for (it = m_inbound_data_buffer.begin(); it != m_inbound_data_buffer.end();++it) {
			if (wsi == (struct lws*)it->lp_connection) {
				bIsFound = true;
				break;
			}
		}
	}

	if (!bIsFound) {
		m_inbound_data_buffer.emplace_back(_ConnectionMessage::DataDirection::inbound, (void*)wsi, "");
		it = m_inbound_data_buffer.end();
		--it;
	}
	it->data = it->data + data;
	size_t remaining = lws_remaining_packet_payload(wsi);
	if (!remaining && lws_is_final_fragment(wsi)) {

		rapidjson::Document document;
		std::string data = it->data;
		document.Parse(data.c_str());
		std::string command = document["command"].GetString();
		if (command == "broadcast") {
			std::string tmp = "Broadcasting :" + data;
			m_pLog->AsyncWrite(tmp.c_str(), true, true);
			m_inbound_data_buffer.erase(it);
			
			for (auto it = m_internal_connection_list.begin(); it != m_internal_connection_list.end(); ++it) {
				if (it->lp_connection != (void*)wsi) {
					it->data = data;
					lws_callback_on_writable((struct lws*)it->lp_connection);
				}
			}

			return 0;
		}

		_ConnectionMessage message(_ConnectionMessage::DataDirection::inbound, (void*)wsi, it->data);
		AsyncAddMessageToQueue(message);
		m_inbound_data_buffer.erase(it);
	}
	else
		m_pLog->AsyncWrite("Websocket Server is choked ...", true, true);
	
	return 0;
}