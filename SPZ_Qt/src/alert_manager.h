#pragma once
#include <QObject>
#include <QSystemTrayIcon>
#include <QList>
#include "anomaly_types.h"
#include "database.h"
#include "recommend_engine.h"

// ─────────────────────────────────────────────────────────────────────────
//  AlertManager — керує активними аномаліями, відображає Toast-сповіщення
//  в Windows, та надає дані для UI таблиці аномалій
// ─────────────────────────────────────────────────────────────────────────
class AlertManager : public QObject
{
    Q_OBJECT
public:
    explicit AlertManager(Database* db, RecommendEngine* recommender, QObject* parent = nullptr);
    ~AlertManager();

    // Отримати список активних (не прийнятих) аномалій
    QList<Anomaly> activeAnomalies() const;

    // Отримати рекомендацію для аномалії
    Recommendation getRecommendation(const QString& anomalyId) const;

public slots:
    // Викликається коли AnomalyEngine знаходить щось
    void onAnomalyDetected(const Anomaly& a);

    // Викликається з UI коли користувач натискає "Прийняти"
    void acknowledgeAnomaly(const QString& id);
    void acknowledgeAll();

signals:
    // Сигнал для оновлення UI
    void alertsChanged();

private:
    Database* m_db;
    RecommendEngine* m_recommender;
    QSystemTrayIcon* m_trayIcon;

    QList<Anomaly> m_activeAnomalies;
    QMap<QString, Recommendation> m_recommendations;

    void showSystemToast(const Anomaly& a, const Recommendation& r);
};
