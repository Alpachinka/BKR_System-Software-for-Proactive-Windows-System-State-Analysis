#include "settings_dialog.h"
#include <QLabel>
#include <QGroupBox>

SettingsDialog::SettingsDialog(SettingsManager* settings, QWidget* parent)
    : QDialog(parent), m_settings(settings)
{
    setWindowTitle("Налаштування");
    resize(420, 520);

    setupUI();
    loadFromManager();
}

void SettingsDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QTabWidget* tabWidget = new QTabWidget(this);
    mainLayout->addWidget(tabWidget);

    // --- Tab 1: General (Загальні) ---
    QWidget* tabGeneral = new QWidget();
    QFormLayout* formGen = new QFormLayout(tabGeneral);

    m_appTheme = new QComboBox(this);
    m_appTheme->addItem("Світла (Light)", "light");
    m_appTheme->addItem("Темна (Dark)", "dark");

    m_chartHistory = new QSpinBox(this);
    m_chartHistory->setRange(10, 3600);

    m_cooldownSecs = new QSpinBox(this);
    m_cooldownSecs->setRange(0, 3600);

    m_showTrustLevel = new QCheckBox("Показувати довіру (System/User)", this);
    
    m_processPromptLevel = new QComboBox(this);
    m_processPromptLevel->addItem("На всіх процесах", 0);
    m_processPromptLevel->addItem("Лише на Системних/Важливих", 1);
    m_processPromptLevel->addItem("Ніколи (Вимкнути запит)", 2);

    formGen->addRow("Тема інтерфейсу:", m_appTheme);
    formGen->addRow("Запит перед діями над процесом:", m_processPromptLevel);
    formGen->addRow("", m_showTrustLevel);
    formGen->addRow("Історія графіку (точок):", m_chartHistory);
    formGen->addRow("Кулдаун сповіщень (сек):", m_cooldownSecs);

    tabWidget->addTab(tabGeneral, "Загальні");

    // --- Tab 2: Anomalies ---
    QWidget* tabAnomalies = new QWidget();
    QFormLayout* formAno = new QFormLayout(tabAnomalies);

    m_cpuSpikeThreshold = new QDoubleSpinBox(this);
    m_cpuSpikeThreshold->setRange(1.0, 100.0);
    m_cpuSpikeTicks = new QSpinBox(this);
    m_cpuSpikeTicks->setRange(1, 1000);
    
    m_ramHighThreshold = new QDoubleSpinBox(this);
    m_ramHighThreshold->setRange(1.0, 100.0);
    m_ramHighTicks = new QSpinBox(this);
    m_ramHighTicks->setRange(1, 1000);

    m_gpuHighThreshold = new QDoubleSpinBox(this);
    m_gpuHighThreshold->setRange(1.0, 100.0);
    m_gpuHighTicks = new QSpinBox(this);
    m_gpuHighTicks->setRange(1, 1000);

    m_diskLowGb = new QDoubleSpinBox(this);
    m_diskLowGb->setRange(0.1, 1000.0);

    formAno->addRow("Поріг CPU (%):", m_cpuSpikeThreshold);
    formAno->addRow("Секунд пікового CPU:", m_cpuSpikeTicks);
    formAno->addRow("Поріг RAM (%):", m_ramHighThreshold);
    formAno->addRow("Секунд пікового RAM:", m_ramHighTicks);
    formAno->addRow("Поріг GPU (%):", m_gpuHighThreshold);
    formAno->addRow("Секунд пікового GPU:", m_gpuHighTicks);
    formAno->addRow("Критичний залишок Диску (ГБ):", m_diskLowGb);

    tabWidget->addTab(tabAnomalies, "Аномалії");

    // --- Tab 3: Baseline ---
    QWidget* tabSys = new QWidget();
    QFormLayout* formSys = new QFormLayout(tabSys);

    m_baselineDeviation = new QDoubleSpinBox(this);
    m_baselineDeviation->setRange(0.01, 2.0);
    m_baselineDeviation->setSingleStep(0.05);

    m_baselineTicks = new QSpinBox(this);
    m_baselineTicks->setRange(1, 1000);

    m_minSamples = new QSpinBox(this);
    m_minSamples->setRange(10, 10000);
    
    m_baselineWindow = new QSpinBox(this);
    m_baselineWindow->setRange(60, 86400);

    formSys->addRow("Відхилення базової лінії (x):", m_baselineDeviation);
    formSys->addRow("Секунд відхилення:", m_baselineTicks);
    formSys->addRow("Мін. зразків для трекера:", m_minSamples);
    formSys->addRow("Вікно трекера (сек):", m_baselineWindow);

    tabWidget->addTab(tabSys, "Аналіз");

    // --- Buttons ---
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* btnReset = new QPushButton("За замовчуванням");
    QPushButton* btnCancel = new QPushButton("Скасувати");
    QPushButton* btnSave = new QPushButton("Зберегти");

    btnLayout->addWidget(btnReset);
    btnLayout->addStretch();
    btnLayout->addWidget(btnCancel);
    btnLayout->addWidget(btnSave);
    mainLayout->addLayout(btnLayout);

    connect(btnSave, &QPushButton::clicked, this, &SettingsDialog::saveSettings);
    connect(btnCancel, &QPushButton::clicked, this, &SettingsDialog::reject);
    connect(btnReset, &QPushButton::clicked, this, &SettingsDialog::resetSettings);
}

void SettingsDialog::loadFromManager()
{
    m_cpuSpikeThreshold->setValue(m_settings->cpuSpikeThreshold);
    m_cpuSpikeTicks->setValue(m_settings->cpuSpikeTicks);
    m_ramHighThreshold->setValue(m_settings->ramHighThreshold);
    m_ramHighTicks->setValue(m_settings->ramHighTicks);
    m_gpuHighThreshold->setValue(m_settings->gpuHighThreshold);
    m_gpuHighTicks->setValue(m_settings->gpuHighTicks);
    m_diskLowGb->setValue(m_settings->diskLowGb);

    m_baselineDeviation->setValue(m_settings->baselineDeviation);
    m_baselineTicks->setValue(m_settings->baselineTicks);
    m_minSamples->setValue(m_settings->minSamples);
    m_baselineWindow->setValue(m_settings->baselineWindow);

    m_cooldownSecs->setValue(m_settings->cooldownSecs);
    m_chartHistory->setValue(m_settings->chartHistory);

    int themeIdx = m_appTheme->findData(m_settings->appTheme);
    if (themeIdx != -1) m_appTheme->setCurrentIndex(themeIdx);
    
    int promptIdx = m_processPromptLevel->findData(m_settings->processPromptLevel);
    if (promptIdx != -1) m_processPromptLevel->setCurrentIndex(promptIdx);
    
    m_showTrustLevel->setChecked(m_settings->showTrustLevel);
}

void SettingsDialog::resetSettings()
{
    m_settings->resetToDefaults();
    loadFromManager();
}

void SettingsDialog::saveSettings()
{
    m_settings->cpuSpikeThreshold = m_cpuSpikeThreshold->value();
    m_settings->cpuSpikeTicks     = m_cpuSpikeTicks->value();
    m_settings->ramHighThreshold  = m_ramHighThreshold->value();
    m_settings->ramHighTicks      = m_ramHighTicks->value();
    m_settings->gpuHighThreshold  = m_gpuHighThreshold->value();
    m_settings->gpuHighTicks      = m_gpuHighTicks->value();
    m_settings->diskLowGb         = m_diskLowGb->value();

    m_settings->baselineDeviation = m_baselineDeviation->value();
    m_settings->baselineTicks     = m_baselineTicks->value();
    m_settings->minSamples        = m_minSamples->value();
    m_settings->baselineWindow    = m_baselineWindow->value();

    m_settings->cooldownSecs      = m_cooldownSecs->value();
    m_settings->chartHistory      = m_chartHistory->value();

    m_settings->appTheme          = m_appTheme->currentData().toString();
    m_settings->processPromptLevel= m_processPromptLevel->currentData().toInt();
    m_settings->showTrustLevel    = m_showTrustLevel->isChecked();

    m_settings->save(); // this also emits settingsChanged()
    accept();
}
