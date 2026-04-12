#pragma once
#define _CRT_SECURE_NO_WARNINGS

// C/C++ Standard Library
#include <map>
#include <mutex>
#include <string>
#include <set>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <vector>
#include <iomanip>

// Windows API
#include <windows.h>
#include <commctrl.h> // Common controls
#include <psapi.h>    // Process Info
#include <tlhelp32.h> // Process Snapshot
#include <pdh.h>      // Performance Data Helper
#include <pdhmsg.h>
#include <dbt.h>      // Device Change
#include <initguid.h>
#include <usbiodef.h> // GUID_DEVINTERFACE_USB_DEVICE
#include <netlistmgr.h> // Network List Manager
#include <shobjidl.h>

#define NUM_TABS 6

// UI Controls IDs
#define ID_TABCONTROL 0x1000
#define ID_LISTVIEW_BASE 0x1001 
#define ID_BUTTON_CLEAR 0x1010
#define ID_BUTTON_SAVE 0x1011

// Structures for Task Manager
struct ProcessInfo {
    std::wstring name;
    SIZE_T memUsage;
    double cpuUsage;
};

// Global variables export
extern const wchar_t* TAB_TITLES[NUM_TABS];
extern const wchar_t* LOG_FOLDERS[NUM_TABS];

extern HWND hTabControl;
extern HWND hListViews[NUM_TABS];
extern HWND hButtonClear;
extern HWND hButtonSave;

extern HWND hCPUProgress, hRAMProgress;
extern HWND hCPULabel, hRAMLabel;

extern int currentTab;

// Process Manager Shared State
extern std::mutex processMutex;
extern bool sortByRAM;
extern bool sortByCPU;
extern bool sortByName;
extern bool sortRAMAsc;
extern bool sortCPUAsc;
extern bool sortNameAsc;
extern DWORD selectedProcessId;

extern std::map<DWORD, ProcessInfo> processData;
extern std::map<DWORD, ULONGLONG> lastCPUTime;
extern ULONGLONG g_lastTotalSystemTimeForProcessCPU;
extern ULONGLONG g_lastIdleSystemTimeForProcessCPU;

// Loggers State
extern std::set<DWORD> previousProcessIds;
extern HDEVNOTIFY g_hDeviceNotify;
extern INetworkListManager* g_pNetListManager;
extern DWORD g_dwCookie;
extern HANDLE g_hDirToMonitor;
extern std::wstring g_monitorPath;

// Functions: GUI & Utils
std::wstring GetCurrentTimeString();
void ResizeControls(HWND hwnd);
void SwitchTab(int tabIndex);
void SaveListViewContent(HWND hListView, const wchar_t* folderName);

// Functions: Logging
void LogEvent(const wchar_t* folderName, const std::wstring& timeStr, const std::wstring& eventStr, const std::wstring& detailsStr);
void LogProcessEvent(const std::wstring& timeStr, const std::wstring& eventStr, const std::wstring& detailsStr);
void LogSystemEvent(const std::wstring& timeStr, const std::wstring& eventStr, const std::wstring& detailsStr);
void LogNetworkEvent(const std::wstring& timeStr, const std::wstring& eventStr, const std::wstring& detailsStr);
void LogFileSystemEvent(const std::wstring& timeStr, const std::wstring& eventStr, const std::wstring& detailsStr);

// Functions: Active Task Manager
void ActiveProcessMonitorThread();
void UpdateSystemInfoUI();
void EnablePrivilege(LPCTSTR lpszPrivilege);
double GetProcessCPUUsage(DWORD processID);

// Functions: Loggers Threads
void ProcessLogMonitorThread();
void MonitorFileSystemThread();
void HandleDeviceChange(WPARAM wParam, LPARAM lParam);
void HandlePowerBroadcast(WPARAM wParam, LPARAM lParam);

// Network COM interface handler
class CNetworkEventsHandler : public INetworkListManagerEvents {
    LONG m_cRef;
public:
    CNetworkEventsHandler() : m_cRef(1) {}
    ~CNetworkEventsHandler() {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (riid == IID_IUnknown || riid == IID_INetworkListManagerEvents) {
            *ppvObject = static_cast<INetworkListManagerEvents*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_cRef);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG lRef = InterlockedDecrement(&m_cRef);
        if (lRef == 0) delete this;
        return lRef;
    }

    HRESULT STDMETHODCALLTYPE ConnectivityChanged(NLM_CONNECTIVITY newConnectivity) override;
};
