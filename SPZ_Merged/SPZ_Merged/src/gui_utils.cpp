#include "../include/GlobalData.h"

// Resizes and repositions all GUI controls
void ResizeControls(HWND hwnd) {
    if (!hTabControl) return;

    RECT rcClient;
    GetClientRect(hwnd, &rcClient);

    int buttonWidth = 100;
    int buttonHeight = 30;
    int spacing = 10;
    int controlAreaHeight = buttonHeight + spacing * 2;

    SetWindowPos(hTabControl, NULL, 0, 0, rcClient.right, rcClient.bottom - controlAreaHeight, SWP_NOZORDER);

    int listX = 10;
    int listY = 40;
    int listW = rcClient.right - 20;
    int listH = rcClient.bottom - controlAreaHeight - 50;

    for (int i = 0; i < NUM_TABS; ++i) {
        if (hListViews[i]) {
            SetWindowPos(hListViews[i], NULL, listX, listY, listW, listH, SWP_NOZORDER);
        }
    }

    // Special layout for Tab 1 (System Info) - leave space for progress bars
    if (currentTab == 1 && hListViews[1]) {
        int listHSys = 300; 
        SetWindowPos(hListViews[1], NULL, listX, listY, listW, listHSys, SWP_NOZORDER);

        SetWindowPos(hCPULabel, NULL, listX, listY + listHSys + 20, 400, 20, SWP_NOZORDER);
        SetWindowPos(hCPUProgress, NULL, listX, listY + listHSys + 40, listW, 20, SWP_NOZORDER);
        
        SetWindowPos(hRAMLabel, NULL, listX, listY + listHSys + 70, 400, 20, SWP_NOZORDER);
        SetWindowPos(hRAMProgress, NULL, listX, listY + listHSys + 90, listW, 20, SWP_NOZORDER);
    }

    // Buttons at the bottom
    int btnY = rcClient.bottom - controlAreaHeight + spacing;
    if (hButtonClear) SetWindowPos(hButtonClear, NULL, rcClient.right - 2 * buttonWidth - 2 * spacing, btnY, buttonWidth, buttonHeight, SWP_NOZORDER);
    if (hButtonSave) SetWindowPos(hButtonSave, NULL, rcClient.right - buttonWidth - spacing, btnY, buttonWidth, buttonHeight, SWP_NOZORDER);
}

// Switches visible ListViews depending on Tab
void SwitchTab(int tabIndex) {
    currentTab = tabIndex;
    
    // Hide all
    for (int i = 0; i < NUM_TABS; ++i) {
        if (hListViews[i]) ShowWindow(hListViews[i], SW_HIDE);
    }
    if (hCPULabel) ShowWindow(hCPULabel, SW_HIDE);
    if (hCPUProgress) ShowWindow(hCPUProgress, SW_HIDE);
    if (hRAMLabel) ShowWindow(hRAMLabel, SW_HIDE);
    if (hRAMProgress) ShowWindow(hRAMProgress, SW_HIDE);

    // Show related
    if (tabIndex >= 0 && tabIndex < NUM_TABS && hListViews[tabIndex]) {
        ShowWindow(hListViews[tabIndex], SW_SHOW);
    }

    if (tabIndex == 1) { // System Info
        if (hCPULabel) ShowWindow(hCPULabel, SW_SHOW);
        if (hCPUProgress) ShowWindow(hCPUProgress, SW_SHOW);
        if (hRAMLabel) ShowWindow(hRAMLabel, SW_SHOW);
        if (hRAMProgress) ShowWindow(hRAMProgress, SW_SHOW);
    }

    // Buttons are only relevant for Logs (Tabs 2-5)
    if (tabIndex >= 2) {
        if (hButtonClear) ShowWindow(hButtonClear, SW_SHOW);
        if (hButtonSave) ShowWindow(hButtonSave, SW_SHOW);
    } else {
        if (hButtonClear) ShowWindow(hButtonClear, SW_HIDE);
        if (hButtonSave) ShowWindow(hButtonSave, SW_HIDE);
    }

    ResizeControls(GetParent(hTabControl));
}

std::wstring GetCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm = *std::localtime(&now_c);

    std::wstringstream wss;
    wss << std::put_time(&now_tm, L"%Y-%m-%d %H:%M:%S");
    return wss.str();
}
