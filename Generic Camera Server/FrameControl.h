#pragma once
#include "SpecialEffects.h"
#include "MediaSource.h"
#include "ClientConnections.h"
#include "MediaServer.h"
#include "LogManager.h"

#include <vector>
//#include <chrono>

#define FC_MAX_FPS 25
#define FC_DELAY_BETWEEN_FRAMES 40
#define FC_FRAME_BUFFER_COUNT 500
#define FC_AVG_FPS_REQ 100

class CFrameControl {
public:
	struct MediaSourceInfo {
		std::shared_ptr<CBaseMediaSource> pMedia = nullptr;		
		int m_source_ref_count=0;		
	};

	struct MediaSourceTask {
		std::shared_ptr<CBaseMediaSource> pMedia = nullptr;
		std::string pending_task= "";
		std::string command_id="";
		std::string data = "";
		bool isLiveStreaming = false;
	};

	struct FrameData {
		cv::Mat frame;
		int FPS;		
		std::chrono::time_point<std::chrono::high_resolution_clock> ts;
	};

private:	
	std::shared_ptr<CBaseMediaServer> m_pMediaServer = nullptr;	
	std::vector<MediaSourceTask> m_sources;	
	
	std::vector<std::shared_ptr<CBaseEffect>> m_effects;

	std::vector<cv::Mat> m_modified_frames;
	std::vector<int> m_fpses;
	std::vector<FrameData> m_frame_datas;
	
	std::shared_ptr<CLogManager> m_pLog = nullptr;
	std::shared_ptr<CBaseServer> m_pClientCon;
	std::mutex m_lock;
	void* m_lp_connection;
	
	static std::vector<MediaSourceInfo>  m_created_media_sources;
	static std::mutex m_created_media_sources_lock;		
	static std::atomic<int> m_media_server_port;
	static std::atomic<int> m_broadcasted_error_count;

	std::shared_ptr<CBaseMediaSource> CheckExistingMediaSource(CBaseMediaSource::SourceType& mediatype, std::string& id);	
	
	int m_fps = 0;
	int m_last_error_count = 0;
	bool m_bIsRecording = false;
	bool m_bIsAsyncTaskInProgress = false;
	cv::Mat m_bg_sub;

	int FindLocalMediaSource(CBaseMediaSource::SourceType& mediatype, std::string& id);
	int GetGlobalMediaRefCount(const CBaseMediaSource::SourceType& mediatype, const std::string& id);
	int FindLiveStreamIndex();
	int GenerateMovie(std::string& data, std::string& res);
	int CreateBGModel();
	int ProcessAsyncTask(MediaSourceTask& x, std::string& res);

	void AddImageModification(const int stack_effect_index, const std::string& data);
	void AddImageModificationWithCustomStackIndex(const int stack_effect_index, const int filter_type, const std::string& data);

	void ProcessCommand(std::string& data, std::string& res);
	void GenerateJSONReply(const char* command_id, const char* command_name, const char* result, const char* payload, std::string& reply);
	void GenerateJSONEvent(const char* event_id, const char* event, int event_code, const char* source_type, const char* source_id, const char* effect_id, const char* payload, std::string& reply);
	void SyncUpGlobalMediaSourceList();
	void GlobalMediaSourceListAddRef(CBaseMediaSource::SourceType& mediatype, std::string& id);	
	void FnControlThread();
	void SetupMediaServer(int source_index, int& port, const char* server_type, double scale);
	void ApplyImageModification(cv::Mat& frame);
	void GenerateUniqueName(std::string& name, const char* mediatypename, const char* id, const char* afix);	

public:		
	CFrameControl(std::shared_ptr<CBaseServer> pServer, void* lp_connection, std::shared_ptr<CLogManager> log);
	~CFrameControl() {
		m_running.store(false);
		if (m_thread.joinable())
			m_thread.join();				
		m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] is destroyed", m_lp_connection).c_str(), true, true);
	}
	std::atomic<bool> m_running;
	std::thread m_thread;

	void Run();	
	void SetLog(std::shared_ptr<CLogManager> log);
	void WriteLog(std::string data);
	void* GetID();

	void GetRBGLimit(int& rl, int& gl, int& bl, int& rh, int& gh, int& bh);
};