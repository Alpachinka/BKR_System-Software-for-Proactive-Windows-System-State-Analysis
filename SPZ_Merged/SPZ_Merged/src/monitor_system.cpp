#include "../include/GlobalData.h"

// System Monitor (Tab 1) UI Update 
void UpdateSystemInfoUI() {
    if (currentTab != 1 || hListViews[1] == NULL) return;

    HWND hList = hListViews[1];
    ListView_DeleteAllItems(hList);

    // 1. RAM Usage
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);

    DWORD totalRAM = (DWORD)(memInfo.ullTotalPhys / (1024 * 1024));
    DWORD availRAM = (DWORD)(memInfo.ullAvailPhys / (1024 * 1024));
    DWORD usedRAM = totalRAM - availRAM;
    int ramPercent = (int)memInfo.dwMemoryLoad;

    std::wstringstream totalRamStr, availRamStr, usedRamStr;
    totalRamStr << totalRAM << L" МБ";
    availRamStr << availRAM << L" МБ";
    usedRamStr << usedRAM << L" МБ (" << ramPercent << L"%)";

    auto insertItem = [&](int index, const wchar_t* param, const std::wstring& val) {
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = index;
        lvi.pszText = (LPWSTR)param;
        ListView_InsertItem(hList, &lvi);
        ListView_SetItemText(hList, index, 1, (LPWSTR)val.c_str());
    };

    insertItem(0, L"Загальна фіз. пам'ять (RAM)", totalRamStr.str());
    insertItem(1, L"Доступна фіз. пам'ять", availRamStr.str());
    insertItem(2, L"Використана пам'ять", usedRamStr.str());

    SendMessage(hRAMProgress, PBM_SETPOS, ramPercent, 0);

    // 2. Total CPU Usage (Using SystemTimes)
    static ULONGLONG prevIdle = 0, prevTotal = 0;
    FILETIME idleTime, kernelTime, userTime;

    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        ULARGE_INTEGER idle, kernel, user;
        idle.LowPart = idleTime.dwLowDateTime; idle.HighPart = idleTime.dwHighDateTime;
        kernel.LowPart = kernelTime.dwLowDateTime; kernel.HighPart = kernelTime.dwHighDateTime;
        user.LowPart = userTime.dwLowDateTime; user.HighPart = userTime.dwHighDateTime;

        ULONGLONG currentIdle = idle.QuadPart;
        ULONGLONG currentTotal = kernel.QuadPart + user.QuadPart;

        if (prevTotal != 0) {
            ULONGLONG diffIdle = currentIdle - prevIdle;
            ULONGLONG diffTotal = currentTotal - prevTotal;

            int cpuPercent = 0;
            if (diffTotal > 0) {
                cpuPercent = (int)(100.0 - (diffIdle * 100.0) / diffTotal);
            }
            if (cpuPercent < 0) cpuPercent = 0;
            if (cpuPercent > 100) cpuPercent = 100;

            std::wstringstream cpuStr;
            cpuStr << cpuPercent << L" %";
            insertItem(3, L"Навантаження процесора (CPU)", cpuStr.str());
            SendMessage(hCPUProgress, PBM_SETPOS, cpuPercent, 0);
        }

        prevIdle = currentIdle;
        prevTotal = currentTotal;
    }

    // 3. System Storage Info (C: Drive)
    ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
    if (GetDiskFreeSpaceExW(L"C:\\", &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
        DWORD totalDisk = (DWORD)(totalNumberOfBytes.QuadPart / (1024 * 1024 * 1024));
        DWORD freeDisk = (DWORD)(totalNumberOfFreeBytes.QuadPart / (1024 * 1024 * 1024));
        DWORD usedDisk = totalDisk - freeDisk;

        std::wstringstream diskStr;
        diskStr << L"Всього: " << totalDisk << L" ГБ, Вільно: " << freeDisk << L" ГБ";
        insertItem(4, L"Пам'ять диску C:", diskStr.str());
    }
}
