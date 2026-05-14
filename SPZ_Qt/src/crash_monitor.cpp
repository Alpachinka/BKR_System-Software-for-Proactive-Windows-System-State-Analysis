#include "crash_monitor.h"
#include <windows.h>
#include <winevt.h>
#include <QDateTime>

#pragma comment(lib, "wevtapi.lib")

std::vector<CrashEvent> CrashMonitor::getRecentCrashes() {
    std::vector<CrashEvent> crashes;
    
    // XPath query: System log, events 41, 1001, 6008 in the last 24 hours (86400000 ms)
    LPCWSTR query = L"*[System[(EventID=41 or EventID=1001 or EventID=6008) and TimeCreated[timediff(@SystemTime) <= 86400000]]]";
    
    EVT_HANDLE hResults = EvtQuery(NULL, L"System", query, EvtQueryChannelPath | EvtQueryReverseDirection);
    if (!hResults) return crashes;

    EVT_HANDLE hEvents[10];
    DWORD returned = 0;

    while (EvtNext(hResults, 10, hEvents, INFINITE, 0, &returned)) {
        for (DWORD i = 0; i < returned; i++) {
            DWORD bufferSize = 0;
            DWORD bufferUsed = 0;
            DWORD propertyCount = 0;
            
            // First call to get required buffer size
            EvtRender(NULL, hEvents[i], EvtRenderEventXml, bufferSize, NULL, &bufferUsed, &propertyCount);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                std::vector<WCHAR> buffer(bufferUsed / sizeof(WCHAR));
                bufferSize = bufferUsed;
                if (EvtRender(NULL, hEvents[i], EvtRenderEventXml, bufferSize, buffer.data(), &bufferUsed, &propertyCount)) {
                    QString xml = QString::fromWCharArray(buffer.data());
                    
                    CrashEvent ce;
                    ce.eventId = 0;
                    
                    // Simple parsing since we know the XML structure
                    int idStart = xml.indexOf("<EventID");
                    if (idStart != -1) {
                        idStart = xml.indexOf(">", idStart) + 1;
                        int idEnd = xml.indexOf("</EventID>", idStart);
                        if (idEnd != -1) {
                            ce.eventId = xml.mid(idStart, idEnd - idStart).toInt();
                        }
                    }
                    
                    int timeStart = xml.indexOf("<TimeCreated SystemTime='");
                    if (timeStart != -1) {
                        timeStart += 25;
                        int timeEnd = xml.indexOf("'", timeStart);
                        if (timeEnd != -1) {
                            QString timeIso = xml.mid(timeStart, timeEnd - timeStart);
                            QDateTime dt = QDateTime::fromString(timeIso, Qt::ISODateWithMs);
                            if(!dt.isValid()) {
                                dt = QDateTime::fromString(timeIso, Qt::ISODate);
                            }
                            ce.timeStr = dt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
                        }
                    }
                    
                    if (ce.eventId == 41) {
                        ce.source = "Kernel-Power (Критичне вимкнення)";
                        ce.description = "Система перезавантажилась без попереднього завершення роботи. Це може статися через зникнення живлення або критичне апаратне зависання.";
                    } else if (ce.eventId == 1001) {
                        ce.source = "BugCheck (BSOD / Синій екран)";
                        ce.description = "Система відновилась після критичної помилки Windows (Синій екран смерті). Рекомендується перевірити дамп пам'яті.";
                    } else if (ce.eventId == 6008) {
                        ce.source = "EventLog (Неочікуване завершення)";
                        ce.description = "Попереднє завершення роботи системи було неочікуваним.";
                    }
                    
                    if(ce.eventId != 0) {
                        crashes.push_back(ce);
                    }
                }
            }
            EvtClose(hEvents[i]);
        }
    }
    EvtClose(hResults);
    
    return crashes;
}
