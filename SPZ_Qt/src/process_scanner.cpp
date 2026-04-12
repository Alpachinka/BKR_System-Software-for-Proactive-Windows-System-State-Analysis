#include "process_scanner.h"
#include <QFile>
#include <QCryptographicHash>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QFileInfo>

ProcessScanner::ProcessScanner(QObject* parent) : QObject(parent)
{
    m_netManager = new QNetworkAccessManager(this);

    // Whitelist системних файлів Windows (завжди безпечні)
    m_whitelist << "svchost.exe" << "explorer.exe" << "system" << "registry"
                << "smss.exe" << "csrss.exe" << "wininit.exe" << "services.exe"
                << "lsass.exe" << "winlogon.exe" << "dwm.exe" << "taskmgr.exe"
                << "spoolsv.exe" << "sihost.exe" << "fontdrvhost.exe";
}

void ProcessScanner::scanProcess(const QString& processName, const QString& exePath)
{
    // 1. Фільтр: Whitelist
    if (m_whitelist.contains(processName.toLower())) return;

    // 2. Фільтр: Тільки файли з диску C:\ (не можемо читати system space)
    if (exePath.isEmpty() || !QFile::exists(exePath)) return;

    // 3. Хешування файлу
    QString hash = calculateSHA256(exePath);
    if (hash.isEmpty()) return;

    // 4. Фільтр: Кеш
    if (m_scannedHashes.contains(hash)) return;
    m_scannedHashes[hash] = QDateTime::currentDateTime(); // запам'ятовуємо

    // 5. Відправка на VirusTotal (якщо є ключ)
    if (VT_API_KEY.startsWith("ВАШ")) {
        // Якщо ключ не введено, просто симулюємо підозру для файлів з Temp
        if (exePath.toLower().contains("\\appdata\\local\\temp\\")) {
            ScanResult res;
            res.isMalicious = true;
            res.maliciousVotes = 3;
            res.totalVotes = 70;
            emit maliciousProcessDetected(processName, exePath, res);
        }
        return;
    }

    checkVirusTotal(processName, exePath, hash);
}

QString ProcessScanner::calculateSHA256(const QString& filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return QString();

    // Читаємо файл невеликими блоками для уникнення зависань на великих файлах
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (hash.addData(&file)) {
        return QString(hash.result().toHex());
    }
    return QString();
}

void ProcessScanner::checkVirusTotal(const QString& processName, const QString& exePath, const QString& hash)
{
    // Документація: https://developers.virustotal.com/reference/file-info
    QUrl url("https://www.virustotal.com/api/v3/files/" + hash);
    QNetworkRequest request(url);
    request.setRawHeader("x-apikey", VT_API_KEY.toUtf8());

    QNetworkReply* reply = m_netManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, processName, exePath]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            // Файл не знайдено в базі VT (це нормально) або помилка мережі
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) return;

        QJsonObject root = doc.object();
        QJsonObject attrs = root["data"].toObject()["attributes"].toObject();
        QJsonObject stats = attrs["last_analysis_stats"].toObject();

        int malicious  = stats["malicious"].toInt();
        int suspicious = stats["suspicious"].toInt();
        int harmless   = stats["harmless"].toInt();
        int total      = malicious + suspicious + harmless + stats["undetected"].toInt();

        // Якщо хоча б 3 антивіруси сказали що це вірус:
        if (malicious + suspicious >= 3) {
            ScanResult res;
            res.isMalicious = true;
            res.maliciousVotes = malicious + suspicious;
            res.totalVotes = total;

            emit maliciousProcessDetected(processName, exePath, res);
        }
    });
}
