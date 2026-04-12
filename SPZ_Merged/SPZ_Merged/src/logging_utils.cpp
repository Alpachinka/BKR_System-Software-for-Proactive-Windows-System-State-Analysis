#include "../include/GlobalData.h"

// Unified event log to ListView (Insert at top) and save to file optionally if needed in future
void LogEvent(const wchar_t* folderName, const std::wstring& timeStr, const std::wstring& eventStr, const std::wstring& detailsStr, int tabIndex) {
    if (tabIndex < 2 || tabIndex >= NUM_TABS || !hListViews[tabIndex]) return;

    HWND hList = hListViews[tabIndex];

    LVITEMW lvI = { 0 };
    lvI.mask = LVIF_TEXT;
    lvI.iItem = 0; // Insert at the top
    lvI.iSubItem = 0;
    lvI.pszText = (LPWSTR)timeStr.c_str();

    int index = ListView_InsertItem(hList, &lvI);

    if (index >= 0) {
        ListView_SetItemText(hList, index, 1, (LPWSTR)eventStr.c_str());
        ListView_SetItemText(hList, index, 2, (LPWSTR)detailsStr.c_str());
    }
}

void LogProcessEvent(const std::wstring& timeStr, const std::wstring& eventStr, const std::wstring& detailsStr) {
    LogEvent(LOG_FOLDERS[2], timeStr, eventStr, detailsStr, 2);
}

void LogSystemEvent(const std::wstring& timeStr, const std::wstring& eventStr, const std::wstring& detailsStr) {
    LogEvent(LOG_FOLDERS[3], timeStr, eventStr, detailsStr, 3);
}

void LogNetworkEvent(const std::wstring& timeStr, const std::wstring& eventStr, const std::wstring& detailsStr) {
    LogEvent(LOG_FOLDERS[4], timeStr, eventStr, detailsStr, 4);
}

void LogFileSystemEvent(const std::wstring& timeStr, const std::wstring& eventStr, const std::wstring& detailsStr) {
    LogEvent(LOG_FOLDERS[5], timeStr, eventStr, detailsStr, 5);
}

void SaveListViewContent(HWND hListView, const wchar_t* folderName) {
    // Create "Logs" directory
    std::filesystem::path logsDir = "Logs";
    if (!std::filesystem::exists(logsDir)) {
        std::filesystem::create_directory(logsDir);
    }

    // Create specific folder
    std::filesystem::path specificDir = logsDir / folderName;
    if (!std::filesystem::exists(specificDir)) {
        std::filesystem::create_directory(specificDir);
    }

    // Generate filename based on timestamp
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm = *std::localtime(&now_c);
    
    std::wstringstream filenameStream;
    filenameStream << specificDir.wstring() << L"\\Log_" 
                   << std::put_time(&now_tm, L"%Y%m%d_%H%M%S") << L".csv";

    std::wofstream outFile(filenameStream.str());
    outFile.imbue(std::locale(""));

    if (outFile.is_open()) {
        outFile << L"Час,Подія,Деталі\n"; // Header
        int itemCount = ListView_GetItemCount(hListView);
        for (int i = 0; i < itemCount; ++i) {
            wchar_t wTime[256], wEvent[256], wDetails[1024];

            ListView_GetItemText(hListView, i, 0, wTime, sizeof(wTime) / sizeof(wchar_t));
            ListView_GetItemText(hListView, i, 1, wEvent, sizeof(wEvent) / sizeof(wchar_t));
            ListView_GetItemText(hListView, i, 2, wDetails, sizeof(wDetails) / sizeof(wchar_t));

            // Quote strings to handle commas in CSV
            outFile << L"\"" << wTime << L"\",\"" << wEvent << L"\",\"" << wDetails << L"\"\n";
        }
        outFile.close();
        
        MessageBoxW(NULL, (std::wstring(L"Логи збережено у: ") + filenameStream.str()).c_str(), L"Успіх", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(NULL, L"Не вдалося зберегти логи.", L"Помилка", MB_OK | MB_ICONERROR);
    }
}
