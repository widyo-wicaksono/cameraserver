#pragma once
#include <string>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>
}

#include <chrono>
#define TIMEOUT_LIMIT 5

class CBasicFFMPEGWrap
{
private:
	AVFormatContext *m_pFormatContext=nullptr;
	AVCodec *m_pCodec = nullptr;	
	AVCodecParameters *m_pCodecParameters = nullptr;
	AVCodecContext *m_pCodecContext = nullptr;
	AVFrame *m_pFrame = nullptr;	
	AVPacket *m_pPacket = nullptr;

	uint8_t *m_pBuffer = nullptr;
	SwsContext* m_pSWSCtx = nullptr;

	int m_iVideoStreamIndex = -1;
	double m_fps = 0.0;
	std::string m_MediaPath;	

public:
	CBasicFFMPEGWrap();
	~CBasicFFMPEGWrap();

	int openStream(std::string& media_path, std::string& error_string);
	int getVideoFrame(unsigned char **buf, unsigned int& xsize, unsigned int& ysize);

	int getVideoFrameAlpha(unsigned char **buf, unsigned int& xsize, unsigned int& ysize);

	void closeStream();
	void releaseFrame();
	void refreshTimeout();

	double GetFPS() {
		return m_fps;
	}
	int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame);
};

