#include "pch.h"
#include "SpecialEffects.h"
#include "BasicFFMPEGWrap.h"
#include "GeneralUtils.h"

#include <future>

SDL_Window* CDynamicFrame::sdl_window = nullptr;
SDL_Renderer* CDynamicFrame::sdl_renderer = nullptr;
SDL_Texture* CDynamicFrame::sdl_base_texture = nullptr;

int CDynamicFrame::object_count = 0;
int CDynamicFrame::processed_object_count = 0;

CFilter::CFilter(const std::string& name, std::shared_ptr<CLogManager> log) : CBaseEffect(name, log) {
	m_stack_effect_index = STACK_INDEX_FILTER;
}

int CFilter::filter(cv::Mat& frame) {
	if (m_name == "Grayscale") {
		cv::cvtColor(frame, frame, cv::COLOR_RGB2GRAY);
		cvtColor(frame, frame, cv::COLOR_GRAY2RGB);
	}
	else if(m_name == "Colormap Cool"){
		applyColorMap(frame, frame, cv::COLORMAP_COOL);
	}
	else if (m_name == "Colormap Hot") {
		applyColorMap(frame, frame, cv::COLORMAP_HOT);
	}
	else if (m_name == "Sepia") {
		cv::Mat kernel =
			(cv::Mat_<float>(3, 3)
				<<
				0.272, 0.534, 0.131,
				0.349, 0.686, 0.168,
				0.393, 0.769, 0.189);
		cv::transform(frame, frame, kernel);
	}
	return 0;
}



CCrop::CCrop(int x, int y, std::shared_ptr<CLogManager> log) : CBaseEffect("Crop", log), m_width(x), m_height(y) {
	m_stack_effect_index = STACK_INDEX_DIM;
}

int CCrop::filter(cv::Mat& frame) {
	if (m_name == "Crop") {
		cv::Rect ROI((frame.cols - frame.rows) / 2, 0, frame.rows, frame.rows);
		frame = frame(ROI);
	}
	else if (m_name == "Resize") {

	}
	return 0;
}

COverlay::COverlay(const std::string& name, std::shared_ptr<CLogManager> log) : CBaseEffect(name, log) {
	m_stack_effect_index = STACK_INDEX_FRAME;
#ifdef _WIN32
	std::string path = "frames\\" + m_name + ".png";
#else
	std::string path = "frames/" + m_name + ".png";
#endif
	frame_overlay = cv::imread(path, -1);
}

COverlay::COverlay(const std::string& name, std::shared_ptr<CLogManager> log, int stack_index) : CBaseEffect(name, log) {
	m_stack_effect_index = stack_index;
	//std::string path = "frames\\" + m_name + ".png";
#ifdef _WIN32
	std::string path = "frames\\" + m_name + ".png";
#else
	std::string path = "frames/" + m_name + ".png";
#endif
	frame_overlay = cv::imread(path, -1);
}

int COverlay::filter(cv::Mat& frame) {
	if (frame_overlay.cols != frame.cols || frame_overlay.rows != frame.rows)
		cv::resize(frame_overlay, frame_overlay, cv::Size(frame.cols, frame.rows));
	overlayImage(frame, frame_overlay, frame, cv::Point(0, 0));
	return 0;
}

void COverlay::overlayImage(const cv::Mat &background, const cv::Mat &foreground, cv::Mat &output, cv::Point2i location)
{
	background.copyTo(output);
	// start at the row indicated by location, or at row 0 if location.y is negative.
	for (int y = (std::max)(location.y, 0); y < background.rows; ++y)
	{
		int fY = y - location.y; // because of the translation
								 // we are done of we have processed all rows of the foreground image.
		if (fY >= foreground.rows)
			break;

		// start at the column indicated by location, 
		// or at column 0 if location.x is negative.
		for (int x = (std::max)(location.x, 0); x < background.cols; ++x)
		{
			int fX = x - location.x; // because of the translation.
									 // we are done with this row if the column is outside of the foreground image.
			if (fX >= foreground.cols)
				break;

			// determine the opacity of the foregrond pixel, using its fourth (alpha) channel.
			double opacity = ((double)foreground.data[fY * foreground.step + fX * foreground.channels() + 3])/ 255.;


			// and now combine the background and foreground pixel, using the opacity, 
			// but only if opacity > 0.
			for (int c = 0; opacity > 0 && c < output.channels(); ++c)
			{
				unsigned char foregroundPx =
					foreground.data[fY * foreground.step + fX * foreground.channels() + c];
				unsigned char backgroundPx =
					background.data[y * background.step + x * background.channels() + c];
				output.data[y*output.step + output.channels()*x + c] =
					backgroundPx * (1. - opacity) + foregroundPx * opacity;
			}
		}
	}
}

CDynamicFrame::CDynamicFrame(const char* name, std::shared_ptr<CLogManager> log, bool greenscreen, bool repeat) : CBaseEffect(name, log)
	, m_greenscreen_mode(greenscreen), m_is_repeat(repeat){
	m_stack_effect_index = STACK_INDEX_DYN_FRAME;

	m_isrunning.store(true);
	m_liveview_thread = std::thread(&CDynamicFrame::FnLiveViewThread, this);
}

CDynamicFrame::CDynamicFrame(const char* name, std::shared_ptr<CLogManager> log, bool greenscreen, int stackindex, bool repeat) : CBaseEffect(name, log)
	, m_greenscreen_mode(greenscreen), m_is_repeat(repeat){
	m_stack_effect_index = stackindex;

	m_isrunning.store(true);
	m_liveview_thread = std::thread(&CDynamicFrame::FnLiveViewThread, this);
}

CDynamicFrame::CDynamicFrame(const char* name, std::shared_ptr<CLogManager> log, bool greenscreen, int stackindex, int width, int height, bool repeat) : CBaseEffect(name, log)
	, m_greenscreen_mode(greenscreen),  m_width(width), m_height(height), m_is_repeat(repeat) {
	m_stack_effect_index = stackindex;
	m_isrunning.store(true);
	m_liveview_thread = std::thread(&CDynamicFrame::FnLiveViewThread, this);
	
	if (m_is_hw_accelerated) {
		if (object_count == 0) {
			if (SDL_Init(SDL_INIT_VIDEO) < 0) {
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s", SDL_GetError());				
			}
			
			sdl_window = SDL_CreateWindow("SDL_CreateTexture", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, m_width, m_height, SDL_WINDOW_MINIMIZED);			
			sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED);
			sdl_base_texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_TARGET, m_width, m_height);			
		}
		object_count++;
		m_sdl_layer = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STREAMING, m_width, m_height);
		SDL_SetTextureBlendMode(m_sdl_layer, SDL_BLENDMODE_BLEND);
	}
}

CDynamicFrame::CDynamicFrame(const char* name, std::shared_ptr<CLogManager> log, bool greenscreen, int stackindex, int rl, int gl, int bl, int rh, int gh, int bh, bool repeat) :CBaseEffect(name, log)
	, m_greenscreen_mode(greenscreen)
	, m_R_low(rl), m_G_low(gl), m_B_low(bl)
	, m_R_high(rh), m_G_high(gh), m_B_high(bh), m_is_repeat(repeat)
{
	m_stack_effect_index = stackindex;
	m_isrunning.store(true);
	m_liveview_thread = std::thread(&CDynamicFrame::FnLiveViewThread, this);
}

CDynamicFrame::CDynamicFrame(const char* name, std::shared_ptr<CLogManager> log, bool greenscreen, int stackindex
	, int width, int height, int rl, int gl, int bl, int rh, int gh, int bh, bool repeat) :CBaseEffect(name, log)
	, m_greenscreen_mode(greenscreen), m_width(width), m_height(height)
	, m_R_low(rl), m_G_low(gl), m_B_low(bl)
	, m_R_high(rh), m_G_high(gh), m_B_high(bh), m_is_repeat(repeat)
{
	m_stack_effect_index = stackindex;
	m_isrunning.store(true);
	m_liveview_thread = std::thread(&CDynamicFrame::FnLiveViewThread, this);
}

int CDynamicFrame::AsyncOverlay(const cv::Mat background, const cv::Mat foreground, cv::Mat output, int partindex, int totalpart) {

	if (background.rows != foreground.rows || background.cols != foreground.cols){
		return -1;
	}

	if (background.rows % totalpart != 0) {
		return -1;
	}

	int y = 0;
	int y_start = (background.rows * (partindex - 1)) / totalpart;
	int y_end = (background.rows * partindex) / totalpart;

	for (y = y_start; y < y_end; ++y)
	{
		for (int x = 0; x < background.cols; ++x)
		{
			double opacity = ((double)foreground.data[y * foreground.step + x * foreground.channels() + 3]) / 255.;
			for (int c = 0; opacity > 0 && c < output.channels(); ++c)
			{
				unsigned char foregroundPx = foreground.data[y * foreground.step + x * foreground.channels() + c];
				unsigned char backgroundPx = background.data[y * background.step + x * background.channels() + c];
				output.data[y*output.step + output.channels()*x + c] = backgroundPx * (1. - opacity) + foregroundPx * opacity;
			}
		}
	}
	return 0;
}

int CDynamicFrame::hwAcceleratedFilter(cv::Mat& frame) {
	
	if (processed_object_count == 0) {		
		cv::cvtColor(frame, frame, cv::COLOR_BGR2BGRA);
		SDL_Texture * temp_base_tex0 = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STREAMING, m_width, m_height);
		SDL_SetTextureBlendMode(temp_base_tex0, SDL_BLENDMODE_BLEND);

		void* pixels = nullptr;
		int pitch = 0;
		SDL_LockTexture(temp_base_tex0, NULL, &pixels, &pitch);
		memcpy(pixels, frame.data, pitch * frame.rows);
		SDL_UnlockTexture(temp_base_tex0);

		SDL_SetRenderTarget(sdl_renderer, sdl_base_texture);
		SDL_RenderCopy(sdl_renderer, temp_base_tex0, NULL, NULL);
		SDL_DestroyTexture(temp_base_tex0);
	}

	cv::Mat layer;
	void* pixels = nullptr;
	int pitch = 0;
	if (GrabMostRecentFrame(layer) == 0) {
		SDL_LockTexture(m_sdl_layer, NULL, &pixels, &pitch);
		memcpy(pixels, layer.data, pitch * frame.rows);
		SDL_UnlockTexture(m_sdl_layer);
		SDL_RenderCopy(sdl_renderer, m_sdl_layer, NULL, NULL);
	}

	processed_object_count++;
	if (processed_object_count == object_count) {
		void* pixelsource = (void*)malloc(4 * m_width * m_height);
		SDL_RenderReadPixels(sdl_renderer, NULL, SDL_PIXELFORMAT_BGRA32, pixelsource, m_width * 4);
		cv::Mat finalframe(m_height, m_width, CV_8UC4, pixelsource, m_width * 4);		
		frame = finalframe.clone();
		//delete (uchar*)pixelsource;
		free(pixelsource);
		processed_object_count = 0;
	}
	return 0;
}

int CDynamicFrame::filter(cv::Mat& frame) {
	int ret = 0;
	if (!m_is_hw_accelerated) {
		cv::Mat layer;
		if (GrabMostRecentFrame(layer) == 0) {
			if (m_greenscreen_mode) {
				cv::Mat mask;
				
				if (layer.cols != frame.cols || layer.rows != frame.rows)
					cv::resize(layer, layer, cv::Size(frame.cols, frame.rows));
				cv::inRange(layer, cv::Scalar(m_B_low, m_G_low, m_R_low), cv::Scalar(m_B_high, m_G_high, m_R_high), mask);				
				cv::bitwise_not(mask, mask);

				layer.copyTo(frame, mask);
			}
			else {
				
				if (layer.cols != frame.cols || layer.rows != frame.rows)
					cv::resize(layer, layer, cv::Size(frame.cols, frame.rows));

				auto fut1 = std::async(std::launch::async, &CDynamicFrame::AsyncOverlay, this, frame, layer, frame, 1, 4);
				auto fut2 = std::async(std::launch::async, &CDynamicFrame::AsyncOverlay, this, frame, layer, frame, 2, 4);
				auto fut3 = std::async(std::launch::async, &CDynamicFrame::AsyncOverlay, this, frame, layer, frame, 3, 4);
				auto fut4 = std::async(std::launch::async, &CDynamicFrame::AsyncOverlay, this, frame, layer, frame, 4, 4);
				auto fut5 = std::async(std::launch::async, &CDynamicFrame::AsyncOverlay, this, frame, layer, frame, 5, 5);

				fut1.get();
				fut2.get();
				fut3.get();
				fut4.get();
				fut5.get();
			}
		}
	}
	else {
		hwAcceleratedFilter(frame);
	}
	return ret;
}

void CDynamicFrame::FnLiveViewThread() {

	int empty_frame_count = 0;

	std::unique_ptr<CFPSCounter> pfps = std::make_unique<CFPSCounter>();

	m_error_code.store(EFFECT_ERR_NONE);
	m_state.store(EFFECT_THREAD_STATE_STARTING);
	bool bIsRunning = m_isrunning.load();

	while (bIsRunning) {
		if (m_greenscreen_mode) {
			cv::VideoCapture cap;
			if (cap.open("movies\\" + m_name)) {

				m_state.store(EFFECT_THREAD_STATE_RUNNING);
				double fps = cap.get(cv::CAP_PROP_FPS);
				bool isFirstFrame = true;				
				std::chrono::time_point<std::chrono::high_resolution_clock> ts;
				double f_delay_per = 1000.0 / fps;

				while (bIsRunning) {
					cv::Mat frame;
					cap >> frame;
					if (!frame.empty()) {

						m_framelock.lock();

						std::string fps = pfps->GetFPSinString();
						putText(frame, fps.c_str(), cv::Point(250, 50), cv::FONT_HERSHEY_SIMPLEX, 1.4, cv::Scalar(255, 0, 0), 4);

						if (m_frame_buffer.size() > 50) {
							m_frame_buffer.erase(m_frame_buffer.begin());
						}

						if (m_width != -1 || m_height != -1) {
							if (frame.cols != m_width || frame.rows != m_height) {
								cv::resize(frame, frame, cv::Size(m_width, m_height));
							}
						}

						m_frame_buffer.push_back(frame);
						m_framelock.unlock();

						if (isFirstFrame) {
							isFirstFrame = false;
						}
						else {
							auto decoding_finished_ts = std::chrono::high_resolution_clock::now();
							auto decoding_duration = (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(decoding_finished_ts - ts).count();
							double time_left_before_next_frame = f_delay_per - (double)decoding_duration;

							time_left_before_next_frame--; //adjustment for waitkey(1)

							if (time_left_before_next_frame > 0) {
								std::this_thread::sleep_for(std::chrono::milliseconds((int)time_left_before_next_frame));
							}
						}
						ts = std::chrono::high_resolution_clock::now();
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
					}
					else {						
						std::this_thread::sleep_for(std::chrono::milliseconds(5));
						empty_frame_count++;
						if (empty_frame_count > 5) {
							empty_frame_count = 0;
							m_error_code.store(EFFECT_ENDED);
							break;
						}
					}
					bIsRunning = m_isrunning.load();
				}
				cap.release();
				cv::destroyAllWindows();
			}
		}
		else {
			CBasicFFMPEGWrap ffmpeg;
			std::string err;
			std::string path = "movies\\" + m_name;
			if (ffmpeg.openStream(path, err) == 0) {
				double fps = ffmpeg.GetFPS();
				bool isFirstFrame = true;				
				std::chrono::time_point<std::chrono::high_resolution_clock> ts;
				double f_delay_per = 1000.0 / fps;

				cv::Mat frame;
				unsigned char* buff = nullptr;
				unsigned int width = 0, height = 0;

				while (bIsRunning) {
					int ret = ffmpeg.getVideoFrameAlpha(&buff, width, height);
					if (ret== 0) {
						cv::Mat fr(height, width, CV_8UC4, buff, width * 4);
						frame = fr.clone();
						ffmpeg.releaseFrame();
					}
					else if(ret== AVERROR_EOF){
						m_error_code.store(EFFECT_ENDED);
						break;
					}
					else if (ret == AVERROR(EAGAIN)) {
						continue;
					}
					else {
						m_error_code.store(EFFECT_ENDED);
						break;
					}
					if (!frame.empty()) {

						m_framelock.lock();

						std::string fps = pfps->GetFPSinString();						
						m_pLog->WriteToOCVScreen(fps.c_str(), 250, 50, 0, 255, 0, 255, frame);
						if (m_frame_buffer.size() > 50) {
							m_frame_buffer.erase(m_frame_buffer.begin());
						}

						if (m_width != -1 || m_height != -1) {
							if (frame.cols != m_width || frame.rows != m_height) {
								cv::resize(frame, frame, cv::Size(m_width, m_height));
							}
						}
						m_frame_buffer.push_back(frame);
						m_framelock.unlock();

						if (isFirstFrame) {
							isFirstFrame = false;
						}
						else {
							auto decoding_finished_ts = std::chrono::high_resolution_clock::now();
							auto decoding_duration = (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(decoding_finished_ts - ts).count();
							double time_left_before_next_frame = f_delay_per - (double)decoding_duration;

							time_left_before_next_frame--; //adjustment for waitkey(1)

							if (time_left_before_next_frame > 0) {
								std::this_thread::sleep_for(std::chrono::milliseconds((int)time_left_before_next_frame));
							}
						}
						ts = std::chrono::high_resolution_clock::now();
						//cv::imshow("TEST", frame);
						//cv::waitKey(5);
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
					}
					else {
						m_error_code.store(EFFECT_ERR_FILE_EMPTY_FRAME);
						std::this_thread::sleep_for(std::chrono::milliseconds(5));
						empty_frame_count++;
						if (empty_frame_count > 5) {
							empty_frame_count = 0;
							break;
						}
					}
					bIsRunning = m_isrunning.load();
				}
				ffmpeg.closeStream();
			}
		}
		bIsRunning = m_isrunning.load();
		if (!m_is_repeat)
			break;
	}	
	m_pLog->AsyncWrite(m_pLog->string_format("Effect Alpha/Greenscreen [%s] exiting ...", m_name.c_str()).c_str(), true, true);
	m_state.store(EFFECT_THREAD_STATE_EXITING);
}

int CDynamicFrame::GrabMostRecentFrame(cv::Mat& frame) {
	int ret = -1;
	m_framelock.lock();
	if (m_frame_buffer.size() > 0) {		
		frame = m_frame_buffer[m_frame_buffer.size() - 1];
		ret = 0;
	}
	m_framelock.unlock();
	return ret;
}

int CDynamicFrame::GetState() {
	return m_state.load();
}

void CDynamicFrame::overlayImage(const cv::Mat &background, const cv::Mat &foreground, cv::Mat &output, cv::Point2i location)
{
	background.copyTo(output);
	// start at the row indicated by location, or at row 0 if location.y is negative.
	for (int y = (std::max)(location.y, 0); y < background.rows; ++y)
	{
		int fY = y - location.y; // because of the translation
								 // we are done of we have processed all rows of the foreground image.
		if (fY >= foreground.rows)
			break;

		// start at the column indicated by location, 
		// or at column 0 if location.x is negative.
		for (int x = (std::max)(location.x, 0); x < background.cols; ++x)
		{
			int fX = x - location.x; // because of the translation.
									 // we are done with this row if the column is outside of the foreground image.
			if (fX >= foreground.cols)
				break;

			// determine the opacity of the foregrond pixel, using its fourth (alpha) channel.
			double opacity = ((double)foreground.data[fY * foreground.step + fX * foreground.channels() + 3]) / 255.;


			// and now combine the background and foreground pixel, using the opacity, 
			// but only if opacity > 0.
			for (int c = 0; opacity > 0 && c < output.channels(); ++c)
			{
				unsigned char foregroundPx =
					foreground.data[fY * foreground.step + fX * foreground.channels() + c];
				unsigned char backgroundPx =
					background.data[y * background.step + x * background.channels() + c];
				output.data[y*output.step + output.channels()*x + c] =
					backgroundPx * (1. - opacity) + foregroundPx * opacity;
			}
		}
	}
}

CBackGorundRemoval::CBackGorundRemoval(const char* name, std::shared_ptr<CLogManager> log, int rl, int gl, int bl, int rh, int gh, int bh) :CBaseEffect(name, log) {
	m_stack_effect_index = STACK_INDEX_BG_REMOVAL;
	m_low = cv::Scalar(bl, gl, rl);
	m_hi = cv::Scalar(bh, gh, rh);	

	m_state.store(EFFECT_THREAD_STATE_IDLE);
	m_bg = cv::imread("backgrounds\\" + m_name, -1);
	if (m_width != -1 || m_height != -1) {
		if (m_width != m_bg.cols || m_height != m_bg.rows)
			cv::resize(m_bg, m_bg, cv::Size(m_width, m_height));
	}
}

CBackGorundRemoval::CBackGorundRemoval(const char* name, std::shared_ptr<CLogManager> log, int rl, int gl, int bl, int rh, int gh, int bh, bool is_static, bool repeat) :CBaseEffect(name, log)
	, m_is_static(is_static), m_is_repeat(repeat)
{
	m_stack_effect_index = STACK_INDEX_BG_REMOVAL;
	m_low = cv::Scalar(bl, gl, rl);
	m_hi = cv::Scalar(bh, gh, rh);

	m_state.store(EFFECT_THREAD_STATE_IDLE);
	if (m_is_static) {
		m_bg = cv::imread("backgrounds\\" + m_name, -1);
		if (m_width != -1 || m_height != -1) {
			if (m_width != m_bg.cols || m_height != m_bg.rows)
				cv::resize(m_bg, m_bg, cv::Size(m_width, m_height));
		}
	}
	else {
		m_total_frames_processed.store(0);
		m_total_frame_count.store(0);
		m_isrunning.store(true);
		m_liveview_thread = std::thread(&CBackGorundRemoval::FnLiveViewThread, this);
	}
}

CBackGorundRemoval::CBackGorundRemoval(const char* name, std::shared_ptr<CLogManager> log, int rl, int gl, int bl, int rh, int gh, int bh, bool is_static, int width, int height, bool repeat) :CBaseEffect(name, log), m_is_static(is_static)
, m_width(width), m_height(height), m_is_repeat(repeat)
{
	m_stack_effect_index = STACK_INDEX_BG_REMOVAL;
	m_low = cv::Scalar(bl, gl, rl);
	m_hi = cv::Scalar(bh, gh, rh);	

	m_state.store(EFFECT_THREAD_STATE_IDLE);
	if (m_is_static) {
		m_bg = cv::imread("backgrounds\\" + m_name, -1);
		if (m_width != -1 || m_height != -1) {
			if(m_width!=m_bg.cols || m_height!=m_bg.rows)
				cv::resize(m_bg, m_bg, cv::Size(m_width, m_height));
		}
	}
	else {				
		m_total_frames_processed.store(0);
		m_total_frame_count.store(0);
		m_isrunning.store(true);
		m_liveview_thread = std::thread(&CBackGorundRemoval::FnLiveViewThread, this);
	}
}

CBackGorundRemoval::CBackGorundRemoval(const char* name, std::shared_ptr<CLogManager> log, bool is_static, bool is_greenscreen, int width, int height, bool repeat, cv::Mat& frame) : CBaseEffect(name, log), m_is_static(is_static)
, m_width(width), m_height(height), m_is_repeat(repeat), m_is_greenscreen(is_greenscreen)
{
	m_stack_effect_index = STACK_INDEX_BG_REMOVAL;
	m_pBackSub = cv::createBackgroundSubtractorMOG2();
	m_bg_sub = frame.clone();
	cv::Mat dummy;
	m_pBackSub->apply(m_bg_sub, dummy, 1);
	m_pBackSub->apply(m_bg_sub, dummy);
	m_pBackSub->apply(m_bg_sub, dummy);
	m_pBackSub->apply(m_bg_sub, dummy);
	m_pBackSub->apply(m_bg_sub, dummy);

	if (m_is_static) {
		m_bg = cv::imread("backgrounds\\" + m_name, -1);
		if (m_width != -1 || m_height != -1) {
			if (m_width != m_bg.cols || m_height != m_bg.rows)
				cv::resize(m_bg, m_bg, cv::Size(m_width, m_height));
		}
	}
	else {
		m_total_frames_processed.store(0);
		m_total_frame_count.store(0);
		m_isrunning.store(true);
		m_liveview_thread = std::thread(&CBackGorundRemoval::FnLiveViewThread, this);
	}
}

int CBackGorundRemoval::GrabMostRecentFrame(cv::Mat& frame) {
	int ret = -1;
	m_framelock.lock();
	if (m_frame_buffer.size() > 0) {
		frame = m_frame_buffer[m_frame_buffer.size() - 1];
		ret = 0;
	}
	m_framelock.unlock();
	return ret;
}

int CBackGorundRemoval::filter(cv::Mat& frame) {	
	cv::Mat layer;
	if (m_is_static) {
		layer = m_bg;		
	}
	else {
		if (GrabMostRecentFrame(layer) != 0)
			return -1;		
	}

	if (layer.cols != frame.cols || layer.rows != frame.rows)
		cv::resize(layer, layer, cv::Size(frame.cols, frame.rows));
	
	cv::Mat tmp;

	if(m_total_frames_processed.load()>m_total_frame_count.load() - 5)
		tmp = layer.clone();
	else
		tmp = layer;

	if (m_is_greenscreen) {
		cv::Mat mask;
		cv::inRange(frame, m_low, m_hi, mask);
		cv::bitwise_not(mask, mask);

		frame.copyTo(tmp, mask);
		frame = tmp;
	}
	else {
		cv::Mat fgMask;
		m_pBackSub->apply(frame, fgMask, 0);
		
		frame.copyTo(tmp, fgMask);
		frame = tmp;
	}	
	return 0;
}

void CBackGorundRemoval::FnLiveViewThread() {	

	std::unique_ptr<CFPSCounter> pfps = std::make_unique<CFPSCounter>();

	m_error_code.store(EFFECT_ERR_NONE);
	m_state.store(EFFECT_THREAD_STATE_STARTING);
	bool bIsRunning = m_isrunning.load();
	
	m_total_frames_processed.store(0);
	m_total_frame_count.store(0);	

	while (bIsRunning) {
		cv::VideoCapture cap;
		if (cap.open("movies\\" + m_name)) {

			m_state.store(EFFECT_THREAD_STATE_RUNNING);
			double fps = cap.get(cv::CAP_PROP_FPS);
			bool isFirstFrame = true;			
			std::chrono::time_point<std::chrono::high_resolution_clock> ts;
			double f_delay_per = 1000.0 / fps;

			m_total_frame_count.store((int)cap.get(cv::CAP_PROP_FRAME_COUNT));

			while (bIsRunning) {
				cv::Mat frame;
				cap >> frame;
				if (!frame.empty()) {

					m_framelock.lock();

					std::string fps = pfps->GetFPSinString();
					putText(frame, fps.c_str(), cv::Point(250, 50), cv::FONT_HERSHEY_SIMPLEX, 1.4, cv::Scalar(255, 0, 0), 4);

					if (m_frame_buffer.size() > 50) {
						m_frame_buffer.erase(m_frame_buffer.begin());
					}

					if (m_width != -1 || m_height != -1) {
						if (frame.cols != m_width || frame.rows != m_height) {
							cv::resize(frame, frame, cv::Size(m_width, m_height));
						}
					}

					m_frame_buffer.push_back(frame);
					m_total_frames_processed.fetch_add(1);

					m_framelock.unlock();

					if (isFirstFrame) {
						isFirstFrame = false;
					}
					else {
						auto decoding_finished_ts = std::chrono::high_resolution_clock::now();
						auto decoding_duration = (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(decoding_finished_ts - ts).count();
						double time_left_before_next_frame = f_delay_per - (double)decoding_duration;

						time_left_before_next_frame--; //adjustment for waitkey(1)

						if (time_left_before_next_frame > 0) {
							std::this_thread::sleep_for(std::chrono::milliseconds((int)time_left_before_next_frame));
						}
					}
					ts = std::chrono::high_resolution_clock::now();
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				else {
					//m_error_code.store(EFFECT_ERR_FILE_EMPTY_FRAME);
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
					m_error_code.store(EFFECT_ENDED);
					break;					
				}
				bIsRunning = m_isrunning.load();
			}

			cap.release();
			cv::destroyAllWindows();
		}
		bIsRunning = m_isrunning.load();
		if (!m_is_repeat)
			break;
	}	
	m_pLog->AsyncWrite(m_pLog->string_format("Effect BG Removal [%s] exiting ...", m_name.c_str()).c_str(), true, true);
	m_state.store(EFFECT_THREAD_STATE_EXITING);
}

CClassicFaceDetect::CClassicFaceDetect(const char* name, std::shared_ptr<CLogManager> log, double confidence, int minimum_size) : CBaseEffect(name, log), m_confidence_threshold(confidence), m_minimum_size(minimum_size)
{
	m_stack_effect_index = STACK_INDEX_ACC;
#ifdef _WIN32
	m_cascade_face.load("models\\haarcascade_frontalface_alt.xml");
	m_overlay = cv::imread("overlays\\" + m_name, -1);
#else
	m_cascade_face.load("models/haarcascade_frontalface_alt.xml");
	m_overlay = cv::imread("overlays/" + m_name, -1);
#endif
	m_state.store(EFFECT_THREAD_STATE_IDLE);
}

CClassicFaceDetect::CClassicFaceDetect(const char* name, std::shared_ptr<CLogManager> log, double confidence, int minimum_size, int stack_index):CBaseEffect(name, log, stack_index), m_confidence_threshold(confidence), m_minimum_size(minimum_size)
{
#ifdef _WIN32
	m_cascade_face.load("models\\haarcascade_frontalface_alt.xml");
	m_overlay = cv::imread("overlays\\" + m_name, -1);
#else
	m_cascade_face.load("models/haarcascade_frontalface_alt.xml");
	m_overlay = cv::imread("overlays/" + m_name, -1);
#endif

	m_state.store(EFFECT_THREAD_STATE_IDLE);
}

#define FACE_FRAME_COUNT 5

int CClassicFaceDetect::FnDetectionThread(cv::Mat frame) {
	
	m_state.store(EFFECT_THREAD_STATE_STARTING);
	m_state.store(EFFECT_THREAD_STATE_RUNNING);	

	cv::Mat gray, smallImg;
	cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
	double fx = 0.5;
	cv::resize(gray, smallImg, cv::Size(), fx, fx, cv::INTER_LINEAR);
	cv::equalizeHist(smallImg, smallImg);

	std::vector<int> reject;
	std::vector<double> confidence;
	
	std::vector<LANDMARK> smallfaces;
	std::vector<cv::Rect> detectedFaces;

	try {		
		m_cascade_face.detectMultiScale(smallImg, detectedFaces, reject, confidence, 1.1, 2, 0 | cv::CASCADE_SCALE_IMAGE, cv::Size(m_minimum_size, m_minimum_size), cv::Size(), true);		
	}
	catch (...) {		
			m_state.store(EFFECT_THREAD_STATE_EXITING);
		return -1;
	}

	std::vector<int> old_scores;
	for (int i = 0; i < (int)m_landmarks.size(); i++) {
		old_scores.push_back(m_landmarks[i].score);
	}
	for (size_t i = 0; i < detectedFaces.size(); i++) {
		if (confidence[i] >= m_confidence_threshold) {
			bool IsIntersect = false;
			for (int j = 0; j < (int)m_landmarks.size(); j++) {
				IsIntersect = ((m_landmarks[j].face & detectedFaces[i]).area() > 0);
				if (IsIntersect) {
					m_landmarks[j].face = detectedFaces[i];
					if (m_landmarks[j].score < FACE_FRAME_COUNT)
						m_landmarks[j].score++;
					else
						m_landmarks[j].bIsPersonExist = true;
					break;
				}
			}
			if (!IsIntersect) {
				LANDMARK lm;
				lm.face = detectedFaces[i];
				lm.score++;
				m_landmarks.push_back(lm);
			}			
		}
	}
	int iCounter = 0;
	while (iCounter < (int)old_scores.size()) {
		if (old_scores[iCounter] >= m_landmarks[iCounter].score) {
			m_landmarks[iCounter].score--;
			if (m_landmarks[iCounter].score < 0) {
				m_landmarks.erase(m_landmarks.begin() + iCounter);
				old_scores.erase(old_scores.begin() + iCounter);
			}
			else
				iCounter++;
		}
		else {			
			iCounter++;
		}
	}

	for (int i = 0; i < (int)m_landmarks.size(); i++) {
		if (m_landmarks[i].bIsPersonExist)
			smallfaces.push_back(m_landmarks[i]);
	}

	std::vector<LANDMARK> UnsortedLandmarks;
	for (int i = 0; i < (int)smallfaces.size(); i++) {
		cv::Rect a;
		a.x = cvRound(smallfaces[i].face.x * 2);
		a.y = cvRound(smallfaces[i].face.y * 2);

		a.width = cvRound(smallfaces[i].face.width * 2);
		a.height = cvRound(smallfaces[i].face.height * 2);

		LANDMARK landmark;
		landmark.bIsFacesFound = true;
		landmark.face = a;
		landmark.score = smallfaces[i].score;
		UnsortedLandmarks.push_back(landmark);
	}

	std::vector<LANDMARK> landmarks_final;
	if (UnsortedLandmarks.size() > 0) {
		
		LANDMARK smallestLandmark = UnsortedLandmarks[0];
		while (UnsortedLandmarks.size() > 0) {
			smallestLandmark = UnsortedLandmarks[0];
			int iRemove;
			int i;
			for (i = 0; i < (int)UnsortedLandmarks.size(); i++) {
				if (UnsortedLandmarks[i].face.x <= smallestLandmark.face.x) {
					smallestLandmark = UnsortedLandmarks[i];
					iRemove = i;
				}
			}
			UnsortedLandmarks.erase(UnsortedLandmarks.begin() + iRemove);
			landmarks_final.push_back(smallestLandmark);
		}		
	}

	m_framelock.lock();
	m_output_landmarks.clear();
	m_output_landmarks = landmarks_final;
	m_framelock.unlock();
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
	
	m_state.store(EFFECT_THREAD_STATE_EXITING);
	return 0;
}

int CClassicFaceDetect::DetectFaceNonThreaded(cv::Mat& frame, std::vector<LANDMARK>& outputs) {
	
	cv::CascadeClassifier cascade_face;

#ifdef _WIN32
	cascade_face.load("models\\haarcascade_frontalface_alt.xml");	
#else
	cascade_face.load("models/haarcascade_frontalface_alt.xml");	
#endif

	cv::Mat gray, smallImg;
	cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
	double fx = 0.5;
	cv::resize(gray, smallImg, cv::Size(), fx, fx, cv::INTER_LINEAR);
	cv::equalizeHist(smallImg, smallImg);

	std::vector<int> reject;
	std::vector<double> confidence;
	
	std::vector<cv::Rect> detectedFaces;

	try {
		cascade_face.detectMultiScale(smallImg, detectedFaces, reject, confidence, 1.1, 2, 0 | cv::CASCADE_SCALE_IMAGE, cv::Size(m_minimum_size, m_minimum_size), cv::Size(), true);
	}
	catch (...) {		
		return -1;
	}
	for (int i = 0; i < (int)detectedFaces.size(); i++) {
		LANDMARK x;
	//	x.face = detectedFaces[i];

		x.face.x = detectedFaces[i].x * 2;
		x.face.y = detectedFaces[i].y * 2;
		x.face.width = detectedFaces[i].width * 2;
		x.face.height = detectedFaces[i].height * 2;

		outputs.push_back(x);
	}
	return 0;
}

int CClassicFaceDetect::filter(cv::Mat& frame) {	

	if (m_last_frame.rows > 0 && (m_last_frame.rows != frame.rows || m_last_frame.cols != frame.cols)) {
		std::vector<LANDMARK> lms;		
		DetectFaceNonThreaded(frame, lms);		

		for (auto& landmark : lms) {

			int y = landmark.face.y;
			if (y - (landmark.face.height / 8) > 0)
				y = y - (landmark.face.height / 8);

			cv::Mat overlay_show = m_overlay.clone();
			cv::resize(overlay_show, overlay_show, landmark.face.size());
			overlayImage(frame, overlay_show, frame, cv::Point(landmark.face.x, y));
		}		
		return 0;
	}
	else {
		int state = m_state.load();
		if (state == EFFECT_THREAD_STATE_EXITING || state == EFFECT_THREAD_STATE_IDLE) {
			if (state == EFFECT_THREAD_STATE_EXITING)
				m_detection_future.get();
			m_last_frame = frame.clone();
			m_detection_future = std::async(std::launch::async, &CClassicFaceDetect::FnDetectionThread, this, m_last_frame);
		}
	}
	
	m_framelock.lock();
	std::vector<LANDMARK> output_landmarks(m_output_landmarks);
	m_framelock.unlock();

	for (auto& landmark : output_landmarks) {

		int y = landmark.face.y;
		if (y - (landmark.face.height / 8) > 0)
			y = y - (landmark.face.height / 8);
		
		cv::Mat overlay_show = m_overlay.clone();
		cv::resize(overlay_show, overlay_show, landmark.face.size());
		overlayImage(frame, overlay_show, frame, cv::Point(landmark.face.x, y));
	}	
	
	return 0;
}

void CClassicFaceDetect::overlayImage(const cv::Mat &background, const cv::Mat &foreground, cv::Mat &output, cv::Point2i location)
{
	background.copyTo(output);

	// start at the row indicated by location, or at row 0 if location.y is negative.
	for (int y = (std::max)(location.y, 0); y < background.rows; ++y)
	{
		int fY = y - location.y; // because of the translation

								 // we are done of we have processed all rows of the foreground image.
		if (fY >= foreground.rows)
			break;

		// start at the column indicated by location, 

		// or at column 0 if location.x is negative.
		for (int x = (std::max)(location.x, 0); x < background.cols; ++x)
		{
			int fX = x - location.x; // because of the translation.

									 // we are done with this row if the column is outside of the foreground image.
			if (fX >= foreground.cols)
				break;

			// determine the opacity of the foregrond pixel, using its fourth (alpha) channel.
			double opacity =
				((double)foreground.data[fY * foreground.step + fX * foreground.channels() + 3])

				/ 255.;


			// and now combine the background and foreground pixel, using the opacity, 

			// but only if opacity > 0.
			for (int c = 0; opacity > 0 && c < output.channels(); ++c)
			{
				unsigned char foregroundPx =
					foreground.data[fY * foreground.step + fX * foreground.channels() + c];
				unsigned char backgroundPx =
					background.data[y * background.step + x * background.channels() + c];
				output.data[y*output.step + output.channels()*x + c] =
					backgroundPx * (1. - opacity) + foregroundPx * opacity;
			}
		}
	}
}