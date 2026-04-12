#pragma once
#include <QObject>
#include "anomaly_types.h"
#include "process_info.h"

// ─────────────────────────────────────────────────────────────────────────
//  RecommendEngine — перетворює аномалію в рекомендацію для користувача
// ─────────────────────────────────────────────────────────────────────────
class RecommendEngine : public QObject
{
    Q_OBJECT
public:
    explicit RecommendEngine(QObject* parent = nullptr);

    Recommendation buildFor(const Anomaly& a,
                            const std::vector<ProcessData>& procs) const;
};
