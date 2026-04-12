#include "backend.h"
#include <QDateTime>
#include <QDebug>
#include <psapi.h>
#include <tlhelp32.h>

// ───────────────────────────── helpers ─────────────────────────────

static ULONGLONG FileTimeToULL(const FILETIME& ft)
{
    ULARGE_INTEGER uli;
    uli.LowPart  = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

// ───────────────────────────── Backend ─────────────────────────────

Backend::Backend(Database* db, BaselineTracker* baseline, AnomalyEngine* anomalyEx, ProcessScanner* scanner, QObject *parent)
    : QObject(parent), m_db(db), m_baseline(baseline), m_anomalyEngine(anomalyEx), m_scanner(scanner)
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
    worker->moveToThread(m_fsThread);

    connect(m_fsThread, &QThread::started, worker, &FsWorker::doWork);
    connect(worker, &FsWorker::fileEvent, this,
            [this](const QString& t, const QString& e, const QString& msg) {
                emit fileSystemEventLogged(t, e, msg);
            });
}

Backend::~Backend()
{
    m_fsThread->quit();
    m_fsThread->wait();
}

void Backend::startMonitoring()
{
    EnablePrivilege(SE_DEBUG_NAME);
    InitGpuPdh();
    m_activeTimer->start(2000);
    m_sysTimer->start(1000);
    m_processLogTimer->start(2000);
    m_fsThread->start();
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
    const QString path = "C:\\Users";
    HANDLE hDir = FindFirstChangeNotificationW(
        path.toStdWString().c_str(), TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME |
        FILE_NOTIFY_CHANGE_DIR_NAME  |
        FILE_NOTIFY_CHANGE_LAST_WRITE);

    if (hDir == INVALID_HANDLE_VALUE) return;

    while (true) {
        DWORD status = WaitForSingleObject(hDir, INFINITE);
        if (status != WAIT_OBJECT_0) break;

        emit fileEvent(
            QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"),
            "Зміна у файлах",
            "Виявлено зміну в: " + path);

        FindNextChangeNotification(hDir);
    }
    FindCloseChangeNotification(hDir);
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
