#pragma once
#include <QObject>
#include <QMap>
#include <QDateTime>
#include "process_info.h"
#include "anomaly_types.h"
#include "baseline_tracker.h"

// ─────────────────────────────────────────────────────────────────────────
//  AnomalyEngine — аналізує дані і генерує сигнал anomalyDetected
//  при виявленні відхилень від норми
// ─────────────────────────────────────────────────────────────────────────
class AnomalyEngine : public QObject
{
    Q_OBJECT
public:
    explicit AnomalyEngine(BaselineTracker* baseline, QObject* parent = nullptr);

    void analyzeProcesses(const std::vector<ProcessData>& procs);
    void analyzeSystem(const SystemData& sys);

    // Calculate overall health score 0–100
    int healthScore() const { return m_healthScore; }

signals:
    void anomalyDetected(const Anomaly& a);
    void healthScoreChanged(int score);

private:
    BaselineTracker* m_baseline;
    int              m_healthScore = 100;

    // ── Per-process persistent counters ──────────────────────────────────
    // How many consecutive ticks a process has been above the CPU threshold
    QMap<DWORD, int>  m_cpuHighTicks;       // pid → tick count
    QMap<DWORD, int>  m_gpuHighTicks;       // reused for GPU global counter

    // ── System-level persistent counters ─────────────────────────────────
    int  m_ramHighTicks       = 0;
    int  m_gpuSpikeSeconds    = 0;
    int  m_baselineHighTicks  = 0;

    // ── Cooldown: avoid spamming the same anomaly ─────────────────────────
    // Maps anomaly key (type + processName) → last emit time
    QMap<QString, QDateTime> m_cooldown;
    static constexpr int COOLDOWN_SECS = 120; // re-emit at most every 2 min

    bool isCooledDown(const QString& key);
    void emit_anomaly(const Anomaly& a);

    // ── Thresholds ───────────────────────────────────────────────────────
    static constexpr double CPU_SPIKE_THRESHOLD  = 70.0; // %
    static constexpr int    CPU_SPIKE_TICKS      = 15;   // ticks × 2s = 30s
    static constexpr int    RAM_HIGH_THRESHOLD   = 90;   // %
    static constexpr int    RAM_HIGH_TICKS       = 150;  // 5 min (1s ticks)
    static constexpr int    GPU_HIGH_THRESHOLD   = 95;   // %
    static constexpr int    GPU_HIGH_TICKS       = 120;  // 2 min
    static constexpr double DISK_LOW_GB          = 5.0;  // GB
    static constexpr double BASELINE_DEVIATION   = 0.40; // 40% above avg
    static constexpr int    BASELINE_TICKS       = 60;   // 1 minute

    void recalcHealthScore(const SystemData& sys);
};
