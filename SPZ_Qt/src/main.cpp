#include <QApplication>
#include "mainwindow.h"
#include "backend.h"
#include <windows.h>
#include <dbt.h>
#include <initguid.h>
#include <usbiodef.h> // GUID_DEVINTERFACE_USB_DEVICE

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Init COM for the main thread (needed for Network manager)
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    Backend backend;
    MainWindow window(&backend);

    // Register Device Notification for WM_DEVICECHANGE
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
    ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
    NotificationFilter.dbcc_size = sizeof(NotificationFilter);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE;
    
    RegisterDeviceNotification(reinterpret_cast<HANDLE>(window.winId()), &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

    window.show();
    int result = app.exec();

    CoUninitialize();
    return result;
}
