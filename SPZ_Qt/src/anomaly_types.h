#pragma once
#include <QObject>
#include <QString>
#include <QDateTime>
#include <QUuid>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────
//  Anomaly — одиниця виявленої аномалії, яку генерує AnomalyEngine
// ─────────────────────────────────────────────────────────────────────────
struct Anomaly {
    QString   id           = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString   type;          // "cpu_spike" | "ram_pressure" | "gpu_overload"
                             // "disk_low"  | "suspicious_proc" | "baseline_deviation"
    QString   processName;   // якщо пов'язано з конкретним процесом
    unsigned long processPid  = 0;
    QString   description;
    int       severity    = 1; // 1=info, 2=warning, 3=critical
    QDateTime detectedAt  = QDateTime::currentDateTime();
    bool      acknowledged= false;

    QString severityLabel() const {
        switch (severity) {
        case 3:  return "🔴 Критично";
        case 2:  return "⚠️ Попередження";
        default: return "ℹ️ Інформація";
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────
//  Recommendation — рекомендація до конкретної аномалії
// ─────────────────────────────────────────────────────────────────────────
struct Recommendation {
    QString   anomalyId;
    QString   shortTitle;    // "Завершити процес chrome.exe"
    QString   longText;      // Детальний опис ситуації + що робити
    QString   actionLabel;   // "Завершити" | "Відкрити" | ""
    std::function<void()> action; // callback для кнопки (може бути nullptr)
};
