#pragma once
#include <QString>
class QWidget;

namespace nodivu {
enum class PendingChangesAction { Close, Open, Restore };
struct PendingChangesText {
    QString message;
    QString cancel;
    QString proceed;
};
PendingChangesText pendingChangesText(PendingChangesAction action);
bool confirmPendingChanges(QWidget* parent, PendingChangesAction action);
} // namespace nodivu
