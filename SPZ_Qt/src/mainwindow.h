#pragma once

#include <QMainWindow>
#include <QTabWidget>
#include <QTableWidget>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QGroupBox>
#include <QScrollArea>

class QLineSeries;
class QChart;
class QChartView;

#include "backend.h"
#include "alert_manager.h"
#include "anomaly_engine.h"
#include "settings_manager.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Backend* backend, AlertManager* alerts, AnomalyEngine* anomalyEngine, SettingsManager* settings, QWidget *parent = nullptr);
    ~MainWindow();

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void updateProcesses(const std::vector<ProcessData>& processes);
    void updateSystemInfo(const SystemData& sysData);
    void updateNetworkConnections(const std::vector<NetworkConnection>& conns);
    void appendLog(QTableWidget* table, const QString& time,
                   const QString& event, const QString& details);
    void saveLogsToCsv();
    void clearCurrentLog();
    void showSettingsDialog();

    void refreshAnomaliesUI();
    void updateHealthScore(int score);
    void ackAllAnomalies();

    void showProcessContextMenu(const QPoint& pos);
    void onStartupChanged(const QString& action, const QString& name,
                          const QString& path, const QString& hive);
    void refreshStartupTable(const std::vector<StartupEntry>& entries);

private:
    Backend* m_backend;
    AlertManager* m_alerts;
    AnomalyEngine* m_anomalyEngine;
    SettingsManager* m_settings;

    QTabWidget*   m_tabWidget;          // outer (3 groups)
    QTabWidget*   m_logsTabWidget;       // inner — logs
    QTabWidget*   m_securityTabWidget;   // inner — security group

    QTableWidget* m_anomaliesTable;  // New anomalies UI
    QLabel*       m_healthScoreLabel;// Dynamic label
    QLabel*       m_noAnomaliesLabel;// Placeholder when empty
    QPushButton*  m_settingsBtn = nullptr; // Overlay settings button

    QTableWidget* m_processTable;
    QTableWidget* m_sysInfoTable;
    QTableWidget* m_networkConnTable;
    QTableWidget* m_startupTable;

    QTableWidget* m_processLogTable;
    QTableWidget* m_sysLogTable;
    QTableWidget* m_networkLogTable;
    QTableWidget* m_fileLogTable;

    // Progress bars
    QProgressBar* m_cpuProgress;
    QProgressBar* m_ramProgress;
    QProgressBar* m_gpuProgress;

    // Single combined resource chart (CPU + RAM + GPU)
    QLineSeries* m_cpuSeries   = nullptr;
    QLineSeries* m_ramSeries   = nullptr;
    QLineSeries* m_gpuSeries   = nullptr;
    QChart*      m_resourceChart = nullptr;
    int          m_chartTick   = 0;
    static constexpr int CHART_HISTORY = 60;

    QPushButton*  m_btnSave;
    QPushButton*  m_btnClear;

    void setupUI();
    QTableWidget* createLogTable();
    QChartView*   createResourceChart();
    QWidget*      buildSystemTab();
    QWidget*      buildNetworkTab();
    QWidget*      buildSecurityTab();
    QWidget*      buildStartupTab();
    QWidget*      buildAnomaliesTab();
    void applyModernStyle();
};
