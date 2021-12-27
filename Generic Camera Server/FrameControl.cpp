
#include "pch.h"
#include "FrameControl.h"
#include "GeneralUtils.h"
#include "Hacks.h"

#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/filewritestream.h>
#include <rapidjson/writer.h>

std::vector<CFrameControl::MediaSourceInfo>  CFrameControl::m_created_media_sources;
std::mutex CFrameControl::m_created_media_sources_lock;
std::atomic<int> CFrameControl::m_media_server_port(7000);
std::atomic<int> CFrameControl::m_broadcasted_error_count(0);

CFrameControl::CFrameControl(std::shared_ptr<CBaseServer> pServer, void* lp_connection, std::shared_ptr<CLogManager> log) : m_pClientCon(pServer), m_lp_connection(lp_connection), m_pLog(log)
{
	m_running.store(true);		
	
	m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] is created", m_lp_connection).c_str(), true, true);
}

int CFrameControl::Run() {
	try {
		m_thread = std::thread(&CFrameControl::FnControlThread, this);
	}
	catch (...)
	{
		return -1;
	}
	return 0;
}

void CFrameControl::FnControlThread() {
	std::unique_ptr<CFPSCounter> pfps = std::make_unique<CFPSCounter>();
	bool bIsRunning = m_running.load();		
	m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] is running", m_lp_connection).c_str(), true, true);
	
	std::chrono::time_point<std::chrono::high_resolution_clock> frame_ts = std::chrono::high_resolution_clock::now();

	while (bIsRunning) {
		
		CBaseServer::_ConnectionMessage out_message(CBaseServer::_ConnectionMessage::DataDirection::outbound, m_lp_connection, "");
		std::string buffer;
				
		for (auto& source : m_sources) {
			//int error_code = source.pMedia->GetError();
			int error_code = 0;
			int error_count = 0;
			source.pMedia->GetError(error_code, error_count);
			if (error_code != ERR_NONE && m_last_error_count< error_count) {
				
				CBaseMediaSource::SourceType mediatype = source.pMedia->GetType();
				std::string smedia_type = "dummy";
				if (mediatype == CBaseMediaSource::SourceType::webcam)
					smedia_type = "webcam";
				else if(mediatype == CBaseMediaSource::SourceType::ipcam)
					smedia_type = "ipcam";
				else if (mediatype == CBaseMediaSource::SourceType::dslr)
					smedia_type = "dslr";
				GenerateJSONEvent("0", "ERROR", error_code, smedia_type.c_str(), source.pMedia->GetID().c_str(), "", "", buffer);
				out_message.data = buffer;
				m_pClientCon->AsyncPutMessage(out_message);				
				m_last_error_count= error_count;

				m_broadcasted_error_count.fetch_add(1);
				int ref_count = GetGlobalMediaRefCount(mediatype, source.pMedia->GetID());
				if (ref_count == m_broadcasted_error_count.load()) {
					source.pMedia->ResetError();
					m_broadcasted_error_count.store(0);
				}
			}			
		}

		for (auto& effect : m_effects) {
			int error_code = effect->GetError();
			if (error_code != EFFECT_ERR_NONE) {
				buffer = "";
				GenerateJSONEvent("0", "ERROR", error_code, "", "", "", effect->GetName().c_str() , buffer);
				out_message.data = buffer;
				effect->ResetError();
				m_pClientCon->AsyncPutMessage(out_message);
			}
		}

		
		bool IsAsybcTaskInProgress = false;

		for (auto& source : m_sources) {
			if (source.pending_task != "") {
				IsAsybcTaskInProgress = true;
				break;
			}
		}

		CBaseServer::_ConnectionMessage in_message(CBaseServer::_ConnectionMessage::DataDirection::none, m_lp_connection, "");
		if (!IsAsybcTaskInProgress) {						
			if (m_pClientCon->AsyncGetMessage(in_message) == 0) {
				ProcessCommand(in_message.data, buffer);
				out_message.data = buffer;
				m_pClientCon->AsyncPutMessage(out_message);
			}
		}
		//async tasks
		for (auto& source : m_sources) {
			if (source.pending_task != "") {
				ProcessAsyncTask(source, buffer);
				out_message.data = buffer;
				m_pClientCon->AsyncPutMessage(out_message);
			}
		}
		
		int liveview_source_index = FindLiveStreamIndex();
		if (liveview_source_index > -1) {			
			if (m_sources[liveview_source_index].pMedia->GetState() == MEDIA_THREAD_STATE_RUNNING) 
			{
				auto now = std::chrono::high_resolution_clock::now();
				int delay_from_last_frame = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - frame_ts).count();				
				if (delay_from_last_frame >= FC_DELAY_BETWEEN_FRAMES) 
				{
					frame_ts = now;
					cv::Mat frame;
					if (m_sources[liveview_source_index].pMedia->GrabMostRecentFrame(frame) == 0) {
						ApplyImageModification(frame);
						
						std::string fps = pfps->GetFPSinString();						
						//int cur_fps = std::stoi(fps);
						//if(cur_fps<15)
						//	m_pLog->AsyncWriteNonUnicode(m_pLog->string_format("FPS drops below 15 [%d]", cur_fps).c_str(), true, true);									
						m_pLog->WriteToOCVScreen(fps.c_str(), 150, 50, 0, 255, 255, frame);
						
						if (m_bIsRecording) {							
							FrameData x;
							x.frame = frame;
							x.FPS = std::stoi(fps);
							x.ts = std::chrono::high_resolution_clock::now();
							m_frame_datas.push_back(x);
						}
						//cv::imshow("TEST", frame);
						//cv::waitKey(1);
						m_pMediaServer->Write(frame);
					}
				}
			}			
		}			
		

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		bIsRunning = m_running.load();
	}
	SyncUpGlobalMediaSourceList();
}

int CFrameControl::ProcessAsyncTask(MediaSourceTask& x, std::string& res) {
	
	if (x.pMedia->GetState() == MEDIA_THREAD_STATE_RUNNING) {
		if (x.pending_task == "capturePhoto") {
			cv::Mat frame;
			std::string path;
			int result = x.pMedia->GetPhoto(frame, path);
			if (result== 0) {
				std::string tmp = "html/" + path;
				frame = cv::imread(tmp, -1);
				ApplyImageModification(frame);

				std::vector<int> params;
				params.push_back(cv::IMWRITE_JPEG_QUALITY);
				params.push_back(99);
				std::string filename;

				auto med_type = x.pMedia->GetType();
				std::string smedia_type("");
				if (med_type == CBaseMediaSource::SourceType::dslr)
					smedia_type = "dslr";

				GenerateUniqueName(filename, smedia_type.c_str(), x.pMedia->GetID().c_str(), ".jpg");
				tmp = "html/" + filename;
				cv::imwrite(tmp, frame, params);

				m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] Capture photo finished", m_lp_connection).c_str(), true, true);
				GenerateJSONReply(x.command_id.c_str(), "capturePhoto", "OK", filename.c_str(), res);
			}
			else if (result == -1) {
				m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] Capture photo failed", m_lp_connection).c_str(), true, true);
				GenerateJSONReply(x.command_id.c_str(), "capturePhoto", "ERROR", "Capture photo failed", res);				
			}
			if(result== 0 || result ==-1)
				x.pending_task = "";
		}
		else if (x.pending_task == "captureContinuousPhotos") {
			std::vector<cv::Mat> frames;
			std::vector<std::string> paths;
			std::vector<std::string> new_paths;
			int result = x.pMedia->GetPhotos(frames, paths);
			if (result == 0) {
				for (auto& path : paths) {
					std::string tmp = "html/" + path;
					cv::Mat frame = cv::imread(tmp, -1);
					ApplyImageModification(frame);

					std::vector<int> params;
					params.push_back(cv::IMWRITE_JPEG_QUALITY);
					params.push_back(99);
					std::string filename;

					auto med_type = x.pMedia->GetType();
					std::string smedia_type("");
					if (med_type == CBaseMediaSource::SourceType::dslr)
						smedia_type = "dslr";

					GenerateUniqueName(filename, smedia_type.c_str(), x.pMedia->GetID().c_str(), ".jpg");
					tmp = "html/" + filename;
					cv::imwrite(tmp, frame, params);
					new_paths.push_back(filename);
				}
				m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] Capture photo finished", m_lp_connection).c_str(), true, true);
				std::string implo_path = implode(new_paths, ',');
				GenerateJSONReply(x.command_id.c_str(), "captureContinuousPhotos", "OK", implo_path.c_str(), res);
			}
			else if (result == -1) {
				m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] Capture photo failed", m_lp_connection).c_str(), true, true);
				GenerateJSONReply(x.command_id.c_str(), "captureContinuousPhotos", "ERROR", "Capture photo failed", res);
			}
			if (result == 0 || result == -1)
				x.pending_task = "";
		}
		else if (x.pending_task == "captureBurstPhotos") {
			std::vector<cv::Mat> frames;
			std::vector<std::string> paths;
			std::vector<std::string> new_paths;
			int result = x.pMedia->GetPhotos(frames, paths);
			if (result == 0) {
				std::vector<cv::Mat> frames;
				rapidjson::Document document;
				document.Parse(x.data.c_str());

				bool bIsGeneratingVideo = document["video"].GetBool();
				for (auto& path : paths) {
					std::string tmp = "html/" + path;
					cv::Mat frame = cv::imread(tmp, -1);
					ApplyImageModification(frame);

					std::vector<int> params;
					params.push_back(cv::IMWRITE_JPEG_QUALITY);
					params.push_back(99);
					std::string filename;

					auto med_type = x.pMedia->GetType();
					std::string smedia_type("");
					if (med_type == CBaseMediaSource::SourceType::dslr)
						smedia_type = "dslr";

					GenerateUniqueName(filename, smedia_type.c_str(), x.pMedia->GetID().c_str(), ".jpg");
					tmp = "html/" + filename;
					cv::imwrite(tmp, frame, params);
					new_paths.push_back(filename);

					if (bIsGeneratingVideo) {
						int width = document["width"].GetInt();
						int height = document["height"].GetInt();

						cv::resize(frame, frame, cv::Size(width, height));
						frames.push_back(frame);
					}
				}
				if (bIsGeneratingVideo) {
					std::string filename;
					GenerateUniqueName(filename, "NONE", "NONE", ".mp4");

					double duration = document["duration"].GetInt();
					double fps = (double)frames.size() / (duration/1000);
					
					cv::VideoWriter video("html\\output.mp4", cv::VideoWriter::fourcc('a', 'v', 'c', '1'), fps, cv::Size(frames[0].cols, frames[0].rows));
					if (video.isOpened()) {
						for (auto& frame : frames) {
							video.write(frame);
						}

						int index = (int)frames.size()-1;
						for (int i = 0; i < (int)frames.size(); i++) {
							video.write(frames[index]);
							index--;
						}
					}
					video.release();					
					std::string path = "html\\" + filename;
					fixForFastPlayback("html\\output.mp4", path.c_str());
				}

				m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] Burst photo finished", m_lp_connection).c_str(), true, true);
				std::string implo_path = implode(new_paths, ',');
				GenerateJSONReply(x.command_id.c_str(), "captureBurstPhotos", "OK", implo_path.c_str(), res);
			}
			else if (result == -1) {
				m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] Capture photo failed", m_lp_connection).c_str(), true, true);
				GenerateJSONReply(x.command_id.c_str(), "captureBurstPhotos", "ERROR", "Capture photo failed", res);
			}
			if (result == 0 || result == -1)
				x.pending_task = "";
		}
		else {
			x.pending_task = "";
		}
	}
	else
		x.pending_task = "";
	return 0;
}

void CFrameControl::ProcessCommand(std::string& data, std::string& res) {
	rapidjson::Document document;
	document.Parse(data.c_str());
	std::string command = document["command"].GetString();
	CBaseMediaSource::SourceType mediatype;

	std::string mediatypename = document["media type"].GetString();
	if (mediatypename == "webcam")
		mediatype = CBaseMediaSource::SourceType::webcam;
	else if (mediatypename == "ipcam")
		mediatype = CBaseMediaSource::SourceType::ipcam;
	else if (mediatypename == "dslr")
		mediatype = CBaseMediaSource::SourceType::dslr;
	
	std::string source_id = document["id"].GetString();
	
	m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] Processing command %s", m_lp_connection, command.c_str()).c_str(), true, true);
	m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] Processing data %s", m_lp_connection, data.c_str()).c_str(), true, false);

	int media_index = FindLocalMediaSource(mediatype, source_id);
	
	if (command == "createNewMediaSource") {
		if (media_index > -1) {
			m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] Media Source already exist", m_lp_connection).c_str(), true, true);
			GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "ERROR", "Already exist", res);
		}
		else if (mediatype == CBaseMediaSource::SourceType::webcam) {			
			std::shared_ptr<CBaseMediaSource> ptrMedia = CheckExistingMediaSource(mediatype, source_id);				
			if (ptrMedia == nullptr) {								
				m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] No existing media sources [%s] found, creating new ...", m_lp_connection, mediatypename.c_str()).c_str(), true, true);

				m_created_media_sources_lock.lock();	
				
				MediaSourceTask x;
				if(document.HasMember("width"))
					x.pMedia = std::make_shared<CWebcamSource>(source_id, m_pLog, document["width"].GetInt(), document["height"].GetInt(), document["landscape"].GetBool());
				else
					x.pMedia = std::make_shared<CWebcamSource>(source_id, m_pLog);
				m_sources.push_back(x);

				MediaSourceInfo minfo;					
				minfo.pMedia = m_sources[m_sources.size() - 1].pMedia ;
				minfo.m_source_ref_count = 1;
				m_created_media_sources.push_back(minfo);
					
				m_created_media_sources_lock.unlock();									
				m_pLog->AsyncWrite("Done", true, true);
			}
			else {									
				m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] Existing media sources [%s] found, subscribing ...", m_lp_connection, mediatypename.c_str()).c_str(), true, true);
				MediaSourceTask x;
				x.pMedia = ptrMedia;
				m_sources.push_back(x);
				GlobalMediaSourceListAddRef(mediatype, source_id);				
				m_pLog->AsyncWrite("Done", true, true);
			}
			GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
		}
		else if (mediatype == CBaseMediaSource::SourceType::ipcam) {
			std::shared_ptr<CBaseMediaSource> ptrMedia = CheckExistingMediaSource(mediatype, source_id);
			if (ptrMedia == nullptr) {				
				m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] No existing media sources [%s] found, creating new ...", m_lp_connection, mediatypename.c_str()).c_str(), true, true);

				m_created_media_sources_lock.lock();				
				MediaSourceTask x;

				if (document.HasMember("width"))
					x.pMedia = std::make_shared<CIPCam>(source_id, m_pLog, document["width"].GetInt(), document["height"].GetInt(), document["landscape"].GetBool());
				else
					x.pMedia = std::make_shared<CIPCam>(source_id, m_pLog);
				m_sources.push_back(x);

				MediaSourceInfo minfo;
				minfo.pMedia = m_sources[m_sources.size() - 1].pMedia;
				minfo.m_source_ref_count = 1;
				m_created_media_sources.push_back(minfo);

				m_created_media_sources_lock.unlock();				
				m_pLog->AsyncWrite("Done", true, true);
			}
			else {				
				m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] Existing media sources [%s] found, subscribing ...", m_lp_connection, mediatypename.c_str()).c_str(), true, true);
				MediaSourceTask x;
				x.pMedia = ptrMedia;
				m_sources.push_back(x);
				GlobalMediaSourceListAddRef(mediatype, source_id);				
				m_pLog->AsyncWrite("Done", true, true);
			}
			GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
		}
		else if (mediatype == CBaseMediaSource::SourceType::dslr) {
			/*
			std::shared_ptr<CBaseMediaSource> ptrMedia = CheckExistingMediaSource(mediatype, source_id);
			if (ptrMedia == nullptr) {
				m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] No existing media sources [%s] found, creating new ...", m_lp_connection, mediatypename.c_str()).c_str(), true, true);

				m_created_media_sources_lock.lock();
				MediaSourceTask x;				
				x.pMedia = std::make_shared<CCanonSLR>(m_pLog, document["width"].GetInt(), document["height"].GetInt(), document["landscape"].GetBool(), (void*)this);
				m_sources.push_back(x);

				MediaSourceInfo minfo;
				minfo.pMedia = m_sources[m_sources.size() - 1].pMedia;
				minfo.m_source_ref_count = 1;
				m_created_media_sources.push_back(minfo);

				m_created_media_sources_lock.unlock();
				m_pLog->AsyncWrite("Done", true, true);
			}
			else {
				m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] Existing media sources [%s] found, subscribing ...", m_lp_connection, mediatypename.c_str()).c_str(), true, true);
				MediaSourceTask x;
				x.pMedia = ptrMedia;
				m_sources.push_back(x);
				GlobalMediaSourceListAddRef(mediatype, source_id);
				m_pLog->AsyncWrite("Done", true, true);
			}
			GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);		
			*/
		}
	}
	else if (command == "startLiveView") {
		if (document.HasMember("fps"))
			m_pLog->SetFPSOutput(document["fps"].GetBool());
				
		if (media_index == -1){
			GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "ERROR", "Media Source not created", res);			
			m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] ERROR ! Media Source not created", m_lp_connection).c_str(), true, true);
			return;
		}
		
		bool IsLiveViewStartOK = false;
		int state = m_sources[media_index].pMedia->GetState();
		if ((state == MEDIA_THREAD_STATE_STARTING || state == MEDIA_THREAD_STATE_RUNNING)) {
			if (m_pMediaServer != nullptr) {
				GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "ERROR", "Media Server already created", res);
				m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] ERROR ! Media Server already created", m_lp_connection).c_str(), true, true);
				return;
			}
			else {
				if (m_sources[media_index].pMedia->StartLiveView() == 0)
					IsLiveViewStartOK = true;
			}
		}
		else {			
			if (m_sources[media_index].pMedia->StartLiveView() == 0)
				IsLiveViewStartOK = true;
		}
		
		if (!IsLiveViewStartOK) {
			GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "ERROR", "StartLiveView failed!", res);
			m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] ERROR ! StartLiveView failed!", m_lp_connection).c_str(), true, true);
			return;
		}

		double scale = 1.0;
		if (document.HasMember("scale")) {
			scale = document["scale"].GetDouble();
		}

		int port=0;
		SetupMediaServer(media_index, port, "MJPEG", scale);
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", std::to_string(port).c_str(), res);
	}
	else if (command == "stopLiveView") {		
		if (media_index == -1) {
			GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "ERROR", "Media Source not created", res);			
			m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] ERROR ! Media Source not created", m_lp_connection).c_str(), true, true);
			return;
		}
		
		int state = m_sources[media_index].pMedia->GetState();
		if (state == MEDIA_THREAD_STATE_IDLE || state == MEDIA_THREAD_STATE_EXITING || state == MEDIA_THREAD_STATE_EXIT) {
			GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "ERROR", "Media Source already stopped", res);			
			m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] ERROR ! Media Source already stopped", m_lp_connection).c_str(), true, true);
			return;
		}
		int irefcount = GetGlobalMediaRefCount(mediatype, source_id); 
		if(irefcount>1){
			GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "ERROR", "Media Source being accessed by more than one client", res);			
			m_pLog->AsyncWrite(m_pLog->string_format("Frame Control [0x%08x] ERROR ! Media Source being accessed by more than one client", m_lp_connection).c_str(), true, true);
			return;
		}

		m_sources[media_index].pMedia->StopLiveView();		
		m_sources[media_index].isLiveStreaming = false;
		m_pMediaServer.reset();
		m_pMediaServer = nullptr;
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
	}	
	else if (command == "captureContinuousPhotos") {
		std::vector<cv::Mat> frames;

		int ret = m_sources[media_index].pMedia->CaptureContinuousPhoto(frames, document["photo count"].GetInt());
		if (ret == ERR_NONE) {
		}
		else if (ret == ERROR_ASYNC_OPERATION) {
			m_sources[media_index].pending_task = "captureContinuousPhotos";
			m_sources[media_index].command_id = document["command id"].GetString();
			m_sources[media_index].data = data;
		}
	}
	else if (command == "captureBurstPhotos") {
		std::vector<cv::Mat> frames;

		int ret = m_sources[media_index].pMedia->CaptureBurstPhoto(frames, document["duration"].GetInt(), document["photo count"].GetInt());
		if (ret == ERR_NONE) {
		}
		else if (ret == ERROR_ASYNC_OPERATION) {
			m_sources[media_index].pending_task = "captureBurstPhotos";
			m_sources[media_index].command_id = document["command id"].GetString();
			m_sources[media_index].data = data;
		}
	}
	else if (command == "capturePhoto") {
		if (media_index > -1) {			
			cv::Mat frame;
			int ret = m_sources[media_index].pMedia->CapturePhoto(frame);
			if (ret == ERR_NONE) {

				ApplyImageModification(frame);

				std::vector<int> params;
				params.push_back(cv::IMWRITE_JPEG_QUALITY);
				params.push_back(99);				
				std::string filename;
				GenerateUniqueName(filename, mediatypename.c_str(), m_sources[media_index].pMedia->GetID().c_str(), ".jpg");

#ifdef _WIN32
				std::string html_path = "html\\" + filename;
#else
				std::string html_path = "html/" + filename;
#endif
				cv::imwrite(html_path, frame, params);
				GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", filename.c_str(), res);
			}
			else if (ret == ERROR_ASYNC_OPERATION) {
				m_sources[media_index].pending_task = "capturePhoto";
				m_sources[media_index].command_id = document["command id"].GetString();
			}
			else {
				GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "ERROR", "Capture photo fail", res);
			}
		}
		else {
			GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "ERROR","Media source not found", res);
		}
	}
	else if (command == "addFilter") {
		std::string filter_name = document["command id"].GetString();
		AddImageModification(STACK_INDEX_FILTER, data);
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
	}
	else if (command == "removeFilter") {
		for (int i = 0; i < (int)m_effects.size(); i++) {
			if (STACK_INDEX_FILTER == m_effects[i]->GetStackIndex()) {
				m_effects.erase(m_effects.begin() + i);
				break;
			}
		}
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
	}
	else if (command == "addFrame") {
		std::string filter_name = document["command id"].GetString();
		AddImageModification(STACK_INDEX_FRAME, data);
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
	}
	else if (command == "removeFrame") {
		for (int i = 0; i < (int)m_effects.size(); i++) {
			if (STACK_INDEX_FRAME == m_effects[i]->GetStackIndex()) {
				m_effects.erase(m_effects.begin() + i);
				break;
			}
		}
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK","", res);
	}
	else if (command == "addBGRemoval") {
		std::string filter_name = document["command id"].GetString();
		AddImageModification(STACK_INDEX_BG_REMOVAL, data);
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
	}
	else if (command == "removeBGRemoval") {
		for (int i = 0; i < (int)m_effects.size(); i++) {
			if (STACK_INDEX_BG_REMOVAL == m_effects[i]->GetStackIndex()) {
				m_effects.erase(m_effects.begin() + i);
				break;
			}
		}
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
	}
	else if (command == "addDynamicFrame") {
		std::string filter_name = document["command id"].GetString();
		AddImageModification(STACK_INDEX_DYN_FRAME, data);
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
	}
	else if (command == "removeDynamicFrame") {
		for (int i = 0; i < (int)m_effects.size(); i++) {
			if (STACK_INDEX_DYN_FRAME == m_effects[i]->GetStackIndex()) {
				m_effects.erase(m_effects.begin() + i);
				break;
			}
		}
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
	}	

	else if (command == "addDynamicFrameWithCustomIndex") {
		int stack_index = document["stack index"].GetInt();
		std::string filter_name = document["command id"].GetString();
		AddImageModificationWithCustomStackIndex(stack_index, STACK_INDEX_DYN_FRAME, data);
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
	}
	else if (command == "removeDynamicFrameWithCustomIndex") {
		int stack_index = document["stack index"].GetInt();
		for (int i = 0; i < (int)m_effects.size(); i++) {
			if (stack_index == m_effects[i]->GetStackIndex()) {
				m_effects.erase(m_effects.begin() + i);
				break;
			}
		}
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
	}
	else if (command == "addFrameWithCustomIndex") {
		int stack_index = document["stack index"].GetInt();
		std::string filter_name = document["command id"].GetString();
		AddImageModificationWithCustomStackIndex(stack_index, STACK_INDEX_DYN_FRAME, data);
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
	}
	else if (command == "removeFrameWithCustomIndex") {
		int stack_index = document["stack index"].GetInt();
		for (int i = 0; i < (int)m_effects.size(); i++) {
			if (stack_index == m_effects[i]->GetStackIndex()) {
				m_effects.erase(m_effects.begin() + i);
				break;
			}
		}
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
	}
	else if (command == "addAccesoriesWithCustomIndex") {
		int stack_index = document["stack index"].GetInt();
		std::string filter_name = document["command id"].GetString();
		AddImageModificationWithCustomStackIndex(stack_index, STACK_INDEX_ACC, data);
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
	}
	else if (command == "removeAccesoriesCustomIndex") {
		int stack_index = document["stack index"].GetInt();
		for (int i = 0; i < (int)m_effects.size(); i++) {
			if (stack_index == m_effects[i]->GetStackIndex()) {
				m_effects.erase(m_effects.begin() + i);
				break;
			}
	}
	GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
	}
	else if (command == "clearLiveviewMovieBuffer") {
		m_fpses.clear();
		m_modified_frames.clear();
	}

	else if (command == "startLiveviewMovieRecording") {
		m_bIsRecording = true;
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
	}
	else if (command == "generateLiveviewMovie") {
		GenerateMovie(data, res);
	}
	else if (command == "generateBGModel") {
		CreateBGModel();
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", "", res);
	}
	else if (command == "getRGBLimit") {
		int rl = 255;
		int gl = 255;
		int bl = 255;
		int rh = 0;
		int gh = 0;
		int bh = 0;
		GetRBGLimit(rl, gl, bl, rh, gh, bh);

		rapidjson::Document return_doc;
		return_doc.SetObject();

		std::string keyname = "command id";
		std::string keyval = document["command id"].GetString();
		rapidjson::Value x;
		x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
		rapidjson::Value y;
		y.SetString(keyval.c_str(), (int)keyval.length(), return_doc.GetAllocator());
		return_doc.AddMember(x, y, return_doc.GetAllocator());

		keyname = "command";
		keyval = document["command"].GetString();
		x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
		y.SetString(keyval.c_str(), (int)keyval.length(), return_doc.GetAllocator());
		return_doc.AddMember(x, y, return_doc.GetAllocator());

		keyname = "result";
		keyval = "OK";
		x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
		y.SetString(keyval.c_str(), (int)keyval.length(), return_doc.GetAllocator());
		return_doc.AddMember(x, y, return_doc.GetAllocator());		

		keyname = "RL";		
		x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
		y.SetInt(rl);
		return_doc.AddMember(x, y, return_doc.GetAllocator());

		keyname = "GL";		
		x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
		y.SetInt(gl);
		return_doc.AddMember(x, y, return_doc.GetAllocator());

		keyname = "BL";		
		x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
		y.SetInt(bl);
		return_doc.AddMember(x, y, return_doc.GetAllocator());

		keyname = "RH";		
		x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
		y.SetInt(rh);
		return_doc.AddMember(x, y, return_doc.GetAllocator());

		keyname = "GH";		
		x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
		y.SetInt(gh);
		return_doc.AddMember(x, y, return_doc.GetAllocator());

		keyname = "BH";		
		x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
		y.SetInt(bh);
		return_doc.AddMember(x, y, return_doc.GetAllocator());

		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		return_doc.Accept(writer);

		res = buffer.GetString();

	}
}

int CFrameControl::GetGlobalMediaRefCount(const CBaseMediaSource::SourceType& mediatype, const std::string& id) {
	m_created_media_sources_lock.lock();
	int ret = -1;	
	for (int i = 0; i < (int)m_created_media_sources.size(); i++) {
		if (m_created_media_sources[i].pMedia->GetID() == id && m_created_media_sources[i].pMedia->GetType() == mediatype) {
			ret = m_created_media_sources[i].m_source_ref_count;
		}
	}
	m_created_media_sources_lock.unlock();
	return ret;
}

int CFrameControl::FindLocalMediaSource(CBaseMediaSource::SourceType& mediatype, std::string& id) {
	int ret = -1;	
	for (int i = 0; i < (int)m_sources.size(); i++) {
		if (m_sources[i].pMedia->GetID() == id && m_sources[i].pMedia->GetType() == mediatype) {
			ret = i;
			break;
		}
	}	
	return ret;
}

int CFrameControl::CreateBGModel() {
	int ret = -1;
	int liveview_source_index = FindLiveStreamIndex();
	if (liveview_source_index > -1) {
		cv::Mat frame;
		if (m_sources[liveview_source_index].pMedia->GrabMostRecentFrame(frame) == 0) {/*
			cv::Ptr<cv::BackgroundSubtractor> pBackSub;
			pBackSub = cv::createBackgroundSubtractorMOG2();
			//pBackSub->apply(frame, fgMask);*/
			m_bg_sub = frame.clone();
		}				
	}
	return ret;
}

void CFrameControl::GetRBGLimit(int& rl, int& gl, int& bl, int& rh, int& gh, int& bh) {
	cv::Mat frame;
	int liveview_source_index = FindLiveStreamIndex();
	if (liveview_source_index > -1) {
		if (m_sources[liveview_source_index].pMedia->GrabMostRecentFrame(frame) == 0) {
			cv::Mat frm = frame.clone();
			for (int i = 0; i < frm.cols; i++) {
				for (int j = 0; j < frm.rows; j++) {
					cv::Vec3b& BGR = frm.at<cv::Vec3b>(j, i);
					if (rl > BGR[2])
						rl = BGR[2];
					if (gl > BGR[1])
						gl = BGR[1];
					if (bl > BGR[0])
						bl = BGR[0];
					if (rh < BGR[2])
						rh = BGR[2];
					if (gh < BGR[1])
						gh = BGR[1];
					if (bh < BGR[0])
						bh = BGR[0];
				}
			}
		}
	}				
}

void CFrameControl::GenerateJSONReply(const char* command_id, const char* command_name, const char* result, const char* payload, std::string& reply) {
	rapidjson::Document return_doc;
	return_doc.SetObject();

	std::string keyname = "command id";
	std::string keyval = command_id;
	rapidjson::Value x;
	x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
	rapidjson::Value y;
	y.SetString(keyval.c_str(), (int)keyval.length(), return_doc.GetAllocator());
	return_doc.AddMember(x, y, return_doc.GetAllocator());

	keyname = "command";
	keyval = command_name;
	x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
	y.SetString(keyval.c_str(), (int)keyval.length(), return_doc.GetAllocator());
	return_doc.AddMember(x, y, return_doc.GetAllocator());

	keyname = "result";
	keyval = result;
	x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
	y.SetString(keyval.c_str(), (int)keyval.length(), return_doc.GetAllocator());
	return_doc.AddMember(x, y, return_doc.GetAllocator());

	keyname = "data";
	keyval = payload;
	x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
	y.SetString(keyval.c_str(), (int)keyval.length(), return_doc.GetAllocator());
	return_doc.AddMember(x, y, return_doc.GetAllocator());

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	return_doc.Accept(writer);

	reply = buffer.GetString();
}

void CFrameControl::GenerateJSONEvent(const char* event_id, const char* event, int event_code, const char* source_type, const char* source_id, const char* effect_id, const char* payload, std::string& reply) {
	rapidjson::Document return_doc;
	return_doc.SetObject();

	std::string keyname = "event id";
	std::string keyval = event_id;
	rapidjson::Value x;
	x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
	rapidjson::Value y;
	y.SetString(keyval.c_str(), (int)keyval.length(), return_doc.GetAllocator());
	return_doc.AddMember(x, y, return_doc.GetAllocator());

	keyname = "event name";
	keyval = event;
	x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
	y.SetString(keyval.c_str(), (int)keyval.length(), return_doc.GetAllocator());
	return_doc.AddMember(x, y, return_doc.GetAllocator());

	keyname = "event code";
	keyval = event_code;
	x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());	
	y.SetInt(event_code);
	return_doc.AddMember(x, y, return_doc.GetAllocator());

	keyname = "media type";
	keyval = source_type;
	x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
	y.SetString(keyval.c_str(), (int)keyval.length(), return_doc.GetAllocator());
	return_doc.AddMember(x, y, return_doc.GetAllocator());

	keyname = "source id";
	keyval = source_id;
	x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
	y.SetString(keyval.c_str(), (int)keyval.length(), return_doc.GetAllocator());
	return_doc.AddMember(x, y, return_doc.GetAllocator());

	keyname = "effect id";
	keyval = effect_id;
	x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
	y.SetString(keyval.c_str(), (int)keyval.length(), return_doc.GetAllocator());
	return_doc.AddMember(x, y, return_doc.GetAllocator());

	keyname = "data";
	keyval = payload;
	x.SetString(keyname.c_str(), (int)keyname.length(), return_doc.GetAllocator());
	y.SetString(keyval.c_str(), (int)keyval.length(), return_doc.GetAllocator());
	return_doc.AddMember(x, y, return_doc.GetAllocator());

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	return_doc.Accept(writer);

	reply = buffer.GetString();
}

void CFrameControl::GlobalMediaSourceListAddRef(CBaseMediaSource::SourceType& mediatype, std::string& id) {
	m_created_media_sources_lock.lock();

	for (int i = 0; i < (int)m_created_media_sources.size(); i++) {
		if (m_created_media_sources[i].pMedia->GetID() == id && m_created_media_sources[i].pMedia->GetType() == mediatype) {
			m_created_media_sources[i].m_source_ref_count++;
			break;
		}
	}

	m_created_media_sources_lock.unlock();
}

std::shared_ptr<CBaseMediaSource> CFrameControl::CheckExistingMediaSource(CBaseMediaSource::SourceType& mediatype, std::string& id) {
	m_created_media_sources_lock.lock();			
	std::shared_ptr<CBaseMediaSource> p = nullptr;

	for (int i = 0; i < (int)m_created_media_sources.size(); i++) {		
		if (  m_created_media_sources[i].pMedia->GetID() == id && m_created_media_sources[i].pMedia->GetType() == mediatype) {			
			p = m_created_media_sources[i].pMedia;			
			break;
		}
	}	
	m_created_media_sources_lock.unlock();
	return p;
}

void* CFrameControl::GetID() {
	return m_lp_connection;
}

void CFrameControl::SyncUpGlobalMediaSourceList() {
	m_created_media_sources_lock.lock();
	for (auto& globalsource : m_created_media_sources) {		
		for (auto localsource : m_sources) {
			if (localsource.pMedia->GetID() == globalsource.pMedia->GetID() && localsource.pMedia->GetType() == globalsource.pMedia->GetType())
				globalsource.m_source_ref_count--;
		}		
	}
	bool is_zero_ref_count_exist;
	while (true) {
		is_zero_ref_count_exist = false;
		for (int i = 0; i < (int)m_created_media_sources.size(); i++) {
			if (m_created_media_sources[i].m_source_ref_count == 0) {
				m_created_media_sources.erase(m_created_media_sources.begin() + i);
				is_zero_ref_count_exist = true;
				break;
			}
		}
		if (is_zero_ref_count_exist == false)
			break;
	}
	m_created_media_sources_lock.unlock();
}

void CFrameControl::SetLog(std::shared_ptr<CLogManager> log) {
	m_pLog = log;
}

void CFrameControl::WriteLog(std::string data) {
	
}

void CFrameControl::SetupMediaServer(int source_index, int& port, const char* server_type, double scale) {
	int currently_live_streaming_index = FindLiveStreamIndex();
	if (currently_live_streaming_index > -1)
		m_sources[currently_live_streaming_index].isLiveStreaming = false;
	m_sources[source_index].isLiveStreaming = true;
	m_media_server_port.fetch_add(1);
	std::string server_t = server_type;
	if(server_t == "MJPEG")		
		m_pMediaServer = std::make_shared<CMJPEGMediaServer>(m_pLog, scale);
	else
		m_pMediaServer = std::make_shared<CMJPEGMediaServer>(m_pLog, scale);
	port = m_media_server_port.load();
	m_pMediaServer->Init(port);
	
}

int CFrameControl::FindLiveStreamIndex() {
	int ret = -1;
	for (int i=0; i < (int)m_sources.size(); i++) {
		if (m_sources[i].isLiveStreaming) {
			ret = i;
			break;
		}
	}
	return ret;
}

void CFrameControl::ApplyImageModification(cv::Mat& frame) {

	for (auto& effect : m_effects) {
		effect->filter(frame);
	}
}

void CFrameControl::AddImageModification(const int stack_effect_index, const std::string& data) {
	rapidjson::Document document;
	document.Parse(data.c_str());

	for (int i = 0; i < (int)m_effects.size(); i++) {
		if (stack_effect_index == m_effects[i]->GetStackIndex()) {
			m_effects.erase(m_effects.begin() + i);
			break;
		}
	}

	if (stack_effect_index == STACK_INDEX_DIM) {
		m_effects.push_back(std::make_shared<CCrop>(document["width"].GetInt(), document["height"].GetInt(), m_pLog));
	}
	else if (stack_effect_index == STACK_INDEX_FILTER) {
		m_effects.push_back(std::make_shared<CFilter>(document["filter name"].GetString(), m_pLog));
	}
	else if (stack_effect_index == STACK_INDEX_FRAME) {
		m_effects.push_back(std::make_shared<COverlay>(document["frame name"].GetString(), m_pLog));
	}
	else if (stack_effect_index == STACK_INDEX_BG_REMOVAL) {
		bool is_using_gscreen = true;
		if (document.HasMember("greenscreen"))
			is_using_gscreen = document["greenscreen"].GetBool();

		if (is_using_gscreen) {
			int r_l = document["RL"].GetInt();
			int g_l = document["GL"].GetInt();
			int b_l = document["BL"].GetInt();

			int r_h = document["RH"].GetInt();
			int g_h = document["GH"].GetInt();
			int b_h = document["BH"].GetInt();

			bool is_static = document["static"].GetBool();

			if (!is_static) {
				if (document.HasMember("width") && document.HasMember("height")) {
					int width = document["width"].GetInt();
					int height = document["height"].GetInt();
					m_effects.push_back(std::make_shared<CBackGorundRemoval>(document["name"].GetString(), m_pLog, r_l, g_l, b_l, r_h, g_h, b_h, is_static, width, height, document["repeat"].GetBool()));
				}
				else
					m_effects.push_back(std::make_shared<CBackGorundRemoval>(document["name"].GetString(), m_pLog, r_l, g_l, b_l, r_h, g_h, b_h, is_static, document["repeat"].GetBool()));
			}
			else
				m_effects.push_back(std::make_shared<CBackGorundRemoval>(document["name"].GetString(), m_pLog, r_l, g_l, b_l, r_h, g_h, b_h));
		}
		else {
			//CBackGorundRemoval(const char* name, std::shared_ptr<CLogManagerUnicode> log, bool is_static, bool is_greenscreen, int width, int height, bool repeat, cv::Mat& frame);
			bool is_static = document["static"].GetBool();
			int width = document["width"].GetInt();
			int height = document["height"].GetInt();
			m_effects.push_back(std::make_shared<CBackGorundRemoval>(document["name"].GetString(), m_pLog, is_static, is_using_gscreen, width, height, document["repeat"].GetBool(), m_bg_sub));
		}
	}
	else if (stack_effect_index == STACK_INDEX_DYN_FRAME) {
		std::string mode = document["mode"].GetString();
		bool is_repeat = false;
		if(document.HasMember("repeat"))
			is_repeat = document["repeat"].GetBool();
		if(mode=="alpha")
			m_effects.push_back(std::make_shared<CDynamicFrame>(document["file name"].GetString(), m_pLog, false, is_repeat));
		else
			m_effects.push_back(std::make_shared<CDynamicFrame>(document["file name"].GetString(), m_pLog, true, is_repeat));
	}
	while (true) {
		bool isSwappped = false;
		for (int i = 0; i < (int)m_effects.size(); i++) {
			isSwappped = false;
			for (int j = i+1; j < (int)m_effects.size(); j++) {
				if (m_effects[i]->GetStackIndex() > m_effects[j]->GetStackIndex()) {
					std::swap(m_effects[i], m_effects[j]);
					isSwappped = true;
					break;
				}
			}
			if (isSwappped == true)
				break;
		}	
		if (isSwappped == false)
			break;
	}
}

void CFrameControl::AddImageModificationWithCustomStackIndex(const int stack_effect_index, const int filter_type, const std::string& data) {
	rapidjson::Document document;
	document.Parse(data.c_str());

	for (int i = 0; i < (int)m_effects.size(); i++) {
		if (stack_effect_index == m_effects[i]->GetStackIndex()) {
			m_effects.erase(m_effects.begin() + i);
			break;
		}
	}

	if (filter_type == STACK_INDEX_DIM) {
		m_effects.push_back(std::make_shared<CCrop>(document["width"].GetInt(), document["height"].GetInt(), m_pLog));
	}
	else if (filter_type == STACK_INDEX_FILTER) {
		m_effects.push_back(std::make_shared<CFilter>(document["filter name"].GetString(), m_pLog));
	}
	else if (filter_type == STACK_INDEX_FRAME) {
		m_effects.push_back(std::make_shared<COverlay>(document["frame name"].GetString(), m_pLog, stack_effect_index));
	}
	else if (filter_type == STACK_INDEX_ACC) {
		double thres = document["confidence"].GetDouble();
		int size_ = document["size"].GetInt();
		m_effects.push_back(std::make_shared<CClassicFaceDetect>(document["id"].GetString(), m_pLog, thres, size_, stack_effect_index));
	}
	else if (filter_type == STACK_INDEX_DYN_FRAME) {
		std::string mode = document["mode"].GetString();
		bool repeat = false;
		if (document.HasMember("repeat"))
			repeat = document["repeat"].GetBool();
		if (mode == "alpha") {			
			if (document.HasMember("width") && document.HasMember("height")) {
				int width = document["width"].GetInt();
				int height = document["height"].GetInt();
				m_effects.push_back(std::make_shared<CDynamicFrame>(document["file name"].GetString(), m_pLog, false, stack_effect_index, width, height, repeat));
			}
			else
				m_effects.push_back(std::make_shared<CDynamicFrame>(document["file name"].GetString(), m_pLog, false, stack_effect_index, repeat));
		}
		else {
			int r_l = document["RL"].GetInt();
			int g_l = document["GL"].GetInt();
			int b_l = document["BL"].GetInt();

			int r_h = document["RH"].GetInt();
			int g_h = document["GH"].GetInt();
			int b_h = document["BH"].GetInt();
			int width = document["width"].GetInt();
			int height = document["height"].GetInt();			
			m_effects.push_back(std::make_shared<CDynamicFrame>(document["file name"].GetString(), m_pLog, true, stack_effect_index, width, height, r_l, g_l, b_l, r_h, g_h, b_h, repeat));
		}
	}
	while (true) {
		bool isSwappped = false;
		for (int i = 0; i < (int)m_effects.size(); i++) {
			isSwappped = false;
			for (int j = i + 1; j < (int)m_effects.size(); j++) {
				if (m_effects[i]->GetStackIndex() > m_effects[j]->GetStackIndex()) {
					std::swap(m_effects[i], m_effects[j]);
					isSwappped = true;
					break;
				}
			}
			if (isSwappped == true)
				break;
		}
		if (isSwappped == false)
			break;
	}
}

void CFrameControl::GenerateUniqueName(std::string& name, const char* mediatypename, const char* id, const char* afix) {	

	std::string day_, time_;
	m_pLog->GetTimeMSResolution(day_, time_, true);		
	
	name = day_ + "_" + time_;
	name = name + "_";
	name = name + mediatypename;
	name = name + "_";
	name = name + id;
	name = name + +afix;
}

int CFrameControl::GenerateMovie(std::string& data, std::string& res) {
	int result =  -1;
	
	rapidjson::Document document;
	document.Parse(data.c_str());

	if (m_frame_datas.size() == 0) {
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "ERROR", "Generate movie failed", res);
		return result;
	}

	m_bIsRecording = false;

	int fps = 0;
	if (m_frame_datas.size() < FC_AVG_FPS_REQ)
		fps = document["fps"].GetInt();
	else {
		for (int i = 0; i < FC_AVG_FPS_REQ; i++) {
			fps = fps + m_frame_datas[i].FPS;
		}
		float ffps = (float)fps / (float)FC_AVG_FPS_REQ;
		fps = (int)ffps;
	}

	if (fps <= 20) {
		if ((fps - 15) < (20 - fps)) {
			fps = 15;
		}
		else
			fps = 20;
	}
	else {
		if ((fps - 20) < (25 - fps)) {
			fps = 20;
		}
		else
			fps = 25;
	}

	m_pLog->AsyncWrite(m_pLog->string_format("FPS : %d", fps).c_str(), true, true);
	m_pLog->AsyncWrite(m_pLog->string_format("Frames available : %d", (int)m_frame_datas.size()).c_str(), true, true);

	int duration = document["duration"].GetInt();

	std::string filename;
	GenerateUniqueName(filename, "NONE", "NONE", ".mp4");
#ifdef _WIN32
	std::string path = "html\\" + filename;
#else
	std::string path = "html/" + filename;
#endif

	
	cv::VideoWriter video("html\\output.mp4", cv::VideoWriter::fourcc('a', 'v', 'c', '1'), fps, cv::Size(m_frame_datas[0].frame.cols, m_frame_datas[0].frame.rows));
	if (video.isOpened()) {		
		int iFrameIndex = 0;
		int total_frame_count = (int)m_frame_datas.size();
		for (int i = 0; i < duration; i++) {
			auto time_start = m_frame_datas[iFrameIndex].ts;
			int iTotalFrameInSecond = 0;
			bool bIsLackOfFrames = false;
			while (iTotalFrameInSecond <= fps) {
				int diffTime = (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(m_frame_datas[iFrameIndex].ts - time_start).count();
				if (diffTime <= 1000) {
					video.write(m_frame_datas[iFrameIndex].frame);
					iTotalFrameInSecond++;

					if (iFrameIndex == total_frame_count - 1) {
						bIsLackOfFrames = true;
						break;
					}
					else
						iFrameIndex++;					
				}
				else {
					bIsLackOfFrames = true;
					break;
				}
			}
			if (bIsLackOfFrames) {
				while (iTotalFrameInSecond <= fps) {
					cv::Mat tmp = m_frame_datas[iFrameIndex].frame.clone();
					video.write(tmp);
					iTotalFrameInSecond++;
				}
			}
			
			if (iFrameIndex < total_frame_count - 1) 
				iFrameIndex++;				
		}
		video.release();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		fixForFastPlayback("html\\output.mp4", path.c_str());
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "OK", filename.c_str(), res);
	}
	else {
		GenerateJSONReply(document["command id"].GetString(), document["command"].GetString(), "ERROR", "Generate movie failed", res);
	}
	m_frame_datas.clear();
	return result;
}