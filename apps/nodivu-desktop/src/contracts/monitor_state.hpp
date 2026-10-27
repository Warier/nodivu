#pragma once
#include <QJsonArray>
#include <QJsonObject>
namespace nodivu {
struct MonitorState {
    QJsonArray devices;
    QString selectedId;
    QString message;
    QString toggleReason;
    QString selectionReason;
    bool active = false;
    bool failed = false;
};
MonitorState monitorState(const QJsonObject& snapshot, const QJsonArray& devices, bool online,
                          bool busy, const QString& selection);
} // namespace nodivu
