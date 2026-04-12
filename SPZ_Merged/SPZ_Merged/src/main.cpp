#include "../include/GlobalData.h"

// Define Global Variables
HWND hTabControl = NULL;
HWND hListViews[NUM_TABS] = { NULL };
HWND hButtonClear = NULL;
HWND hButtonSave = NULL;

HWND hCPUProgress = NULL;
HWND hRAMProgress = NULL;
HWND hCPULabel = NULL;
HWND hRAMLabel = NULL;

int currentTab = 0;

// Shared state for Process Manager
std::mutex processMutex;
bool sortByRAM = false;
bool sortByCPU = false;
bool sortByName = true;
bool sortRAMAsc = false;
bool sortCPUAsc = false;
bool sortNameAsc = true;
DWORD selectedProcessId = 0;

std::map<DWORD, ProcessInfo> processData;
std::map<DWORD, ULONGLONG> lastCPUTime;
ULONGLONG g_lastTotalSystemTimeForProcessCPU = 0;
ULONGLONG g_lastIdleSystemTimeForProcessCPU = 0;

// Shared state for Loggers
std::set<DWORD> previousProcessIds;
HDEVNOTIFY g_hDeviceNotify = NULL;
INetworkListManager* g_pNetListManager = NULL;
DWORD g_dwCookie = 0;
HANDLE g_hDirToMonitor = INVALID_HANDLE_VALUE;
std::wstring g_monitorPath;

const wchar_t* TAB_TITLES[NUM_TABS] = {
    L"Процеси (Активні)", L"Системні Ресурси", L"Лог Процесів", L"Лог Системи", L"Лог Мережі", L"Лог Файлів"
};

const wchar_t* LOG_FOLDERS[NUM_TABS] = {
    L"ProcessActivity", L"SystemInfo", L"ProcessLogs", L"SystemLogs", L"NetworkLogs", L"FileLogs"
};

HWND CreateLogListView(HWND hwndParent, int id) {
    HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, hwndParent, (HMENU)id, GetModuleHandle(NULL), NULL);

    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW lvc = { 0 };
    lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

    lvc.cx = 150;
    lvc.pszText = (LPWSTR)L"Час";
    ListView_InsertColumn(hList, 0, &lvc);

    lvc.cx = 200;
    lvc.pszText = (LPWSTR)L"Подія";
    ListView_InsertColumn(hList, 1, &lvc);

    lvc.cx = 400;
    lvc.pszText = (LPWSTR)L"Деталі";
    ListView_InsertColumn(hList, 2, &lvc);

    return hList;
}

HWND CreateProcessListView(HWND hwndParent, int id) {
    HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        0, 0, 0, 0, hwndParent, (HMENU)id, GetModuleHandle(NULL), NULL);

    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW lvc = { 0 };
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    
    lvc.cx = 250;
    lvc.pszText = (LPWSTR)L"Ім'я процесу";
    ListView_InsertColumn(hList, 0, &lvc);

    lvc.cx = 150;
    lvc.pszText = (LPWSTR)L"Пам'ять (RAM)";
    ListView_InsertColumn(hList, 1, &lvc);

    lvc.cx = 150;
    lvc.pszText = (LPWSTR)L"CPU (%)";
    ListView_InsertColumn(hList, 2, &lvc);

    return hList;
}

HWND CreateSystemListView(HWND hwndParent, int id) {
    HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        0, 0, 0, 0, hwndParent, (HMENU)id, GetModuleHandle(NULL), NULL);

    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW lvc = { 0 };
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;

    lvc.cx = 200;
    lvc.pszText = (LPWSTR)L"Параметр";
    ListView_InsertColumn(hList, 0, &lvc);

    lvc.cx = 560;
    lvc.pszText = (LPWSTR)L"Значення";
    ListView_InsertColumn(hList, 1, &lvc);

    return hList;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
    {
        InitCommonControls();

        HRESULT hr_local = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        
        hTabControl = CreateWindowW(WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, 0, 0, hwnd, (HMENU)ID_TABCONTROL, GetModuleHandle(NULL), NULL);

        for (int i = 0; i < NUM_TABS; ++i) {
            TCITEMW tie = { 0 };
            tie.mask = TCIF_TEXT;
            tie.pszText = (LPWSTR)TAB_TITLES[i];
            TabCtrl_InsertItem(hTabControl, i, &tie);
        }

        // Tab 0: Process ListView
        hListViews[0] = CreateProcessListView(hwnd, ID_LISTVIEW_BASE + 0);
        // Tab 1: System Info ListView
        hListViews[1] = CreateSystemListView(hwnd, ID_LISTVIEW_BASE + 1);
        // Tabs 2-5: Log ListViews
        for (int i = 2; i < NUM_TABS; ++i) {
            hListViews[i] = CreateLogListView(hwnd, ID_LISTVIEW_BASE + i);
        }

        // Progress Bars for System Info (Tab 1)
        hCPULabel = CreateWindowW(L"STATIC", L"Використання CPU:", WS_CHILD,
            0, 0, 0, 0, hwnd, NULL, GetModuleHandle(NULL), NULL);
        hCPUProgress = CreateWindowExW(0, PROGRESS_CLASSW, NULL,
            WS_CHILD | PBS_SMOOTH,
            0, 0, 0, 0, hwnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(hCPUProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessage(hCPUProgress, PBM_SETSTEP, 1, 0);

        hRAMLabel = CreateWindowW(L"STATIC", L"Використання RAM:", WS_CHILD,
            0, 0, 0, 0, hwnd, NULL, GetModuleHandle(NULL), NULL);
        hRAMProgress = CreateWindowExW(0, PROGRESS_CLASSW, NULL,
            WS_CHILD | PBS_SMOOTH,
            0, 0, 0, 0, hwnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(hRAMProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessage(hRAMProgress, PBM_SETSTEP, 1, 0);

        // Buttons
        hButtonClear = CreateWindowW(L"BUTTON", L"Очистити", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)ID_BUTTON_CLEAR, GetModuleHandle(NULL), NULL);
        hButtonSave = CreateWindowW(L"BUTTON", L"Зберегти", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)ID_BUTTON_SAVE, GetModuleHandle(NULL), NULL);

        ResizeControls(hwnd);
        SwitchTab(0);

        // Subscriptions for events
        DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
        ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
        NotificationFilter.dbcc_size = sizeof(NotificationFilter);
        NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE; 
        g_hDeviceNotify = RegisterDeviceNotification(hwnd, &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

        hr_local = CoCreateInstance(CLSID_NetworkListManager, NULL, CLSCTX_ALL, IID_INetworkListManager, (void**)&g_pNetListManager);
        if (SUCCEEDED(hr_local)) {
            CNetworkEventsHandler* pNetEventsHandler = new CNetworkEventsHandler();
            if (pNetEventsHandler) {
                IConnectionPointContainer* pCPC = NULL;
                if (SUCCEEDED(g_pNetListManager->QueryInterface(IID_IConnectionPointContainer, (void**)&pCPC))) {
                    IConnectionPoint* pCP = NULL;
                    if (SUCCEEDED(pCPC->FindConnectionPoint(IID_INetworkListManagerEvents, &pCP))) {
                        pCP->Advise(static_cast<INetworkListManagerEvents*>(pNetEventsHandler), &g_dwCookie);
                        pCP->Release();
                    }
                    pCPC->Release();
                }
            }
        }

        // Start Monitors (Threads)
        std::thread(ActiveProcessMonitorThread).detach();
        std::thread(ProcessLogMonitorThread).detach();
        std::thread(MonitorFileSystemThread).detach();

        // System update timer for Tab 1
        SetTimer(hwnd, 1, 1000, NULL); 
        break;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BUTTON_CLEAR) {
            ListView_DeleteAllItems(hListViews[currentTab]);
        }
        else if (LOWORD(wParam) == ID_BUTTON_SAVE) {
            SaveListViewContent(hListViews[currentTab], LOG_FOLDERS[currentTab]);
        }
        break;

    case WM_TIMER:
        if (wParam == 1 && currentTab == 1) {
            UpdateSystemInfoUI(); // Update UI in main thread using data
        }
        break;

    case WM_NOTIFY:
    {
        LPNMHDR lpnmh = (LPNMHDR)lParam;
        if (lpnmh->idFrom == ID_TABCONTROL && lpnmh->code == TCN_SELCHANGE) {
            int iSelTab = TabCtrl_GetCurSel(hTabControl);
            if (iSelTab != -1) SwitchTab(iSelTab);
        }
        break;
    }

    case WM_SIZE:
        ResizeControls(hwnd);
        break;

    case WM_DEVICECHANGE:
        HandleDeviceChange(wParam, lParam);
        break;

    case WM_POWERBROADCAST:
        HandlePowerBroadcast(wParam, lParam);
        break;

    case WM_DESTROY:
        if (g_hDeviceNotify != NULL) UnregisterDeviceNotification(g_hDeviceNotify);
        if (g_pNetListManager) {
            IConnectionPointContainer* pCPC = NULL;
            if (SUCCEEDED(g_pNetListManager->QueryInterface(IID_IConnectionPointContainer, (void**)&pCPC))) {
                IConnectionPoint* pCP = NULL;
                if (SUCCEEDED(pCPC->FindConnectionPoint(IID_INetworkListManagerEvents, &pCP))) {
                    if (g_dwCookie != 0) pCP->Unadvise(g_dwCookie);
                    pCP->Release();
                }
                pCPC->Release();
            }
            g_pNetListManager->Release();
        }
        CoUninitialize();
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"SPZMergedClass";

    WNDCLASSW wc = { };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    if (!RegisterClassW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"SPZ_Merged: Диспетчер та Моніторинг", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 700,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = { };
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
