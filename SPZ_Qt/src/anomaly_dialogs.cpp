#include "anomaly_dialogs.h"
#include <QHeaderView>
#include <QTableWidget>

// ─────────────────────────────────────────────────────────────────────────
// AnomalyDetailsDialog
// ─────────────────────────────────────────────────────────────────────────

AnomalyDetailsDialog::AnomalyDetailsDialog(const Anomaly& a, const Recommendation& r, QWidget* parent)
    : QDialog(parent)
{
    setupUI(a, r);
}

void AnomalyDetailsDialog::setupUI(const Anomaly& a, const Recommendation& r)
{
    setWindowTitle("Деталі аномалії");
    resize(500, 350);

    auto* mainLay = new QVBoxLayout(this);
    mainLay->setSpacing(15);
    mainLay->setContentsMargins(20, 20, 20, 20);

    // Header with Icon/Severity and Type
    auto* headerLay = new QHBoxLayout();
    QLabel* iconLabel = new QLabel(a.severityLabel(), this);
    iconLabel->setStyleSheet("font-size: 24px; font-weight: bold;");
    headerLay->addWidget(iconLabel);

    QLabel* timeLabel = new QLabel(a.detectedAt.toString("dd.MM.yyyy HH:mm:ss"), this);
    timeLabel->setStyleSheet("color: #888;");
    headerLay->addStretch();
    headerLay->addWidget(timeLabel);
    
    mainLay->addLayout(headerLay);

    // Title
    QLabel* titleLabel = new QLabel(r.shortTitle.isEmpty() ? a.type : r.shortTitle, this);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #fff;");
    mainLay->addWidget(titleLabel);

    // Description Box
    QLabel* descLabel = new QLabel(a.description, this);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet("background: #2b2b2b; padding: 10px; border-radius: 5px; border: 1px solid #444; color: #ccc;");
    mainLay->addWidget(descLabel);

    // Advice Box (if available)
    if (!r.longText.isEmpty()) {
        QLabel* adviceTitle = new QLabel("Порада:", this);
        adviceTitle->setStyleSheet("font-weight: bold; color: #64b5f6;");
        mainLay->addWidget(adviceTitle);

        QLabel* adviceLabel = new QLabel(r.longText, this);
        adviceLabel->setWordWrap(true);
        adviceLabel->setStyleSheet("color: #ddd;");
        mainLay->addWidget(adviceLabel);
    }

    mainLay->addStretch();

    // Bottom buttons
    auto* btnLay = new QHBoxLayout();
    
    QPushButton* btnIgnore = new QPushButton("Ігнорувати", this);
    btnIgnore->setStyleSheet("background: #555; padding: 8px 15px;");
    connect(btnIgnore, &QPushButton::clicked, this, [this]() {
        emit acknowledgeTriggered();
        accept();
    });
    btnLay->addWidget(btnIgnore);

    btnLay->addStretch();

    if (!r.actionLabel.isEmpty() && r.action) {
        QPushButton* btnAction = new QPushButton(r.actionLabel, this);
        btnAction->setStyleSheet(a.severity == 3 ? "background: #c62828; font-weight: bold; padding: 8px 15px;" 
                                                 : "background: #f57c00; font-weight: bold; padding: 8px 15px;");
        connect(btnAction, &QPushButton::clicked, this, [this, r]() {
            r.action();
            emit actionTriggered();
            accept();
        });
        btnLay->addWidget(btnAction);
    }

    mainLay->addLayout(btnLay);
}

// ─────────────────────────────────────────────────────────────────────────
// AnomaliesHelpDialog
// ─────────────────────────────────────────────────────────────────────────

AnomaliesHelpDialog::AnomaliesHelpDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUI();
}

void AnomaliesHelpDialog::setupUI()
{
    setWindowTitle("Довідка: Відстежувані аномалії");
    resize(700, 450);

    auto* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(15, 15, 15, 15);

    QLabel* title = new QLabel("Які загрози відстежує SPZ System Monitor?", this);
    title->setStyleSheet("font-size: 16px; font-weight: bold; margin-bottom: 10px;");
    mainLay->addWidget(title);

    QTableWidget* table = new QTableWidget(6, 3, this);
    table->setHorizontalHeaderLabels({"Тип Аномалії", "Умова спрацювання", "Доступні Дії"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setStyleSheet("QTableWidget::item { padding: 5px; }");

    int row = 0;
    auto addRow = [&](const QString& name, const QString& condition, const QString& action) {
        table->setItem(row, 0, new QTableWidgetItem(name));
        table->setItem(row, 1, new QTableWidgetItem(condition));
        table->setItem(row, 2, new QTableWidgetItem(action));
        table->setRowHeight(row, 60);
        row++;
    };

    addRow("Шифрувальник (Ransomware)", "Більше 50 змін файлів за 30 секунд у папці C:\\Users", "Запустити антивірус (Windows Defender)");
    addRow("Стрибок CPU (Майнер)", "Один процес навантажує CPU > 70% довше 1 хвилини", "Вбити підозрілий процес");
    addRow("Брак оперативної пам'яті", "Загальне використання RAM > 90%", "Відкрити Диспетчер завдань");
    addRow("Перевантаження GPU", "Використання відеокарти > 95% довше 2 хвилин", "Ручна перевірка фонових процесів");
    addRow("Мало місця на диску", "На диску C:\\ залишилося менше 5 ГБ", "Відкрити 'Очищення диска'");
    addRow("Відхилення від норми", "Споживання ресурсів відхилилось від базової лінії > 40%", "Відкрити 'Монітор ресурсів'");

    mainLay->addWidget(table);

    QPushButton* btnClose = new QPushButton("Закрити", this);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    
    auto* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    btnLay->addWidget(btnClose);
    mainLay->addLayout(btnLay);
}
