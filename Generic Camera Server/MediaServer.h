#pragma once
#include <opencv2/opencv.hpp>
#include <thread>
#include <atomic>
#include <condition_variable>

#include "LogManager.h"

#ifdef _WIN32
	#include <winsock.h>
#else
	#include <unistd.h>
	#include <sys/time.h>
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netdb.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#define PORT        unsigned short
	#define SOCKET    int
	#define HOSTENT  struct hostent
	#define SOCKADDR    struct sockaddr
	#define SOCKADDR_IN  struct sockaddr_in
	#define ADDRPOINTER  unsigned int*
	#define INVALID_SOCKET -1
	#define SOCKET_ERROR   -1
#endif

class CBaseMediaServer {

protected:
	int m_port= 7778;
	double m_scale = 1.0;	
	
	CLogManager* m_pLog = nullptr;

	CBaseMediaServer(double scale);
	CBaseMediaServer(const CBaseMediaServer&) = delete;
	CBaseMediaServer(CBaseMediaServer&&) = delete;
	CBaseMediaServer& operator=(const CBaseMediaServer&) = delete;
	CBaseMediaServer& operator=(CBaseMediaServer&&) = delete;

public:		
	/*
	CBaseMediaServer(double scale) {
		m_pLog = CLogManager::getInstance();
		m_scale = scale;
		m_pLog->AsyncWrite("Media Server created", true, true);
	}
	*/
	virtual ~CBaseMediaServer() {		
		m_pLog->AsyncWrite("Media Server destroyed", true, true);
	};
	virtual int Init(int port=7778) { return 0; };
	virtual int Write(const cv::Mat& frame) { return 0; };
};

class CMJPEGMediaServer : public CBaseMediaServer
{
private:
	SOCKET m_sock = INVALID_SOCKET;
	fd_set m_master;
	int m_timeout = 500; // master sock timeout, shutdown after timeout millis.
	int m_quality = 50; // jpeg compression [1..100]
	bool m_isInited = false;
	
	std::thread m_thread;
	std::atomic<bool> m_IsRunning;
	std::mutex m_mxLoop;
	std::mutex m_mxData;
	cv::Mat m_frame;
	std::condition_variable_any m_cv;

	int Init(int port);
	int release();	
	bool isOpened();		

	int WriteToClient(const cv::Mat frame);
	void WriteToClientEx();
	int _write(int sock, const char *s, int len);
public:	
	
	CMJPEGMediaServer(double scale);
	~CMJPEGMediaServer();

	CMJPEGMediaServer(CMJPEGMediaServer&&) = delete;
	CMJPEGMediaServer& operator=(const CMJPEGMediaServer&) = delete;
	CMJPEGMediaServer& operator=(CMJPEGMediaServer&&) = delete;

	/*
	~CMJPEGMediaServer() {		
		m_IsRunning.store(false);
		m_cv.notify_one();
		if (m_thread.joinable())
			m_thread.join();
		release();		
	};
	*/
	int putFrame(const cv::Mat frame);
	int Write(const cv::Mat& frame);

};