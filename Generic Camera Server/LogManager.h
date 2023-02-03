#pragma once
#include <string>
#include <mutex>

#include <opencv2/opencv.hpp>

#define LOG_TYPE_STANDARD 1
#define LOG_TYPE_LOCAL 2
#define LOG_TYPE_CSV 3

#define MAX_PATH 260
/*
class CLogManager {
public:
	CLogManager(const char* application_name, int log_type);
	void GetTimeMSResolution(std::string& day_, std::string& time_, bool is_filename=false);

	void AsyncWrite(const char* data, bool bIsGenerateTimeStamps =true, bool bIsWritingToStdOutput= false);
	void WriteToOCVScreen(const char* text, int x, int y, int b, int g, int r, int alpha, cv::Mat& frame);
	void WriteToOCVScreen(const char* text, int x, int y, int b, int g, int r, cv::Mat& frame);
	void SetFPSOutput(bool is_fps);

	int Init();
	int Write(const char* data, bool bIsGenerateTimeStamps = true, bool bIsWritingToStdOutput = false);
	int WriteNote(const char* path, const char* data);
	
	std::string string_format(const std::string fmt_str, ...);
	//std::string string_format(const char* str, ...);

private:
	std::string m_sAppName;
	
	unsigned int m_uLogType;
	char m_sPath[MAX_PATH] = { 0 };
	bool m_bIsInited = false;
	bool m_bIsOutputFPS = true;

	std::mutex m_mutex;
};
*/

class CLogManager {
public:
	CLogManager(const CLogManager&) = delete;
	CLogManager& operator = (const CLogManager&) = delete;
	
	void GetTimeMSResolution(std::string& day_, std::string& time_, bool is_filename = false);

	void AsyncWrite(const char* data, bool bIsGenerateTimeStamps = true, bool bIsWritingToStdOutput = false);
	void WriteToOCVScreen(const char* text, int x, int y, int b, int g, int r, int alpha, cv::Mat& frame);
	void WriteToOCVScreen(const char* text, int x, int y, int b, int g, int r, cv::Mat& frame);
	void SetFPSOutput(bool is_fps);

	
	int Write(const char* data, bool bIsGenerateTimeStamps = true, bool bIsWritingToStdOutput = false);
	int WriteNote(const char* path, const char* data);

	std::string string_format(const std::string fmt_str, ...);
	
	static CLogManager* getInstance(const char* application_name="", int log_type= LOG_TYPE_STANDARD) {
		static CLogManager instance(application_name, log_type);        // (1)
		static bool bIsInstantiated = false;

		if (!bIsInstantiated) {
			instance.Init();
		}

		bIsInstantiated = true;
		return &instance;
	}
private:
	CLogManager(const char* application_name = "", int log_type = LOG_TYPE_STANDARD);
	~CLogManager() = default;
	int Init();

	std::string m_sAppName;

	unsigned int m_uLogType;
	char m_sPath[MAX_PATH] = { 0 };
	bool m_bIsInited = false;
	bool m_bIsOutputFPS = true;

	std::mutex m_mutex;
};