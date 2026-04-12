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

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Backend* backend, AlertManager* alerts, AnomalyEngine* anomalyEngine, QWidget *parent = nullptr);
    ~MainWindow();

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private slots:
    void updateProcesses(const std::vector<ProcessData>& processes);
    void updateSystemInfo(const SystemData& sysData);
    void appendLog(QTableWidget* table, const QString& time,
                   const QString& event, const QString& details);
    void saveLogsToCsv();
    void clearCurrentLog();

    void refreshAnomaliesUI();
    void updateHealthScore(int score);
    void ackAllAnomalies();

private:
    Backend* m_backend;
    AlertManager* m_alerts;
    AnomalyEngine* m_anomalyEngine;

    QTabWidget*   m_tabWidget;       // outer
    QTabWidget*   m_logsTabWidget;   // inner

    QTableWidget* m_anomaliesTable;  // New anomalies UI
    QLabel*       m_healthScoreLabel;// Dynamic label

    QTableWidget* m_processTable;
    QTableWidget* m_sysInfoTable;
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
    QWidget*      buildAnomaliesTab();
    void applyModernStyle();
};
