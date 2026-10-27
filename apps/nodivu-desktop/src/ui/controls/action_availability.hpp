#pragma once
#include <QString>
class QWidget;
class QAction;
namespace nodivu {
// An empty reason enables the action. Keep the reason available to mouse and assistive tools.
void setActionAvailability(QWidget* widget, const QString& purpose, const QString& reason = {});
void setActionAvailability(QAction* action, const QString& purpose, const QString& reason = {});
} // namespace nodivu
