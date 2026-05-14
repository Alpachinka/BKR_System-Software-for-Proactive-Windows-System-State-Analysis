#pragma once
#include <QObject>
#include <QSettings>

class SettingsManager : public QObject {
    Q_OBJECT
public:
    explicit SettingsManager(QObject* parent = nullptr);

    void load();
    void save();
    void resetToDefaults();

    double cpuSpikeThreshold; // 70.0%
    int cpuSpikeTicks;        // 15 ticks (30s)
    double ramHighThreshold;  // 90.0%
    int ramHighTicks;         // 150 ticks (5m)
    double gpuHighThreshold;  // 95.0%
    int gpuHighTicks;         // 120 ticks (4m)
    double diskLowGb;         // 5.0 GB
    double baselineDeviation; // 0.40 (40%)
    int baselineTicks;        // 60 ticks (2m)
    int cooldownSecs;         // 120 secs

    int minSamples;           // 300
    int baselineWindow;       // 3600

    int chartHistory;         // 60
    QString appTheme;         // "dark" or "light"
    int processPromptLevel;   // 0=All, 1=System Only, 2=None
    bool showTrustLevel;      // true/false

    QString logSavePath;      // Path for saving CSV logs
    QStringList watchedFolders; // Folders for file watcher
    bool enableNetworkScanner;// Deep network scanning and ping

    // New settings based on plan
    int ransomwareThresholdEvents; // 50
    int ransomwareThresholdTime;   // 30
    QString pingServer;            // "8.8.8.8"
    int pingMaxLatency;            // 500
    int refreshIntervalMs;         // 1000
    bool enableTrayNotifications;  // true

signals:
    void settingsChanged();
};
