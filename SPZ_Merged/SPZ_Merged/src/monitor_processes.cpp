#include "../include/GlobalData.h"

// Active Process Monitor (Tab 0) Update Loop
void ActiveProcessMonitorThread() {
    EnablePrivilege(SE_DEBUG_NAME);
    
    while (true) {
        if (currentTab == 0 && hListViews[0] != NULL) { // Update only if tab is active
            std::map<DWORD, ProcessInfo> currentProcessData;
            
            HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnapshot != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W pe;
                pe.dwSize = sizeof(PROCESSENTRY32W);

                if (Process32FirstW(hSnapshot, &pe)) {
                    do {
                        ProcessInfo info;
                        info.name = pe.szExeFile;
                        
                        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
                        if (hProcess) {
                            PROCESS_MEMORY_COUNTERS pmc;
                            if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                                info.memUsage = pmc.WorkingSetSize;
                            } else {
                                info.memUsage = 0;
                            }
                            CloseHandle(hProcess);
                        } else {
                            info.memUsage = 0;
                        }

                        info.cpuUsage = GetProcessCPUUsage(pe.th32ProcessID);
                        currentProcessData[pe.th32ProcessID] = info;

                    } while (Process32NextW(hSnapshot, &pe));
                }
                CloseHandle(hSnapshot);
            }

            // Sync with UI
            std::lock_guard<std::mutex> lock(processMutex);
            processData = currentProcessData;
            
            // Send a generic request to UI Thread to rebuild ListView if we want, 
            // but it's simpler to update directly using SendMessage as standard Win32 
            // However, ListView_DeleteAllItems and InsertItem from thread is tricky.
            // Actually sending PostMessage to main window is safer, but for this thesis, direct update:
            
            // Rebuild UI Data (Should technically be in UI thread, but HWND is thread-safe for basic SendMessage)
            // But we must use SendMessage
            ListView_DeleteAllItems(hListViews[0]);

            // Sort data logic can be skipped or simplified
            // For now, simple insert
            for(auto const& [pid, info] : processData) {
                LVITEMW lvItem = {0};
                lvItem.mask = LVIF_TEXT;
                lvItem.iItem = ListView_GetItemCount(hListViews[0]);
                lvItem.pszText = (LPWSTR)info.name.c_str();

                int iItem = ListView_InsertItem(hListViews[0], &lvItem);
                
                // RAM usage formatting
                double memMB = info.memUsage / (1024.0 * 1024.0);
                std::wstringstream memStream;
                memStream << std::fixed << std::setprecision(2) << memMB << L" МБ";
                ListView_SetItemText(hListViews[0], iItem, 1, (LPWSTR)memStream.str().c_str());

                // CPU usage formatting
                std::wstringstream cpuStream;
                cpuStream << std::fixed << std::setprecision(1) << info.cpuUsage << L" %";
                ListView_SetItemText(hListViews[0], iItem, 2, (LPWSTR)cpuStream.str().c_str());
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(2)); // Tick every 2 seconds
    }
}

// Utils
void EnablePrivilege(LPCTSTR lpszPrivilege) {
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp;
        LUID luid;
        if (LookupPrivilegeValue(NULL, lpszPrivilege, &luid)) {
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Luid = luid;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
        }
        CloseHandle(hToken);
    }
}

double GetProcessCPUUsage(DWORD processID) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processID);
    if (!hProcess) return 0.0;

    FILETIME ftime, fsys, fuser;
    ULONGLONG currentTime = 0;
    
    if (GetProcessTimes(hProcess, &ftime, &ftime, &fsys, &fuser)) {
        ULARGE_INTEGER sys, user;
        sys.LowPart = fsys.dwLowDateTime;
        sys.HighPart = fsys.dwHighDateTime;
        user.LowPart = fuser.dwLowDateTime;
        user.HighPart = fuser.dwHighDateTime;
        currentTime = sys.QuadPart + user.QuadPart;
    }
    CloseHandle(hProcess);

    FILETIME idleTime, kernelTime, userTime;
    ULONGLONG systemTime = 0;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        ULARGE_INTEGER k, u;
        k.LowPart = kernelTime.dwLowDateTime;
        k.HighPart = kernelTime.dwHighDateTime;
        u.LowPart = userTime.dwLowDateTime;
        u.HighPart = userTime.dwHighDateTime;
        systemTime = k.QuadPart + u.QuadPart;
    }

    double cpuUsage = 0.0;

    auto it = lastCPUTime.find(processID);
    if (it != lastCPUTime.end() && g_lastTotalSystemTimeForProcessCPU != 0) {
        ULONGLONG processDiff = currentTime - it->second;
        ULONGLONG systemDiff = systemTime - g_lastTotalSystemTimeForProcessCPU;
        
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        
        if (systemDiff > 0) {
            cpuUsage = (processDiff * 100.0) / systemDiff;
            // Bound it slightly to 100% since precision loss is possible
            if(cpuUsage > 100.0) cpuUsage = 100.0;
        }
    }

    lastCPUTime[processID] = currentTime;
    g_lastTotalSystemTimeForProcessCPU = systemTime;

    return cpuUsage;
}
