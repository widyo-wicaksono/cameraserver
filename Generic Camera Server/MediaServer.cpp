#include "pch.h"
#include "MediaServer.h"



CMJPEGMediaServer::CMJPEGMediaServer(std::shared_ptr<CLogManager> log, double scale) :CBaseMediaServer(log, scale) {
	FD_ZERO(&m_master);		
}

int CMJPEGMediaServer::Init(int port) {
	m_port = port;
	m_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	SOCKADDR_IN address;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_family = AF_INET;
	address.sin_port = ::htons(m_port);
	if (::bind(m_sock, (SOCKADDR*)&address, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
	{
		return release();
	}
	
	if (::listen(m_sock, 10) == SOCKET_ERROR)
	{
		return release();
	}
	FD_SET(m_sock, &m_master);
	m_pLog->AsyncWrite("Socket is listening ...", true, true);
	return 0;
}

int CMJPEGMediaServer::release() {
	if (m_sock != INVALID_SOCKET)
		::shutdown(m_sock, 2);
	m_sock = (INVALID_SOCKET);
	return -1;
}

bool CMJPEGMediaServer::isOpened()
{
	return m_sock != INVALID_SOCKET;
}

int CMJPEGMediaServer::Write(const cv::Mat & frame)
{
	
	if (m_thread.joinable())
		m_thread.join();
	m_thread = std::thread(&CMJPEGMediaServer::WriteToClient, this, frame);
	
	//WriteToClient(frame);
#ifndef _WIN32
	cv::imshow("Webcam", frame);
	cv::waitKey(1);
#endif
	return 0;
}

int CMJPEGMediaServer::_write(int sock, const char *s, int len)
{
	if (len < 1) { len = (int)strlen(s); }
	return ::send(sock, s, len, 0);
}

int CMJPEGMediaServer::WriteToClient(const cv::Mat frame)
{
	fd_set rread = m_master;
	struct timeval to = { 0, m_timeout };
	SOCKET maxfd = m_sock + 1;

	if (::select(maxfd, &rread, NULL, NULL, &to) <= 0) {
		std::cout << "nobody is listening" << std::endl;
		return true; // nothing broken, there's just noone listening
	}

	cv::Mat frm = frame.clone();
	if (m_scale != 1.0) {		
		cv::resize(frm, frm, cv::Size(), m_scale, m_scale);
	}

	std::vector<uchar>outbuf;
	std::vector<int> params;
	params.push_back(cv::IMWRITE_JPEG_QUALITY);
	params.push_back(m_quality);
		
	cv::imencode(".jpg", frm, outbuf, params);
	int outlen = (int)outbuf.size();	

#ifdef _WIN32 
	for (unsigned i = 0; i < rread.fd_count; i++)
	{
		SOCKET s = rread.fd_array[i];    // fd_set on win is an array, while ...
#else         
	for (int s = 0; s < maxfd; s++)
	{
		if (!FD_ISSET(s, &rread))      // ... on linux it's a bitmask ;)
		{
			//std::cout << "FD_ISSET false" << std::endl;
			continue;
		}
#endif    	         
		if (s == m_sock) // request on master socket, accept and send main header.
		{
			
			SOCKADDR_IN address = { 0 };
#ifdef _WIN32
			int addrlen = sizeof(SOCKADDR);
			SOCKET client = ::accept(m_sock, (SOCKADDR*)&address, &addrlen);
#else
			unsigned int addrlen = (unsigned int)sizeof(SOCKADDR);
			SOCKET client = ::accept(m_sock, (SOCKADDR*)&address, &addrlen);
			m_pLog->AsyncWrite("Accepting new connection ...", true, true);

#endif
			if (client == SOCKET_ERROR)
			{
				m_pLog->AsyncWrite("Socket error! ...", true, true);
				return -1;
			}
			else
				m_pLog->AsyncWrite("Connection accepted", true, true);

			maxfd = (maxfd > client ? maxfd : client);
			FD_SET(client, &m_master);

			std::string header_reply = "HTTP/1.0 200 OK\r\n";
			//_write(client, "HTTP/1.0 200 OK\r\n", 0);
			int n = _write(client, header_reply.c_str(), 0);
			if(n < (int)header_reply.size())
				m_pLog->AsyncWrite("Error in sending header reply part 1!", true, true);
			else
				m_pLog->AsyncWrite("Header reply part 1 sent!", true, true);
			/*
			_write(client,
				"Server: Mozarella/2.2\r\n"
				"Accept-Range: bytes\r\n"
				"Connection: close\r\n"
				"Max-Age: 0\r\n"
				"Expires: 0\r\n"
				"Cache-Control: no-cache, private\r\n"
				"Pragma: no-cache\r\n"
				"Content-Type: multipart/x-mixed-replace; boundary=mjpegstream\r\n"
				"\r\n", 0);
				*/
			header_reply = "Server: Mozarella/2.2\r\n"
				"Accept-Range: bytes\r\n"
				"Connection: close\r\n"
				"Max-Age: 0\r\n"
				"Expires: 0\r\n"
				"Cache-Control: no-cache, private\r\n"
				"Pragma: no-cache\r\n"
				"Content-Type: multipart/x-mixed-replace; boundary=mjpegstream\r\n"
				"\r\n";
			n = _write(client, header_reply.c_str(), 0);
			if (n < (int)header_reply.size())
				m_pLog->AsyncWrite("Error in sending header reply part 2!", true, true);
			else
				m_pLog->AsyncWrite("Header reply part 2 sent!", true, true);
		}
		else // existing client, just stream pix
		{
#ifndef _WIN32
			std::cout << "Sending frames ..." << std::endl;
#endif // !_WIN32

			char head[400] = { 0 };
			sprintf(head, "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: %lu\r\n\r\n", (long unsigned int)outlen);
			int n = _write(s, head, 0);
			if(n<(int)strlen(head))
				m_pLog->AsyncWrite("Error in sending header data", true, true);
			n = _write(s, (char*)(&outbuf[0]), outlen);			
			if (n < outlen)
			{
				m_pLog->AsyncWrite("Error in sending data, shutting down connection", true, true);
				::shutdown(s, 2);
				FD_CLR(s, &m_master);
			}
		}		
	}
	//cv::imshow("Webcam", frm);
	//cv::waitKey(1);

	return 0;
}
