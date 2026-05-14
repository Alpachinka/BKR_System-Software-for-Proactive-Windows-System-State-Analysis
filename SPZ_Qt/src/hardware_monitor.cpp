#include "hardware_monitor.h"
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <thread>

HardwareMonitor::HardwareMonitor(QObject* parent) : QObject(parent) {
}

void HardwareMonitor::scanHardwareAsync() {
    // Run in a background thread to avoid blocking the main UI while powershell starts
    std::thread([this]() {
        this->runPowerShellScript();
    }).detach();
}

void HardwareMonitor::runPowerShellScript() {
    QProcess process;
    process.setProgram("powershell");
    
    QString script = R"(
        $ErrorActionPreference = 'SilentlyContinue'
        $cpu = Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, MaxClockSpeed
        $gpu = Get-CimInstance Win32_VideoController | Select-Object Name, AdapterRAM
        $ram = Get-CimInstance Win32_PhysicalMemory | Select-Object Manufacturer, Capacity, Speed
        $disk = Get-PhysicalDisk | Select-Object FriendlyName, MediaType, HealthStatus, Size
        
        $result = @{
            cpu = $cpu
            gpu = $gpu
            ram = $ram
            disk = $disk
        }
        $result | ConvertTo-Json -Depth 3 -Compress
    )";
    
    process.setArguments({"-NoProfile", "-Command", script});
    process.start();
    process.waitForFinished(15000); // Wait up to 15 seconds
    
    QByteArray output = process.readAllStandardOutput();
    
    QJsonDocument doc = QJsonDocument::fromJson(output);
    if (doc.isNull() || !doc.isObject()) {
        emit scanCompleted(m_lastResults);
        return;
    }
    
    QJsonObject root = doc.object();
    std::vector<HardwareComponent> results;
    
    // Parse CPU
    QJsonValue cpuVal = root["cpu"];
    if (cpuVal.isArray() || cpuVal.isObject()) {
        QJsonArray cpus;
        if (cpuVal.isObject()) cpus.append(cpuVal.toObject());
        else cpus = cpuVal.toArray();
        
        for (const QJsonValue& v : cpus) {
            QJsonObject obj = v.toObject();
            HardwareComponent comp;
            comp.type = "Процесор (CPU)";
            comp.name = obj["Name"].toString().trimmed();
            comp.details = QString("%1 Ядер, %2 МГц").arg(obj["NumberOfCores"].toInt()).arg(obj["MaxClockSpeed"].toInt());
            comp.status = "OK";
            results.push_back(comp);
        }
    }
    
    // Parse GPU
    QJsonValue gpuVal = root["gpu"];
    if (gpuVal.isArray() || gpuVal.isObject()) {
        QJsonArray gpus;
        if (gpuVal.isObject()) gpus.append(gpuVal.toObject());
        else gpus = gpuVal.toArray();
        
        for (const QJsonValue& v : gpus) {
            QJsonObject obj = v.toObject();
            HardwareComponent comp;
            comp.type = "Відеокарта (GPU)";
            comp.name = obj["Name"].toString().trimmed();
            qulonglong ramBytes = obj["AdapterRAM"].toVariant().toULongLong();
            comp.details = QString("%1 МБ Відеопам'яті").arg(ramBytes / (1024 * 1024));
            comp.status = "OK";
            results.push_back(comp);
        }
    }
    
    // Parse RAM
    QJsonValue ramVal = root["ram"];
    if (ramVal.isArray() || ramVal.isObject()) {
        QJsonArray rams;
        if (ramVal.isObject()) rams.append(ramVal.toObject());
        else rams = ramVal.toArray();
        
        int slot = 1;
        for (const QJsonValue& v : rams) {
            QJsonObject obj = v.toObject();
            HardwareComponent comp;
            comp.type = QString("Пам'ять (RAM Слот %1)").arg(slot++);
            comp.name = obj["Manufacturer"].toString().trimmed();
            if (comp.name.isEmpty() || comp.name == "Unknown") comp.name = "Невідомий виробник";
            qulonglong capBytes = obj["Capacity"].toVariant().toULongLong();
            comp.details = QString("%1 ГБ, %2 МГц").arg(capBytes / (1024ULL * 1024 * 1024)).arg(obj["Speed"].toInt());
            comp.status = "OK";
            results.push_back(comp);
        }
    }
    
    // Parse Disks
    QJsonValue diskVal = root["disk"];
    if (diskVal.isArray() || diskVal.isObject()) {
        QJsonArray disks;
        if (diskVal.isObject()) disks.append(diskVal.toObject());
        else disks = diskVal.toArray();
        
        for (const QJsonValue& v : disks) {
            QJsonObject obj = v.toObject();
            HardwareComponent comp;
            
            int mediaType = obj["MediaType"].toInt();
            QString typeStr = "Невідомий диск";
            if (mediaType == 3) typeStr = "Жорсткий диск (HDD)";
            else if (mediaType == 4) typeStr = "Твердотільний накопичувач (SSD)";
            
            comp.type = typeStr;
            comp.name = obj["FriendlyName"].toString().trimmed();
            
            qulonglong sizeBytes = obj["Size"].toVariant().toULongLong();
            QString health = obj["HealthStatus"].toString().trimmed();
            
            comp.details = QString("%1 ГБ").arg(sizeBytes / (1024ULL * 1024 * 1024));
            
            if (health.compare("Healthy", Qt::CaseInsensitive) == 0) {
                comp.status = "OK";
                comp.details += " | Здоров'я: Відмінне";
            } else if (health.compare("Warning", Qt::CaseInsensitive) == 0) {
                comp.status = "Warning";
                comp.details += " | Здоров'я: Увага (Можлива деградація)";
            } else {
                comp.status = "Critical";
                comp.details += " | Здоров'я: Критичний стан!";
            }
            
            results.push_back(comp);
        }
    }
    
    m_lastResults = results;
    emit scanCompleted(results);
}
