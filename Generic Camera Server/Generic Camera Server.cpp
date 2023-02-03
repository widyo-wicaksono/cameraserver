// Generic Camera Server.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include <memory> 

#include <signal.h>

#include "ClientConnections.h"
#include "FrameControl.h"
#include "LogManager.h"

bool g_isKeepRunning = true;

void sighandler(int sig) {
	g_isKeepRunning = false;	
}

#ifdef _WIN32
BOOL CtrlHandler(DWORD fdwCtrlType)
{
	g_isKeepRunning = false;
	return TRUE;
}
#endif

int main(int argc, char *args[])
{

#ifdef _WIN32
	HANDLE hInput;
	DWORD prev_mode;
	hInput = GetStdHandle(STD_INPUT_HANDLE);
	GetConsoleMode(hInput, &prev_mode);
	SetConsoleMode(hInput, prev_mode & ENABLE_EXTENDED_FLAGS);
#endif
	/*
	std::shared_ptr<CLogManager> pLog = std::make_shared<CLogManager>("Camera Platform Server", LOG_TYPE_STANDARD);
	pLog->Init();	
	
	*/
	CLogManager* pLog = CLogManager::getInstance("Camera Platform Server", LOG_TYPE_STANDARD);
	pLog->AsyncWrite("Camera Platform Server Ver. 0.0.1", true, true);
	pLog->AsyncWrite("=================================", true, true);	
	pLog->AsyncWrite("Initiating ...", true, true);

#ifdef _WIN32
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);
#else
	signal(SIGINT, sighandler);
#endif		
	
	pLog->AsyncWrite("Done", true, true);
	pLog->AsyncWrite("Getting ready to accept connections ...", true, true);
	
	//std::shared_ptr<CBaseServer> pBaseServer = std::make_shared<CWSServer>(pLog);
	std::shared_ptr<CBaseServer> pBaseServer = std::make_shared<CWSServer>();
	if (pBaseServer->Run() == -1){
		pLog->AsyncWrite("Failed to start Networking!", true, true);
		return 0;
	}
	pLog->AsyncWrite("Done", true, true);
	
	//std::vector<std::shared_ptr<CFrameControl>> frame_controls;
	std::list<std::shared_ptr<CFrameControl>> frame_controls;
	while (g_isKeepRunning) {
		void* lp_con = nullptr;		
		if (pBaseServer->AsyncGetNewConnection(&lp_con)) {			
			pLog->AsyncWrite(pLog->string_format("New Connection inbound[0x%08x]", lp_con).c_str(), true, true);
			
			//frame_controls.emplace_back(std::move(std::make_shared<CFrameControl>(pBaseServer, lp_con, pLog)));
			frame_controls.emplace_back(std::move(std::make_shared<CFrameControl>(pBaseServer, lp_con)));
			//if (frame_controls[frame_controls.size() - 1]->Run()==-1) {
			if (frame_controls.back()->Run() == -1) {
				pLog->AsyncWrite(pLog->string_format("Frame Control failed to start![0x%08x]", lp_con).c_str(), true, true);
				frame_controls.pop_back();
			}
		}
		if (pBaseServer->AsyncGetDisconnectedConnection(&lp_con)) {
			/*
			for (int i = 0; i < (int)frame_controls.size(); i++) {
				if (frame_controls[i]->GetID() == lp_con) {
					frame_controls.erase(frame_controls.begin() + i);					
					pLog->AsyncWrite(pLog->string_format("Connection Terminated [0x%08x]", lp_con).c_str(), true, true);
				}
			}
			*/
			for (auto it = frame_controls.begin(); it != frame_controls.end();) {
				if ((*it)->GetID() == lp_con) {
					it = frame_controls.erase(it);
					pLog->AsyncWrite(pLog->string_format("Connection Terminated [0x%08x]", lp_con).c_str(), true, true);
					break;
				}
				else
					++it;
			}
		}		
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}		
	pLog->AsyncWrite("Application is exiting ...", true, true);
	
	return 0;
}
