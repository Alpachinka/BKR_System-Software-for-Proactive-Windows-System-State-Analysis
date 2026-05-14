#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QString>
#include <QDateTime>
#include <QDebug>
#include "process_info.h"

// Forward declaration
struct Anomaly;
struct Recommendation;

class Database : public QObject
{
    Q_OBJECT
public:
    explicit Database(QObject* parent = nullptr);
    ~Database();

    bool open(const QString& path = "spz_monitor.db");
    void close();
    bool isOpen() const;

    // Metrics
    void saveSystemMetrics(const SystemData& d);
    void saveProcessSnapshot(const std::vector<ProcessData>& procs);

    // Anomalies
    void saveAnomaly(const Anomaly& a);
    void acknowledgeAnomaly(const QString& id);

    // Baseline queries
    double getAverageCpu(int lastMinutes = 60);
    double getAverageRam(int lastMinutes = 60);
    int getCrashCount(int lastDays = 7);

private:
    QSqlDatabase m_db;
    bool createTables();

    // Helper for safe query execution
    bool exec(QSqlQuery& q, const QString& context = {});
};
