#pragma once
#include "EDSDK.h"
#include "EDSDKErrors.h"
#include "EDSDKTypes.h"

#include "LogManager.h"

#include <atomic>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#define WM_CAMERA_EVENTS WM_USER+50
#define WM_CAMERA_ERRORS WM_USER+100

#define CAM_STATUS_OK 100
#define CAM_STATUS_ERROR 200

#define EVT_NEW_FRAME 1
#define EVT_INIT_OK 2
#define EVT_NEW_PHOTO 3
#define EVT_CAM_THREAD_EXITING 4
#define EVT_CAM_LIVE_FEED_TOGLED 5
#define EVT_CAM_LIVE_FEED_STATUS 6

#define PARAM_CAM_LIVEFEED_ON 1
#define PARAM_CAM_LIVEFEED_OFF 0

#define ERR_CAMERA_OK 0
#define ERR_CAMERA_INIT 1
#define ERR_CAMERA_FRAME_FAIL 2
#define ERR_CAMERA_TAKE_PHOTO_FAIL 3
#define ERR_CAMERA_RES 4
#define ERR_CAMERA_FRAME_EMPTY 5
#define ERR_CAMERA_PERMANENT_FRAME_FAIL 6
#define ERR_CAMERA_DC 7
#define ERR_CAMERA_LV_FAIL 8
#define ERR_IN_PROGRESS 9

#define CAM_THREAD_STATE_RUNNING 1
#define CAM_THREAD_STATE_EXITING 2
#define CAM_THREAD_STATE_EXIT 3
#define CAM_THREAD_FORCE_EXIT 4

#define TIMER_ID_SLEEP 1

#define COMMAND_CAPTURE_PHOTO 0 
#define COMMAND_START_LV 1
#define COMMAND_STOP_LV 2

struct COMMAND_CAM {
	std::string command;	
	
	std::vector<std::string> results;
	int param00 = 0;
	int param01 = 0;
	
	void* pOwner = nullptr;

	int status = -2;
};

struct CANON_CAM_HANDLER {
	HANDLE hThreadHandle = NULL;
	HWND hMainWnd = NULL;

	std::atomic<int> bIsRunning;	
	std::vector<cv::Mat> frames;
	std::vector<COMMAND_CAM> command_queues;
	std::vector<COMMAND_CAM> result_queues;
	std::vector<EdsBaseRef> photo_refs;

	CRITICAL_SECTION cs;
	std::atomic<int> error_code;
	std::atomic<int> error_count;	
	std::atomic<int> pic_count_in_buffer;

	std::atomic<bool> is_cam_inited;
	std::atomic<bool> is_cam_ready_for_lv;
	std::atomic<bool> is_cam_ready_for_photo;
	std::atomic<bool> is_cam_lv_on;	

	std::shared_ptr<CLogManager> pLog = nullptr;

	int photo_count = 0;
	int burst_duration = 0;	

	CANON_CAM_HANDLER() {
		InitializeCriticalSection(&cs);
		is_cam_inited.store(false);
		is_cam_ready_for_lv.store(false);
		is_cam_ready_for_photo.store(false);
		is_cam_lv_on.store(false);
		error_code.store(ERR_CAMERA_OK);
		error_count.store(0);
		pic_count_in_buffer.store(0);
	}	

	void SetError(int error_code_) {
		if (pLog) {
			pLog->AsyncWrite("Error occured", true, true);
		}
		error_code.store(error_code_);
		error_count.fetch_add(1);
	}
};

DWORD WINAPI CanonCameraThread(LPVOID pParam);

void CleanUpCameraThread(CANON_CAM_HANDLER& data);
void StopCameraThread(CANON_CAM_HANDLER& data);

//EdsError downloadImage(EdsDirectoryItemRef& directoryItem);