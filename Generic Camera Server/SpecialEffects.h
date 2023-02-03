#pragma once
#include <string>
#include <atomic>
#include <thread>
#include <future>

#include <opencv2/opencv.hpp>
#include <SDL2/SDL.h>

#include "LogManager.h"

#define STACK_INDEX_DIM 0
#define STACK_INDEX_BG_REMOVAL 3
#define STACK_INDEX_DYN_FRAME 5
#define STACK_INDEX_FILTER 10
#define STACK_INDEX_ACC 12
#define STACK_INDEX_FRAME 15

#define EFFECT_THREAD_STATE_IDLE 1
#define EFFECT_THREAD_STATE_STARTING 2
#define EFFECT_THREAD_STATE_RUNNING 3
#define EFFECT_THREAD_STATE_EXITING 4
#define EFFECT_THREAD_STATE_EXIT 5

#define EFFECT_ERR_NONE 100
#define EFFECT_FILE_FAIL_TO_OPEN 101
#define EFFECT_ERR_FILE_EMPTY_FRAME 102
#define EFFECT_ENDED 103

class CBaseEffect{
protected:
	int m_stack_effect_index = 0;
	std::string m_name = "DUMMY";	
	
	CLogManager* m_pLog = nullptr;
	std::atomic<int> m_error_code;

	cv::Mat frame_overlay;

public:	
	CBaseEffect(const std::string& name) {
		m_name = name; 
		m_pLog = CLogManager::getInstance();
		m_error_code.store(EFFECT_ERR_NONE);		
		m_pLog->AsyncWrite(m_pLog->string_format("Effect [%s] created", m_name.c_str()).c_str(), true, true);
	};

	CBaseEffect(const std::string& name, int stack_index) {
		m_name = name;
		m_pLog = CLogManager::getInstance();
		m_stack_effect_index = stack_index;
		m_error_code.store(EFFECT_ERR_NONE);
		m_pLog->AsyncWrite(m_pLog->string_format("Effect [%s] created", m_name.c_str()).c_str(), true, true);
	};
	
	virtual ~CBaseEffect() {		
		m_pLog->AsyncWrite(m_pLog->string_format("Effect [%s] destroyed", m_name.c_str()).c_str(), true, true);
	};
	virtual int filter(cv::Mat& frame) { return 0; };	
	std::string GetName() { return m_name; };
	void SetName(const std::string& name) { m_name = name; };
	int GetStackIndex() {
		return m_stack_effect_index;
	};

	int GetError() {
		return m_error_code.load();
	};
	void ResetError() {
		m_error_code.store(EFFECT_ERR_NONE);
	};
};

class CCrop : public CBaseEffect{
private:
	int m_width = 0;
	int m_height = 0;
public:	
	CCrop(int x, int y);
	int filter(cv::Mat& frame);
};

class CFilter : public CBaseEffect {
public:
	CFilter(const std::string& name);
	int filter(cv::Mat& frame);
};


class COverlay : public CBaseEffect {
protected:
	int overlay_index = 0;
public:	
	COverlay(const std::string& name);
	COverlay(const std::string& name, int stack_index);

	int filter(cv::Mat& frame);
	void overlayImage(const cv::Mat &background, const cv::Mat &foreground, cv::Mat &output, cv::Point2i location);
};

class CBackGorundRemoval : public CBaseEffect {
private:
	cv::Scalar m_low = cv::Scalar(0, 255, 0);
	cv::Scalar m_hi = cv::Scalar(10, 255, 0);
	cv::Mat m_bg;

	std::atomic<bool> m_isrunning;
	std::atomic<int> m_state;
	std::atomic<int> m_total_frames_processed;
	std::atomic<int> m_total_frame_count;

	std::mutex m_framelock;
	std::thread m_liveview_thread;

	std::vector<cv::Mat> m_frame_buffer;

	bool m_is_static = true;
	bool m_is_repeat = false;
	bool m_is_greenscreen = true;
	//bool m_is_greenscreen = false;

	int m_width = -1;
	int m_height = -1;

	cv::Mat m_bg_sub;
	cv::Ptr<cv::BackgroundSubtractor> m_pBackSub;

public:		

	CBackGorundRemoval(const char* name, int rl, int gl, int bl, int rh, int gh, int bh);
	CBackGorundRemoval(const char* name, int rl, int gl, int bl, int rh, int gh, int bh, bool is_static, bool repeat);
	CBackGorundRemoval(const char* name, int rl, int gl, int bl, int rh, int gh, int bh, bool is_static, int width, int height, bool repeat);
	CBackGorundRemoval(const char* name, bool is_static, bool is_greenscreen, int width, int height, bool repeat, cv::Mat& frame);

	~CBackGorundRemoval() {
		m_isrunning.store(false);
		if (m_liveview_thread.joinable()) {
			m_liveview_thread.join();
		}
	}
	int filter(cv::Mat& frame);
	int GrabMostRecentFrame(cv::Mat& frame);
	int GetLoopState() {
		return m_state.load();
	}
	void FnLiveViewThread();
};

class CDynamicFrame : public CBaseEffect {
private:
	std::thread m_liveview_thread;

	std::atomic<bool> m_isrunning;	
	std::atomic<int> m_state;
	std::mutex m_framelock;
	std::string m_filename;

	std::vector<cv::Mat> m_frame_buffer;

	int m_R_low = 0;
	int m_G_low = 1;
	int m_B_low = 0;

	int m_R_high = 50;
	int m_G_high = 255;
	int m_B_high = 50;

	bool m_greenscreen_mode = true;
	bool m_is_hw_accelerated = false;
	bool m_is_repeat = false;

	static SDL_Window* sdl_window;
	static SDL_Renderer* sdl_renderer;
	static SDL_Texture* sdl_base_texture;

	SDL_Texture* m_sdl_layer;

	static int object_count;
	static int processed_object_count;

	int m_width = -1;
	int m_height = -1;

public:
	
	CDynamicFrame(const char* name, bool greenscreen, bool repeat);
	CDynamicFrame(const char* name, bool greenscreen, int stackindex, bool repeat);
	CDynamicFrame(const char* name, bool greenscreen, int stackindex, int width, int height, bool repeat);
	CDynamicFrame(const char* name, bool greenscreen, int stackindex, int rl, int gl, int bl, int rh, int gh, int bh, bool repeat);
	CDynamicFrame(const char* name, bool greenscreen, int stackindex, int width, int height, int rl, int gl, int bl, int rh, int gh, int bh, bool repeat);

	~CDynamicFrame() {		
		m_isrunning.store(false);
		if (m_liveview_thread.joinable())
			m_liveview_thread.join();
		if (m_is_hw_accelerated) {
			SDL_DestroyTexture(m_sdl_layer);
			object_count--;
			if (object_count == 0) {
				SDL_DestroyTexture(sdl_base_texture);
				SDL_DestroyRenderer(sdl_renderer);
				SDL_DestroyWindow(sdl_window);
				SDL_Quit();
			}			
		}
	}
	int filter(cv::Mat& frame);
	int hwAcceleratedFilter(cv::Mat& frame);
	int GrabMostRecentFrame(cv::Mat& frame);
	int GetState();

	void overlayImage(const cv::Mat &background, const cv::Mat &foreground, cv::Mat &output, cv::Point2i location);
	void FnLiveViewThread();
	
	int AsyncOverlay(const cv::Mat background, const cv::Mat foreground, cv::Mat output, int partindex, int totalpart);
};

class CClassicFaceDetect : public CBaseEffect {
	struct LANDMARK {
		bool bIsFacesFound = false;
		cv::Rect face;
		cv::Rect eye01;
		cv::Rect eye02;
		int score = 0;
		bool bIsPersonExist = false;
	};
private:
	cv::CascadeClassifier m_cascade_face;
	double m_confidence_threshold;
	int m_minimum_size;

	std::future<int> m_detection_future;

	std::atomic<bool> m_isrunning;
	std::atomic<int> m_state;
	std::mutex m_framelock;

	std::vector<LANDMARK> m_landmarks;	
	std::vector<LANDMARK> m_output_landmarks;
	cv::Mat m_overlay;
	cv::Mat m_last_frame;

public:
	CClassicFaceDetect(const char* name, double confidence, int minimum_size);
	CClassicFaceDetect(const char* name, double confidence, int minimum_size, int stack_index);

	~CClassicFaceDetect() {
		int state = m_state.load();
		if (state != EFFECT_THREAD_STATE_IDLE)
			m_detection_future.get();
	};

	int FnDetectionThread(cv::Mat frame);
	int DetectFaceNonThreaded(cv::Mat& frame, std::vector<LANDMARK>& outputs);
	int filter(cv::Mat& frame);

	void overlayImage(const cv::Mat &background, const cv::Mat &foreground, cv::Mat &output, cv::Point2i location);
};