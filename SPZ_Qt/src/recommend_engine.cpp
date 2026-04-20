#include "recommend_engine.h"
#define NOMINMAX
#include <windows.h>
#include <QDesktopServices>
#include <QUrl>

RecommendEngine::RecommendEngine(QObject* parent) : QObject(parent) {}

Recommendation RecommendEngine::buildFor(const Anomaly& a,
                                         const std::vector<ProcessData>& procs) const
{
    Recommendation r;
    r.anomalyId = a.id;

    if (a.type == "cpu_spike") {
        r.shortTitle = QString("Завершити %1").arg(a.processName);
        r.longText   = QString("Процес %1 незвично довго споживає багато процесорного часу. "
                               "Якщо ви не запускали важких завдань (напр. рендеринг), "
                               "це може бути помилка в програмі або фоновий майнінг. "
                               "Рекомендовано завершити процес і перезапустити програму.").arg(a.processName);
        r.actionLabel = "Завершити процес";
        r.action      = [pid = a.processPid]() {
            if (pid == 0) return;
            HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (hProc) {
                TerminateProcess(hProc, 1);
                CloseHandle(hProc);
            }
        };
    }
    else if (a.type == "ram_pressure") {
        r.shortTitle = "Оптимізувати пам'ять";
        r.longText   = "Система майже вичерпала доступну оперативну пам'ять. "
                       "Збережіть важливі дані та закрийте ресурсоємні програми "
                       "(браузери, ігри), щоб уникнути зависання системи.";
        r.actionLabel = "Відкрити 'Диспетчер завдань'";
        r.action      = []() {
            QDesktopServices::openUrl(QUrl("file:///C:/Windows/system32/taskmgr.exe"));
        };
    }
    else if (a.type == "gpu_overload") {
        r.shortTitle = "Перевірте фонові задачі GPU";
        r.longText   = "Ваша відеокарта завантажена на максимум вже досить довго. "
                       "Якщо ви зараз не граєте в гру і не обробляєте відео, "
                       "перевірте систему на наявність прихованих майнерів.";
        r.actionLabel = "";
        r.action      = nullptr;
    }
    else if (a.type == "disk_low") {
        r.shortTitle = "Очистити диск";
        r.longText   = "На системному диску залишилося критично мало місця. "
                       "Це призведе до неможливості встановлення оновлень та "
                       "заповільнення роботи файлу підкачки.";
        r.actionLabel = "Відкрити 'Очищення диска'";
        r.action      = []() {
            QDesktopServices::openUrl(QUrl("file:///C:/Windows/system32/cleanmgr.exe"));
        };
    }
    else if (a.type == "suspicious_proc") {
        r.shortTitle = "Перевірити на VirusTotal";
        r.longText   = QString("Виявлено новий запущений файл %1 з підозрілого розташування. "
                               "Рекомендовано негайно перевірити його в антивірусних базах.").arg(a.processName);
        r.actionLabel = "Пошук в Google";
        r.action      = [name = a.processName]() {
            QDesktopServices::openUrl(QUrl("https://www.google.com/search?q=" + name + "+virus"));
        };
    }
    else if (a.type == "baseline_deviation") {
        r.shortTitle = "Відхилення від базової норми";
        r.longText   = "Споживання ресурсів значно вище, ніж було в середньому за останню годину. "
                       "Це може означати фонове встановлення оновлень або запуск "
                       "запланованих завдань.";
        r.actionLabel = "Відкрити 'Монітор ресурсів'";
        r.action      = []() {
            QDesktopServices::openUrl(QUrl("file:///C:/Windows/system32/resmon.exe"));
        };
    }
    else if (a.type == "ransomware_suspected") {
        r.shortTitle = "Перевірити антивірусом";
        r.longText   = a.description; // Description is already very detailed
        r.actionLabel = "Запустити Windows Defender";
        r.action      = []() {
            QDesktopServices::openUrl(QUrl("windowsdefender://threat/"));
        };
    }
    else {
        r.shortTitle = "Зверніть увагу";
        r.longText   = a.description;
        r.actionLabel = "";
        r.action      = nullptr;
    }

    return r;
}
