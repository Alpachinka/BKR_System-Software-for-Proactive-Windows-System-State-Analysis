#include "settings_manager.h"
#include <QCoreApplication>

SettingsManager::SettingsManager(QObject* parent) : QObject(parent)
{
    load();
}

void SettingsManager::resetToDefaults()
{
    cpuSpikeThreshold = 70.0;
    cpuSpikeTicks     = 15;
    ramHighThreshold  = 90.0;
    ramHighTicks      = 150;
    gpuHighThreshold  = 95.0;
    gpuHighTicks      = 120;
    diskLowGb         = 5.0;
    baselineDeviation = 0.40;
    baselineTicks     = 60;
    cooldownSecs      = 120;

    minSamples        = 300;
    baselineWindow    = 3600;
    chartHistory      = 60;
    
    appTheme          = "dark";
    processPromptLevel= 1; // 1 = Important/System only
    showTrustLevel    = true;
}

void SettingsManager::load()
{
    QSettings settings("spz_config.ini", QSettings::IniFormat);

    settings.beginGroup("AnomalyThresholds");
    cpuSpikeThreshold = settings.value("cpuSpikeThreshold", 70.0).toDouble();
    cpuSpikeTicks     = settings.value("cpuSpikeTicks", 15).toInt();
    ramHighThreshold  = settings.value("ramHighThreshold", 90.0).toDouble();
    ramHighTicks      = settings.value("ramHighTicks", 150).toInt();
    gpuHighThreshold  = settings.value("gpuHighThreshold", 95.0).toDouble();
    gpuHighTicks      = settings.value("gpuHighTicks", 120).toInt();
    diskLowGb         = settings.value("diskLowGb", 5.0).toDouble();
    baselineDeviation = settings.value("baselineDeviation", 0.40).toDouble();
    baselineTicks     = settings.value("baselineTicks", 60).toInt();
    cooldownSecs      = settings.value("cooldownSecs", 120).toInt();
    settings.endGroup();

    settings.beginGroup("Baseline");
    minSamples        = settings.value("minSamples", 300).toInt();
    baselineWindow    = settings.value("baselineWindow", 3600).toInt();
    settings.endGroup();

    settings.beginGroup("UI");
    chartHistory      = settings.value("chartHistory", 60).toInt();
    appTheme          = settings.value("appTheme", "dark").toString();
    processPromptLevel= settings.value("processPromptLevel", 1).toInt();
    showTrustLevel    = settings.value("showTrustLevel", true).toBool();
    settings.endGroup();
}

void SettingsManager::save()
{
    QSettings settings("spz_config.ini", QSettings::IniFormat);

    settings.beginGroup("AnomalyThresholds");
    settings.setValue("cpuSpikeThreshold", cpuSpikeThreshold);
    settings.setValue("cpuSpikeTicks", cpuSpikeTicks);
    settings.setValue("ramHighThreshold", ramHighThreshold);
    settings.setValue("ramHighTicks", ramHighTicks);
    settings.setValue("gpuHighThreshold", gpuHighThreshold);
    settings.setValue("gpuHighTicks", gpuHighTicks);
    settings.setValue("diskLowGb", diskLowGb);
    settings.setValue("baselineDeviation", baselineDeviation);
    settings.setValue("baselineTicks", baselineTicks);
    settings.setValue("cooldownSecs", cooldownSecs);
    settings.endGroup();

    settings.beginGroup("Baseline");
    settings.setValue("minSamples", minSamples);
    settings.setValue("baselineWindow", baselineWindow);
    settings.endGroup();

    settings.beginGroup("UI");
    settings.setValue("chartHistory", chartHistory);
    settings.setValue("appTheme", appTheme);
    settings.setValue("processPromptLevel", processPromptLevel);
    settings.setValue("showTrustLevel", showTrustLevel);
    settings.endGroup();

    emit settingsChanged();
}
