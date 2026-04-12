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

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QChart>

#include "backend.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Backend* backend, QWidget *parent = nullptr);
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

private:
    Backend* m_backend;

    QTabWidget*   m_tabWidget;       // outer: Processes / System / Logs
    QTabWidget*   m_logsTabWidget;    // inner sub-tabs inside Logs
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
    QChartView*   createResourceChart();   // single combined chart
    QWidget*      buildSystemTab();
    void applyModernStyle();
};
