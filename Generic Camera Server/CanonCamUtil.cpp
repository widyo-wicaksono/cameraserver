#include "pch.h"
#include "CanonCamUtil.h"

//#include <opencv2/highgui/highgui_c.h>

CRITICAL_SECTION g_cam_cs;
std::vector<std::string> g_cam_global_buffer;

static EdsError EDSCALLBACK  handlePropertyEvent(
	EdsUInt32			inEvent,
	EdsUInt32			inPropertyID,
	EdsUInt32			inParam,
	EdsVoid *			inContext
)
{
	// do something	
	switch (inEvent)
	{
		case kEdsPropertyEvent_PropertyChanged:
			if (inPropertyID == kEdsPropID_Evf_OutputDevice) {
				CANON_CAM_HANDLER* pData = (CANON_CAM_HANDLER*)inContext;
				if (pData->is_cam_inited.load())										
					pData->is_cam_ready_for_lv.store(true);
			}
			break;
	}
	return EDS_ERR_OK;
}

static EdsError EDSCALLBACK  handleObjectEvent(
	EdsUInt32			inEvent,
	EdsBaseRef			inRef,
	EdsVoid *			inContext
) 
{
	CANON_CAM_HANDLER* pData = (CANON_CAM_HANDLER*)inContext;
	switch (inEvent)
	{	
	case kEdsObjectEvent_DirItemRequestTransfer:	
		EnterCriticalSection(&pData->cs);		
		pData->photo_refs.push_back(inRef);
		LeaveCriticalSection(&pData->cs);
	//	pData->pic_count_in_buffer.fetch_add(1);
		pData->is_cam_ready_for_photo.store(true);
		break;	
	default:
		//Object without the necessity is released
		if (inRef != NULL){
			EdsRelease(inRef);
		}
		break;
	}
	return EDS_ERR_OK;
}

static EdsError EDSCALLBACK  handleStateEvent(
	EdsUInt32			inEvent,
	EdsUInt32			inParam,
	EdsVoid *			inContext
)
{
	CANON_CAM_HANDLER* pData = (CANON_CAM_HANDLER*)inContext;
	switch (inEvent)
	{
	case kEdsStateEvent_Shutdown:
		//PostMessage(pData->hMainWnd, WM_CAMERA_ERRORS, ERR_CAMERA_DC, NULL);
		pData->SetError(ERR_CAMERA_DC);
		pData->bIsRunning.store(false); //WTF man
		break;
	}
	return EDS_ERR_OK;
}

EdsError downloadImage(EdsDirectoryItemRef& directoryItem, std::string& filename)
{
	EdsError err = EDS_ERR_OK;
	EdsStreamRef stream = NULL;
	// Get directory item information
	EdsDirectoryItemInfo dirItemInfo;
	std::string s = "html\\";
	//std::string filename;
	err = EdsGetDirectoryItemInfo(directoryItem, &dirItemInfo);
	// Create file stream for transfer destination
	if (err == EDS_ERR_OK){		
		s = s + dirItemInfo.szFileName;
		filename = dirItemInfo.szFileName;		
		err = EdsCreateFileStream(s.c_str(), kEdsFileCreateDisposition_CreateAlways, kEdsAccess_ReadWrite, &stream);
	}
	
	// Download image
	if (err == EDS_ERR_OK){
		err = EdsDownload(directoryItem, dirItemInfo.size, stream);
	}
	
	// Issue notification that download is complete
	if (err == EDS_ERR_OK){
		err = EdsDownloadComplete(directoryItem);
	}
	/*
	//g_cam_cs.Lock();
	EnterCriticalSection(&g_cam_cs);
	if (err == EDS_ERR_OK) {					
		g_cam_global_buffer.push_back(filename);
	}	
	//g_cam_cs.Unlock();
	LeaveCriticalSection(&g_cam_cs);
	*/
	if (directoryItem != NULL) {
		EdsRelease(directoryItem);
		directoryItem = NULL;
	}

	// Release stream
	if (stream != NULL){
		EdsRelease(stream);
		stream = NULL;
	}

	return err;
}

EdsError takePicture(EdsCameraRef camera)
{
	EdsError err;
	
	err = EdsSendCommand(camera, kEdsCameraCommand_PressShutterButton, kEdsCameraCommand_ShutterButton_Completely_NonAF);
	EdsSendCommand(camera, kEdsCameraCommand_PressShutterButton, kEdsCameraCommand_ShutterButton_OFF);
	
	return err;
}

EdsError activateBurstMode(EdsCameraRef camera, EdsUInt32& oldmode) {
	EdsUInt32 drive_mode = 1;
	EdsUInt32 ret = EdsGetPropertyData(camera, kEdsPropID_DriveMode, 0, sizeof(EdsUInt32), &oldmode);
	if(ret== EDS_ERR_OK)
		return EdsSetPropertyData(camera, kEdsPropID_DriveMode, 0, sizeof(EdsUInt32), &drive_mode);	
	return ret;
}


EdsError deActivateBurstMode(EdsCameraRef camera, EdsUInt32& oldmode) {	
	return EdsSetPropertyData(camera, kEdsPropID_DriveMode, 0, sizeof(EdsUInt32), &oldmode);
}

EdsError pressShutterButton(EdsCameraRef camera)
{	
	return EdsSendCommand(camera, kEdsCameraCommand_PressShutterButton, kEdsCameraCommand_ShutterButton_Completely_NonAF);		
}

EdsError releaseShutterButton(EdsCameraRef camera)
{
	return EdsSendCommand(camera, kEdsCameraCommand_PressShutterButton, kEdsCameraCommand_ShutterButton_OFF);
}

EdsError getFirstCamera(EdsCameraRef *camera)
{
	EdsError err = EDS_ERR_OK;
	EdsCameraListRef cameraList = NULL;
	EdsUInt32 count = 0;
	// Get camera list
	err = EdsGetCameraList(&cameraList);
	// Get number of cameras
	if (err == EDS_ERR_OK)
	{
		err = EdsGetChildCount(cameraList, &count);
		if (count == 0)
		{
			err = EDS_ERR_DEVICE_NOT_FOUND;
		}
	}
	// Get first camera retrieved
	if (err == EDS_ERR_OK)
	{
		err = EdsGetChildAtIndex(cameraList, 0, camera);
	}
	// Release camera list
	if (cameraList != NULL)
	{
		EdsRelease(cameraList);
		cameraList = NULL;
	}
	return err;
}

int InitCanonCam(EdsCameraRef &camera, EdsVoid* pData) {
	EdsError err = EDS_ERR_OK;	
	bool	 locked = false;
	// Get first camera
	if (err == EDS_ERR_OK)
	{
		// See Sample 2.
		err = getFirstCamera(&camera);
	}
	// Set Object event handler
	if (err == EDS_ERR_OK)
	{
		err = EdsSetObjectEventHandler(camera, kEdsObjectEvent_All, handleObjectEvent, pData);
	}
	// Set Property event handler
	if (err == EDS_ERR_OK)
	{
		err = EdsSetPropertyEventHandler(camera, kEdsPropertyEvent_All, handlePropertyEvent, pData);
	}
	// Set State event handler
	if (err == EDS_ERR_OK)
	{
		err = EdsSetCameraStateEventHandler(camera, kEdsStateEvent_All, handleStateEvent, pData);
	}	
	if (err == EDS_ERR_OK)
	{
		err = EdsOpenSession(camera);
	}
	
	if (err == EDS_ERR_OK)
	{
		EdsUInt32 saveTo = kEdsSaveTo_Host;
		err = EdsSetPropertyData(camera, kEdsPropID_SaveTo, 0, sizeof(saveTo), &saveTo);
	}
	//UI lock
	if (err == EDS_ERR_OK)
	{
		err = EdsSendStatusCommand(camera, kEdsCameraStatusCommand_UILock, 0);
	}

	if (err == EDS_ERR_OK)
	{
		locked = true;
	}

	if (err == EDS_ERR_OK)
	{
		EdsCapacity capacity = { 0x7FFFFFFF, 0x1000, 1 };
		err = EdsSetCapacity(camera, capacity);
	}

	//It releases it when locked
	if (locked)
	{
		EdsSendStatusCommand(camera, kEdsCameraStatusCommand_UIUnLock, 0);
	}
	
	/*
	EdsUInt32 drive_mode = 1;
	int result = EdsSetPropertyData(camera, kEdsPropID_DriveMode, 0, sizeof(EdsUInt32), &drive_mode);
	assert(result == EDS_ERR_OK);

	EdsUInt32 new_drive_mode;
	result = EdsGetPropertyData(camera, kEdsPropID_DriveMode, 0, sizeof(EdsUInt32), &new_drive_mode);
	assert(result == EDS_ERR_OK);
	assert(new_drive_mode == drive_mode);
*/
	if(err== EDS_ERR_OK)
		return 0;


	return -1;
}

int UninitCanonCam(EdsCameraRef &camera) {
	EdsError err = EDS_ERR_OK;
	if (err == EDS_ERR_OK && camera != NULL)
	{
		err = EdsCloseSession(camera);
	}
	// Release camera
	if (camera != NULL)
	{
		EdsRelease(camera);
	}	
	return 0;
}

EdsError startLiveview(EdsCameraRef camera)
{
	EdsError err = EDS_ERR_OK;
	// Get the output device for the live view image
	EdsUInt32 device;
	err = EdsGetPropertyData(camera, kEdsPropID_Evf_OutputDevice, 0, sizeof(device), &device);
	// PC live view starts by setting the PC as the output device for the live view image.
	if (err == EDS_ERR_OK)
	{
		device |= kEdsEvfOutputDevice_PC;
		err = EdsSetPropertyData(camera, kEdsPropID_Evf_OutputDevice, 0, sizeof(device), &device);		
	}
	// A property change event notification is issued from the camera if property settings are made successfully.
	// Start downloading of the live view image once the property change notification arrives.
	return err;
}

bool IsLiveViewStarted(EdsCameraRef &camera)
{
	EdsError err = EDS_ERR_OK;
	EdsUInt32 device;
	err = EdsGetPropertyData(camera, kEdsPropID_Evf_OutputDevice, 0, sizeof(device), &device);
	if ((device & kEdsEvfOutputDevice_PC) != 0)
	{
		return true;
	}
	return false;
}

EdsError endLiveview(EdsCameraRef camera)
{
	EdsError err = EDS_ERR_OK;
	// Get the output device for the live view image
	EdsUInt32 device;
	err = EdsGetPropertyData(camera, kEdsPropID_Evf_OutputDevice, 0, sizeof(device), &device);
	// PC live view ends if the PC is disconnected from the live view image output device.
	if (err == EDS_ERR_OK)
	{
		device &= ~kEdsEvfOutputDevice_PC;
		err = EdsSetPropertyData(camera, kEdsPropID_Evf_OutputDevice, 0, sizeof(device), &device);
	}
	return err;
}

EdsError downloadEvfData(EdsCameraRef camera, HWND main_window, cv::Mat& frame_) {
	EdsError err = EDS_ERR_OK;
	EdsStreamRef stream = NULL;
	EdsEvfImageRef evfImage = NULL;
	// Create memory stream.
	err = EdsCreateMemoryStream(0, &stream);
	// Create EvfImageRef.
	if (err == EDS_ERR_OK) {
		err = EdsCreateEvfImageRef(stream, &evfImage);
	}
	else
		return err;
	// Download live view image data.
	if (err == EDS_ERR_OK){
		err = EdsDownloadEvfImage(camera, evfImage);
	}
	else
		return err;
	// Get the incidental data of the image.
	if (err == EDS_ERR_OK){
		// Get the zoom ratio
		EdsUInt32 zoom;
		EdsGetPropertyData(evfImage, kEdsPropID_Evf_ZoomPosition, 0, sizeof(zoom), &zoom);
		// Get the focus and zoom border position
		EdsPoint point;
		EdsGetPropertyData(evfImage, kEdsPropID_Evf_ZoomPosition, 0, sizeof(point), &point);
	}
	else
		return err;

	EdsUInt64 size;
	EdsGetLength(stream, &size);
	
	if (size > 0) {				
		unsigned char* pbyteImage = NULL;

		EdsGetPointer(stream, (EdsVoid**)&pbyteImage);
		std::vector<unsigned char> ImVec(pbyteImage, pbyteImage + size);

		cv::Mat cvFrame = cv::imdecode(ImVec, 1);
		cv::resize(cvFrame, cvFrame, cv::Size(), 0.5, 0.5);

		frame_ =  cv::imdecode(ImVec, 1);					
	}
	
	// Release stream
	if (stream != NULL)
	{
		EdsRelease(stream);
		stream = NULL;
	}
	// Release evfImage
	if (evfImage != NULL)
	{
		EdsRelease(evfImage);
		evfImage = NULL;
	}
	return err;
}

int GetCommand(CANON_CAM_HANDLER* pData, std::string& command, void** pOwner) {	
	EnterCriticalSection(&pData->cs);
	int ret = -1;	

	if (pData->command_queues.size() > 0) {
		command = pData->command_queues[0].command;
		*pOwner = pData->command_queues[0].pOwner;
		if(command=="takecontinuousphotos")
			pData->photo_count = pData->command_queues[0].param01;
		else if (command == "takeburstphotos") {
			pData->burst_duration = pData->command_queues[0].param01;
			pData->photo_count = pData->command_queues[0].param00;
		}
		pData->command_queues.erase(pData->command_queues.begin());
		ret = 0;
	}

	LeaveCriticalSection(&pData->cs);
	return ret;
}

void CleanUpCameraThread(CANON_CAM_HANDLER& data) {
	if (data.hThreadHandle) {
		if (WAIT_TIMEOUT == ::WaitForSingleObject(data.hThreadHandle, 5000)) {
			::TerminateThread(data.hThreadHandle, -1000);
		}
		CloseHandle(data.hThreadHandle);
		data.hThreadHandle = NULL;
	}

	data.is_cam_inited.store(false);
	data.is_cam_ready_for_lv.store(false);
	data.is_cam_ready_for_photo.store(false);
	data.is_cam_lv_on.store(false);
}

void StopCameraThread(CANON_CAM_HANDLER& data) {
	data.bIsRunning.store(CAM_THREAD_FORCE_EXIT);
}

int BurstPhotos(CANON_CAM_HANDLER& data, EdsCameraRef camera, int& step, void* issuer_id) {

	static EdsUInt32 old_mode;
	static std::chrono::high_resolution_clock::time_point burst_start_time;
	static int picture_downloaded = 0;
	static int duration = 0;

	int ret = -1;

	auto now = std::chrono::high_resolution_clock::now();

	if (step == 0) {
		EnterCriticalSection(&data.cs);
		COMMAND_CAM x;
		x.pOwner = issuer_id;
		x.status = -2;
		data.result_queues.push_back(x);
		LeaveCriticalSection(&data.cs);

		//data.pic_count_in_buffer.store(0);
		
		activateBurstMode(camera, old_mode);
		pressShutterButton(camera);
		burst_start_time = std::chrono::high_resolution_clock::now();
		picture_downloaded = 0;
		step++;
		return ERR_IN_PROGRESS;
	}
	else if (step == 1) {		
		duration = (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(now - burst_start_time).count();
		if (duration > data.burst_duration ) {
			releaseShutterButton(camera);
			deActivateBurstMode(camera, old_mode);
			step++;
		}		
		return ERR_IN_PROGRESS;
	}

	if (picture_downloaded < data.photo_count) {		
		std::string filename;
		EnterCriticalSection(&data.cs);
		int refs_size = (int)data.photo_refs.size();
		LeaveCriticalSection(&data.cs);
		if (picture_downloaded < refs_size) {
			EdsError res = downloadImage(data.photo_refs[picture_downloaded], filename);
			if (res == EDS_ERR_OK) {
				EnterCriticalSection(&data.cs);
				data.result_queues[data.result_queues.size() - 1].results.push_back(filename);
				LeaveCriticalSection(&data.cs);
				picture_downloaded++;
				data.is_cam_ready_for_photo.store(false);
				return ERR_IN_PROGRESS;
			}
			else {
				ret = -1;
			}
		}
		else {
			duration = (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(now - burst_start_time).count();
			if (duration > data.burst_duration * 2) {
				data.photo_refs.clear();
				data.result_queues[data.result_queues.size() - 1].status = 0;
				duration = 0;
				ret = 0;
			}
			else
				return ERR_IN_PROGRESS;
		}
	}
	else{
		data.photo_refs.clear();
		data.result_queues[data.result_queues.size() - 1].status = 0;
		duration = 0;
		ret = 0;
	}	
	return ret;
}

int ContinuousPhotos(CANON_CAM_HANDLER& data, EdsCameraRef camera, int& step, void* issuer_id) {
	static int picture_taken = 0;
	int picture_downloaded = 0;

	int ret = -1;

	if (step == 0) {

		EnterCriticalSection(&data.cs);
		COMMAND_CAM x;
		x.pOwner = issuer_id;		
		x.status = -2;
		data.result_queues.push_back(x);
		LeaveCriticalSection(&data.cs);

		if (takePicture(camera) == EDS_ERR_OK) {
			picture_taken++;
			step++;
			return ERR_IN_PROGRESS;
		}
		else
			ret = -1;
	}
	else if (step == 1) {
		if (data.is_cam_ready_for_photo.load()) {
			if (takePicture(camera) == EDS_ERR_OK) {
				data.is_cam_ready_for_photo.store(false);
				picture_taken++;
				if (picture_taken == data.photo_count) {
					step++;					
				}
				return ERR_IN_PROGRESS;
			}
			else {				
				ret = -1;
			}
		}
		else
			return ERR_IN_PROGRESS;
	}
	else if (step == 2) {
		if (data.is_cam_ready_for_photo.load()) {
			for (int i = 0; i < data.photo_count; i++) {
				std::string filename;
				EdsError res = downloadImage(data.photo_refs[i], filename);
				if (res == EDS_ERR_OK) {					
					EnterCriticalSection(&data.cs);					
					data.result_queues[data.result_queues.size() - 1].results.push_back(filename);
					LeaveCriticalSection(&data.cs);
					picture_downloaded++;
					data.is_cam_ready_for_photo.store(false);
				}
				else {
					ret = -1;
				}
			}
		}
		else
			return ERR_IN_PROGRESS;
	}
	if (picture_taken == data.photo_count && picture_downloaded == data.photo_count) {	
		data.result_queues[data.result_queues.size() - 1].status = 0;
		ret = 0;
	}
	data.photo_refs.clear();
	picture_taken = 0;
	return ret;
}

DWORD WINAPI CanonCameraThread(LPVOID pParam) {	
	static int iFrameFailCount = 0;

	InitializeCriticalSection(&g_cam_cs);
	CoInitializeEx(NULL, COINIT_MULTITHREADED);

	EdsInitializeSDK();

	CANON_CAM_HANDLER* pData = (CANON_CAM_HANDLER*)pParam;
	pData->pLog->AsyncWrite("DSLR Thread started", true, true);

	EdsCameraRef camera = NULL;
	if (InitCanonCam(camera, (EdsVoid*)pData) == 0) {
		cv::namedWindow("Canon Camera Output");
		//HWND hWnd = (HWND)cvGetWindowHandle("Canon Camera Output");
		//ShowWindow(hWnd, SW_HIDE);

		RECT desktop;
		// Get a handle to the desktop window
		const HWND hDesktop = GetDesktopWindow();
		// Get the size of screen to the variable desktop
		GetWindowRect(hDesktop, &desktop);
		// The top left corner will have coordinates (0,0)
		// and the bottom right corner will have coordinates
		// (horizontal, vertical)		
		int vertical = desktop.bottom;
		cv::moveWindow("Canon Camera Output", 0, vertical + 10);

		cv::Mat bg = cv::Mat::zeros(cv::Size(640, 320), CV_8UC3);
				
		pData->is_cam_inited.store(true);

		int bIsRunning = CAM_THREAD_STATE_RUNNING;
		pData->bIsRunning.store(bIsRunning);		

		std::string command("");		
		void* issuer_id = nullptr;
		int task_step = 0;

		while (bIsRunning == CAM_THREAD_STATE_RUNNING) {			
			if (command == "")				
				GetCommand(pData, command, &issuer_id);								
			if (command == "takephoto") {
				if (task_step == 0) {
					if (takePicture(camera) != EDS_ERR_OK) {
						pData->SetError(ERR_CAMERA_TAKE_PHOTO_FAIL);
						pData->pLog->AsyncWrite("Taking photo fail", true, true);
						command = "";
					}
					else {
						task_step++;
						Sleep(1000);
					}
				}
				else {					
					if (task_step == 1) {
						if (pData->is_cam_ready_for_photo.load()) {
							std::string filename;							
							EdsError res = downloadImage(pData->photo_refs[0], filename);
							if (res == EDS_ERR_OK) {
								Sleep(1000);
								COMMAND_CAM x;
								x.pOwner = issuer_id;								
								x.results.push_back(filename);
								x.status = 0;								
								EnterCriticalSection(&pData->cs);
								pData->result_queues.push_back(x);
								LeaveCriticalSection(&pData->cs);			
								pData->photo_refs.clear();
							}
							else if (res == EDS_ERR_OBJECT_NOTREADY) {

							}
							else {
								pData->pLog->AsyncWrite("Taking photo fail", true, true);
								COMMAND_CAM x;
								x.pOwner = issuer_id;								
								x.results.push_back(filename);
								x.status = -1;
								EnterCriticalSection(&pData->cs);
								pData->result_queues.push_back(x);
								LeaveCriticalSection(&pData->cs);
								
							}
							if (res != EDS_ERR_OBJECT_NOTREADY) {
								task_step = 0;
								command = "";
								pData->is_cam_ready_for_photo.store(false);
							}
						}
					}
				}
			}
			else if (command == "takecontinuousphotos") {								
				int res = ContinuousPhotos(*pData, camera, task_step, issuer_id);
				if (res == 0 || res == -1) {
					task_step = 0;
					command = "";
					if(res == -1)
						pData->SetError(ERR_CAMERA_TAKE_PHOTO_FAIL);
				}				
			}
			else if (command == "takeburstphotos") {
				int res = BurstPhotos(*pData, camera, task_step, issuer_id);
				if (res == 0 || res == -1) {
					task_step = 0;
					command = "";
					if (res == -1)
						pData->SetError(ERR_CAMERA_TAKE_PHOTO_FAIL);
				}
			}
			else if (command == "livefeed") {
				command = "";
				if (!pData->is_cam_lv_on.load()) {
					EdsError err = startLiveview(camera);
					if (err != EDS_ERR_OK) {					
						pData->SetError(ERR_CAMERA_LV_FAIL);						
					}							
					else
						pData->is_cam_lv_on.store(true);
				}
				else {
					endLiveview(camera);												
					pData->is_cam_lv_on.store(false);						
					pData->is_cam_ready_for_lv.store(false);
				}
			}
			else if (command == "stoplivefeed") {
				command = "";

				endLiveview(camera);
				pData->is_cam_lv_on.store(false);
				pData->is_cam_ready_for_lv.store(false);
			}
			/*
			else if (command == "livefeedstatus") {
				if (pData->is_cam_lv_on.load()) {
					PostMessage(pData->hMainWnd, WM_CAMERA_EVENTS, EVT_CAM_LIVE_FEED_STATUS, PARAM_CAM_LIVEFEED_ON);
				}
				else {
					PostMessage(pData->hMainWnd, WM_CAMERA_EVENTS, EVT_CAM_LIVE_FEED_STATUS, PARAM_CAM_LIVEFEED_OFF);
				}
			}
			*/
			

			if (pData->is_cam_lv_on.load()) {
				if (pData->is_cam_ready_for_lv.load()) {
					EdsError err = downloadEvfData(camera, pData->hMainWnd, bg);
					EnterCriticalSection(&pData->cs);
					if (pData->frames.size() > 50)
						pData->frames.erase(pData->frames.begin());
					pData->frames.push_back(bg);
					LeaveCriticalSection(&pData->cs);
					if (err != EDS_ERR_OK ) {						
						if (err == EDS_ERR_OBJECT_NOTREADY) {
							Sleep(1000);
						}
						else {
							iFrameFailCount++;
							/*
							if (iFrameFailCount > 25) {
								endLiveview(camera);
								pData->is_cam_lv_on.store(false);
								pData->is_cam_ready_for_lv.store(false);
								iFrameFailCount = 0;
								
								pData->SetError(ERR_CAMERA_PERMANENT_FRAME_FAIL);
								break;
							}
							*/
						}
					}					
					
				}
			}

			bIsRunning = pData->bIsRunning.load();			
			cv::imshow("Canon Camera Output", bg);
			cv::waitKey(5);
		}

		if (!pData->is_cam_lv_on.load()) {
			endLiveview(camera);
		}
		UninitCanonCam(camera);
	}	
	else {		
	//	pData->error_code.store(ERR_CAMERA_INIT);
		pData->SetError(ERR_CAMERA_INIT);
	}

	EdsTerminateSDK();
	CoUninitialize();	
	pData->command_queues.clear();
	pData->result_queues.clear();
	pData->pLog->AsyncWrite("DSLR Thread exiting ...", true, true);
	pData->bIsRunning.store(CAM_THREAD_STATE_EXIT);		
	return 0;
}
