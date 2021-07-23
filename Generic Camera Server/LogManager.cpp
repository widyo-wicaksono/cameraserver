#include "pch.h"
#include "LogManager.h"

#include <iostream>
#include <fstream>

#ifdef _WIN32
	#include <shlobj_core.h>
#endif

#define BUFFER_LEN_DATETIME 64
#define BUFFER_LOG_ENTRY 512

CLogManager::CLogManager(const char* application_name, int log_type) {
	m_uLogType = log_type;
	m_sAppName = application_name;
#ifdef _WIN32
	if (m_uLogType == LOG_TYPE_STANDARD) {
		SHGetSpecialFolderPathA(NULL, m_sPath, 35, 0);
		strcat(m_sPath, "\\");
		strcat(m_sPath, application_name);
	}
	else {
		strcpy(m_sPath, application_name);
	}
#else
	strcpy(m_sPath, application_name);
#endif
}

int CLogManager::Init() {
#ifdef _WIN32
	if (m_uLogType == LOG_TYPE_STANDARD) {
		int err = CreateDirectoryA(m_sPath, NULL);
		if (err == 0) {
			if (GetLastError() != ERROR_ALREADY_EXISTS)
				return -1;
		}
		strcat(m_sPath, "\\");
		strcat(m_sPath, m_sAppName.c_str());
	}
#endif

	char time[BUFFER_LEN_DATETIME] = { 0 };	
	std::string time_;
	std::string day_;
	GetTimeMSResolution(day_, time_);	
	strcpy(time, day_.c_str());	

	strcat(m_sPath, "_");
	strcat(m_sPath, time);

	if (m_uLogType == LOG_TYPE_STANDARD || m_uLogType == LOG_TYPE_LOCAL)		
		strcat(m_sPath, ".log");
	else		
		strcat(m_sPath, ".csv");

	FILE * pfile = nullptr;	
	pfile = fopen(m_sPath, "r");
	
	if (pfile == nullptr) {
		pfile = fopen(m_sPath,"w");
	}
	
	if (pfile) {
		fclose(pfile);
		m_bIsInited = true;
		return 0;
	}
	return -1;	
}

void CLogManager::GetTimeMSResolution(std::string& day_, std::string& time_, bool is_filename)
{

	std::chrono::high_resolution_clock::time_point p = std::chrono::high_resolution_clock::now();

	std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(p.time_since_epoch());
	int fractional_seconds = ms.count() % 1000;

	std::chrono::seconds second = std::chrono::duration_cast<std::chrono::seconds>(p.time_since_epoch());
	int seconds = second.count() % 60;

	std::chrono::minutes minute = std::chrono::duration_cast<std::chrono::minutes>(p.time_since_epoch());
	int minutes = minute.count() % 60;

	std::chrono::hours hour = std::chrono::duration_cast<std::chrono::hours>(p.time_since_epoch());
	int hours = hour.count() % 24;

	char buff[BUFFER_LEN_DATETIME] = { 0 };
	if(!is_filename)
		sprintf(buff, "[%02d:%02d:%02d:%03d]", hours, minutes, seconds, fractional_seconds);
	else
		sprintf(buff, "[%02d-%02d-%02d-%03d]", hours, minutes, seconds, fractional_seconds);
	time_ = buff;

	memset(buff, '\0', BUFFER_LEN_DATETIME);

	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	time_t tt = std::chrono::system_clock::to_time_t(now);
	tm local_tm = *localtime(&tt);
	sprintf(buff, "%04d%02d%02d", local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday);
	day_ = buff;
}

int CLogManager::Write(const char* data, bool bIsGenerateTimeStamps, bool bIsWritingToStdOutput)
{
	if (m_bIsInited) {
		FILE * file = nullptr;
		file = fopen(m_sPath, "a+");
		if (file) {
			char buff[BUFFER_LOG_ENTRY] = { 0 };
			if (bIsGenerateTimeStamps) {				
				std::string buff_datetime;
				std::string tmp;				
				GetTimeMSResolution(tmp, buff_datetime);				
				strcpy(buff, buff_datetime.c_str());
				if (m_uLogType == LOG_TYPE_CSV) {					
					strcat(buff, ";");
				}
				strcat(buff, data);
				strcat(buff, "\n");
			}
			fputs(buff, file);
			fflush(file);
			fclose(file);			
		}
		if (bIsWritingToStdOutput)
			std::cout << data << std::endl;
		return 0;
	}
	return -1;
}

int CLogManager::WriteNote(const char* path, const char* data) {
	std::ofstream myfile;
	myfile.open(path, std::ios::trunc);
	myfile << data;
	myfile.close();
	return 0;
}

void CLogManager::AsyncWrite(const char* data, bool bIsGenerateTimeStamps, bool bIsWritingToStdOutput) {
	m_mutex.lock();
	Write(data, bIsGenerateTimeStamps);
	if (bIsWritingToStdOutput) {
		std::cout << data << std::endl;
	}
	m_mutex.unlock();
}

std::string CLogManager::string_format(const std::string fmt_str, ...) {
	int final_n, n = ((int)fmt_str.size()) * 2; /* Reserve two times as much as the length of the fmt_str */
	std::unique_ptr<char[]> formatted;
	va_list ap;
	while (1) {
		formatted.reset(new char[n]); /* Wrap the plain char array into the unique_ptr */
		strcpy(&formatted[0], fmt_str.c_str());
		va_start(ap, fmt_str);
		final_n = vsnprintf(&formatted[0], n, fmt_str.c_str(), ap);
		va_end(ap);
		if (final_n < 0 || final_n >= n)
			n += abs(final_n - n + 1);
		else
			break;
	}
	return std::string(formatted.get());
}

void CLogManager::WriteToOCVScreen(const char* text, int x, int y, int b, int g, int r, cv::Mat& frame) {
	if (m_bIsOutputFPS)
		putText(frame, text, cv::Point(x, y), cv::FONT_HERSHEY_SIMPLEX, 1.4, cv::Scalar(b, g, r), 4);	
}

void CLogManager::WriteToOCVScreen(const char* text, int x, int y, int b, int g, int r, int alpha, cv::Mat& frame) {
	if (m_bIsOutputFPS)
		putText(frame, text, cv::Point(x, y), cv::FONT_HERSHEY_SIMPLEX, 1.4, cv::Scalar(b, g, r, alpha), 4);	
}

void CLogManager::SetFPSOutput(bool is_fps) {
	m_mutex.lock();
	m_bIsOutputFPS = is_fps;
	m_mutex.unlock();
}