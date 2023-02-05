#pragma once
//#ifdef _WIN32


//#include "EDSDKErrors.h"
//#include "EDSDKTypes.h"

//#endif


#include <vector>
#include <atomic>
#include <thread>

#include <opencv2/opencv.hpp>

#include "LogManager.h"
#include "BasicFFMPEGWrap.h"


#define ERR_NONE 0
#define ERR_WEBCAM_FAIL_TO_OPEN 1
#define ERR_WEBCAM_EMPTY_FRAME 2
#define ERROR_ASYNC_OPERATION 3
#define ERR_IPCAM_FAIL_TO_OPEN 4

#define MEDIA_THREAD_STATE_IDLE 1
#define MEDIA_THREAD_STATE_STARTING 2
#define MEDIA_THREAD_STATE_RUNNING 3
#define MEDIA_THREAD_STATE_EXITING 4
#define MEDIA_THREAD_STATE_EXIT 5

#define MEDIA_WIDTH 1280
#define MEDIA_HEIGHT 720


class CBaseMediaSource {
public:
	enum class SourceType { dummy, webcam, dslr, mobile, ipcam };

protected:
	CBaseMediaSource();
	CBaseMediaSource(int width, int height, bool is_landscape);

	CBaseMediaSource(const CBaseMediaSource&) = delete;
	CBaseMediaSource(CBaseMediaSource&&) = delete;
	CBaseMediaSource& operator=(const CBaseMediaSource&) = delete;
	CBaseMediaSource& operator=(CBaseMediaSource&&) = delete;

	struct CommandData {
		std::vector<cv::Mat> frames;
		void* pConnection = nullptr;
	};

	std::list<cv::Mat> m_frame_buffer;

	std::vector<CommandData> m_op_res;

	CLogManager* m_pLog = nullptr;

	std::atomic<bool> m_isrunning;
	std::atomic<int> m_error_code;
	std::atomic<int> m_state;
	std::atomic<int> m_error_count;

	std::thread m_liveview_thread;

	std::mutex m_framelock;
	std::mutex m_result_buffer_lock;

	std::string m_id = "";

	bool m_isLiveViewOn = false;

	int m_lv_width = 1280;
	int m_lv_height = 720;
	bool m_is_lv_landscape = true;

	SourceType m_type = SourceType::dummy;

	void SetError(int code_);

public:

	virtual ~CBaseMediaSource() {};
	virtual int StartLiveView() { return 0; };
	virtual void StopLiveView();
	virtual int GrabMostRecentFrame(cv::Mat& frame);// { return 0; };
	virtual int CapturePhoto(cv::Mat& frame) = 0;
	virtual int CaptureContinuousPhoto(std::vector<cv::Mat>& frames, int count) { return 0; };
	virtual int CaptureBurstPhoto(std::vector<cv::Mat>& framaes, int duration, int count) { return 0; };
	virtual int GetPhoto(cv::Mat& frame, std::string& path) { return 0; };
	virtual int GetPhotos(std::vector<cv::Mat>& frame, std::vector<std::string>& paths) { return 0; };

	inline std::string GetID() { return m_id; };
	inline SourceType GetType(){return m_type;};
		
	virtual int GetState() { return m_state.load(std::memory_order_relaxed); };

	virtual void GetError(int& error_code, int& error_count);
	virtual void ResetError() {
		m_error_code.store(ERR_NONE);
		m_error_count.store(0);
	}

};

class CWebcamSource : public CBaseMediaSource {

private:	
	
public:
	~CWebcamSource();	
	
	CWebcamSource(std::string& index);
	CWebcamSource(std::string& index, int width, int height, bool is_landscape);

	void FnLiveViewThread();
	int StartLiveView();	

	//int GrabMostRecentFrame(cv::Mat& frame);
	int CapturePhoto(cv::Mat& frame);
};


class CIPCam : public CBaseMediaSource {
private:
	
public:
	~CIPCam();
	
	CIPCam(std::string& address);
	CIPCam(std::string& address, int width, int height, bool is_landscape);

	void FnLiveViewThread();
	int StartLiveView();	

	//int GrabMostRecentFrame(cv::Mat& frame);
	int CapturePhoto(cv::Mat& frame);
};

#ifdef _WIN32
/*
class CCanonSLR : public CBaseMediaSource {
private:
	void* m_pData = nullptr;
	void* m_pFrameControl = nullptr;
public:
	CCanonSLR(std::shared_ptr<CLogManager> log, int width, int height, bool is_landscape, void* pframe_control);
	~CCanonSLR();

	void StartLiveView();
	void StopLiveView();

	int GrabMostRecentFrame(cv::Mat& frame);
	int GetState();
	int GetPhoto(cv::Mat& frame, std::string& path);
	int GetPhotos(std::vector<cv::Mat>& frame, std::vector<std::string>& paths);
	int CapturePhoto(cv::Mat& frame);
	int CaptureContinuousPhoto(std::vector<cv::Mat>& framaes, int count);
	int CaptureBurstPhoto(std::vector<cv::Mat>& framaes, int duration, int count);

	void GetError(int& error_code, int& error_count);
	void ResetError();	
};
*/
#endif