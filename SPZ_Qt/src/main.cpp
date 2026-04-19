#include <QApplication>
#include "mainwindow.h"
#include "backend.h"
#define NOMINMAX
#include <windows.h>
#include <dbt.h>
#include <initguid.h>
#include <usbiodef.h> // GUID_DEVINTERFACE_USB_DEVICE

#include "database.h"
#include "settings_manager.h"
#include "baseline_tracker.h"
#include "anomaly_engine.h"
#include "recommend_engine.h"
#include "process_scanner.h"
#include "alert_manager.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Init COM for the main thread (needed for Network manager)
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    // Load configuration
    SettingsManager settings;

    // Init Database first
    Database db;
    db.open();

    // Baseline Tracking
    BaselineTracker baseline(&settings);

    // AI/Anomaly Rules Engine
    AnomalyEngine anomalyEngine(&baseline, &settings);
    RecommendEngine recommender;
    ProcessScanner scanner;

    // Alert manager controls notifications
    AlertManager alertManager(&db, &recommender);

    // Connect ProcessScanner -> AlertManager
    QObject::connect(&scanner, &ProcessScanner::maliciousProcessDetected,
                     &alertManager, [&alertManager](const QString& processName, const QString& exePath, const ScanResult& result) {
        Anomaly a;
        a.type = "suspicious_proc";
        a.severity = 3; // Critical
        a.processName = processName;
        a.description = QString("Файл %1 класифіковано як шкідливий %2 з %3 антивірусами на VirusTotal.\nШлях: %4")
                            .arg(processName).arg(result.maliciousVotes).arg(result.totalVotes).arg(exePath);
        alertManager.onAnomalyDetected(a);
    });

    QObject::connect(&anomalyEngine, &AnomalyEngine::anomalyDetected, &alertManager, &AlertManager::onAnomalyDetected);

    Backend backend(&db, &baseline, &anomalyEngine, &scanner, &settings);
    MainWindow window(&backend, &alertManager, &anomalyEngine, &settings);

    // Register Device Notification for WM_DEVICECHANGE
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
    ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
    NotificationFilter.dbcc_size = sizeof(NotificationFilter);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE;
    
    RegisterDeviceNotification(reinterpret_cast<HANDLE>(window.winId()), &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

    QObject::connect(&alertManager, &AlertManager::showMainWindowRequested, &window, [&window]() {
        window.setWindowState((window.windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
        window.showNormal();
        window.raise();
        window.activateWindow();
    });

    window.show();
    int result = app.exec();

    CoUninitialize();
    return result;
}
