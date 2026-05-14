#pragma once

#include <QObject>
#include <QThread>
#include <QTimer>
#include <vector>
#include <map>
#include <set>
#include <QString>
#include "process_info.h"

#define NOMINMAX
#include <windows.h>
#include <dbt.h>
#include <netlistmgr.h>
#include <pdh.h>
#include "database.h"
#include "baseline_tracker.h"
#include "anomaly_engine.h"
#include "process_scanner.h"
#include "settings_manager.h"

// ─────────────────────── FsWorker ────────────────────────
class FsWorker : public QObject {
    Q_OBJECT
public:
    QStringList m_watchPaths; // set before starting thread
    void doWork();
signals:
    void fileEvent(const QString& time, const QString& event, const QString& msg);
};

// ─────────────────────── NetworkWorker ─────────────────────
struct NetworkConnection {
    QString protocol;
    QString localAddr;
    QString remoteAddr;
    QString state;
    DWORD pid;
};

class NetworkWorker : public QObject {
    Q_OBJECT
public:
    bool m_enableScanner = true;
    bool m_stop = false;
    QString m_pingServer = "8.8.8.8";
    int m_pingMaxLatency = 500;
    void doWork();
signals:
    void connectionsUpdated(const std::vector<NetworkConnection>& conns);
    void pingResult(int latency, double loss, bool isAnomaly);
    void networkEventLogged(const QString& time, const QString& event, const QString& details);
};

// ─────────────────────── RegistryWorker ────────────────────
struct StartupEntry {
    QString hive;   // "HKCU" or "HKLM"
    QString name;
    QString path;
};

class RegistryWorker : public QObject {
    Q_OBJECT
public:
    bool m_stop = false;
    void doWork();
signals:
    void startupChanged(const QString& action, const QString& name,
                        const QString& path, const QString& hive);
    void startupSnapshotReady(const std::vector<StartupEntry>& entries);
};

// ─────────────────────── Backend ─────────────────────────
class Backend : public QObject {
    Q_OBJECT
public:
    explicit Backend(Database* db, BaselineTracker* baseline, AnomalyEngine* anomalyEx, ProcessScanner* scanner, SettingsManager* settings, QObject *parent = nullptr);
    ~Backend();

    void startMonitoring();

    // Windows API Device Changes
    void handleDeviceChange(UINT eventType, DWORD_PTR data);

    // Process Actions
    bool suspendProcess(DWORD pid);
    bool resumeProcess(DWORD pid);
    bool terminateProcess(DWORD pid);

signals:
    void processesUpdated(const std::vector<ProcessData>& processes);
    void systemInfoUpdated(const SystemData& sysData);
    void connectionsUpdated(const std::vector<NetworkConnection>& conns);
    void startupSnapshotReady(const std::vector<StartupEntry>& entries);
    void startupEntryChanged(const QString& action, const QString& name,
                             const QString& path, const QString& hive);
    void processEventLogged(const QString& time, const QString& event, const QString& details);
    void systemEventLogged(const QString& time, const QString& event, const QString& details);
    void networkEventLogged(const QString& time, const QString& event, const QString& details);
    void fileSystemEventLogged(const QString& time, const QString& event, const QString& details);

private slots:
    void activeProcessLoop();
    void systemMonitorLoop();
    void processLogLoop();

private:
    QTimer* m_activeTimer;
    QTimer* m_sysTimer;
    QTimer* m_processLogTimer;

    Database* m_db;
    BaselineTracker* m_baseline;
    AnomalyEngine* m_anomalyEngine;
    ProcessScanner* m_scanner;
    SettingsManager* m_settings;

    // Per-process CPU tracking
    std::map<DWORD, ULONGLONG> m_lastCPUTime;
    ULONGLONG m_lastTotalSystemTimeForProcessCPU = 0;

    // System-wide CPU tracking (for system tab)
    ULONGLONG m_prevSysCpuIdle  = 0;
    ULONGLONG m_prevSysCpuTotal = 0;

    // Process logger state
    std::set<DWORD> m_previousProcessIds;

    QThread* m_fsThread;
    QThread* m_netThread;
    NetworkWorker* m_netWorker;
    QThread* m_regThread;
    RegistryWorker* m_regWorker;

    // GPU via PDH
    PDH_HQUERY   m_gpuQuery     = nullptr;
    PDH_HCOUNTER m_gpuCounter   = nullptr;
    bool         m_gpuReady     = false;
    void         InitGpuPdh();
    int          ReadGpuUsage();

    void EnablePrivilege(LPCTSTR lpszPrivilege);
    QString GetCurrentTimeString();
};

// ─────────────────────── Network COM ─────────────────────
class ModernNetworkEventsHandler : public INetworkListManagerEvents {
    LONG    m_cRef;
    Backend* m_backend;
public:
    ModernNetworkEventsHandler(Backend* b) : m_cRef(1), m_backend(b) {}
    ~ModernNetworkEventsHandler() {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override;
    ULONG   STDMETHODCALLTYPE AddRef()  override;
    ULONG   STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE ConnectivityChanged(NLM_CONNECTIVITY) override;
};
