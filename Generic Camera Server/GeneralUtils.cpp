#include "pch.h"
#include "GeneralUtils.h"

/*
#include <codecvt>
#include <dirent.h>

std::wstring ConvertFromBytes(const char* text) {
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
	return converter.from_bytes(text);
}

std::string ConvertToBytes(const wchar_t* text) {
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
	return converter.to_bytes(text);
}

int GetFilesList(std::vector<std::string>& filenames, const char* path, bool is_parent_folder) {
	
	DIR *dir;
	struct dirent *ent;
	if ((dir = opendir(path)) != NULL) {
		// print all the files and directories within directory 
		while ((ent = readdir(dir)) != NULL) {
			std::string fname = ent->d_name;
			if (fname != "." && fname != "..") {
				if (is_parent_folder) {
					fname = path;
					fname = fname + "\\";
					fname = fname + ent->d_name;
				}
				filenames.push_back(fname.c_str());
			}
		}
		closedir(dir);
		return 0;
	}
	return -1;
}

*/

int concatStr(const char* _source, char* _dest, int max_len) {
	if (strlen(_source) + strlen(_dest) < max_len) {
		strcat_s(_dest, max_len, _source);		
	}
	else
		return -1;
	return 0;
}

int copyStr(const char* _source, char* _dest, int max_len) {
	if (strlen(_source) + strlen(_dest) < max_len) {
		strcpy_s(_dest, max_len, _source);
	}
	else
		return -1;
	return 0;
}

int printStr(const char* _source, char* _dest, int max_len) {
	if (strlen(_source) + strlen(_dest) < max_len) {
		strcpy_s(_dest, max_len, _source);
	}
	else
		return -1;
	return 0;
}

CFPSCounter::CFPSCounter() {
	m_fps_ts = std::chrono::high_resolution_clock::now();
}

std::string CFPSCounter::GetFPSinString() {
	auto now = std::chrono::high_resolution_clock::now();
	m_iDuration = (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(now - m_fps_ts).count();
	if (m_iDuration > 1000) {
		m_fps_ts = now;
		m_iDisplayFPS = (int)m_iFps;
		m_iFps = 0;
	}
	else {
		m_iFps++;
	}

	//char buff[16] = { 0 };
	//_itoa_s(m_iDisplayFPS, buff, 16, 10);
	return std::to_string(m_iDisplayFPS);
}

std::string& implode(const std::vector<std::string>& elems, char delim, std::string& s)
{
	for (std::vector<std::string>::const_iterator ii = elems.begin(); ii != elems.end(); ++ii)
	{
		s += (*ii);
		if (ii + 1 != elems.end()) {
			s += delim;
		}
	}

	return s;
}

std::string implode(const std::vector<std::string>& elems, char delim)
{
	std::string s;
	return implode(elems, delim, s);
}