#pragma once

#include <QString>
#include <QVariantMap>
#include <vector>
#include <QObject>

struct HardwareComponent {
    QString type;      // "CPU", "GPU", "RAM", "Disk"
    QString name;      // e.g. "Intel Core i7-10700K", "NVIDIA RTX 3080", "Samsung SSD 970"
    QString details;   // e.g. "8 Cores, 3.8 GHz", "16GB, 3200MHz", "Healthy, NVMe"
    QString status;    // "OK", "Warning", "Critical", "Unknown"
};

class HardwareMonitor : public QObject {
    Q_OBJECT
public:
    explicit HardwareMonitor(QObject* parent = nullptr);
    
    // Call this to begin the asynchronous hardware scan
    void scanHardwareAsync();
    
    const std::vector<HardwareComponent>& getLastResults() const { return m_lastResults; }

signals:
    void scanCompleted(const std::vector<HardwareComponent>& results);

private:
    std::vector<HardwareComponent> m_lastResults;
    void runPowerShellScript();
};
