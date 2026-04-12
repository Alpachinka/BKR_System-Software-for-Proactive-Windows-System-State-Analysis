#include "alert_manager.h"
#include <QIcon>
#include <QApplication>
#include <QStyle>
#include <QMenu>
#include <QAction>

AlertManager::AlertManager(Database* db, RecommendEngine* recommender, QObject* parent)
    : QObject(parent), m_db(db), m_recommender(recommender)
{
    m_trayIcon = new QSystemTrayIcon(this);
    // Використовуємо стандартну іконку системи
    m_trayIcon->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    m_trayIcon->setToolTip("SPZ System Monitor");
    
    QMenu* trayMenu = new QMenu();
    QAction* openAction = trayMenu->addAction("Розгорнути (Open)");
    connect(openAction, &QAction::triggered, this, &AlertManager::showMainWindowRequested);
    
    QAction* exitAction = trayMenu->addAction("Вийти (Exit)");
    connect(exitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_trayIcon->setContextMenu(trayMenu);
    m_trayIcon->show();

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) {
            emit showMainWindowRequested();
        }
    });
}

AlertManager::~AlertManager()
{
    m_trayIcon->hide();
}

QList<Anomaly> AlertManager::activeAnomalies() const
{
    return m_activeAnomalies;
}

Recommendation AlertManager::getRecommendation(const QString& anomalyId) const
{
    return m_recommendations.value(anomalyId);
}

void AlertManager::onAnomalyDetected(const Anomaly& a)
{
    // Зберегти в базу
    m_db->saveAnomaly(a);

    // Додати в список активних
    m_activeAnomalies.prepend(a); // Нові зверху

    // Згенерувати рекомендацію
    Recommendation r = m_recommender->buildFor(a, {}); // Процеси можна передавати якщо треба глибокий аналіз
    m_recommendations.insert(a.id, r);

    // Сортування (Критичні нагорі)
    std::sort(m_activeAnomalies.begin(), m_activeAnomalies.end(),
              [](const Anomaly& x, const Anomaly& y) {
                  if (x.severity != y.severity)
                      return x.severity > y.severity;
                  return x.detectedAt > y.detectedAt;
              });

    // Показати Windows Toast
    showSystemToast(a, r);

    emit alertsChanged();
}

void AlertManager::showSystemToast(const Anomaly& a, const Recommendation& r)
{
    QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information;
    if (a.severity == 2) icon = QSystemTrayIcon::Warning;
    if (a.severity == 3) icon = QSystemTrayIcon::Critical;

    QString title = "Виявлено системну аномалію!";
    if (!r.shortTitle.isEmpty()) title = r.shortTitle;

    m_trayIcon->showMessage(title, a.description, icon, 5000); // Показувати 5 секунд
}

void AlertManager::acknowledgeAnomaly(const QString& id)
{
    for (int i = 0; i < m_activeAnomalies.size(); ++i) {
        if (m_activeAnomalies[i].id == id) {
            m_activeAnomalies.removeAt(i);
            m_recommendations.remove(id);
            m_db->acknowledgeAnomaly(id);
            emit alertsChanged();
            break;
        }
    }
}

void AlertManager::acknowledgeAll()
{
    for (const auto& a : qAsConst(m_activeAnomalies)) {
        m_db->acknowledgeAnomaly(a.id);
    }
    m_activeAnomalies.clear();
    m_recommendations.clear();
    emit alertsChanged();
}
