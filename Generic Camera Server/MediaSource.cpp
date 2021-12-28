#include "pch.h"
#include "MediaSource.h"
#include "GeneralUtils.h"

#ifdef _WIN32
//#include  "CanonCamUtil.h"
#endif

CBaseMediaSource::CBaseMediaSource(std::shared_ptr<CLogManager> log) {
	m_pLog = log;	
	m_error_code.store(ERR_NONE);
	m_error_count.store(0);
}

CBaseMediaSource::CBaseMediaSource(std::shared_ptr<CLogManager> log, int width, int height, bool is_landscape) :m_lv_width(width), m_lv_height(height), m_is_lv_landscape(is_landscape) {
	m_pLog = log;	
	m_error_code.store(ERR_NONE);
	m_error_count.store(0);
}


std::string CBaseMediaSource::GetID() {
	return m_id;
}

CBaseMediaSource::SourceType CBaseMediaSource::GetType() {
	return m_type;
}

int CBaseMediaSource::GetState() {	
	return m_state.load();
}

/*
int CBaseMediaSource::GetError() {
	return m_error_code.load();
}
*/


void CBaseMediaSource::GetError(int& error_code, int& error_count) {
	error_code = m_error_code.load();
	error_count = m_error_count.load();
}

void CBaseMediaSource::StopLiveView() {
	m_isrunning.store(false);
	if (m_liveview_thread.joinable())
		m_liveview_thread.join();
}

int CBaseMediaSource::GrabMostRecentFrame(cv::Mat& frame) {
	int ret = -1;
	m_framelock.lock();
	if (m_frame_buffer.size() > 0) {
		frame = m_frame_buffer[m_frame_buffer.size() - 1].clone();
		ret = 0;
	}
	m_framelock.unlock();
	return ret;
}

void CBaseMediaSource::SetError(int error_) {
	if (m_error_code.load() == ERR_NONE) {
		m_error_code.store(error_);
		m_error_count.fetch_add(1);
	}
}

CWebcamSource::CWebcamSource(std::string& index, std::shared_ptr<CLogManager> log) :CBaseMediaSource(log) {
	m_id = index;
	m_type = CBaseMediaSource::SourceType::webcam;		
	m_pLog->AsyncWrite(m_pLog->string_format("Media Source Webcam[%s] created", m_id.c_str()).c_str(), true, true);
}

CWebcamSource::CWebcamSource(std::string& index, std::shared_ptr<CLogManager> log, int width, int height, bool is_landscape) :CBaseMediaSource(log, width, height, is_landscape) {
	m_id = index;
	m_type = CBaseMediaSource::SourceType::webcam;	
	m_pLog->AsyncWrite(m_pLog->string_format("Media Source Webcam[%s] created", m_id.c_str()).c_str(), true, true);
}

CWebcamSource::~CWebcamSource() {	
	StopLiveView();		
	m_pLog->AsyncWrite(m_pLog->string_format("Media Source Webcam[%s] destroyed", m_id.c_str()).c_str(), true, true);
}

int CWebcamSource::StartLiveView() {
	/*
	m_isrunning.store(true);
	if (m_liveview_thread.joinable())
		m_liveview_thread.join();
		*/
	StopLiveView();	
	try {
		m_liveview_thread = std::thread(&CWebcamSource::FnLiveViewThread, this);
	}
	catch (...) {
		return -1;
	}
	return 0;
}

void CWebcamSource::FnLiveViewThread() {
	
	std::unique_ptr<CFPSCounter> pfps = std::make_unique<CFPSCounter>();
	m_isrunning.store(true);
	
	m_state.store(MEDIA_THREAD_STATE_STARTING);
	bool bIsRunning= m_isrunning.load();

	cv::VideoCapture cap;

	//if (cap.open(std::stoi(m_id), cv::CAP_FFMPEG)) {
	//if (cap.open(std::stoi(m_id), cv::CAP_MSMF)) {
#ifdef _WIN32
	if (cap.open(std::stoi(m_id), cv::CAP_DSHOW)) {
#else
	if (cap.open(std::stoi(m_id))) {
#endif
		
		int width = (int)cap.get(cv::CAP_PROP_FRAME_WIDTH);
		int height = (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT);		
		
		if (width != m_lv_width || height != m_lv_height) {
			if (m_lv_width == -1 || m_lv_height == -1) {
				m_lv_width = width;
				m_lv_height = height;
			}
			else {
				cap.set(cv::CAP_PROP_FRAME_WIDTH, m_lv_width);
				cap.set(cv::CAP_PROP_FRAME_HEIGHT, m_lv_height);				
			}
		}
		
		m_state.store(MEDIA_THREAD_STATE_RUNNING);
		while (bIsRunning) {
			cv::Mat frame;			
			cap >> frame;
			if (!frame.empty()) {			
				cv::flip(frame, frame, 1);
				if (!m_is_lv_landscape)
					cv::rotate(frame, frame, cv::ROTATE_90_CLOCKWISE);

				std::string fps = pfps->GetFPSinString();					
				m_pLog->WriteToOCVScreen(fps.c_str(), 50, 50, 0, 0, 255, frame);

				m_framelock.lock();
				if (m_frame_buffer.size() > 50) {
					m_frame_buffer.erase(m_frame_buffer.begin());
				}
				m_frame_buffer.push_back(frame);				
				m_framelock.unlock();			

				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			else {				
				SetError(ERR_WEBCAM_EMPTY_FRAME);
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			}
			bIsRunning = m_isrunning.load();
		}
		
		cap.release();
		cv::destroyAllWindows();		
	}
	else {
		m_pLog->AsyncWrite(m_pLog->string_format("Failed to webcam [%s]", m_id.c_str()).c_str(), true, true);		
		SetError(ERR_WEBCAM_FAIL_TO_OPEN);
	}
	m_pLog->AsyncWrite(m_pLog->string_format("Stream Thread [%s] exiting ...", m_id.c_str()).c_str(), true, true);
	m_state.store(MEDIA_THREAD_STATE_EXITING);
}
/*
int CWebcamSource::GrabMostRecentFrame(cv::Mat& frame) {
	int ret = -1;
	m_framelock.lock();
	if (m_frame_buffer.size() > 0) {
		frame = m_frame_buffer[m_frame_buffer.size() - 1].clone();
		ret = 0;
	}
	m_framelock.unlock();
	return ret;
}
*/
int CWebcamSource::CapturePhoto(cv::Mat& frame) {
	int ret = GrabMostRecentFrame(frame);
	if (ret != -1)
		return 0;
	return ret;
}

CIPCam::CIPCam(std::string& address, std::shared_ptr<CLogManager> log) :CBaseMediaSource(log) {
	m_id = address;
	m_type = CBaseMediaSource::SourceType::ipcam;
	m_pLog->AsyncWrite(m_pLog->string_format("Media Source IPCam[%s] created", m_id.c_str()).c_str(), true, true);
}

CIPCam::CIPCam(std::string& address, std::shared_ptr<CLogManager> log, int width, int height, bool is_landscape) :CBaseMediaSource(log, width, height, is_landscape) {
	m_id = address;
	m_type = CBaseMediaSource::SourceType::ipcam;	
	m_pLog->AsyncWrite(m_pLog->string_format("Media Source IPCam[%s] created", m_id.c_str()).c_str(), true, true);
}

CIPCam::~CIPCam() {
	StopLiveView();	
	m_pLog->AsyncWrite(m_pLog->string_format("Media Source IPCam[%s] destroyed", m_id.c_str()).c_str(), true, true);
}


void CIPCam::FnLiveViewThread() {
	
	std::unique_ptr<CFPSCounter> pfps = std::make_unique<CFPSCounter>();
	m_isrunning.store(true);
	//m_error_code.store(ERR_NONE);
	m_state.store(MEDIA_THREAD_STATE_STARTING);
	bool bIsRunning = m_isrunning.load();

	CBasicFFMPEGWrap ffmpeg;
	std::string error_string;

	if (ffmpeg.openStream(m_id, error_string) == 0) {
		m_state.store(MEDIA_THREAD_STATE_RUNNING);
		while (bIsRunning) {
			cv::Mat frame;

			unsigned char* buff = nullptr;
			unsigned int width = 0, height = 0;			

			if (ffmpeg.getVideoFrame(&buff, width, height) == 0) {

				cv::Mat fr = cv::Mat(height, width, CV_8UC3, buff);
				frame = fr.clone();
				ffmpeg.releaseFrame();								

				if (!m_is_lv_landscape)
					cv::rotate(frame, frame, cv::ROTATE_90_CLOCKWISE);

				if (frame.cols != m_lv_width || frame.rows != m_lv_height) {
					if (m_lv_width == -1 || m_lv_height == -1) {
						m_lv_width = frame.cols;
						m_lv_height = frame.rows;
					}
					else
						cv::resize(frame, frame, cv::Size(m_lv_width, m_lv_height));
				}

				std::string fps = pfps->GetFPSinString();								
				m_pLog->WriteToOCVScreen(fps.c_str(), 50, 50, 0, 0, 255, frame);

				m_framelock.lock();
				if (m_frame_buffer.size() > 50) {
					m_frame_buffer.erase(m_frame_buffer.begin());
				}

				m_frame_buffer.push_back(frame);
				m_framelock.unlock();				
				//cv::imshow("IPCAM", frame);
				//cv::waitKey(5);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			else {
				//m_error_code.store(ERR_WEBCAM_EMPTY_FRAME);
				SetError(ERR_WEBCAM_EMPTY_FRAME);
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			}
			bIsRunning = m_isrunning.load();
		}				
		ffmpeg.closeStream();
	}
	else {		
		m_pLog->AsyncWrite(m_pLog->string_format("Failed to open network stream : %s[%s]", error_string.c_str(), m_id.c_str()).c_str(), true, true);
		//m_error_code.store(ERR_IPCAM_FAIL_TO_OPEN);
		SetError(ERR_WEBCAM_EMPTY_FRAME);
	}	
	m_pLog->AsyncWrite(m_pLog->string_format("Stream Thread [%s] exiting ...", m_id.c_str()).c_str(), true, true);
	m_state.store(MEDIA_THREAD_STATE_EXITING);
}
/*
int CIPCam::GrabMostRecentFrame(cv::Mat& frame) {
	int ret = -1;
	m_framelock.lock();
	if (m_frame_buffer.size() > 0) {
		frame = m_frame_buffer[m_frame_buffer.size() - 1].clone();
		ret = 0;
	}
	m_framelock.unlock();
	return ret;
}
*/
int CIPCam::CapturePhoto(cv::Mat& frame) {
	int ret = GrabMostRecentFrame(frame);
	if (ret != -1)
		return 0;
	return ret;
}

int CIPCam::StartLiveView() {
	StopLiveView();
	//m_liveview_thread = std::thread(&CIPCam::FnLiveViewThread, this);

	try {
		m_liveview_thread = std::thread(&CIPCam::FnLiveViewThread, this);
	}
	catch (...) {
		return -1;
	}
	return 0;
}

#ifdef _WIN32
/*
CCanonSLR::CCanonSLR(std::shared_ptr<CLogManager> log, int width, int height, bool is_landscape, void* pframe_control) : CBaseMediaSource(log, width, height, is_landscape) 
{
	m_id = "CANON";
	m_type = CBaseMediaSource::SourceType::dslr;
	m_pFrameControl = pframe_control;
	m_pLog->AsyncWrite(m_pLog->string_format("Media Source DSLR Canon[%s] created", m_id.c_str()).c_str(), true, true);

	CANON_CAM_HANDLER* pData = new CANON_CAM_HANDLER();
	m_pData = (void*)pData;
	pData->pLog = log;

	CANON_CAM_HANDLER* pHandler = (CANON_CAM_HANDLER*)m_pData;
	pHandler->hThreadHandle = CreateThread(NULL, 0, CanonCameraThread, (LPVOID)pHandler, 0, NULL);
}

CCanonSLR::~CCanonSLR() {
	//StopLiveView();

	StopCameraThread(*((CANON_CAM_HANDLER*)m_pData));
	CleanUpCameraThread(*((CANON_CAM_HANDLER*)m_pData));

	m_pLog->AsyncWrite(m_pLog->string_format("Media Source DSLR Canon[%s] destroyed", m_id.c_str()).c_str(), true, true);
	delete m_pData;
}

void CCanonSLR::StartLiveView() {
	//StopLiveView();
	CANON_CAM_HANDLER* pHandler = (CANON_CAM_HANDLER*)m_pData;
	//pHandler->hThreadHandle = CreateThread(NULL, 0, CanonCameraThread, (LPVOID)pHandler, 0, NULL);
	EnterCriticalSection(&pHandler->cs);	

	COMMAND_CAM x;
	x.command = "livefeed";	
	pHandler->command_queues.push_back(x);	
	LeaveCriticalSection(&pHandler->cs);
}

void CCanonSLR::StopLiveView() {

	CANON_CAM_HANDLER* pHandler = (CANON_CAM_HANDLER*)m_pData;	
	EnterCriticalSection(&pHandler->cs);

	COMMAND_CAM x;
	x.command = "stoplivefeed";
	pHandler->command_queues.push_back(x);
	LeaveCriticalSection(&pHandler->cs);
}

int CCanonSLR::GrabMostRecentFrame(cv::Mat& frame) {
	int ret = -1;
		
	CANON_CAM_HANDLER* pData = (CANON_CAM_HANDLER*)m_pData;
	EnterCriticalSection(&pData->cs);	
	int size_ = (int)pData->frames.size();
	if (size_> 0) {
		frame = pData->frames[size_ - 1].clone();
		ret = 0;
	}	
	LeaveCriticalSection(&pData->cs);
	return ret;
}

int CCanonSLR::GetState() {
	CANON_CAM_HANDLER* pData = (CANON_CAM_HANDLER*)m_pData;
	if (pData->bIsRunning.load() == CAM_THREAD_STATE_RUNNING)
		return MEDIA_THREAD_STATE_RUNNING;
	else if (pData->bIsRunning.load() == CAM_THREAD_STATE_EXIT)
		return MEDIA_THREAD_STATE_EXITING;
	return MEDIA_THREAD_STATE_IDLE;
}

int CCanonSLR::CapturePhoto(cv::Mat& frame) {
	CANON_CAM_HANDLER* pData = (CANON_CAM_HANDLER*)m_pData;
	COMMAND_CAM x;
	x.command = "takephoto";
	x.pOwner = m_pFrameControl;
	EnterCriticalSection(&pData->cs);
	pData->command_queues.push_back(x);
	LeaveCriticalSection(&pData->cs);
	return ERROR_ASYNC_OPERATION;
}

int CCanonSLR::CaptureContinuousPhoto(std::vector<cv::Mat>& framaes, int count) {
	CANON_CAM_HANDLER* pData = (CANON_CAM_HANDLER*)m_pData;
	COMMAND_CAM x;
	x.command = "takecontinuousphotos";
	x.pOwner = m_pFrameControl;
	x.param01 = count;
	EnterCriticalSection(&pData->cs);
	pData->command_queues.push_back(x);
	LeaveCriticalSection(&pData->cs);
	return ERROR_ASYNC_OPERATION;
}

int CCanonSLR::CaptureBurstPhoto(std::vector<cv::Mat>& framaes, int duration, int count) {
	CANON_CAM_HANDLER* pData = (CANON_CAM_HANDLER*)m_pData;
	COMMAND_CAM x;
	x.command = "takeburstphotos";
	x.pOwner = m_pFrameControl;
	x.param01 = duration;
	x.param00 = count;
	EnterCriticalSection(&pData->cs);
	pData->command_queues.push_back(x);
	LeaveCriticalSection(&pData->cs);
	return ERROR_ASYNC_OPERATION;
}

void CCanonSLR::GetError(int& error_code, int& error_count) {
	CANON_CAM_HANDLER* pData = (CANON_CAM_HANDLER*)m_pData;
	error_code = pData->error_code.load();
	error_count = pData->error_count.load();
}

void CCanonSLR::ResetError() {
	CANON_CAM_HANDLER* pData = (CANON_CAM_HANDLER*)m_pData;
	pData->error_code.store(ERR_CAMERA_OK);
	pData->error_count.store(0);
}

int CCanonSLR::GetPhoto(cv::Mat& frame, std::string& path) {
	CANON_CAM_HANDLER* pData = (CANON_CAM_HANDLER*)m_pData;
	EnterCriticalSection(&pData->cs);
	bool isFound = false;
	int i = 0;
	int ret = ERROR_ASYNC_OPERATION;
	for (auto& result : pData->result_queues) {
		if (result.pOwner == m_pFrameControl) {	
			isFound = true;
			break;			
		}
		i++;
	}
	if (isFound) {
		if (pData->result_queues[i].status == 0) {			
			path = pData->result_queues[i].results[0];
			pData->result_queues.erase(pData->result_queues.begin() + i);
			ret = 0;
		}
		else if (pData->result_queues[i].status == -1) {
			pData->result_queues.erase(pData->result_queues.begin() + i);
			ret = -1;
		}
		else if (pData->result_queues[i].status == -2)
			ret = ERROR_ASYNC_OPERATION;
	}
	LeaveCriticalSection(&pData->cs);
	return ret;
}

int CCanonSLR::GetPhotos(std::vector<cv::Mat>& frame, std::vector<std::string>& paths) {
	CANON_CAM_HANDLER* pData = (CANON_CAM_HANDLER*)m_pData;
	EnterCriticalSection(&pData->cs);
	bool isFound = false;
	int i = 0;
	int ret = ERROR_ASYNC_OPERATION;
	for (auto& result : pData->result_queues) {
		if (result.pOwner == m_pFrameControl) {
			isFound = true;
			break;
		}
		i++;
	}
	if (isFound) {
		if (pData->result_queues[i].status == 0) {
			for (auto& path : pData->result_queues[i].results) {				
				paths.push_back(path);
			}
			pData->result_queues.erase(pData->result_queues.begin() + i);
			ret = 0;
		}
		else if (pData->result_queues[i].status == -1) {
			pData->result_queues.erase(pData->result_queues.begin() + i);
			ret = -1;
		}
		else if (pData->result_queues[i].status == -2)
			ret = ERROR_ASYNC_OPERATION;
	}
	LeaveCriticalSection(&pData->cs);
	return ret;
}
*/
#endif