#pragma once

#include <QString>
#include <vector>

struct CrashEvent {
    QString timeStr;
    int eventId;
    QString source;
    QString description;
};

class CrashMonitor {
public:
    // Returns critical crashes (BSOD, unexpected power loss) from the last 24 hours.
    static std::vector<CrashEvent> getRecentCrashes();
};
