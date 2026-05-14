#include "anomaly_engine.h"
#include <QUuid>

AnomalyEngine::AnomalyEngine(BaselineTracker* baseline, SettingsManager* settings, QObject* parent)
    : QObject(parent), m_baseline(baseline), m_settings(settings) {}

// ─────────────────────── Cooldown helper ─────────────────────────────────

bool AnomalyEngine::isCooledDown(const QString& key)
{
    auto it = m_cooldown.find(key);
    if (it == m_cooldown.end()) return true;
    return it.value().secsTo(QDateTime::currentDateTime()) >= m_settings->cooldownSecs;
}

void AnomalyEngine::emit_anomaly(const Anomaly& a)
{
    const QString key = a.type + "|" + a.processName;
    if (!isCooledDown(key)) return;
    m_cooldown[key] = QDateTime::currentDateTime();
    emit anomalyDetected(a);
}

// ─────────────────────── Process analysis ────────────────────────────────

void AnomalyEngine::analyzeProcesses(const std::vector<ProcessData>& procs)
{
    // Collect current pids to clean up stale counters
    QSet<DWORD> currentPids;
    for (const auto& p : procs) currentPids.insert(p.pid);

    // Remove stale entries (process terminated)
    for (auto it = m_cpuHighTicks.begin(); it != m_cpuHighTicks.end(); ) {
        if (!currentPids.contains(it.key())) it = m_cpuHighTicks.erase(it);
        else ++it;
    }

    for (const auto& p : procs) {
        // ── Detector 1: CPU spike ─────────────────────────────────────────
        if (p.cpuUsagePercent >= m_settings->cpuSpikeThreshold) {
            m_cpuHighTicks[p.pid]++;
            if (m_cpuHighTicks[p.pid] == m_settings->cpuSpikeTicks) {
                Anomaly a;
                a.type        = "cpu_spike";
                a.processName = p.name;
                a.processPid  = p.pid;
                a.severity    = 2;
                a.description = QString(
                    "Процес '%1' (PID %2) утримує навантаження на CPU %3% "
                    "протягом більш ніж 30 секунд.")
                    .arg(p.name)
                    .arg(p.pid)
                    .arg(p.cpuUsagePercent, 0, 'f', 1);
                emit_anomaly(a);
            }
        } else {
            m_cpuHighTicks[p.pid] = 0;
        }
    }

    // Sort processes by RAM to find top 3
    std::vector<ProcessData> sortedProcs = procs;
    std::sort(sortedProcs.begin(), sortedProcs.end(), [](const ProcessData& a, const ProcessData& b) {
        return a.memUsageMB > b.memUsageMB;
    });

    m_topRamProcs.clear();
    for (int i = 0; i < std::min(3, (int)sortedProcs.size()); ++i) {
        m_topRamProcs += QString("%1. %2 (%3 MB)\n")
                         .arg(i + 1)
                         .arg(sortedProcs[i].name)
                         .arg(sortedProcs[i].memUsageMB, 0, 'f', 1);
    }
}

// ─────────────────────── System analysis ─────────────────────────────────

void AnomalyEngine::analyzeSystem(const SystemData& sys)
{
    // ── Detector 2: RAM pressure ──────────────────────────────────────────
    if (sys.ramUsagePercent >= m_settings->ramHighThreshold) {
        m_ramHighTicks++;
        if (m_ramHighTicks == m_settings->ramHighTicks) {
            Anomaly a;
            a.type        = "ram_pressure";
            a.severity    = 3; // Critical
            a.description = QString(
                "Використання RAM перевищує %1%% протягом довгого часу "
                "(доступно лише %.1f ГБ із %.1f ГБ). "
                "Система може стати нестабільною.\n\n"
                "Найбільші споживачі пам'яті:\n%2")
                .arg(sys.ramUsagePercent)
                .arg(sys.availRamMB / 1024.0)
                .arg(sys.totalRamMB / 1024.0)
                .arg(m_topRamProcs);
            emit_anomaly(a);
        }
    } else {
        m_ramHighTicks = 0;
    }

    // ── Detector 3: GPU overload ──────────────────────────────────────────
    if (sys.gpuUsagePercent >= m_settings->gpuHighThreshold) {
        m_gpuSpikeSeconds++;
        if (m_gpuSpikeSeconds == m_settings->gpuHighTicks) {
            Anomaly a;
            a.type        = "gpu_overload";
            a.severity    = 2;
            a.description = QString(
                "GPU завантажено на %1%% протягом більше 2 хвилин. "
                "Можлива некоректна робота графічного застосунку або майнер.")
                .arg(sys.gpuUsagePercent);
            emit_anomaly(a);
        }
    } else {
        m_gpuSpikeSeconds = 0;
    }

    // ── Detector 4: Disk space low ────────────────────────────────────────
    if (sys.freeDiskGB > 0 && sys.freeDiskGB < m_settings->diskLowGb) {
        Anomaly a;
        a.type        = "disk_low";
        a.severity    = 2;
        a.description = QString(
            "На диску C: залишилось лише %1 ГБ із %2 ГБ. "
            "Рекомендовано очистити тимчасові файли або перенести дані.")
            .arg(sys.freeDiskGB, 0, 'f', 1)
            .arg(sys.totalDiskGB, 0, 'f', 1);
        emit_anomaly(a);
    }

    // ── Detector 5: Baseline deviation ───────────────────────────────────
    if (m_baseline->isReady()) {
        const double avgCpu = m_baseline->avgCpu();
        const double avgRam = m_baseline->avgRam();

        bool cpuAbove = (avgCpu > 5.0) &&
                        (sys.cpuUsagePercent > avgCpu * (1.0 + m_settings->baselineDeviation));
        bool ramAbove = (avgRam > 5.0) &&
                        (sys.ramUsagePercent > avgRam * (1.0 + m_settings->baselineDeviation));

        if (cpuAbove || ramAbove) {
            m_baselineHighTicks++;
            if (m_baselineHighTicks == m_settings->baselineTicks) {
                Anomaly a;
                a.type     = "baseline_deviation";
                a.severity = 2;
                QString what;
                if (cpuAbove && ramAbove) what = "CPU та RAM";
                else if (cpuAbove)        what = "CPU";
                else                      what = "RAM";
                a.description = QString(
                    "Система демонструє аномально високе навантаження на %1 "
                    "порівняно з базовою лінією (avg CPU: %2%, avg RAM: %3%). "
                    "Можлива підозріла фонова активність.")
                    .arg(what)
                    .arg(avgCpu, 0, 'f', 0)
                    .arg(avgRam, 0, 'f', 0);
                emit_anomaly(a);
            }
        } else {
            m_baselineHighTicks = 0;
        }
    }

    recalcHealthScore(sys);
}

// ─────────────────────── Health score ────────────────────────────────────

void AnomalyEngine::recalcHealthScore(const SystemData& sys)
{
    // Start from 100 and deduct based on resource levels
    int score = 100;

    // CPU penalty: proportional above 50%
    if (sys.cpuUsagePercent > 50)
        score -= (sys.cpuUsagePercent - 50) * 1; // up to -50

    // RAM penalty: proportional above 60%
    if (sys.ramUsagePercent > 60)
        score -= (sys.ramUsagePercent - 60) * 1; // up to -40

    // GPU penalty
    if (sys.gpuUsagePercent > 70)
        score -= (sys.gpuUsagePercent - 70) / 3; // up to -10

    // Disk penalty
    if (sys.freeDiskGB < 10.0 && sys.freeDiskGB > 0)
        score -= static_cast<int>((10.0 - sys.freeDiskGB) * 2); // up to -20

    score = qBound(0, score, 100);
    if (score != m_healthScore) {
        m_healthScore = score;
        emit healthScoreChanged(score);
    }
}

// ─────────────────────── File System analysis ──────────────────────────────

void AnomalyEngine::analyzeFileSystemEvent(const QString& path)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_fsEvents.enqueue({now, path});

    // Remove events older than configured threshold time
    while (!m_fsEvents.isEmpty() && now - m_fsEvents.head().ts > m_settings->ransomwareThresholdTime * 1000) {
        m_fsEvents.dequeue();
    }

    // Threshold check using settings
    if (m_fsEvents.size() > m_settings->ransomwareThresholdEvents) {
        // Prevent spamming — only emit once per minute
        if (now - m_lastRansomwareAlertTime > 60000) {
            
            // Extract up to 5 unique paths to show the user what's happening
            QStringList recentFiles;
            for (int i = m_fsEvents.size() - 1; i >= 0 && recentFiles.size() < 5; --i) {
                QString fileMsg = m_fsEvents[i].path;
                if (!recentFiles.contains(fileMsg)) {
                    recentFiles.append(fileMsg);
                }
            }
            
            QString filesDetails = recentFiles.join("\n- ");
            
            Anomaly a;
            a.type = "ransomware_suspected";
            a.description = QString("Зафіксовано аномально високу кількість змін файлів (>%1 за останні %2 с).\n"
                                    "Приклади порушених файлів:\n- %3")
                                    .arg(m_settings->ransomwareThresholdEvents)
                                    .arg(m_settings->ransomwareThresholdTime)
                                    .arg(filesDetails);
            a.severity = 3; // CRITICAL
            
            // Note: We bypass emit_anomaly since we handle our own cooldown here
            // to avoid string mapping overhead, but we could use emit_anomaly as well.
            emit anomalyDetected(a);
            
            m_lastRansomwareAlertTime = now;
        }
    }
}

