#include "database.h"
#include "anomaly_types.h"
#include <QStandardPaths>
#include <QDir>

Database::Database(QObject* parent) : QObject(parent) {}

Database::~Database() { close(); }

bool Database::open(const QString& path)
{
    // Store DB next to the executable
    QString fullPath = path;

    m_db = QSqlDatabase::addDatabase("QSQLITE", "spz_conn");
    m_db.setDatabaseName(fullPath);

    if (!m_db.open()) {
        qWarning() << "[DB] Cannot open:" << m_db.lastError().text();
        return false;
    }

    // Enable WAL for better concurrent write performance
    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("PRAGMA synchronous=NORMAL");

    return createTables();
}

void Database::close()
{
    if (m_db.isOpen()) {
        m_db.close();
        QSqlDatabase::removeDatabase("spz_conn");
    }
}

bool Database::isOpen() const { return m_db.isOpen(); }

bool Database::exec(QSqlQuery& q, const QString& ctx)
{
    if (!q.exec()) {
        qWarning() << "[DB]" << ctx << q.lastError().text();
        return false;
    }
    return true;
}

// ─────────────────────── Table creation ──────────────────────────────────

bool Database::createTables()
{
    QSqlQuery q(m_db);

    // System metrics: one row per second
    exec(q, "create system_metrics");
    q.prepare(R"(
        CREATE TABLE IF NOT EXISTS system_metrics (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            ts        TEXT NOT NULL,
            cpu       INTEGER NOT NULL,
            ram       INTEGER NOT NULL,
            gpu       INTEGER NOT NULL,
            disk_free REAL NOT NULL
        )
    )");
    if (!exec(q, "create system_metrics")) return false;

    // Process snapshots: lightweight, only top columns
    q.prepare(R"(
        CREATE TABLE IF NOT EXISTS process_snapshots (
            id       INTEGER PRIMARY KEY AUTOINCREMENT,
            ts       TEXT NOT NULL,
            pid      INTEGER NOT NULL,
            name     TEXT NOT NULL,
            cpu      REAL NOT NULL,
            ram_mb   REAL NOT NULL
        )
    )");
    if (!exec(q, "create process_snapshots")) return false;

    // Anomalies table
    q.prepare(R"(
        CREATE TABLE IF NOT EXISTS anomalies (
            id           TEXT PRIMARY KEY,
            type         TEXT NOT NULL,
            process_name TEXT,
            process_pid  INTEGER DEFAULT 0,
            description  TEXT NOT NULL,
            severity     INTEGER NOT NULL,
            detected_at  TEXT NOT NULL,
            acknowledged INTEGER DEFAULT 0
        )
    )");
    if (!exec(q, "create anomalies")) return false;

    // Auto-cleanup: keep only last 7 days of metrics to avoid DB bloat
    q.prepare("DELETE FROM system_metrics WHERE ts < datetime('now', '-7 days')");
    q.exec();
    q.prepare("DELETE FROM process_snapshots WHERE ts < datetime('now', '-2 days')");
    q.exec();

    return true;
}

// ─────────────────────── System metrics ──────────────────────────────────

void Database::saveSystemMetrics(const SystemData& d)
{
    if (!m_db.isOpen()) return;

    QSqlQuery q(m_db);
    q.prepare("INSERT INTO system_metrics(ts, cpu, ram, gpu, disk_free) "
              "VALUES(:ts, :cpu, :ram, :gpu, :df)");
    q.bindValue(":ts",  QDateTime::currentDateTime().toString(Qt::ISODate));
    q.bindValue(":cpu", d.cpuUsagePercent);
    q.bindValue(":ram", d.ramUsagePercent);
    q.bindValue(":gpu", d.gpuUsagePercent);
    q.bindValue(":df",  d.freeDiskGB);
    exec(q, "saveSystemMetrics");
}

void Database::saveProcessSnapshot(const std::vector<ProcessData>& procs)
{
    if (!m_db.isOpen()) return;

    m_db.transaction();
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO process_snapshots(ts, pid, name, cpu, ram_mb) "
              "VALUES(:ts, :pid, :name, :cpu, :ram)");
    const QString ts = QDateTime::currentDateTime().toString(Qt::ISODate);

    for (const auto& p : procs) {
        // Only store processes that actually consume resources — saves DB space
        if (p.cpuUsagePercent < 0.05 && p.memUsageMB < 1.0) continue;
        q.bindValue(":ts",   ts);
        q.bindValue(":pid",  static_cast<int>(p.pid));
        q.bindValue(":name", p.name);
        q.bindValue(":cpu",  p.cpuUsagePercent);
        q.bindValue(":ram",  p.memUsageMB);
        q.exec();
    }
    m_db.commit();
}

// ─────────────────────── Anomalies ────────────────────────────────────────

void Database::saveAnomaly(const Anomaly& a)
{
    if (!m_db.isOpen()) return;

    QSqlQuery q(m_db);
    q.prepare("INSERT OR IGNORE INTO anomalies"
              "(id, type, process_name, process_pid, description, severity, detected_at, acknowledged) "
              "VALUES(:id, :type, :pname, :ppid, :desc, :sev, :dtime, 0)");
    q.bindValue(":id",    a.id);
    q.bindValue(":type",  a.type);
    q.bindValue(":pname", a.processName);
    q.bindValue(":ppid",  static_cast<int>(a.processPid));
    q.bindValue(":desc",  a.description);
    q.bindValue(":sev",   a.severity);
    q.bindValue(":dtime", a.detectedAt.toString(Qt::ISODate));
    exec(q, "saveAnomaly");
}

void Database::acknowledgeAnomaly(const QString& id)
{
    if (!m_db.isOpen()) return;
    QSqlQuery q(m_db);
    q.prepare("UPDATE anomalies SET acknowledged=1 WHERE id=:id");
    q.bindValue(":id", id);
    exec(q, "acknowledgeAnomaly");
}

// ─────────────────────── Baseline queries ────────────────────────────────

double Database::getAverageCpu(int lastMinutes)
{
    if (!m_db.isOpen()) return -1.0;
    QSqlQuery q(m_db);
    q.prepare("SELECT AVG(cpu) FROM system_metrics "
              "WHERE ts >= datetime('now', :mins)");
    q.bindValue(":mins", QString("-%1 minutes").arg(lastMinutes));
    if (q.exec() && q.next())
        return q.value(0).toDouble();
    return -1.0;
}

double Database::getAverageRam(int lastMinutes)
{
    if (!m_db.isOpen()) return -1.0;
    QSqlQuery q(m_db);
    q.prepare("SELECT AVG(ram) FROM system_metrics "
              "WHERE ts >= datetime('now', :mins)");
    q.bindValue(":mins", QString("-%1 minutes").arg(lastMinutes));
    if (q.exec() && q.next())
        return q.value(0).toDouble();
    return -1.0;
}

int Database::getCrashCount(int lastDays)
{
    if (!m_db.isOpen()) return 0;
    QSqlQuery q(m_db);
    q.prepare("SELECT COUNT(*) FROM anomalies "
              "WHERE type = 'system_crash' AND detected_at >= datetime('now', :days)");
    q.bindValue(":days", QString("-%1 days").arg(lastDays));
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return 0;
}
