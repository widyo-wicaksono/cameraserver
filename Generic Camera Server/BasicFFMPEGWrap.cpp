#include "pch.h"
#include "BasicFFMPEGWrap.h"

#define SERVER_TIMEOUT 10

std::chrono::seconds SecondsOfLastFrameReceived;

CBasicFFMPEGWrap::CBasicFFMPEGWrap()
{
	refreshTimeout();
}


CBasicFFMPEGWrap::~CBasicFFMPEGWrap()
{
	closeStream();
}


static int interrupt_cb(void *ctx)
{	
	std::chrono::seconds seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
	if (seconds.count() - SecondsOfLastFrameReceived.count() > SERVER_TIMEOUT) {
		return 1;
	}
	return 0;
}

static const AVIOInterruptCB int_cb = { interrupt_cb, NULL };

int CBasicFFMPEGWrap::openStream(std::string& media_path, std::string& error_string) {
	
	closeStream();
	m_MediaPath = media_path;

	bool bVideoStreamFound = false;
	m_pFormatContext = avformat_alloc_context();
	m_pFormatContext->interrupt_callback= int_cb;

	int ret = -1;
	try {
		ret = avformat_open_input(&m_pFormatContext, m_MediaPath.c_str(), NULL, NULL);
	}
	catch (...) {
		printf("Exception on open stream !\n");
		return ret;
	}

	
	if (ret == 0) {
		if (avformat_find_stream_info(m_pFormatContext, NULL) >= 0) {					
			for (unsigned int i = 0; i < m_pFormatContext->nb_streams; i++){
				AVCodecParameters *pLocalCodecParameters = m_pFormatContext->streams[i]->codecpar;
				AVCodec *pLocalCodec = nullptr;				
				pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);
				if (pLocalCodec) {				
					if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
						m_iVideoStreamIndex = i;
						m_pCodec = pLocalCodec;
						m_pCodecParameters = pLocalCodecParameters;			
						printf("Video Codec: resolution %d x %d\n", pLocalCodecParameters->width, pLocalCodecParameters->height);
						m_fps = av_q2d(m_pFormatContext->streams[i]->r_frame_rate);
						bVideoStreamFound = true;
					}
				}
			}
		}
	}
	else {
		char buff[512] = { 0 };
		av_strerror(ret, buff, 512);
		error_string = buff;
		//printf("Failed to open stream : %s\n", buff);
	}
	if (bVideoStreamFound) {
		m_pCodecContext = avcodec_alloc_context3(m_pCodec);
		if (m_pCodecContext) {
			if (avcodec_parameters_to_context(m_pCodecContext, m_pCodecParameters) >= 0){			
				if (avcodec_open2(m_pCodecContext, m_pCodec, NULL) >= 0){
					m_pFrame = av_frame_alloc();
					if (m_pFrame) {
						m_pPacket = av_packet_alloc();
						if (m_pPacket) {
							return 0;
						}
					}
				}
			}
		}
	}
	return ret;
}

void CBasicFFMPEGWrap::closeStream() {
	releaseFrame();
	if (m_pSWSCtx) {
		sws_freeContext(m_pSWSCtx);
		m_pSWSCtx = nullptr;
	}
	
	if (m_pPacket) {
		av_packet_free(&m_pPacket);
		m_pPacket = nullptr;
	}
	if (m_pFrame) {
		av_frame_free(&m_pFrame);
		m_pFrame = nullptr;
	}
	if (m_pCodecContext) {
		avcodec_close(m_pCodecContext);
		avcodec_free_context(&m_pCodecContext);
		m_pCodecContext = nullptr;
	}
	if (m_pFormatContext) {
		avformat_close_input(&m_pFormatContext);		
		avformat_free_context(m_pFormatContext);
		m_pFormatContext = nullptr;
	}
}

int CBasicFFMPEGWrap::getVideoFrame(unsigned char **buf, unsigned int& xsize, unsigned int& ysize) {
	int iRet = -1;	
	bool bIsVideoPacketFound = false;
	int response;
	while (!bIsVideoPacketFound) {
		response = av_read_frame(m_pFormatContext, m_pPacket);
		if (response >= 0) {
			if (m_pPacket->stream_index == m_iVideoStreamIndex) {
				bIsVideoPacketFound = true;
				refreshTimeout();				
			}
			else
				av_packet_unref(m_pPacket);			
		}
		else {
			if (response == AVERROR_EXIT)
				printf("AV Exit\n");
			return response;
		}
	}
	
	if (bIsVideoPacketFound) {
		response = avcodec_send_packet(m_pCodecContext, m_pPacket);
		if (response == AVERROR(EAGAIN)) {
			while (response == AVERROR(EAGAIN)) {
				avcodec_receive_frame(m_pCodecContext, m_pFrame);				
				response = avcodec_send_packet(m_pCodecContext, m_pPacket);
			}
		}
		if (response >= 0) {			
			// Return decoded output data (into a frame) from a decoder
			// https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga11e6542c4e66d3028668788a1a74217c
			response = avcodec_receive_frame(m_pCodecContext, m_pFrame);
			if (response != AVERROR(EAGAIN) && response != AVERROR_EOF && response >= 0) {
				xsize = m_pFrame->width;
				ysize = m_pFrame->height;

				if (!m_pSWSCtx) {
					m_pSWSCtx = sws_getContext(xsize, ysize, m_pCodecContext->pix_fmt, xsize, ysize, AV_PIX_FMT_BGR24, SWS_BICUBIC, NULL, NULL, NULL);
				}
				if (m_pSWSCtx) {
					m_pBuffer = (uint8_t*)calloc(3 * m_pFrame->width * m_pFrame->height, sizeof(uint8_t));
					uint8_t *rgb24[1] = { m_pBuffer };
					int rgb24_stride[1] = { 3 * m_pFrame->width };
					sws_scale(m_pSWSCtx, m_pFrame->data, m_pFrame->linesize, 0, m_pFrame->height, rgb24, rgb24_stride);								
					*buf = m_pBuffer;
					iRet = 0;
				}
			}
			else {
				iRet = response;
			}
		}
		av_packet_unref(m_pPacket);
	}	
	return iRet;
}

int CBasicFFMPEGWrap::getVideoFrameAlpha(unsigned char **buf, unsigned int& xsize, unsigned int& ysize) {
	int iRet = -1;
	
	bool bIsVideoPacketFound = false;
	int response;
	while (!bIsVideoPacketFound) {
		response = av_read_frame(m_pFormatContext, m_pPacket);
		if (response >= 0) {
			if (m_pPacket->stream_index == m_iVideoStreamIndex) {
				bIsVideoPacketFound = true;
				refreshTimeout();
			}
			else
				av_packet_unref(m_pPacket);
		}
		else {
			if (response == AVERROR_EXIT)
				printf("AV Exit\n");
			return response;
		}
	}

	if (bIsVideoPacketFound) {
		response = avcodec_send_packet(m_pCodecContext, m_pPacket);
		if (response == AVERROR(EAGAIN)) {
			while (response == AVERROR(EAGAIN)) {
				avcodec_receive_frame(m_pCodecContext, m_pFrame);				
				response = avcodec_send_packet(m_pCodecContext, m_pPacket);
			}
		}
		if (response >= 0) {
			// Return decoded output data (into a frame) from a decoder
			// https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga11e6542c4e66d3028668788a1a74217c
			response = avcodec_receive_frame(m_pCodecContext, m_pFrame);
			if (response != AVERROR(EAGAIN) && response != AVERROR_EOF && response >= 0) {
				xsize = m_pFrame->width;
				ysize = m_pFrame->height;

				if (!m_pSWSCtx) {
					m_pSWSCtx = sws_getContext(xsize, ysize, m_pCodecContext->pix_fmt, xsize, ysize, AV_PIX_FMT_BGRA, SWS_BICUBIC, NULL, NULL, NULL);
				}
				if (m_pSWSCtx) {
					m_pBuffer = (uint8_t*)calloc(4 * m_pFrame->width * m_pFrame->height, sizeof(uint8_t));
					uint8_t *rgb32[1] = { m_pBuffer };
					int rgb32_stride[1] = { 4 * m_pFrame->width };

					sws_scale(m_pSWSCtx, m_pFrame->data, m_pFrame->linesize, 0, m_pFrame->height, rgb32, rgb32_stride);
					*buf = m_pBuffer;
					iRet = 0;
				}
			}
			else
				iRet = response;
		}
		av_packet_unref(m_pPacket);
	}
	return iRet;
}

void CBasicFFMPEGWrap::releaseFrame() {	
	if (m_pBuffer) {
		delete m_pBuffer;
		m_pBuffer = nullptr;
	}
}

void CBasicFFMPEGWrap::refreshTimeout() {
	SecondsOfLastFrameReceived = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
}

int CBasicFFMPEGWrap::decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame)
{
	// Supply raw packet data as input to a decoder
	// https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga58bc4bf1e0ac59e27362597e467efff3
	int response = avcodec_send_packet(pCodecContext, pPacket);

	if (response < 0) {
		//logging("Error while sending a packet to the decoder: %s", av_err2str(response));
		return response;
	}

	while (response >= 0)
	{
		// Return decoded output data (into a frame) from a decoder
		// https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga11e6542c4e66d3028668788a1a74217c
		response = avcodec_receive_frame(pCodecContext, pFrame);
		if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
			break;
		}
		else if (response < 0) {
			//logging("Error while receiving a frame from the decoder: %s", av_err2str(response));
			return response;
		}

		if (response >= 0) {
			printf(
				"Frame %d (type=%c, size=%d bytes) pts %d key_frame %d [DTS %d]",
				pCodecContext->frame_number,
				av_get_picture_type_char(pFrame->pict_type),
				pFrame->pkt_size,
				(int)pFrame->pts,
				pFrame->key_frame,
				pFrame->coded_picture_number
			);

			char frame_filename[1024];
			snprintf(frame_filename, sizeof(frame_filename), "%s-%d.pgm", "frame", pCodecContext->frame_number);
			// save a grayscale frame into a .pgm file
			//save_gray_frame(pFrame->data[0], pFrame->linesize[0], pFrame->width, pFrame->height, frame_filename);

			av_frame_unref(pFrame);
		}
	}
	return 0;
}