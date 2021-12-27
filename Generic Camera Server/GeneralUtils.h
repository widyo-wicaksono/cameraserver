#pragma once
#include <string>
#include <vector>
#include <chrono>

/*
std::wstring ConvertFromBytes(const char* text);
std::string ConvertToBytes(const wchar_t* text);
*/

class CFPSCounter {
public:
	CFPSCounter();
	~CFPSCounter() {};

	std::string GetFPSinString();
private:	
	std::chrono::time_point < std::chrono::high_resolution_clock> m_fps_ts;
	int m_iFps = 0;
	int m_iDisplayFPS = 0;
	unsigned int m_iDuration = 0;

};

std::string implode(const std::vector<std::string>& elems, char delim);
int concatStr(const char* _source, char* _dest, int max_len);
int copyStr(const char* _source, char* _dest, int max_len);