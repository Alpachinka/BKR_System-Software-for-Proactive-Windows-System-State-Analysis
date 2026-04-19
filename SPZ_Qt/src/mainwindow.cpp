#include "mainwindow.h"
#include "settings_dialog.h"
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

    auto* btnAckAll = new QPushButton("Прийняти всі", this);
    connect(btnAckAll, &QPushButton::clicked, this, &MainWindow::ackAllAnomalies);

    topBar->addWidget(m_healthScoreLabel);
    topBar->addStretch();
    topBar->addWidget(btnAckAll);
    lay->addLayout(topBar);

    m_anomaliesTable = new QTableWidget(0, 4, this);
    m_anomaliesTable->setHorizontalHeaderLabels({"Тип", "Опис", "Критичність", "Дія"});
    m_anomaliesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
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

// ─────────────────────────────── setupUI ──────────────────────────────

void MainWindow::setupUI()
{
    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    auto* mainLay = new QVBoxLayout(central);
    mainLay->setContentsMargins(6, 6, 6, 6);
    mainLay->setSpacing(4);

    m_tabWidget = new QTabWidget(this);
    mainLay->addWidget(m_tabWidget, 1);

    // Tab 0: Processes
    m_processTable = new QTableWidget(0, 4, this);
    m_processTable->setHorizontalHeaderLabels({"Ім'я процесу", "Пам'ять (МБ)", "CPU (%)", "Довіра"});
    m_processTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_processTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_processTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_processTable->setSortingEnabled(true);
    m_processTable->setColumnHidden(3, !m_settings->showTrustLevel);

    // Context Menu for Process Table
    m_processTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_processTable, &QTableWidget::customContextMenuRequested, this, &MainWindow::showProcessContextMenu);

    m_tabWidget->addTab(m_processTable, "Процеси (Активні)");

    // Corner widget for Settings — absolute positioned over the tab widget
    // We will place it using an absolute layout or adjusting geometry in resizeEvent.
    // Instead of using corner widget which can mess up layout on different OSes,
    // let's create a dedicated top layout.
    
    // We actually need to change how the tab widget and button are laid out.
    // Let's create an overlay button.
    auto* btnSettings = new QPushButton(m_tabWidget); // Child of tab widget so it floats above
    btnSettings->setText(QString(QChar(0xE713)));
    btnSettings->setFont(QFont("Segoe MDL2 Assets", 12));
    btnSettings->setToolTip("Налаштування програми");
    btnSettings->setFixedSize(30, 26);
    btnSettings->setStyleSheet(
        "QPushButton { "
        "  background: transparent; "
        "  border: none; "
        "  color: #888; "
        "  font-size: 14px; "
        "  margin: 0px; "
        "  padding: 0px; "
        "}"
        "QPushButton:hover { color: #fff; background: #333; border-radius: 4px; }"
    );
    connect(btnSettings, &QPushButton::clicked, this, &MainWindow::showSettingsDialog);
    
    // To position it correctly, we need to handle it when the tab widget resizes.
    // We can use an event filter to reposition it whenever the tab widget resizes.
    m_tabWidget->installEventFilter(this);
    // Store pointer so event filter can access it
    m_settingsBtn = btnSettings;

    // Tab 1: Anomalies and Recommendations (NEW)
    m_tabWidget->addTab(buildAnomaliesTab(), "Аномалії та Поради");

    // Tab 2: System
    m_tabWidget->addTab(buildSystemTab(), "Системні Ресурси");

    // Tab 3: Logs (with inner sub-tabs)
    QWidget* logsTab = new QWidget(this);
    auto* logsLay    = new QVBoxLayout(logsTab);
    logsLay->setContentsMargins(4, 6, 4, 4);

    m_logsTabWidget = new QTabWidget(logsTab);
    m_logsTabWidget->setTabPosition(QTabWidget::North);

    m_processLogTable = createLogTable();
    m_logsTabWidget->addTab(m_processLogTable, "📋  Процеси");

    m_sysLogTable = createLogTable();
    m_logsTabWidget->addTab(m_sysLogTable,     "🖥  Система");

    m_networkLogTable = createLogTable();
    m_logsTabWidget->addTab(m_networkLogTable, "🌐  Мережа");

    m_fileLogTable = createLogTable();
    m_logsTabWidget->addTab(m_fileLogTable,    "📁  Файли");

    logsLay->addWidget(m_logsTabWidget);
    m_tabWidget->addTab(logsTab, "Логи");

    // Bottom buttons
    auto* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    m_btnClear = new QPushButton("Очистити", this);
    m_btnSave  = new QPushButton("Зберегти", this);
    btnLay->addWidget(m_btnClear);
    btnLay->addWidget(m_btnSave);
    mainLay->addLayout(btnLay);

    setWindowTitle("SPZ: Системний Монітор  ·  Qt6 Edition");
    resize(1000, 720);
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

        m_anomaliesTable->setItem(i, 0, new QTableWidgetItem(a.type));
        m_anomaliesTable->setItem(i, 1, new QTableWidgetItem(a.description + "\n\nПорада: " + r.longText));
        m_anomaliesTable->setItem(i, 2, new QTableWidgetItem(a.severityLabel()));

        // Make row bigger to fit text
        m_anomaliesTable->setRowHeight(i, 80);

        QWidget* btnWidget = new QWidget(this);
        auto* lay = new QHBoxLayout(btnWidget);
        lay->setContentsMargins(4, 4, 4, 4);
        
        if (!r.actionLabel.isEmpty() && r.action) {
            QPushButton* actionBtn = new QPushButton(r.actionLabel, btnWidget);
            actionBtn->setStyleSheet(a.severity == 3 ? "background: #c62828;" : "background: #f57c00;");
            connect(actionBtn, &QPushButton::clicked, this, r.action);
            lay->addWidget(actionBtn);
        }

        QPushButton* ackBtn = new QPushButton("Прийняти", btnWidget);
        ackBtn->setStyleSheet("background: #0e639c;");
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
    // Logs outer tab is index 3 now
    if (m_tabWidget->currentIndex() != 3) {
        QMessageBox::information(this, "Увага",
            "Перейдіть на вкладку \"Логи\" перед збереженням.");
        return;
    }

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
