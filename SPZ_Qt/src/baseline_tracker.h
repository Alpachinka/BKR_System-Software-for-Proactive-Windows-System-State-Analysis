#pragma once
#include <QObject>
#include <deque>

// ─────────────────────────────────────────────────────────────────────────
//  BaselineTracker — зберігає ковзне середнє CPU та RAM за останню годину
//  і дозволяє AnomalyEngine виявляти відхилення від норми
// ─────────────────────────────────────────────────────────────────────────
class BaselineTracker : public QObject
{
    Q_OBJECT
public:
    explicit BaselineTracker(QObject* parent = nullptr);

    // Feed new samples every second
    void addSample(int cpuPercent, int ramPercent);

    // Returns average over the sliding window (-1 if not enough data)
    double avgCpu() const;
    double avgRam() const;

    // How many samples have been collected (needed by DB for bootstrap)
    int    sampleCount() const { return static_cast<int>(m_cpu.size()); }

    // Minimum samples before we trust the baseline (~5 minutes)
    static constexpr int MIN_SAMPLES = 300;

    // Window size: 60 minutes of 1-second samples
    static constexpr int WINDOW = 3600;

    // Returns true when baseline is considered "warm" (usable)
    bool isReady() const { return sampleCount() >= MIN_SAMPLES; }

    // Deviation threshold to flag an anomaly
    static constexpr double DEVIATION_THRESHOLD = 0.40; // 40% above baseline

private:
    std::deque<int> m_cpu;
    std::deque<int> m_ram;
    long long       m_cpuSum = 0;
    long long       m_ramSum = 0;
};
