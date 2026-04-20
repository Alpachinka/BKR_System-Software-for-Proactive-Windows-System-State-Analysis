#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "anomaly_types.h"
#include "recommend_engine.h"

// ─────────────────────────────────────────────────────────────────────────
// AnomalyDetailsDialog - Detailed view for an individual anomaly
// ─────────────────────────────────────────────────────────────────────────
class AnomalyDetailsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AnomalyDetailsDialog(const Anomaly& a, const Recommendation& r, QWidget* parent = nullptr);

signals:
    void actionTriggered();
    void acknowledgeTriggered();

private:
    void setupUI(const Anomaly& a, const Recommendation& r);
};

// ─────────────────────────────────────────────────────────────────────────
// AnomaliesHelpDialog - Summary of tracked anomalies
// ─────────────────────────────────────────────────────────────────────────
class AnomaliesHelpDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AnomaliesHelpDialog(QWidget* parent = nullptr);

private:
    void setupUI();
};
