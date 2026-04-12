#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QSet>
#include <QMap>
#include <QDateTime>

// ─────────────────────────────────────────────────────────────────────────
//  ProcessScanner — перевіряє невідомі процеси через VirusTotal API
// ─────────────────────────────────────────────────────────────────────────
struct ScanResult {
    bool    isMalicious = false;
    int     maliciousVotes = 0;
    int     totalVotes = 0;
    QString errorMessage;
};

class ProcessScanner : public QObject
{
    Q_OBJECT
public:
    explicit ProcessScanner(QObject* parent = nullptr);

    // Ініціює перевірку файлу за його шляхом (асинхронно)
    void scanProcess(const QString& processName, const QString& exePath);

signals:
    // Викликається коли VT повертає результат, і файл є шкідливим
    void maliciousProcessDetected(const QString& processName, const QString& exePath, const ScanResult& result);

private:
    QNetworkAccessManager* m_netManager;

    // Власний міні-whitelist системних файлів, щоб не перевіряти зайве
    QSet<QString> m_whitelist;

    // Кеш хешів, щоб не перевіряти один і той же файл двічі за сесію
    QMap<QString, QDateTime> m_scannedHashes;

    QString calculateSHA256(const QString& filePath) const;
    void    checkVirusTotal(const QString& processName, const QString& exePath, const QString& hash);

    const QString VT_API_KEY = "b858f369159786e178a9f80bbd8e310be70676c82c6e6e425b9573001e292246"; // Real API key
};
