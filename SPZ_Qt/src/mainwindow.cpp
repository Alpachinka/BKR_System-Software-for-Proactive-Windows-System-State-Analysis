#include "mainwindow.h"
#include "settings_dialog.h"
#include "anomaly_dialogs.h"
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QChart>
#include <QHeaderView>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QMenu>
#include <QAction>
#include <QDir>
#include <QDateTime>
#include <windows.h>
#include <dbt.h>
#include <initguid.h>
#include <usbiodef.h>

using namespace Qt::StringLiterals;

// ─────────────────────────────── constructor ──────────────────────────
MainWindow::MainWindow(Backend* backend, AlertManager* alerts, AnomalyEngine* anomalyEx, SettingsManager* settings, QWidget *parent)
    : QMainWindow(parent), m_backend(backend), m_alerts(alerts), m_anomalyEngine(anomalyEx), m_settings(settings)
{
    setupUI();
    applyModernStyle();

    // Connect Settings changed signal to re-apply style and table headers
    connect(m_settings, &SettingsManager::settingsChanged, this, [this]() {
        applyModernStyle();
        m_processTable->setColumnHidden(3, !m_settings->showTrustLevel);
    });

    connect(m_backend, &Backend::processesUpdated,  this, &MainWindow::updateProcesses);
    connect(m_backend, &Backend::systemInfoUpdated, this, &MainWindow::updateSystemInfo);
    connect(m_backend, &Backend::processEventLogged, this,
        [this](const QString& t, const QString& e, const QString& d){ appendLog(m_processLogTable, t, e, d); });
    connect(m_backend, &Backend::systemEventLogged, this,
        [this](const QString& t, const QString& e, const QString& d){ appendLog(m_sysLogTable, t, e, d); });
    connect(m_backend, &Backend::networkEventLogged, this,
        [this](const QString& t, const QString& e, const QString& d){ appendLog(m_networkLogTable, t, e, d); });
    connect(m_backend, &Backend::fileSystemEventLogged, this,
        [this](const QString& t, const QString& e, const QString& d){ appendLog(m_fileLogTable, t, e, d); });

    connect(m_anomalyEngine, &AnomalyEngine::healthScoreChanged, this, &MainWindow::updateHealthScore);
    connect(m_alerts, &AlertManager::alertsChanged, this, &MainWindow::refreshAnomaliesUI);
    connect(m_backend, &Backend::connectionsUpdated, this, &MainWindow::updateNetworkConnections);
    connect(m_backend, &Backend::startupSnapshotReady, this, &MainWindow::refreshStartupTable);
    connect(m_backend, &Backend::startupEntryChanged, this, &MainWindow::onStartupChanged);

    connect(m_btnSave,  &QPushButton::clicked, this, &MainWindow::saveLogsToCsv);
    connect(m_btnClear, &QPushButton::clicked, this, &MainWindow::clearCurrentLog);

    m_backend->startMonitoring();
}
MainWindow::~MainWindow() {}

// ─────────────────────────────── combined resource chart ─────────────

QChartView* MainWindow::createResourceChart()
{
    m_cpuSeries = new QLineSeries(this);
    m_cpuSeries->setName("CPU %");
    m_cpuSeries->setColor(QColor("#4fc3f7"));
    m_cpuSeries->pen().setWidthF(1.8);

    m_ramSeries = new QLineSeries(this);
    m_ramSeries->setName("RAM %");
    m_ramSeries->setColor(QColor("#81c784"));
    m_ramSeries->pen().setWidthF(1.8);

    m_gpuSeries = new QLineSeries(this);
    m_gpuSeries->setName("GPU %");
    m_gpuSeries->setColor(QColor("#ff8a65"));
    m_gpuSeries->pen().setWidthF(1.8);

    m_resourceChart = new QChart();
    m_resourceChart->addSeries(m_cpuSeries);
    m_resourceChart->addSeries(m_ramSeries);
    m_resourceChart->addSeries(m_gpuSeries);
    m_resourceChart->setBackgroundBrush(QBrush(QColor("#1e1e1e")));
    m_resourceChart->setPlotAreaBackgroundBrush(QBrush(QColor("#252526")));
    m_resourceChart->setPlotAreaBackgroundVisible(true);
    m_resourceChart->legend()->setLabelColor(Qt::white);
    m_resourceChart->legend()->setAlignment(Qt::AlignTop);
    m_resourceChart->setTitle("Навантаження системи (останні 60 с)");
    m_resourceChart->setTitleBrush(QBrush(QColor("#d4d4d4")));
    m_resourceChart->setMargins(QMargins(0, 0, 0, 0));

    auto* axisX = new QValueAxis();
    axisX->setRange(0, CHART_HISTORY);
    axisX->setLabelsVisible(false);
    axisX->setGridLineColor(QColor("#2a2a2a"));
    axisX->setLinePenColor(QColor("#444"));

    auto* axisY = new QValueAxis();
    axisY->setRange(0, 100);
    axisY->setLabelFormat("%d%%");
    axisY->setLabelsColor(QColor("#9d9d9d"));
    axisY->setGridLineColor(QColor("#2a2a2a"));
    axisY->setLinePenColor(QColor("#444"));
    axisY->setTickCount(6);

    m_resourceChart->addAxis(axisX, Qt::AlignBottom);
    m_resourceChart->addAxis(axisY, Qt::AlignLeft);
    m_cpuSeries->attachAxis(axisX); m_cpuSeries->attachAxis(axisY);
    m_ramSeries->attachAxis(axisX); m_ramSeries->attachAxis(axisY);
    m_gpuSeries->attachAxis(axisX); m_gpuSeries->attachAxis(axisY);

    auto* view = new QChartView(m_resourceChart, this);
    view->setRenderHint(QPainter::Antialiasing);
    view->setMinimumHeight(220);
    return view;
}

// ─────────────────────────────── Anomalies tab ────────────────────────

QWidget* MainWindow::buildAnomaliesTab()
{
    QWidget* tab = new QWidget(this);
    auto* lay    = new QVBoxLayout(tab);
    lay->setSpacing(8);
    lay->setContentsMargins(8, 8, 8, 8);

    auto* topBar = new QHBoxLayout();
    m_healthScoreLabel = new QLabel("Health Score: 100/100", this);
    m_healthScoreLabel->setStyleSheet("font-weight: bold; color: #a5d6a7; font-size: 14px;");

    auto* btnAckAll = new QPushButton("Ігнорувати всі", this);
    connect(btnAckAll, &QPushButton::clicked, this, &MainWindow::ackAllAnomalies);

    auto* btnHelp = new QPushButton("ℹ️ Довідка по аномаліям", this);
    btnHelp->setStyleSheet("background: #0078d7; padding: 4px 10px; font-weight: bold; border-radius: 4px;");
    connect(btnHelp, &QPushButton::clicked, this, [this]() {
        AnomaliesHelpDialog dlg(this);
        dlg.exec();
    });

    topBar->addWidget(m_healthScoreLabel);
    topBar->addStretch();
    topBar->addWidget(btnHelp);
    topBar->addWidget(btnAckAll);
    lay->addLayout(topBar);

    m_anomaliesTable = new QTableWidget(0, 4, this);
    m_anomaliesTable->setHorizontalHeaderLabels({"Тип", "Опис", "Критичність", "Дія"});
    m_anomaliesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_anomaliesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_anomaliesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_anomaliesTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_anomaliesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_anomaliesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_anomaliesTable, 1);

    // Placeholder when no anomalies
    m_noAnomaliesLabel = new QLabel(this);
    m_noAnomaliesLabel->setText(
        "✅  Система працює стабільно\n\n"
        "Аномалій не виявлено. Монітор продовжує аналіз у фоновому режимі.\n"
        "Якщо будуть виявлені відхилення — вони з'являться тут автоматично.");
    m_noAnomaliesLabel->setAlignment(Qt::AlignCenter);
    m_noAnomaliesLabel->setStyleSheet("font-size: 14px; color: #888; padding: 40px;");
    m_noAnomaliesLabel->setWordWrap(true);
    lay->addWidget(m_noAnomaliesLabel);

    return tab;
}

// ─────────────────────────────── System tab ───────────────────────────

static QString queryRegistryString(HKEY root, const wchar_t* path, const wchar_t* key)
{
    wchar_t buf[512] = {};
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, path, key, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
        return QString::fromWCharArray(buf);
    return "—";
}

static DWORD queryRegistryDWORD(HKEY root, const wchar_t* path, const wchar_t* key, DWORD defaultVal = 0)
{
    DWORD val = 0;
    DWORD size = sizeof(val);
    if (RegGetValueW(root, path, key, RRF_RT_REG_DWORD, nullptr, &val, &size) == ERROR_SUCCESS)
        return val;
    return defaultVal;
}

QWidget* MainWindow::buildSystemTab()
{
    QWidget* tab = new QWidget(this);
    auto* lay    = new QVBoxLayout(tab);
    lay->setSpacing(8);
    lay->setContentsMargins(8, 8, 8, 8);

    // ── Hardware info table ──
    auto* hwTable = new QTableWidget(0, 2, this);
    hwTable->setHorizontalHeaderLabels({"Компонент", "Значення"});
    hwTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    hwTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    hwTable->setMaximumHeight(180);

    auto addHwRow = [&](const QString& param, const QString& value) {
        int r = hwTable->rowCount();
        hwTable->insertRow(r);
        hwTable->setItem(r, 0, new QTableWidgetItem(param));
        hwTable->setItem(r, 1, new QTableWidgetItem(value));
    };

    // CPU
    addHwRow("Процесор (CPU)", queryRegistryString(
        HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"ProcessorNameString"));

    // RAM total
    MEMORYSTATUSEX memStat{};
    memStat.dwLength = sizeof(memStat);
    GlobalMemoryStatusEx(&memStat);
    addHwRow("Оперативна пам'ять (RAM)",
        QString::number(memStat.ullTotalPhys / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " ГБ");

    // GPU
    addHwRow("Відеоадаптер (GPU)", queryRegistryString(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000",
        L"DriverDesc"));

    // Motherboard
    addHwRow("Материнська плата", queryRegistryString(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\HardwareConfig\\Current",
        L"BaseBoardProduct"));

    // OS
    addHwRow("Операційна система", queryRegistryString(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        L"ProductName"));

    // Computer name
    wchar_t compName[256];
    DWORD compSize = 256;
    GetComputerNameW(compName, &compSize);
    addHwRow("Ім'я комп'ютера", QString::fromWCharArray(compName));

    lay->addWidget(hwTable);

    // ── Live metrics table ──
    m_sysInfoTable = new QTableWidget(0, 2, this);
    m_sysInfoTable->setHorizontalHeaderLabels({"Параметр", "Значення"});
    m_sysInfoTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_sysInfoTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sysInfoTable->setMaximumHeight(120);
    lay->addWidget(m_sysInfoTable);

    // ── Three compact progress bars ──
    auto addBar = [&](const QString& label, QProgressBar*& bar, const QString& color) {
        auto* row = new QHBoxLayout();
        auto* lbl = new QLabel(label, this);
        lbl->setFixedWidth(42);
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bar = new QProgressBar(this);
        bar->setRange(0, 100);
        bar->setFormat("%v%");
        bar->setFixedHeight(18);
        bar->setStyleSheet(QString(
            "QProgressBar::chunk { background: %1; border-radius: 3px; }").arg(color));
        row->addWidget(lbl);
        row->addWidget(bar);
        lay->addLayout(row);
    };
    addBar("CPU",  m_cpuProgress, "#4fc3f7");
    addBar("RAM",  m_ramProgress, "#81c784");
    addBar("GPU",  m_gpuProgress, "#ff8a65");

    // ── Single combined chart ──
    lay->addWidget(createResourceChart(), 1);

    return tab;
}

// ─────────────────────────────── Network tab ──────────────────────────

QWidget* MainWindow::buildNetworkTab()
{
    QWidget* tab = new QWidget(this);
    auto* lay = new QVBoxLayout(tab);
    lay->setSpacing(8);
    lay->setContentsMargins(8, 8, 8, 8);

    QLabel* title = new QLabel("Активні мережеві з'єднання (Network Analyzer)", this);
    title->setStyleSheet("font-size: 16px; font-weight: bold; margin-bottom: 4px;");
    lay->addWidget(title);

    m_networkConnTable = new QTableWidget(0, 5, this);
    m_networkConnTable->setHorizontalHeaderLabels({"Протокол", "Локальна адреса", "Віддалена адреса", "Стан", "PID"});
    m_networkConnTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_networkConnTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_networkConnTable->setSortingEnabled(true);
    lay->addWidget(m_networkConnTable);

    return tab;
}

// ─────────────────────────────── Startup tab ───────────────────────

QWidget* MainWindow::buildStartupTab()
{
    QWidget* tab = new QWidget(this);
    auto* lay = new QVBoxLayout(tab);
    lay->setSpacing(8);
    lay->setContentsMargins(10, 10, 10, 10);

    // Header row
    auto* headerRow = new QHBoxLayout();
    QLabel* title = new QLabel("Моніторинг Автозавантаження", this);
    title->setStyleSheet("font-size: 18px; font-weight: bold;");
    headerRow->addWidget(title);
    headerRow->addStretch();

    QLabel* hint = new QLabel("📡 Зміни відстежуються в реальному часі (HKCU + HKLM)", this);
    hint->setStyleSheet("color: #888; font-size: 12px;");
    headerRow->addWidget(hint);
    lay->addLayout(headerRow);

    lay->addSpacing(4);

    // Table
    m_startupTable = new QTableWidget(0, 4, this);
    m_startupTable->setHorizontalHeaderLabels({"Кущ (Hive)", "Назва запису", "Шлях / Команда", "Статус"});
    m_startupTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_startupTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_startupTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_startupTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_startupTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_startupTable->setSortingEnabled(true);
    m_startupTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_startupTable->setAlternatingRowColors(true);
    lay->addWidget(m_startupTable, 1);

    // Info label at bottom
    QLabel* info = new QLabel(
        "ℹ️  Записи в HKLM мають право на зміну лише адміністратори. "
        "Поява нових записів — можливий признак шкідливого ПЗ.", this);
    info->setWordWrap(true);
    info->setStyleSheet("color: #aaa; font-size: 12px; padding: 4px;");
    lay->addWidget(info);

    return tab;
}

// ─────────────────────────────── Security tab ─────────────────────────

QWidget* MainWindow::buildSecurityTab()
{
    QWidget* tab = new QWidget(this);
    auto* lay = new QVBoxLayout(tab);
    lay->setSpacing(10);
    lay->setContentsMargins(16, 16, 16, 16);

    auto* headerLay = new QHBoxLayout();
    QLabel* title = new QLabel("Аудит безпеки Windows", this);
    title->setStyleSheet("font-size: 20px; font-weight: bold; margin-bottom: 10px;");
    headerLay->addWidget(title);
    headerLay->addStretch();
    
    QPushButton* btnRefresh = new QPushButton("🔄 Оновити", this);
    btnRefresh->setCursor(Qt::PointingHandCursor);
    btnRefresh->setStyleSheet("QPushButton { padding: 5px 15px; font-weight: bold; background: #3a3f4b; border: 1px solid #555; border-radius: 4px; }"
                              "QPushButton:hover { background: #4a4f5b; }");
    connect(btnRefresh, &QPushButton::clicked, this, &MainWindow::refreshSecurityTab);
    headerLay->addWidget(btnRefresh);
    
    lay->addLayout(headerLay);

    int score = 0;
    int maxScore = 0;

    auto addCheck = [&](const QString& name, bool isOk, const QString& okMsg, const QString& badMsg) {
        maxScore += 25;
        if (isOk) score += 25;

        QFrame* card = new QFrame(this);
        card->setStyleSheet("QFrame { background: #1e1e1e; border-radius: 8px; padding: 10px; }");
        auto* cardLay = new QHBoxLayout(card);
        
        QLabel* icon = new QLabel(isOk ? "✅" : "❌", this);
        icon->setStyleSheet("font-size: 24px;");
        cardLay->addWidget(icon);

        auto* textLay = new QVBoxLayout();
        QLabel* lblName = new QLabel(name, this);
        lblName->setStyleSheet("font-size: 16px; font-weight: bold;");
        QLabel* lblDesc = new QLabel(isOk ? okMsg : badMsg, this);
        lblDesc->setStyleSheet(isOk ? "color: #81c784; font-size: 14px;" : "color: #e57373; font-size: 14px;");
        textLay->addWidget(lblName);
        textLay->addWidget(lblDesc);
        
        cardLay->addLayout(textLay);
        cardLay->addStretch();
        
        lay->addWidget(card);
    };

    // 1. UAC (User Account Control)
    DWORD uac = queryRegistryDWORD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", L"EnableLUA", 1);
    addCheck("User Account Control (UAC)", uac == 1, 
             "UAC увімкнено. Система захищена від несанкціонованих змін.", 
             "УВАГА: UAC вимкнено! Будь-яка програма може отримати права адміністратора.");

    // 2. Windows Defender (AntiSpyware)
    DWORD def = queryRegistryDWORD(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows Defender", L"DisableAntiSpyware", 0);
    addCheck("Антивірусний захист (Windows Defender)", def == 0, 
             "Windows Defender активний.", 
             "УВАГА: Windows Defender вимкнено через політики!");

    // 3. Secure Boot
    DWORD sb = queryRegistryDWORD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State", L"UEFISecureBootEnabled", 0);
    addCheck("Secure Boot (Безпечне завантаження)", sb == 1, 
             "Secure Boot увімкнено. Захист від руткітів активний.", 
             "Secure Boot вимкнено або не підтримується.");

    // 4. Windows Firewall
    DWORD fw = queryRegistryDWORD(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\SharedAccess\\Parameters\\FirewallPolicy\\StandardProfile", L"EnableFirewall", 1);
    addCheck("Брандмауер Windows (Firewall)", fw == 1, 
             "Брандмауер увімкнено. Мережеві атаки блокуються.", 
             "УВАГА: Брандмауер для стандартного профілю вимкнено!");

    lay->addSpacing(20);
    
    // Overall Score
    QLabel* scoreLabel = new QLabel(QString("Загальна оцінка безпеки: %1 / %2").arg(score).arg(maxScore), this);
    QString scoreColor = (score == maxScore) ? "#81c784" : ((score >= 50) ? "#ffb74d" : "#e57373");
    scoreLabel->setStyleSheet(QString("font-size: 24px; font-weight: bold; color: %1;").arg(scoreColor));
    scoreLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(scoreLabel);

    lay->addStretch();

    return tab;
}

void MainWindow::refreshSecurityTab()
{
    if (!m_securityTabWidget) return;
    
    int index = 1; // "Аудит Безпеки"
    QWidget* oldTab = m_securityTabWidget->widget(index);
    if (!oldTab) return;
    
    m_securityTabWidget->blockSignals(true);
    bool wasCurrent = (m_securityTabWidget->currentIndex() == index);
    
    QWidget* newTab = buildSecurityTab();
    m_securityTabWidget->removeTab(index);
    m_securityTabWidget->insertTab(index, newTab, "🔒  Аудит Безпеки");
    
    if (wasCurrent) {
        m_securityTabWidget->setCurrentIndex(index);
    }
    
    oldTab->deleteLater();
    m_securityTabWidget->blockSignals(false);
}

// ─────────────────────────────── setupUI ──────────────────────────────

void MainWindow::setupUI()
{
    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    auto* mainLay = new QVBoxLayout(central);
    mainLay->setContentsMargins(6, 6, 6, 6);
    mainLay->setSpacing(4);

    // ── Outer tab widget (3 groups) ────────────────────────────────────
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setDocumentMode(false);
    mainLay->addWidget(m_tabWidget, 1);

    // Shared stylesheet for inner (nested) tab widgets
    const QString innerStyle =
        "QTabWidget::pane { border: 1px solid #333; }"
        "QTabBar::tab { padding: 5px 16px; margin-right: 2px; border-radius: 4px 4px 0 0; background: #2b2b2b; color: #aaa; }"
        "QTabBar::tab:selected { background: #3a3f4b; color: #fff; font-weight: bold; }"
        "QTabBar::tab:hover { background: #35393f; color: #ddd; }";

    // ══════════════════════════════════════════════════
    //  TAB 1 — 📊 МОНІТОРИНГ
    // ══════════════════════════════════════════════════
    {
        QWidget* monWidget = new QWidget(this);
        auto* monLay = new QVBoxLayout(monWidget);
        monLay->setContentsMargins(4, 6, 4, 4);
        monLay->setSpacing(0);

        auto* monTabs = new QTabWidget(monWidget);
        monTabs->setStyleSheet(innerStyle);
        monLay->addWidget(monTabs);

        // ── Sub-tab: Процеси ────────────────────────
        m_processTable = new QTableWidget(0, 4, this);
        m_processTable->setHorizontalHeaderLabels({"Ім'я процесу", "Пам'ять (МБ)", "CPU (%)", "Довіра"});
        m_processTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_processTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_processTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_processTable->setSortingEnabled(true);
        m_processTable->setColumnHidden(3, !m_settings->showTrustLevel);
        m_processTable->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_processTable, &QTableWidget::customContextMenuRequested,
                this, &MainWindow::showProcessContextMenu);
        monTabs->addTab(m_processTable, "🖥  Процеси");

        // ── Sub-tab: Ресурси ────────────────────────
        monTabs->addTab(buildSystemTab(), "📈  Ресурси");

        // ── Sub-tab: Мережа ─────────────────────────
        monTabs->addTab(buildNetworkTab(), "🌐  Мережа");

        m_tabWidget->addTab(monWidget, "📊  Моніторинг");
    }

    // ══════════════════════════════════════════════════
    //  TAB 2 — 🛡 БЕЗПЕКА
    // ══════════════════════════════════════════════════
    {
        QWidget* secWidget = new QWidget(this);
        auto* secLay = new QVBoxLayout(secWidget);
        secLay->setContentsMargins(4, 6, 4, 4);
        secLay->setSpacing(0);

        auto* secTabs = new QTabWidget(secWidget);
        secTabs->setStyleSheet(innerStyle);
        secLay->addWidget(secTabs);

        // ── Sub-tab: Аномалії ───────────────────────
        secTabs->addTab(buildAnomaliesTab(), "⚠  Аномалії та Поради");

        // ── Sub-tab: Аудит безпеки ──────────────────
        secTabs->addTab(buildSecurityTab(), "🔒  Аудит Безпеки");

        // ── Sub-tab: Автозавантаження ───────────────
        secTabs->addTab(buildStartupTab(), "🚀  Автозавантаження");
        // Store pointer to secTabs so onStartupChanged can flash the tab
        m_securityTabWidget = secTabs;

        m_tabWidget->addTab(secWidget, "🛡  Безпека");
        
        // Auto-refresh when opening the security tab
        connect(secTabs, &QTabWidget::currentChanged, this, [this](int index) {
            if (index == 1) { // 1 is "Аудит Безпеки"
                refreshSecurityTab();
            }
        });
        
        connect(m_tabWidget, &QTabWidget::currentChanged, this, [this, secTabs](int index) {
            if (index == 1 && secTabs->currentIndex() == 1) { // Outer is "Безпека", Inner is "Аудит Безпеки"
                refreshSecurityTab();
            }
        });
    }

    // ══════════════════════════════════════════════════
    //  TAB 3 — 📋 ЛОГИ
    // ══════════════════════════════════════════════════
    {
        QWidget* logsTab = new QWidget(this);
        auto* logsLay = new QVBoxLayout(logsTab);
        logsLay->setContentsMargins(4, 6, 4, 4);
        logsLay->setSpacing(4);

        m_logsTabWidget = new QTabWidget(logsTab);
        m_logsTabWidget->setStyleSheet(innerStyle);

        m_processLogTable = createLogTable();
        m_logsTabWidget->addTab(m_processLogTable, "📋  Процеси");

        m_sysLogTable = createLogTable();
        m_logsTabWidget->addTab(m_sysLogTable, "🖥  Система");

        m_networkLogTable = createLogTable();
        m_logsTabWidget->addTab(m_networkLogTable, "🌐  Мережа");

        m_fileLogTable = createLogTable();
        m_logsTabWidget->addTab(m_fileLogTable, "📁  Файли");

        logsLay->addWidget(m_logsTabWidget, 1);

        // Save/Clear buttons only visible in Logs tab
        auto* btnLay = new QHBoxLayout();
        btnLay->addStretch();
        m_btnClear = new QPushButton("Очистити", this);
        m_btnSave  = new QPushButton("💾  Зберегти CSV", this);
        btnLay->addWidget(m_btnClear);
        btnLay->addWidget(m_btnSave);
        logsLay->addLayout(btnLay);

        m_tabWidget->addTab(logsTab, "📋  Логи");
    }

    // ── Settings overlay button ───────────────────────────────────────
    auto* btnSettings = new QPushButton(m_tabWidget);
    btnSettings->setText(QString(QChar(0x2699)));
    btnSettings->setToolTip("Налаштування програми");
    btnSettings->setFixedSize(30, 26);
    btnSettings->setStyleSheet(
        "QPushButton { background: transparent; border: none; color: #888; "
        "font-family: 'Segoe UI Symbol', 'Segoe UI', Arial; font-size: 16px; "
        "margin: 0px; padding: 0px; }"
        "QPushButton:hover { color: #fff; background: #333; border-radius: 4px; }"
    );
    connect(btnSettings, &QPushButton::clicked, this, &MainWindow::showSettingsDialog);
    m_tabWidget->installEventFilter(this);
    m_settingsBtn = btnSettings;

    setWindowTitle("SPZ: Системний Монітор  ·  Qt6 Edition");
    resize(1100, 750);
}

QTableWidget* MainWindow::createLogTable()
{
    auto* t = new QTableWidget(0, 3, this);
    t->setHorizontalHeaderLabels({"Час", "Подія", "Деталі"});
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    return t;
}

// ─────────────────────────────── update slots ─────────────────────────

void MainWindow::updateProcesses(const std::vector<ProcessData>& procs)
{
    // Block sorting temporarily for smooth update
    m_processTable->setSortingEnabled(false);
    m_processTable->setRowCount(static_cast<int>(procs.size()));

    for (int i = 0; i < static_cast<int>(procs.size()); ++i) {
        const auto& p = procs[i];
        auto* nameItem = new QTableWidgetItem(p.name);
        nameItem->setData(Qt::UserRole, static_cast<qulonglong>(p.pid));
        auto* memItem  = new QTableWidgetItem(
            QString::number(p.memUsageMB, 'f', 2) + " МБ");
        auto* cpuItem  = new QTableWidgetItem(
            QString::number(p.cpuUsagePercent, 'f', 1) + " %");

        // Make RAM and CPU sortable numerically
        memItem->setData(Qt::UserRole, p.memUsageMB);
        cpuItem->setData(Qt::UserRole, p.cpuUsagePercent);

        auto* itemTrust = new QTableWidgetItem();
        itemTrust->setText(p.isSystem ? "Системний" : "Користувацький");
        if (p.isSystem) itemTrust->setForeground(QBrush(QColor("#4fc3f7")));
        else itemTrust->setForeground(QBrush(Qt::white));

        m_processTable->setItem(i, 0, nameItem);
        m_processTable->setItem(i, 1, memItem);
        m_processTable->setItem(i, 2, cpuItem);
        m_processTable->setItem(i, 3, itemTrust);
    }
    m_processTable->setSortingEnabled(true);
}

void MainWindow::updateNetworkConnections(const std::vector<NetworkConnection>& conns)
{
    m_networkConnTable->setSortingEnabled(false);
    m_networkConnTable->setRowCount(static_cast<int>(conns.size()));

    for (int i = 0; i < static_cast<int>(conns.size()); ++i) {
        const auto& c = conns[i];
        m_networkConnTable->setItem(i, 0, new QTableWidgetItem(c.protocol));
        m_networkConnTable->setItem(i, 1, new QTableWidgetItem(c.localAddr));
        m_networkConnTable->setItem(i, 2, new QTableWidgetItem(c.remoteAddr));
        
        auto* stateItem = new QTableWidgetItem(c.state);
        if (c.state == "ESTABLISHED") stateItem->setForeground(QBrush(QColor("#81c784"))); // Green
        else if (c.state == "LISTEN") stateItem->setForeground(QBrush(QColor("#4fc3f7"))); // Blue
        m_networkConnTable->setItem(i, 3, stateItem);
        
        m_networkConnTable->setItem(i, 4, new QTableWidgetItem(QString::number(c.pid)));
    }
    m_networkConnTable->setSortingEnabled(true);
}

void MainWindow::refreshStartupTable(const std::vector<StartupEntry>& entries)
{
    m_startupTable->setSortingEnabled(false);
    m_startupTable->setRowCount(static_cast<int>(entries.size()));

    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto& e = entries[i];

        // Hive column — color-coded
        auto* hiveItem = new QTableWidgetItem(e.hive);
        if (e.hive == "HKLM")
            hiveItem->setForeground(QBrush(QColor("#ffb74d"))); // orange = system-wide
        else
            hiveItem->setForeground(QBrush(QColor("#4fc3f7"))); // blue = user
        m_startupTable->setItem(i, 0, hiveItem);

        m_startupTable->setItem(i, 1, new QTableWidgetItem(e.name));
        m_startupTable->setItem(i, 2, new QTableWidgetItem(e.path));

        auto* statusItem = new QTableWidgetItem("✅ Активний");
        statusItem->setForeground(QBrush(QColor("#81c784")));
        m_startupTable->setItem(i, 3, statusItem);
    }
    m_startupTable->setSortingEnabled(true);
}

void MainWindow::onStartupChanged(const QString& action, const QString& name,
                                   const QString& path, const QString& hive)
{
    QString icon = (action == "Додано") ? "🆕" : (action == "Видалено") ? "🗑" : "✏️";
    QString fullAction = icon + " Автозавантаження " + action + " [" + hive + "]";

    appendLog(m_sysLogTable,
              QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"),
              fullAction,
              name + "\n" + path);

    // If new entry added — visually flag the inner startup sub-tab
    if (action == "Додано" && m_securityTabWidget) {
        // Sub-tab index 2 = Автозавантаження
        m_securityTabWidget->setTabText(2, "⚠  Автозавантаження");
        // Also flag the outer Security tab
        m_tabWidget->setTabText(1, "🛡  Безпека ⚠");
    }
}

void MainWindow::updateSystemInfo(const SystemData& d)
{
    // ── Table ──
    auto setRow = [&](int row, const QString& param, const QString& val) {
        if (m_sysInfoTable->rowCount() <= row)
            m_sysInfoTable->setRowCount(row + 1);
        if (!m_sysInfoTable->item(row, 0))
            m_sysInfoTable->setItem(row, 0, new QTableWidgetItem());
        if (!m_sysInfoTable->item(row, 1))
            m_sysInfoTable->setItem(row, 1, new QTableWidgetItem());
        m_sysInfoTable->item(row, 0)->setText(param);
        m_sysInfoTable->item(row, 1)->setText(val);
    };

    setRow(0, "Загальна RAM",
           QString::number(d.totalRamMB / 1024.0, 'f', 2) + " ГБ");
    setRow(1, "Доступна RAM",
           QString::number(d.availRamMB / 1024.0, 'f', 2) + " ГБ");
    setRow(2, "Використана RAM",
           QString::number(d.usedRamMB / 1024.0, 'f', 2) +
           " ГБ  (" + QString::number(d.ramUsagePercent) + " %)");
    setRow(3, "Навантаження CPU",
           QString::number(d.cpuUsagePercent) + " %");
    setRow(4, "Диск C:  Всього / Вільно",
           QString::number(d.totalDiskGB, 'f', 1) + " ГБ  /  " +
           QString::number(d.freeDiskGB,  'f', 1) + " ГБ");

    // ── Progress bars ──
    m_cpuProgress->setValue(d.cpuUsagePercent);
    m_ramProgress->setValue(d.ramUsagePercent);
    m_gpuProgress->setValue(d.gpuUsagePercent);

    // ── Chart: append all three series ──
    ++m_chartTick;
    m_cpuSeries->append(m_chartTick, d.cpuUsagePercent);
    m_ramSeries->append(m_chartTick, d.ramUsagePercent);
    m_gpuSeries->append(m_chartTick, d.gpuUsagePercent);

    // Trim old points
    while (m_cpuSeries->count() > m_settings->chartHistory) m_cpuSeries->remove(0);
    while (m_ramSeries->count() > m_settings->chartHistory) m_ramSeries->remove(0);
    while (m_gpuSeries->count() > m_settings->chartHistory) m_gpuSeries->remove(0);

    // Keep only chartHistory points horizontally
    auto* axisX = m_resourceChart->axes(Qt::Horizontal).first();
    if (m_chartTick > m_settings->chartHistory) {
        axisX->setRange(m_chartTick - m_settings->chartHistory, m_chartTick);
    } else {
        axisX->setRange(0, m_settings->chartHistory);
    }
}

void MainWindow::appendLog(QTableWidget* table,
                           const QString& time, const QString& event,
                           const QString& details)
{
    table->insertRow(0);
    table->setItem(0, 0, new QTableWidgetItem(time));
    table->setItem(0, 1, new QTableWidgetItem(event));
    table->setItem(0, 2, new QTableWidgetItem(details));
}

// ─────────────────────────────── Anomalies Updates ───────────────────────────

void MainWindow::refreshAnomaliesUI()
{
    auto anomalies = m_alerts->activeAnomalies();
    
    // Show/hide placeholder
    bool hasAnomalies = !anomalies.isEmpty();
    m_noAnomaliesLabel->setVisible(!hasAnomalies);
    m_anomaliesTable->setVisible(hasAnomalies);

    // Disable sorting during update
    m_anomaliesTable->setSortingEnabled(false);
    m_anomaliesTable->setRowCount(anomalies.size());

    for (int i = 0; i < anomalies.size(); ++i) {
        const auto& a = anomalies[i];
        Recommendation r = m_alerts->getRecommendation(a.id);

        m_anomaliesTable->setItem(i, 0, new QTableWidgetItem(r.shortTitle.isEmpty() ? a.type : r.shortTitle));
        m_anomaliesTable->setItem(i, 1, new QTableWidgetItem(a.description));
        m_anomaliesTable->setItem(i, 2, new QTableWidgetItem(a.severityLabel()));

        // Make row height standard
        m_anomaliesTable->setRowHeight(i, 50);

        QWidget* btnWidget = new QWidget(this);
        auto* lay = new QHBoxLayout(btnWidget);
        lay->setContentsMargins(4, 4, 4, 4);
        
        // "Details" button
        QPushButton* detailsBtn = new QPushButton("Деталі...", btnWidget);
        detailsBtn->setStyleSheet("background: #555; padding: 4px;");
        connect(detailsBtn, &QPushButton::clicked, this, [this, a, r]() {
            AnomalyDetailsDialog dlg(a, r, this);
            connect(&dlg, &AnomalyDetailsDialog::actionTriggered, this, [this, id = a.id]() {
                m_alerts->acknowledgeAnomaly(id);
            });
            connect(&dlg, &AnomalyDetailsDialog::acknowledgeTriggered, this, [this, id = a.id]() {
                m_alerts->acknowledgeAnomaly(id);
            });
            dlg.exec();
        });
        lay->addWidget(detailsBtn);

        // Optional quick action button (if has action)
        if (!r.actionLabel.isEmpty() && r.action) {
            QPushButton* actionBtn = new QPushButton(r.actionLabel, btnWidget);
            actionBtn->setStyleSheet(a.severity == 3 ? "background: #c62828;" : "background: #f57c00;");
            connect(actionBtn, &QPushButton::clicked, this, [this, r, id = a.id]() {
                r.action();
                m_alerts->acknowledgeAnomaly(id);
            });
            lay->addWidget(actionBtn);
        }

        // Quick Ignore button
        QPushButton* ackBtn = new QPushButton("Ігнорувати", btnWidget);
        ackBtn->setStyleSheet("background: #444;");
        connect(ackBtn, &QPushButton::clicked, this, [this, id = a.id]() {
            m_alerts->acknowledgeAnomaly(id);
        });
        lay->addWidget(ackBtn);
        
        m_anomaliesTable->setCellWidget(i, 3, btnWidget);
    }
    m_anomaliesTable->setSortingEnabled(true);
}

void MainWindow::updateHealthScore(int score)
{
    QString color = "#a5d6a7"; // green
    if (score < 80) color = "#ffb74d"; // orange
    if (score < 50) color = "#ef5350"; // red
    
    m_healthScoreLabel->setText(QString("Health Score: %1/100").arg(score));
    m_healthScoreLabel->setStyleSheet(QString("font-weight: bold; color: %1; font-size: 14px;").arg(color));
}

void MainWindow::ackAllAnomalies()
{
    m_alerts->acknowledgeAll();
}

// ─────────────────────────────── save / clear ─────────────────────────

void MainWindow::saveLogsToCsv()
{
    int sub = m_logsTabWidget->currentIndex();

    QTableWidget* tbl = nullptr;
    QString folder;
    switch (sub) {
    case 0: tbl = m_processLogTable; folder = "ProcessLogs"; break;
    case 1: tbl = m_sysLogTable;     folder = "SysLogs";     break;
    case 2: tbl = m_networkLogTable; folder = "NetLogs";     break;
    case 3: tbl = m_fileLogTable;    folder = "FileLogs";    break;
    default: return;
    }

    QString basePath = m_settings->logSavePath;
    if (basePath.isEmpty()) basePath = "Logs";
    QDir dir(basePath + "/" + folder);
    if (!dir.exists()) dir.mkpath(".");

    QString fname = dir.path() + "/Log_" +
        QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".csv";

    QFile file(fname);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "Час,Подія,Деталі\n";
        for (int r = 0; r < tbl->rowCount(); ++r)
            out << "\"" << tbl->item(r,0)->text() << "\","
                << "\""  << tbl->item(r,1)->text() << "\","
                << "\""  << tbl->item(r,2)->text() << "\"\n";
        file.close();
        QMessageBox::information(this, "Успіх", "Збережено: " + fname);
    }
}

void MainWindow::clearCurrentLog()
{
    if (m_tabWidget->currentIndex() != 3) return;
    switch (m_logsTabWidget->currentIndex()) {
    case 0: m_processLogTable->setRowCount(0); break;
    case 1: m_sysLogTable->setRowCount(0);     break;
    case 2: m_networkLogTable->setRowCount(0); break;
    case 3: m_fileLogTable->setRowCount(0);    break;
    }
}

void MainWindow::showSettingsDialog()
{
    SettingsDialog dlg(m_settings, this);
    dlg.exec();
}

void MainWindow::showProcessContextMenu(const QPoint& pos)
{
    QTableWidgetItem* item = m_processTable->itemAt(pos);
    if (!item) return;

    int row = item->row();
    QString processName = m_processTable->item(row, 0)->text();
    bool isSystem = (m_processTable->item(row, 3)->text() == "Системний");

    DWORD pid = m_processTable->item(row, 0)->data(Qt::UserRole).toUInt();
    if (pid == 0) return;

    QMenu menu(this);
    QAction* actSuspend = menu.addAction("Призупинити (Suspend)");
    QAction* actResume = menu.addAction("Відновити (Resume)");
    QAction* actTerminate = menu.addAction("Завершити (Terminate)");

    QAction* selected = menu.exec(m_processTable->viewport()->mapToGlobal(pos));

    if (!selected) return;

    // Check Prompt Level
    bool needsPrompt = false;
    if (m_settings->processPromptLevel == 0) needsPrompt = true; // All
    else if (m_settings->processPromptLevel == 1 && isSystem) needsPrompt = true; // Important only

    if (needsPrompt) {
        QMessageBox::StandardButton reply = QMessageBox::warning(this, "Підтвердження",
            QString("Ви впевнені, що хочете виконати цю дію над процесом '%1' (PID: %2)?\n\nНеобережні дії з системними процесами можуть призвести до збою Windows.").arg(processName).arg(pid),
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
    }

    if (selected == actSuspend) {
        m_backend->suspendProcess(pid);
    } else if (selected == actResume) {
        m_backend->resumeProcess(pid);
    } else if (selected == actTerminate) {
        m_backend->terminateProcess(pid);
    }
}


// ─────────────────────────────── native events ────────────────────────

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_DEVICECHANGE) {
        auto* hdr = reinterpret_cast<PDEV_BROADCAST_HDR>(msg->lParam);
        if (hdr && hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
            QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
            if (msg->wParam == DBT_DEVICEARRIVAL)
                emit m_backend->systemEventLogged(ts, "USB підключено",  "USB-пристрій підключено");
            else if (msg->wParam == DBT_DEVICEREMOVECOMPLETE)
                emit m_backend->systemEventLogged(ts, "USB відключено",  "USB-пристрій відключено");
        }
    } else if (msg->message == WM_POWERBROADCAST) {
        if (msg->wParam == PBT_APMPOWERSTATUSCHANGE) {
            emit m_backend->systemEventLogged(
                QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"),
                "Живлення", "Статус живлення змінився");
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

// ─────────────────────────────── Event Filter ─────────────────────────

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tabWidget && event->type() == QEvent::Resize && m_settingsBtn) {
        // Position button in the top right corner of the tab widget,
        // exactly where the tab bar ends.
        int btnWidth = m_settingsBtn->width();
        int btnHeight = m_settingsBtn->height();
        
        // Qt draws tabs at the top. We place the button on the far right.
        m_settingsBtn->move(m_tabWidget->width() - btnWidth - 2, 2);
    }
    return QMainWindow::eventFilter(watched, event);
}

// ─────────────────────────────── Dynamic Theme ────────────────────────

void MainWindow::applyModernStyle()
{
    bool isDark = (m_settings->appTheme == "dark");

    if (isDark) {
        setStyleSheet(R"(
            QMainWindow, QWidget { background-color: #1e1e1e; color: #d4d4d4;
                                   font-family: 'Segoe UI', Arial; font-size: 13px; }

            QTabWidget::pane   { border: 1px solid #333; background: #252526; border-radius: 4px; }
            QTabBar::tab       { background: #2d2d30; border: 1px solid #333; padding: 9px 18px;
                                 border-radius: 4px 4px 0 0; margin-right: 2px; }
            QTabBar::tab:selected { background: #3f3f46; color: #fff; font-weight: bold; }
            QTabBar::tab:hover { background: #3e3e42; }

            QTableWidget       { background: #1e1e1e; alternate-background-color: #252526;
                                 border: none; gridline-color: #333;
                                 selection-background-color: #094771; }
            QHeaderView::section { background: #2d2d30; padding: 6px; border: 1px solid #1e1e1e;
                                   color: #fff; font-weight: bold; }

            QPushButton        { background: #0e639c; color: #fff; border: none;
                                 padding: 7px 22px; border-radius: 4px; font-weight: bold; }
            QPushButton:hover  { background: #1177bb; }
            QPushButton:pressed{ background: #094771; }

            QProgressBar       { border: 1px solid #444; background: #2d2d30;
                                 border-radius: 4px; text-align: center; color: #fff;
                                 font-weight: bold; height: 22px; }
            QProgressBar::chunk{ background: #0e639c; border-radius: 3px; }

            QGroupBox          { border: 1px solid #333; border-radius: 4px;
                                 margin-top: 6px; padding-top: 4px; }
            QGroupBox::title   { subcontrol-origin: margin; left: 8px; color: #9d9d9d; }

            QScrollBar:vertical { background: #252526; width: 8px; border-radius: 4px; }
            QScrollBar::handle:vertical { background: #555; border-radius: 4px; }

            QLabel             { color: #d4d4d4; }
            QComboBox, QSpinBox, QDoubleSpinBox { background: #2d2d30; color: #d4d4d4; border: 1px solid #444; padding: 3px; }
        )");
    } else {
        setStyleSheet(R"(
            QMainWindow, QWidget { background-color: #f0f0f0; color: #1e1e1e;
                                   font-family: 'Segoe UI', Arial; font-size: 13px; }

            QTabWidget::pane   { border: 1px solid #ccc; background: #fff; border-radius: 4px; }
            QTabBar::tab       { background: #e0e0e0; border: 1px solid #ccc; padding: 9px 18px;
                                 border-radius: 4px 4px 0 0; margin-right: 2px; color: #333; }
            QTabBar::tab:selected { background: #fff; color: #000; font-weight: bold; }
            QTabBar::tab:hover { background: #d0d0d0; }

            QTableWidget       { background: #fff; alternate-background-color: #f5f5f5;
                                 border: none; gridline-color: #ddd;
                                 selection-background-color: #cce8ff; color: #1e1e1e; }
            QHeaderView::section { background: #e8e8e8; padding: 6px; border: 1px solid #ccc;
                                   color: #333; font-weight: bold; }

            QPushButton        { background: #0078d4; color: #fff; border: none;
                                 padding: 7px 22px; border-radius: 4px; font-weight: bold; }
            QPushButton:hover  { background: #106ebe; }
            QPushButton:pressed{ background: #005a9e; }

            QProgressBar       { border: 1px solid #ccc; background: #e0e0e0;
                                 border-radius: 4px; text-align: center; color: #333;
                                 font-weight: bold; height: 22px; }
            QProgressBar::chunk{ background: #0078d4; border-radius: 3px; }

            QGroupBox          { border: 1px solid #ccc; border-radius: 4px;
                                 margin-top: 6px; padding-top: 4px; }
            QGroupBox::title   { subcontrol-origin: margin; left: 8px; color: #666; }

            QScrollBar:vertical { background: #f0f0f0; width: 8px; border-radius: 4px; }
            QScrollBar::handle:vertical { background: #bbb; border-radius: 4px; }

            QLabel             { color: #1e1e1e; }
            QComboBox, QSpinBox, QDoubleSpinBox { background: #fff; color: #1e1e1e; border: 1px solid #ccc; padding: 3px; }
        )");
    }
}
