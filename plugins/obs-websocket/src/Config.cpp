/*
obs-websocket
Copyright (C) 2016-2021 Stephane Lepin <stephane.lepin@gmail.com>
Copyright (C) 2020-2021 Kyle Manning <tt2468@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-frontend-api.h>
//#include <obs-frontend-api.h> commented by yug sir
#include "Config.h"
#include "utils/Crypto.h"
#include "utils/Platform.h"
#include <QtNetwork>
#include <QSharedMemory>
#include <QApplication>
#include <QMessageBox>
#include <QProcess>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif
#include <iostream>
#include <string>
#include <vector>
#ifndef _WIN32
#include <dirent.h>
#include <cstring>
#include <fstream>
#include <locale>
#include <codecvt>
#endif



#define CONFIG_SECTION_NAME "OBSWebSocket"

#define PARAM_FIRSTLOAD "FirstLoad"
#define PARAM_ENABLED "ServerEnabled"
#define PARAM_PORT "ServerPort"
#define PARAM_ALERTS "AlertsEnabled"
#define PARAM_AUTHREQUIRED "AuthRequired"
#define PARAM_PASSWORD "ServerPassword"

#define CMDLINE_WEBSOCKET_PORT "websocket_port"
#define CMDLINE_WEBSOCKET_IPV4_ONLY "websocket_ipv4_only"
#define CMDLINE_WEBSOCKET_PASSWORD "websocket_password"
#define CMDLINE_WEBSOCKET_DEBUG "websocket_debug"

Config::Config()
{
	SetDefaultsToGlobalStore();
}


//iamramking
//int CountInstances(const std::wstring &processName)
//{
//	DWORD processes[1024];
//	DWORD bytesReturned;
//
//	if (!EnumProcesses(processes, sizeof(processes), &bytesReturned)) {
//		return -1; // Error
//	}
//
//	DWORD numProcesses = bytesReturned / sizeof(DWORD);
//	int count = 0;
//
//	for (DWORD i = 0; i < numProcesses; i++) {
//		HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processes[i]);
//		if (hProcess != nullptr) {
//			TCHAR szProcessName[MAX_PATH];
//			if (GetProcessImageFileName(hProcess, szProcessName, MAX_PATH) != 0) {
//				std::wstring processPath = szProcessName;
//				size_t pos = processPath.find_last_of(L"\\");
//				if (pos != std::wstring::npos) {
//					std::wstring exeName = processPath.substr(pos + 1);
//					if (_wcsicmp(exeName.c_str(), processName.c_str()) == 0) {
//						count++;
//					}
//				}
//			}
//			CloseHandle(hProcess);
//		}
//	}
//
//	return count;
//}

int CountInstances(const std::wstring &processName)
{
	int count = 0;

#ifdef _WIN32
	DWORD processes[1024];
	DWORD bytesReturned;

	if (!EnumProcesses(processes, sizeof(processes), &bytesReturned)) {
		return -1; // Error
	}

	DWORD numProcesses = bytesReturned / sizeof(DWORD);

	for (DWORD i = 0; i < numProcesses; i++) {
		HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processes[i]);
		if (hProcess != nullptr) {
			TCHAR szProcessName[MAX_PATH];
			if (GetProcessImageFileName(hProcess, szProcessName, MAX_PATH) != 0) {
				std::wstring processPath = szProcessName;
				size_t pos = processPath.find_last_of(L"\\");
				if (pos != std::wstring::npos) {
					std::wstring exeName = processPath.substr(pos + 1);
					if (_wcsicmp(exeName.c_str(), processName.c_str()) == 0) {
						count++;
					}
				}
			}
			CloseHandle(hProcess);
		}
	}
#else
    
DIR *dir;
struct dirent *ent;
if ((dir = opendir("/proc")) != nullptr) {
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_type == DT_DIR) {
            // Directly use ent->d_name which is a narrow string
            std::string pid = ent->d_name;
            // Check if the pid directory name is all digits
            if (std::all_of(pid.begin(), pid.end(), ::isdigit)) {
                // Construct the narrow string path
                std::string path = "/proc/" + pid + "/comm";
                // Use std::ifstream with the narrow string path
                std::ifstream cmdline(path);
                if (cmdline.is_open()) {
                    std::string line;
                    std::getline(cmdline, line);
                    // Convert processName to a narrow string for comparison
                    std::string narrowProcessName(processName.begin(), processName.end());
                    if (line == narrowProcessName) {
                        count++;
                    }
                }
            }
        }
    }
    closedir(dir);
}
#endif

    return count;
}
//iamramking

void Config::Load()
{
	config_t *obsConfig = GetConfigStore();
	if (!obsConfig) {
		blog(LOG_ERROR, "[Config::Load] Unable to fetch OBS config!");
		return;
	}
//iamramking

#ifdef _WIN32
	std::wstring processName = L"MDC-DIPV2.exe";
#else
	std::wstring processName = L"MDC-DIPV2";
#endif
	int count = CountInstances(processName);
	if (count >= 0) {
		ServerPort = 4455 + count - 1;
		//std::wcout << L"Number of instances running: " << count << std::endl;
	} else {
		std::cerr << "Error counting instances." << std::endl;
	}

	//iamramking

	FirstLoad = config_get_bool(obsConfig, CONFIG_SECTION_NAME, PARAM_FIRSTLOAD);
	ServerEnabled = config_get_bool(obsConfig, CONFIG_SECTION_NAME, PARAM_ENABLED);
	AlertsEnabled = config_get_bool(obsConfig, CONFIG_SECTION_NAME, PARAM_ALERTS);
	//ServerPort = config_get_uint(obsConfig, CONFIG_SECTION_NAME, PARAM_PORT); //iamramking
	AuthRequired = config_get_bool(obsConfig, CONFIG_SECTION_NAME, PARAM_AUTHREQUIRED);
	ServerPassword = config_get_string(obsConfig, CONFIG_SECTION_NAME, PARAM_PASSWORD);

	// Set server password and save it to the config before processing overrides,
	// so that there is always a true configured password regardless of if
	// future loads use the override flag.
	if (FirstLoad) {
		FirstLoad = false;
		if (ServerPassword.isEmpty()) {
			blog(LOG_INFO, "[Config::Load] (FirstLoad) Generating new server password.");
			ServerPassword = QString::fromStdString(Utils::Crypto::GeneratePassword());
		} else {
			blog(LOG_INFO, "[Config::Load] (FirstLoad) Not generating new password since one is already configured.");
		}
		Save();
	}

	// Process `--websocket_port` override
	QString portArgument = Utils::Platform::GetCommandLineArgument(CMDLINE_WEBSOCKET_PORT);
	if (portArgument != "") {
		bool ok;
		uint16_t serverPort = portArgument.toUShort(&ok);
		if (ok) {
			blog(LOG_INFO, "[Config::Load] --websocket_port passed. Overriding WebSocket port with: %d", serverPort);
			PortOverridden = true;
			ServerPort = serverPort;
		} else {
			blog(LOG_WARNING, "[Config::Load] Not overriding WebSocket port since integer conversion failed.");
		}
	}

	// Process `--websocket_ipv4_only` override
	if (Utils::Platform::GetCommandLineFlagSet(CMDLINE_WEBSOCKET_IPV4_ONLY)) {
		blog(LOG_INFO, "[Config::Load] --websocket_ipv4_only passed. Binding only to IPv4 interfaces.");
		Ipv4Only = true;
	}

	// Process `--websocket_password` override
	QString passwordArgument = Utils::Platform::GetCommandLineArgument(CMDLINE_WEBSOCKET_PASSWORD);
	if (passwordArgument != "") {
		blog(LOG_INFO, "[Config::Load] --websocket_password passed. Overriding WebSocket password.");
		PasswordOverridden = true;
		AuthRequired = true;
		ServerPassword = passwordArgument;
	}

	// Process `--websocket_debug` override
	if (Utils::Platform::GetCommandLineFlagSet(CMDLINE_WEBSOCKET_DEBUG)) {
		// Debug does not persist on reload, so we let people override it with a flag.
		blog(LOG_INFO, "[Config::Load] --websocket_debug passed. Enabling debug logging.");
		DebugEnabled = true;
	}
}

void Config::Save()
{
	config_t *obsConfig = GetConfigStore();
	if (!obsConfig) {
		blog(LOG_ERROR, "[Config::Save] Unable to fetch OBS config!");
		return;
	}

	config_set_bool(obsConfig, CONFIG_SECTION_NAME, PARAM_FIRSTLOAD, FirstLoad);
	config_set_bool(obsConfig, CONFIG_SECTION_NAME, PARAM_ENABLED, ServerEnabled);
	if (!PortOverridden) {
		config_set_uint(obsConfig, CONFIG_SECTION_NAME, PARAM_PORT, ServerPort);
	}
	config_set_bool(obsConfig, CONFIG_SECTION_NAME, PARAM_ALERTS, AlertsEnabled);
	if (!PasswordOverridden) {
		config_set_bool(obsConfig, CONFIG_SECTION_NAME, PARAM_AUTHREQUIRED, AuthRequired);
		config_set_string(obsConfig, CONFIG_SECTION_NAME, PARAM_PASSWORD, QT_TO_UTF8(ServerPassword));
	}

	config_save(obsConfig);
}

void Config::SetDefaultsToGlobalStore()
{
	config_t *obsConfig = GetConfigStore();
	if (!obsConfig) {
		blog(LOG_ERROR, "[Config::SetDefaultsToGlobalStore] Unable to fetch OBS config!");
		return;
	}

	config_set_default_bool(obsConfig, CONFIG_SECTION_NAME, PARAM_FIRSTLOAD, FirstLoad);
	config_set_default_bool(obsConfig, CONFIG_SECTION_NAME, PARAM_ENABLED, ServerEnabled);
	config_set_default_uint(obsConfig, CONFIG_SECTION_NAME, PARAM_PORT, ServerPort);
	config_set_default_bool(obsConfig, CONFIG_SECTION_NAME, PARAM_ALERTS, AlertsEnabled);
	config_set_default_bool(obsConfig, CONFIG_SECTION_NAME, PARAM_AUTHREQUIRED, AuthRequired);
	config_set_default_string(obsConfig, CONFIG_SECTION_NAME, PARAM_PASSWORD, QT_TO_UTF8(ServerPassword));
}

config_t *Config::GetConfigStore()
{
	return obs_frontend_get_global_config();
}
