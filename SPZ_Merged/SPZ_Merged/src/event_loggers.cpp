#include "../include/GlobalData.h"

// 1. Process Logger Thread
void ProcessLogMonitorThread() {
    while (true) {
        std::set<DWORD> currentProcessIds;
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        
        if (hSnapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(PROCESSENTRY32W);

            if (Process32FirstW(hSnapshot, &pe)) {
                do {
                    currentProcessIds.insert(pe.th32ProcessID);
                    if (previousProcessIds.find(pe.th32ProcessID) == previousProcessIds.end() && !previousProcessIds.empty()) {
                        LogProcessEvent(GetCurrentTimeString(), L"Запуск процесу", pe.szExeFile);
                    }
                } while (Process32NextW(hSnapshot, &pe));
            }
            CloseHandle(hSnapshot);
        }

        if (!previousProcessIds.empty()) {
            for (DWORD pid : previousProcessIds) {
                if (currentProcessIds.find(pid) == currentProcessIds.end()) {
                    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
                    wchar_t name[MAX_PATH] = L"<Невідомий процес>";
                    if (hProcess) {
                        HMODULE hMod;
                        DWORD cbNeeded;
                        if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded)) {
                            GetModuleBaseNameW(hProcess, hMod, name, sizeof(name) / sizeof(wchar_t));
                        }
                        CloseHandle(hProcess);
                    }
                    LogProcessEvent(GetCurrentTimeString(), L"Завершення процесу", name);
                }
            }
        }
        previousProcessIds = currentProcessIds;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

// 2. File System Logger Thread
void MonitorFileSystemThread() {
    g_monitorPath = L"C:\\Users"; // By default monitor Users dir to avoid too many events on entire C:
    
    g_hDirToMonitor = FindFirstChangeNotificationW(
        g_monitorPath.c_str(),
        TRUE, 
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE
    );

    if (g_hDirToMonitor == INVALID_HANDLE_VALUE) return;

    while (true) {
        DWORD dwWaitStatus = WaitForSingleObject(g_hDirToMonitor, INFINITE);

        if (dwWaitStatus == WAIT_OBJECT_0) {
            LogFileSystemEvent(GetCurrentTimeString(), L"Зміна у файлах", L"Виявлено зміну в директорії: " + g_monitorPath);
            FindNextChangeNotification(g_hDirToMonitor);
        } else {
            break; 
        }
    }
}

// 3. Device Connect/Disconnect Handler (System Logs)
void HandleDeviceChange(WPARAM wParam, LPARAM lParam) {
    if (wParam == DBT_DEVICEARRIVAL) {
        PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
        if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
            LogSystemEvent(GetCurrentTimeString(), L"Пристрій підключено", L"USB-пристрій був підключений до системи.");
        }
    }
    else if (wParam == DBT_DEVICEREMOVECOMPLETE) {
        PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
        if (pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
            LogSystemEvent(GetCurrentTimeString(), L"Пристрій відключено", L"USB-пристрій був відключений від системи.");
        }
    }
}

// 4. Power State Broadcast (System Logs)
void HandlePowerBroadcast(WPARAM wParam, LPARAM lParam) {
    if (wParam == PBT_APMPOWERSTATUSCHANGE) {
        SYSTEM_POWER_STATUS sps;
        if (GetSystemPowerStatus(&sps)) {
            std::wstring powerState = (sps.ACLineStatus == 1) ? L"Підключено до мережі" : L"Робота від батареї";
            LogSystemEvent(GetCurrentTimeString(), L"Зміна живлення", powerState);
        }
    } else if (wParam == PBT_APMSUSPEND) {
        LogSystemEvent(GetCurrentTimeString(), L"Сплячий режим", L"Система переходить у сплячий режим.");
    } else if (wParam == PBT_APMRESUMESUSPEND) {
        LogSystemEvent(GetCurrentTimeString(), L"Відновлення", L"Система вийшла зі сплячого режиму.");
    }
}

// 5. Network Connectivity Event (Network Logs)
HRESULT STDMETHODCALLTYPE CNetworkEventsHandler::ConnectivityChanged(NLM_CONNECTIVITY newConnectivity) {
    std::wstring statusStr = L"Невідомо";
    
    if (newConnectivity == NLM_CONNECTIVITY_DISCONNECTED) {
        statusStr = L"Відключено від мережі";
    } else if (newConnectivity & NLM_CONNECTIVITY_IPV4_INTERNET || newConnectivity & NLM_CONNECTIVITY_IPV6_INTERNET) {
        statusStr = L"Підключено до Інтернету";
    } else if (newConnectivity & NLM_CONNECTIVITY_IPV4_LOCALNETWORK || newConnectivity & NLM_CONNECTIVITY_IPV6_LOCALNETWORK) {
        statusStr = L"Локальна мережа (Без доступу до Інтернет)";
    }

    LogNetworkEvent(GetCurrentTimeString(), L"Зміна мережі", statusStr);
    return S_OK;
}
