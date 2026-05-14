#pragma once
#include <QObject>
#include <QQueue>
#include <QMap>
#include <QDateTime>
#include "process_info.h"
#include "anomaly_types.h"
#include "baseline_tracker.h"
#include "settings_manager.h"

// ─────────────────────────────────────────────────────────────────────────
//  AnomalyEngine — аналізує дані і генерує сигнал anomalyDetected
//  при виявленні відхилень від норми
// ─────────────────────────────────────────────────────────────────────────
class AnomalyEngine : public QObject
{
    Q_OBJECT
public:
    explicit AnomalyEngine(BaselineTracker* baseline, SettingsManager* settings, QObject* parent = nullptr);

    void analyzeProcesses(const std::vector<ProcessData>& procs);
    void analyzeSystem(const SystemData& sys);
    void analyzeFileSystemEvent(const QString& path);
    void analyzeLongTermTrends(class Database* db);

public slots:
    void onHardwareScanCompleted(const std::vector<struct HardwareComponent>& results);

    // Calculate overall health score 0–100
    int healthScore() const { return m_healthScore; }

signals:
    void anomalyDetected(const Anomaly& a);
    void healthScoreChanged(int score);

private:
    BaselineTracker* m_baseline;
    SettingsManager* m_settings;
    int              m_healthScore = 100;

    // ── Per-process persistent counters ──────────────────────────────────
    // How many consecutive ticks a process has been above the CPU threshold
    QMap<DWORD, int>  m_cpuHighTicks;       // pid → tick count
    QMap<DWORD, int>  m_gpuHighTicks;       // reused for GPU global counter

    // ── System-level persistent counters ─────────────────────────────────
    int  m_ramHighTicks       = 0;
    int  m_gpuSpikeSeconds    = 0;
    int  m_baselineHighTicks  = 0;
    
    QString m_topRamProcs; // Stores top 3 RAM consumers

    // ── Ransomware / FS tracking ─────────────────────────────────────────
    struct FsEventRec {
        qint64 ts;
        QString path;
    };
    QQueue<FsEventRec> m_fsEvents;
    qint64 m_lastRansomwareAlertTime = 0;

    // ── Cooldown: avoid spamming the same anomaly ─────────────────────────
    // Maps anomaly key (type + processName) → last emit time
    QMap<QString, QDateTime> m_cooldown;

    bool isCooledDown(const QString& key);
    void emit_anomaly(const Anomaly& a);

    // ── Thresholds ───────────────────────────────────────────────────────
    // Values are now dynamically drawn from m_settings.

    void recalcHealthScore(const SystemData& sys);
};
