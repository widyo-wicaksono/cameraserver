#include "pch.h"
#include "LogManager.h"
#include "GeneralUtils.h"

#include <iostream>
#include <fstream>

#ifdef _WIN32
	#include <shlobj_core.h>
#endif

#define BUFFER_LEN_DATETIME 64
#define BUFFER_LOG_ENTRY 512

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

CLogManager::CLogManager(const char* application_name, int log_type) : m_uLogType(log_type), m_sAppName(application_name){
	
}

int CLogManager::Init() {
#ifdef _WIN32
	if (m_uLogType == LOG_TYPE_STANDARD) {
		SHGetSpecialFolderPathA(NULL, m_sPath, 35, 0);
		if (concatStr("\\", m_sPath, MAX_PATH) == 0) {
			if (concatStr(m_sAppName.c_str(), m_sPath, MAX_PATH) == 0) {
				int err = CreateDirectoryA(m_sPath, NULL);
				if (err == 0) {
					if (GetLastError() != ERROR_ALREADY_EXISTS)
						return -1;
				}
				if (concatStr("\\", m_sPath, MAX_PATH) != 0)
					return -1;
				else if (concatStr(m_sAppName.c_str(), m_sPath, MAX_PATH) != 0)
					return -1;
			}
		}				
	}
	else {		
		if (copyStr(m_sAppName.c_str(), m_sPath, MAX_PATH)!=0) {
			return -1;
		}		
	}
#else	
	if (copyStr(m_sAppName.c_str(), m_sPath, MAX_PATH) != 0) {
		return -1;
	}
#endif

	char time[BUFFER_LEN_DATETIME] = { 0 };	
	std::string time_;
	std::string day_;
	GetTimeMSResolution(day_, time_);	

	copyStr(day_.c_str(), time, BUFFER_LEN_DATETIME);
	
	if (concatStr("_", m_sPath, MAX_PATH) == 0) {		
		if (concatStr(time, m_sPath, MAX_PATH) != 0)
			return -1;
	}

	if (m_uLogType == LOG_TYPE_STANDARD || m_uLogType == LOG_TYPE_LOCAL) {		
		if (concatStr(".log", m_sPath, MAX_PATH) != 0)
			return -1;
	}
	else {		
		if (concatStr(".csv", m_sPath, MAX_PATH) != 0)
			return -1;
	}
	FILE * pfile = nullptr;		
	auto err = fopen_s(&pfile, m_sPath, "r");
	if (err != 0) {		
		err = fopen_s(&pfile, m_sPath, "w");
		if (err != 0)
			return -1;
	}	
	if (pfile && err ==0) {
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
		sprintf_s(buff, BUFFER_LEN_DATETIME, "[%02d:%02d:%02d:%03d]", hours, minutes, seconds, fractional_seconds);
	else		
		sprintf_s(buff, BUFFER_LEN_DATETIME, "[%02d-%02d-%02d-%03d]", hours, minutes, seconds, fractional_seconds);
	time_ = buff;

	memset(buff, '\0', BUFFER_LEN_DATETIME);

	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	time_t tt = std::chrono::system_clock::to_time_t(now);
	//tm local_tm = *localtime(&tt);
	tm local_tm;
	localtime_s(&local_tm, &tt);
	sprintf_s(buff, BUFFER_LEN_DATETIME, "%04d%02d%02d", local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday);
	day_ = buff;
}

int CLogManager::Write(const char* data, bool bIsGenerateTimeStamps, bool bIsWritingToStdOutput)
{
	if (m_bIsInited) {
		try {
			FILE * file = nullptr;			
			auto err = fopen_s(&file, m_sPath, "a+");			
			if(err == 0 && file!=nullptr){
				char buff[BUFFER_LOG_ENTRY] = { 0 };
				if (bIsGenerateTimeStamps) {
					std::string buff_datetime;
					std::string tmp;
					GetTimeMSResolution(tmp, buff_datetime);					
					if (copyStr(buff_datetime.c_str(), buff, BUFFER_LOG_ENTRY) != 0)
						return -1;
					if (m_uLogType == LOG_TYPE_CSV) {						
						if (concatStr(";", buff, BUFFER_LOG_ENTRY) != 0)
							return -1;
					}					
					if (concatStr(data, buff, BUFFER_LOG_ENTRY) != 0)
						return -1;					
					if (concatStr("\n", buff, BUFFER_LOG_ENTRY) != 0)
						return -1;
				}
				fputs(buff, file);
				fflush(file);
				fclose(file);
			}
			if (bIsWritingToStdOutput)
				std::cout << data << std::endl;
			return 0;
		}
		catch (...) {

		}
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
//std::string CLogManager::string_format(const char* str, ...) {
	//std::string fmt_str = std::move(str);
	int final_n, n = ((int)fmt_str.size()) * 2; /* Reserve two times as much as the length of the fmt_str */
	std::unique_ptr<char[]> formatted;
	va_list ap;
	while (1) {
		formatted.reset(new char[n]); /* Wrap the plain char array into the unique_ptr */
		//strcpy(&formatted[0], fmt_str.c_str());
		strcpy_s(&formatted[0], n,fmt_str.c_str());
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