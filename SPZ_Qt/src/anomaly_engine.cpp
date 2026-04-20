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
                    "Процес '%1' (PID %2) утримує навантаження на CPU %.1f%% "
                    "протягом більш ніж 30 секунд.")
                    .arg(p.name).arg(p.pid).arg(p.cpuUsagePercent);
                emit_anomaly(a);
            }
        } else {
            m_cpuHighTicks[p.pid] = 0;
        }
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
                "Використання RAM перевищує %1%% протягом останніх 5 хвилин "
                "(доступно лише %.1f ГБ із %.1f ГБ). "
                "Система може стати нестабільною.")
                .arg(sys.ramUsagePercent)
                .arg(sys.availRamMB / 1024.0)
                .arg(sys.totalRamMB / 1024.0);
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
            "На диску C: залишилось лише %.1f ГБ із %.1f ГБ. "
            "Рекомендовано очистити тимчасові файли або перенести дані.")
            .arg(sys.freeDiskGB)
            .arg(sys.totalDiskGB);
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
                    "порівняно з базовою лінією (avg CPU: %.0f%%, avg RAM: %.0f%%). "
                    "Можлива підозріла фонова активність.")
                    .arg(what).arg(avgCpu).arg(avgRam);
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
    m_fsEvents.enqueue(now);

    // Remove events older than 30 seconds
    while (!m_fsEvents.isEmpty() && now - m_fsEvents.head() > 30000) {
        m_fsEvents.dequeue();
    }

    // Threshold: > 50 file changes within 30 seconds
    if (m_fsEvents.size() > 50) {
        // Prevent spamming — only emit once per minute
        if (now - m_lastRansomwareAlertTime > 60000) {
            Anomaly a;
            a.type = "ransomware_suspected";
            a.description = "⚠️ Можлива активність вірусу-шифрувальника (Ransomware)! Зафіксовано аномально високу кількість змін файлів (>50 за останні 30 с).\nРекомендовано заблокувати невідомі процеси та виконати антивірусне сканування.";
            a.severity = 3; // CRITICAL
            
            // Note: We bypass emit_anomaly since we handle our own cooldown here
            // to avoid string mapping overhead, but we could use emit_anomaly as well.
            emit anomalyDetected(a);
            
            m_lastRansomwareAlertTime = now;
        }
    }
}

