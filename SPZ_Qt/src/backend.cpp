#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include "backend.h"
#include <QDateTime>
#include <QDebug>
#include <psapi.h>
#include <tlhelp32.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

// ───────────────────────────── helpers ─────────────────────────────

static ULONGLONG FileTimeToULL(const FILETIME& ft)
{
    ULARGE_INTEGER uli;
    uli.LowPart  = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

// ───────────────────────────── Backend ─────────────────────────────

Backend::Backend(Database* db, BaselineTracker* baseline, AnomalyEngine* anomalyEx, ProcessScanner* scanner, SettingsManager* settings, QObject *parent)
    : QObject(parent), m_db(db), m_baseline(baseline), m_anomalyEngine(anomalyEx), m_scanner(scanner), m_settings(settings)
{
    m_activeTimer = new QTimer(this);
    connect(m_activeTimer, &QTimer::timeout, this, &Backend::activeProcessLoop);

    m_sysTimer = new QTimer(this);
    connect(m_sysTimer, &QTimer::timeout, this, &Backend::systemMonitorLoop);

    m_processLogTimer = new QTimer(this);
    connect(m_processLogTimer, &QTimer::timeout, this, &Backend::processLogLoop);

    // File-system watcher on a separate thread
    m_fsThread = new QThread(this);
    FsWorker* worker = new FsWorker();
    worker->m_watchPaths = settings->watchedFolders;
    worker->moveToThread(m_fsThread);

    connect(m_fsThread, &QThread::started, worker, &FsWorker::doWork);
    connect(worker, &FsWorker::fileEvent, this,
            [this](const QString& t, const QString& e, const QString& msg) {
                emit fileSystemEventLogged(t, e, msg);
                // Also send to Anomaly Engine for Ransomware heuristic
                if (m_anomalyEngine) {
                    m_anomalyEngine->analyzeFileSystemEvent(msg);
                }
            });

    // Network watcher on a separate thread
    m_netThread = new QThread(this);
    m_netWorker = new NetworkWorker();
    m_netWorker->m_enableScanner = settings->enableNetworkScanner;
    m_netWorker->m_pingServer = settings->pingServer;
    m_netWorker->m_pingMaxLatency = settings->pingMaxLatency;
    m_netWorker->moveToThread(m_netThread);

    connect(m_netThread, &QThread::started, m_netWorker, &NetworkWorker::doWork);
    connect(m_netWorker, &NetworkWorker::networkEventLogged, this,
            [this](const QString& t, const QString& e, const QString& msg) {
                emit networkEventLogged(t, e, msg);
            });
    connect(m_netWorker, &NetworkWorker::connectionsUpdated, this, &Backend::connectionsUpdated);

    // Registry watcher on a separate thread
    m_regThread = new QThread(this);
    m_regWorker = new RegistryWorker();
    m_regWorker->moveToThread(m_regThread);

    connect(m_regThread, &QThread::started, m_regWorker, &RegistryWorker::doWork);
    connect(m_regWorker, &RegistryWorker::startupChanged, this,
            [this](const QString& action, const QString& name, const QString& path, const QString& hive) {
                // Forward to UI slot
                emit startupEntryChanged(action, name, path, hive);
                // Also write to system log
                QString details = QString("[%1] %2\nШлях: %3").arg(hive, name, path);
                emit systemEventLogged(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"),
                                       "Автозавантаження: " + action, details);
            });
    connect(m_regWorker, &RegistryWorker::startupSnapshotReady,
            this, &Backend::startupSnapshotReady);
}

Backend::~Backend()
{
    m_fsThread->quit();
    m_fsThread->wait();
    
    m_netWorker->m_stop = true;
    m_netThread->quit();
    m_netThread->wait();

    m_regWorker->m_stop = true;
    m_regThread->quit();
    m_regThread->wait();
}

void Backend::startMonitoring()
{
    EnablePrivilege(SE_DEBUG_NAME);
    InitGpuPdh();
    int interval = m_settings->refreshIntervalMs;
    m_activeTimer->start(interval);
    m_sysTimer->start(interval);
    m_processLogTimer->start(interval);
    m_fsThread->start();
    m_netThread->start();
    m_regThread->start();
}

QString Backend::GetCurrentTimeString()
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
}

// ───────────────── Active Process list (Tab 0) ─────────────────────

void Backend::activeProcessLoop()
{
    // ── 1. Snapshot the current total-system CPU time (100-ns ticks) ──
    FILETIME ftIdle, ftKernel, ftUser;
    ULONGLONG curSystemTime = 0;
    if (GetSystemTimes(&ftIdle, &ftKernel, &ftUser))
        curSystemTime = FileTimeToULL(ftKernel) + FileTimeToULL(ftUser);

    ULONGLONG prevSystemTime = m_lastTotalSystemTimeForProcessCPU;
    ULONGLONG systemDelta    = (curSystemTime > prevSystemTime)
                                   ? (curSystemTime - prevSystemTime)
                                   : 1ull;  // avoid /0

    // ── 2. Walk the process snapshot ──
    std::vector<ProcessData> result;
    result.reserve(300);

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        emit processesUpdated(result);
        return;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            ProcessData info;
            info.pid  = pe.th32ProcessID;
            info.name = QString::fromWCharArray(pe.szExeFile);

            HANDLE hProc = OpenProcess(
                PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);

            // Some system pids can't be opened; try limited access
            if (!hProc)
                hProc = OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);

            if (hProc) {
                // ── Memory ──
                PROCESS_MEMORY_COUNTERS_EX pmc{};
                pmc.cb = sizeof(pmc);
                if (GetProcessMemoryInfo(hProc,
                        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                        sizeof(pmc)))
                    info.memUsageMB = pmc.WorkingSetSize / (1024.0 * 1024.0);

                // ── CPU ──
                FILETIME ftCreate, ftExit, ftKernelP, ftUserP;
                if (GetProcessTimes(hProc, &ftCreate, &ftExit, &ftKernelP, &ftUserP)) {
                    ULONGLONG curProcTime =
                        FileTimeToULL(ftKernelP) + FileTimeToULL(ftUserP);

                    auto it = m_lastCPUTime.find(pe.th32ProcessID);
                    if (it != m_lastCPUTime.end() && prevSystemTime != 0) {
                        ULONGLONG procDelta = (curProcTime > it->second)
                                                 ? (curProcTime - it->second)
                                                 : 0ull;
                        double cpu = 100.0 * procDelta / systemDelta;
                        info.cpuUsagePercent = qBound(0.0, cpu, 100.0);
                    }
                    m_lastCPUTime[pe.th32ProcessID] = curProcTime;
                }

                CloseHandle(hProc);
            }

            QString lowerName = info.name.toLower();
            info.isSystem = lowerName.startsWith("svchost") || 
                           lowerName == "system" || 
                           lowerName == "registry" ||
                           lowerName == "smss.exe" ||
                           lowerName == "csrss.exe" ||
                           lowerName == "wininit.exe" ||
                           lowerName == "services.exe" ||
                           lowerName == "lsass.exe" ||
                           lowerName == "winlogon.exe" ||
                           lowerName == "dwm.exe" ||
                           lowerName == "explorer.exe";

            result.push_back(std::move(info));

        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);

    // Update stored system time ONCE per cycle
    m_lastTotalSystemTimeForProcessCPU = curSystemTime;

    // Save snapshot to DB
    m_db->saveProcessSnapshot(result);

    // Feed to anomaly engine
    m_anomalyEngine->analyzeProcesses(result);

    emit processesUpdated(result);
}

// ───────────────── System Resources (Tab 1) ────────────────────────

void Backend::systemMonitorLoop()
{
    SystemData data{};

    // ── RAM ──
    MEMORYSTATUSEX memInfo{};
    memInfo.dwLength = sizeof(memInfo);
    GlobalMemoryStatusEx(&memInfo);
    data.totalRamMB  = memInfo.ullTotalPhys / (1024.0 * 1024.0);
    data.availRamMB  = memInfo.ullAvailPhys / (1024.0 * 1024.0);
    data.usedRamMB   = data.totalRamMB - data.availRamMB;
    data.ramUsagePercent = static_cast<int>(memInfo.dwMemoryLoad);

    // ── CPU (system-wide) ──
    FILETIME ftIdle, ftKernel, ftUser;
    if (GetSystemTimes(&ftIdle, &ftKernel, &ftUser)) {
        ULONGLONG curIdle  = FileTimeToULL(ftIdle);
        ULONGLONG curTotal = FileTimeToULL(ftKernel) + FileTimeToULL(ftUser);

        if (m_prevSysCpuTotal != 0) {
            ULONGLONG idleDelta  = curIdle  - m_prevSysCpuIdle;
            ULONGLONG totalDelta = curTotal - m_prevSysCpuTotal;
            if (totalDelta > 0) {
                int pct = static_cast<int>(
                    100.0 - (100.0 * idleDelta / totalDelta));
                data.cpuUsagePercent = qBound(0, pct, 100);
            }
        }
        m_prevSysCpuIdle  = curIdle;
        m_prevSysCpuTotal = curTotal;
    }

    // ── Disk C: ──
    ULARGE_INTEGER freeBytes{}, totalBytes{}, totalFreeBytes{};
    if (GetDiskFreeSpaceExW(L"C:\\", &freeBytes, &totalBytes, &totalFreeBytes)) {
        data.totalDiskGB = totalBytes.QuadPart     / (1024.0 * 1024.0 * 1024.0);
        data.freeDiskGB  = totalFreeBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
    }

    // ── GPU ──
    data.gpuUsagePercent = ReadGpuUsage();

    // Store metrics in DB
    m_db->saveSystemMetrics(data);

    // Update baseline
    m_baseline->addSample(data.cpuUsagePercent, data.ramUsagePercent);

    // Feed to anomaly engine
    m_anomalyEngine->analyzeSystem(data);

    emit systemInfoUpdated(data);
}

// ───────────────── Process-lifecycle logger (Tab 2) ────────────────

void Backend::processLogLoop()
{
    std::set<DWORD> current;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            current.insert(pe.th32ProcessID);
            if (!m_previousProcessIds.empty() &&
                m_previousProcessIds.find(pe.th32ProcessID) == m_previousProcessIds.end())
            {
                QString processName = QString::fromWCharArray(pe.szExeFile);
                emit processEventLogged(
                    GetCurrentTimeString(), "Запуск процесу",
                    processName);

                // Get full path using process handle
                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (hProc) {
                    wchar_t pathBuf[MAX_PATH];
                    DWORD size = MAX_PATH;
                    if (QueryFullProcessImageNameW(hProc, 0, pathBuf, &size)) {
                        QString exePath = QString::fromWCharArray(pathBuf);
                        // Start VirusTotal scan
                        m_scanner->scanProcess(processName, exePath);
                    }
                    CloseHandle(hProc);
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);

    // Detect terminations
    if (!m_previousProcessIds.empty()) {
        for (DWORD pid : m_previousProcessIds) {
            if (current.find(pid) == current.end())
                emit processEventLogged(GetCurrentTimeString(),
                    "Завершення процесу",
                    QString("PID %1").arg(pid));
        }
    }
    m_previousProcessIds = current;
}

// ───────────────── Privilege helper ────────────────────────────────

void Backend::EnablePrivilege(LPCTSTR lpszPrivilege)
{
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return;

    TOKEN_PRIVILEGES tp{};
    LUID luid{};
    if (LookupPrivilegeValue(nullptr, lpszPrivilege, &luid)) {
        tp.PrivilegeCount           = 1;
        tp.Privileges[0].Luid       = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    }
    CloseHandle(hToken);
}

// ───────────────── GPU PDH ─────────────────────────────────────────

void Backend::InitGpuPdh()
{
    if (PdhOpenQuery(nullptr, 0, &m_gpuQuery) != ERROR_SUCCESS)
        return;

    // Counter covers all GPU engines of type "3D" across all adapters
    if (PdhAddEnglishCounterW(
            m_gpuQuery,
            L"\\GPU Engine(*engtype_3D)\\Utilization Percentage",
            0, &m_gpuCounter) != ERROR_SUCCESS)
    {
        PdhCloseQuery(m_gpuQuery);
        m_gpuQuery = nullptr;
        return;
    }
    // First collect initializes the baseline
    PdhCollectQueryData(m_gpuQuery);
    m_gpuReady = true;
}

int Backend::ReadGpuUsage()
{
    if (!m_gpuReady) return 0;

    if (PdhCollectQueryData(m_gpuQuery) != ERROR_SUCCESS) return 0;

    DWORD bufSize = 0, itemCount = 0;
    // First call: determine required buffer size
    PdhGetFormattedCounterArrayW(
        m_gpuCounter, PDH_FMT_DOUBLE, &bufSize, &itemCount, nullptr);
    if (bufSize == 0) return 0;

    std::vector<BYTE> buf(bufSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    if (PdhGetFormattedCounterArrayW(
            m_gpuCounter, PDH_FMT_DOUBLE, &bufSize, &itemCount, items)
        != ERROR_SUCCESS)
        return 0;

    // Sum utilization across all engine instances (can exceed 100% for multi-GPU)
    double total = 0.0;
    for (DWORD i = 0; i < itemCount; ++i)
        total += items[i].FmtValue.doubleValue;

    return qBound(0, static_cast<int>(total), 100);
}

// ───────────────── FsWorker (file-system events) ───────────────────

void FsWorker::doWork()
{
    // Monitor each directory in m_watchPaths
    for (const QString& path : m_watchPaths) {
        // Each path runs in the same thread sequentially via overlapped IO
        HANDLE hDir = CreateFileW(
            path.toStdWString().c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr);

        if (hDir == INVALID_HANDLE_VALUE) continue;

        BYTE buffer[4096];
        DWORD bytesReturned;

        // Run monitoring loop for this directory
        while (ReadDirectoryChangesW(
                   hDir, buffer, sizeof(buffer), TRUE,
                   FILE_NOTIFY_CHANGE_FILE_NAME |
                   FILE_NOTIFY_CHANGE_DIR_NAME |
                   FILE_NOTIFY_CHANGE_LAST_WRITE |
                   FILE_NOTIFY_CHANGE_CREATION,
                   &bytesReturned, nullptr, nullptr))
        {
            FILE_NOTIFY_INFORMATION* fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer);

            while (true) {
                QString fileName = QString::fromWCharArray(fni->FileName, fni->FileNameLength / sizeof(WCHAR));

                QString action;
                switch (fni->Action) {
                case FILE_ACTION_ADDED:            action = "Створено"; break;
                case FILE_ACTION_REMOVED:          action = "Видалено"; break;
                case FILE_ACTION_MODIFIED:         action = "Змінено"; break;
                case FILE_ACTION_RENAMED_OLD_NAME: action = "Перейменовано (старе)"; break;
                case FILE_ACTION_RENAMED_NEW_NAME: action = "Перейменовано (нове)"; break;
                default:                           action = "Невідома дія"; break;
                }

                emit fileEvent(
                    QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"),
                    action,
                    path + "\\" + fileName);

                if (fni->NextEntryOffset == 0) break;
                fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                    reinterpret_cast<BYTE*>(fni) + fni->NextEntryOffset);
            }
        }

        CloseHandle(hDir);
    }
}

// ───────────────── NetworkWorker (TCP/UDP and Ping) ──────────────────

static QString FormatIpAddress(DWORD ip) {
    struct in_addr ipAddr;
    ipAddr.S_un.S_addr = ip;
    return QString::fromLatin1(inet_ntoa(ipAddr));
}

static QString GetTcpStateString(DWORD state) {
    switch (state) {
        case MIB_TCP_STATE_CLOSED: return "CLOSED";
        case MIB_TCP_STATE_LISTEN: return "LISTEN";
        case MIB_TCP_STATE_SYN_SENT: return "SYN_SENT";
        case MIB_TCP_STATE_SYN_RCVD: return "SYN_RCVD";
        case MIB_TCP_STATE_ESTAB: return "ESTABLISHED";
        case MIB_TCP_STATE_FIN_WAIT1: return "FIN_WAIT1";
        case MIB_TCP_STATE_FIN_WAIT2: return "FIN_WAIT2";
        case MIB_TCP_STATE_CLOSE_WAIT: return "CLOSE_WAIT";
        case MIB_TCP_STATE_CLOSING: return "CLOSING";
        case MIB_TCP_STATE_LAST_ACK: return "LAST_ACK";
        case MIB_TCP_STATE_TIME_WAIT: return "TIME_WAIT";
        case MIB_TCP_STATE_DELETE_TCB: return "DELETE_TCB";
        default: return "UNKNOWN";
    }
}

void NetworkWorker::doWork()
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    HANDLE hIcmpFile = IcmpCreateFile();
    DWORD ipaddr = inet_addr(m_pingServer.toStdString().c_str());
    char sendData[32] = "Data Buffer";
    DWORD replySize = sizeof(ICMP_ECHO_REPLY) + sizeof(sendData);
    void* replyBuffer = malloc(replySize);

    while (!m_stop) {
        if (!m_enableScanner) {
            QThread::sleep(2);
            continue;
        }

        std::vector<NetworkConnection> conns;

        // --- Get TCP Connections ---
        DWORD tcpSize = 0;
        GetExtendedTcpTable(nullptr, &tcpSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
        if (tcpSize > 0) {
            PMIB_TCPTABLE_OWNER_PID pTcpTable = (PMIB_TCPTABLE_OWNER_PID)malloc(tcpSize);
            if (GetExtendedTcpTable(pTcpTable, &tcpSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
                for (DWORD i = 0; i < pTcpTable->dwNumEntries; i++) {
                    NetworkConnection conn;
                    conn.protocol = "TCP";
                    conn.localAddr = FormatIpAddress(pTcpTable->table[i].dwLocalAddr) + ":" + QString::number(ntohs((u_short)pTcpTable->table[i].dwLocalPort));
                    conn.remoteAddr = FormatIpAddress(pTcpTable->table[i].dwRemoteAddr) + ":" + QString::number(ntohs((u_short)pTcpTable->table[i].dwRemotePort));
                    conn.state = GetTcpStateString(pTcpTable->table[i].dwState);
                    conn.pid = pTcpTable->table[i].dwOwningPid;
                    conns.push_back(conn);
                }
            }
            free(pTcpTable);
        }

        // --- Get UDP Connections ---
        DWORD udpSize = 0;
        GetExtendedUdpTable(nullptr, &udpSize, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
        if (udpSize > 0) {
            PMIB_UDPTABLE_OWNER_PID pUdpTable = (PMIB_UDPTABLE_OWNER_PID)malloc(udpSize);
            if (GetExtendedUdpTable(pUdpTable, &udpSize, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
                for (DWORD i = 0; i < pUdpTable->dwNumEntries; i++) {
                    NetworkConnection conn;
                    conn.protocol = "UDP";
                    conn.localAddr = FormatIpAddress(pUdpTable->table[i].dwLocalAddr) + ":" + QString::number(ntohs((u_short)pUdpTable->table[i].dwLocalPort));
                    conn.remoteAddr = "*:*";
                    conn.state = "—";
                    conn.pid = pUdpTable->table[i].dwOwningPid;
                    conns.push_back(conn);
                }
            }
            free(pUdpTable);
        }

        emit connectionsUpdated(conns);

        // --- Ping (Stability) ---
        if (hIcmpFile != INVALID_HANDLE_VALUE) {
            DWORD ret = IcmpSendEcho(hIcmpFile, ipaddr, sendData, sizeof(sendData), nullptr, replyBuffer, replySize, 1000);
            if (ret != 0) {
                PICMP_ECHO_REPLY pEchoReply = (PICMP_ECHO_REPLY)replyBuffer;
                int latency = pEchoReply->RoundTripTime;
                bool anomaly = (latency > m_pingMaxLatency); // configurable threshold
                emit pingResult(latency, 0.0, anomaly);
                if (anomaly) {
                    emit networkEventLogged(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"), "Пінг", "Висока затримка мережі: " + QString::number(latency) + " ms");
                }
            } else {
                emit pingResult(0, 100.0, true); // 100% loss
                emit networkEventLogged(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"), "Пінг", "Втрата пакетів (Request Timed Out)");
            }
        }

        QThread::sleep(2); // Scan every 2 seconds
    }

    if (hIcmpFile != INVALID_HANDLE_VALUE) IcmpCloseHandle(hIcmpFile);
    free(replyBuffer);
    WSACleanup();
}

// ───────────────── RegistryWorker (Startup Monitoring) ───────────

static std::map<QString, QString> ReadRunKeyFrom(HKEY root, const wchar_t* subkey)
{
    std::map<QString, QString> entries;
    HKEY hKey;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return entries;

    DWORD index = 0;
    wchar_t valueName[16383];
    DWORD valueNameSize;
    DWORD type;
    BYTE  data[2048];
    DWORD dataSize;

    while (true) {
        valueNameSize = 16383;
        dataSize      = sizeof(data);
        LSTATUS ret = RegEnumValueW(hKey, index, valueName, &valueNameSize, nullptr, &type, data, &dataSize);
        if (ret != ERROR_SUCCESS) break;
        if (type == REG_SZ || type == REG_EXPAND_SZ) {
            entries[QString::fromWCharArray(valueName)] =
                QString::fromWCharArray(reinterpret_cast<wchar_t*>(data));
        }
        index++;
    }
    RegCloseKey(hKey);
    return entries;
}

// Helper: compare two snapshots and emit signals for differences
static void DiffAndEmit(RegistryWorker* w,
                        const std::map<QString, QString>& before,
                        const std::map<QString, QString>& after,
                        const QString& hive)
{
    for (const auto& [name, path] : before)
        if (after.find(name) == after.end())
            emit w->startupChanged("Видалено", name, path, hive);

    for (const auto& [name, path] : after) {
        auto it = before.find(name);
        if (it == before.end())
            emit w->startupChanged("Додано", name, path, hive);
        else if (it->second != path)
            emit w->startupChanged("Змінено", name, path, hive);
    }
}

void RegistryWorker::doWork()
{
    const wchar_t* hkcuPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* hklmPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";

    // Read initial snapshot
    auto hkcuEntries = ReadRunKeyFrom(HKEY_CURRENT_USER,  hkcuPath);
    auto hklmEntries = ReadRunKeyFrom(HKEY_LOCAL_MACHINE, hklmPath);

    // Emit initial snapshot for UI table
    {
        std::vector<StartupEntry> snapshot;
        for (const auto& [name, path] : hkcuEntries)
            snapshot.push_back({"HKCU", name, path});
        for (const auto& [name, path] : hklmEntries)
            snapshot.push_back({"HKLM", name, path});
        emit startupSnapshotReady(snapshot);
    }

    // Open keys for change notifications
    HKEY hKeyHkcu = nullptr, hKeyHklm = nullptr;
    RegOpenKeyExW(HKEY_CURRENT_USER,  hkcuPath, 0, KEY_NOTIFY | KEY_READ, &hKeyHkcu);
    RegOpenKeyExW(HKEY_LOCAL_MACHINE, hklmPath, 0, KEY_NOTIFY | KEY_READ, &hKeyHklm);

    HANDLE hEvtHkcu = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE hEvtHklm = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    auto reRegister = [&]() {
        if (hKeyHkcu && hEvtHkcu)
            RegNotifyChangeKeyValue(hKeyHkcu, FALSE,
                REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, hEvtHkcu, TRUE);
        if (hKeyHklm && hEvtHklm)
            RegNotifyChangeKeyValue(hKeyHklm, FALSE,
                REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, hEvtHklm, TRUE);
    };

    reRegister();

    HANDLE handles[2] = { hEvtHkcu, hEvtHklm };
    DWORD  handleCount = (hEvtHkcu && hEvtHklm) ? 2 : 1;

    while (!m_stop) {
        DWORD waitRes = WaitForMultipleObjects(handleCount, handles, FALSE, 1000);

        if (waitRes == WAIT_OBJECT_0 && hEvtHkcu) {          // HKCU changed
            auto fresh = ReadRunKeyFrom(HKEY_CURRENT_USER, hkcuPath);
            DiffAndEmit(this, hkcuEntries, fresh, "HKCU");
            hkcuEntries = fresh;

            // Re-emit full snapshot
            std::vector<StartupEntry> snapshot;
            for (const auto& [n, p] : hkcuEntries) snapshot.push_back({"HKCU", n, p});
            for (const auto& [n, p] : hklmEntries) snapshot.push_back({"HKLM", n, p});
            emit startupSnapshotReady(snapshot);

            ResetEvent(hEvtHkcu);
            reRegister();
        } else if (waitRes == WAIT_OBJECT_0 + 1 && hEvtHklm) { // HKLM changed
            auto fresh = ReadRunKeyFrom(HKEY_LOCAL_MACHINE, hklmPath);
            DiffAndEmit(this, hklmEntries, fresh, "HKLM");
            hklmEntries = fresh;

            std::vector<StartupEntry> snapshot;
            for (const auto& [n, p] : hkcuEntries) snapshot.push_back({"HKCU", n, p});
            for (const auto& [n, p] : hklmEntries) snapshot.push_back({"HKLM", n, p});
            emit startupSnapshotReady(snapshot);

            ResetEvent(hEvtHklm);
            reRegister();
        }
    }

    if (hEvtHkcu) CloseHandle(hEvtHkcu);
    if (hEvtHklm) CloseHandle(hEvtHklm);
    if (hKeyHkcu) RegCloseKey(hKeyHkcu);
    if (hKeyHklm) RegCloseKey(hKeyHklm);
}


// ───────────────── Network COM handler ─────────────────────────────

HRESULT STDMETHODCALLTYPE ModernNetworkEventsHandler::QueryInterface(
    REFIID riid, void** ppv)
{
    if (riid == IID_IUnknown ||
        riid == IID_INetworkListManagerEvents)
    {
        *ppv = static_cast<INetworkListManagerEvents*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE ModernNetworkEventsHandler::AddRef()
{ return InterlockedIncrement(&m_cRef); }

ULONG STDMETHODCALLTYPE ModernNetworkEventsHandler::Release()
{
    LONG ref = InterlockedDecrement(&m_cRef);
    if (ref == 0) delete this;
    return ref;
}

HRESULT STDMETHODCALLTYPE ModernNetworkEventsHandler::ConnectivityChanged(
    NLM_CONNECTIVITY c)
{
    QString status;
    if (c == NLM_CONNECTIVITY_DISCONNECTED)
        status = "Відключено від мережі";
    else if (c & (NLM_CONNECTIVITY_IPV4_INTERNET | NLM_CONNECTIVITY_IPV6_INTERNET))
        status = "Підключено до Інтернету";
    else
        status = "Локальна мережа (без Інтернет)";

    emit m_backend->networkEventLogged(
        QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"),
        "Зміна мережі", status);
    return S_OK;
}

// ─────────────────────────────────────────────────────────────────────────
//  Process Actions (Suspend / Resume / Terminate)
// ─────────────────────────────────────────────────────────────────────────

typedef LONG (NTAPI *NtSuspendProcess)(IN HANDLE ProcessHandle);
typedef LONG (NTAPI *NtResumeProcess)(IN HANDLE ProcessHandle);

bool Backend::suspendProcess(DWORD pid)
{
    HANDLE hProc = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
    if (!hProc) return false;

    auto suspend = (NtSuspendProcess)GetProcAddress(GetModuleHandleA("ntdll"), "NtSuspendProcess");
    bool result = false;
    if (suspend) {
        result = (suspend(hProc) == 0);
    }
    CloseHandle(hProc);
    return result;
}

bool Backend::resumeProcess(DWORD pid)
{
    HANDLE hProc = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
    if (!hProc) return false;

    auto resume = (NtResumeProcess)GetProcAddress(GetModuleHandleA("ntdll"), "NtResumeProcess");
    bool result = false;
    if (resume) {
        result = (resume(hProc) == 0);
    }
    CloseHandle(hProc);
    return result;
}

bool Backend::terminateProcess(DWORD pid)
{
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!hProc) return false;
    bool result = TerminateProcess(hProc, 1);
    CloseHandle(hProc);
    return result;
}
