#pragma once
#include <QString>
#include <windows.h>

struct ProcessData {
    DWORD pid = 0;
    QString name;
    double memUsageMB = 0.0;
    double cpuUsagePercent = 0.0;
};


struct SystemData {
    int cpuUsagePercent = 0;
    int ramUsagePercent = 0;
    int gpuUsagePercent = 0;   // ← new
    double totalRamMB  = 0.0;
    double availRamMB  = 0.0;
    double usedRamMB   = 0.0;
    double totalDiskGB = 0.0;
    double freeDiskGB  = 0.0;
};
