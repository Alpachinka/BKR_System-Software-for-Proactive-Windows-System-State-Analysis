#pragma once

#include <QDialog>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QLineEdit>
#include <QListWidget>

#include "settings_manager.h"

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(SettingsManager* settings, QWidget* parent = nullptr);

private slots:
    void saveSettings();
    void resetSettings();

private:
    SettingsManager* m_settings;

    QDoubleSpinBox* m_cpuSpikeThreshold;
    QSpinBox*       m_cpuSpikeTicks;
    QDoubleSpinBox* m_ramHighThreshold;
    QSpinBox*       m_ramHighTicks;
    QDoubleSpinBox* m_gpuHighThreshold;
    QSpinBox*       m_gpuHighTicks;
    QDoubleSpinBox* m_diskLowGb;
    
    QDoubleSpinBox* m_baselineDeviation;
    QSpinBox*       m_baselineTicks;
    QSpinBox*       m_minSamples;
    QSpinBox*       m_baselineWindow;
    
    QSpinBox*       m_cooldownSecs;
    QSpinBox*       m_chartHistory;
    QComboBox*      m_appTheme;
    QComboBox*      m_processPromptLevel;
    QCheckBox*      m_showTrustLevel;

    QLineEdit*      m_logSavePath;
    QListWidget*    m_watchedFoldersList;

    void setupUI();
    void loadFromManager();
};
