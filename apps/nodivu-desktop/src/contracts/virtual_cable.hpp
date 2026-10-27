#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace nodivu {
struct OutputPresentation {
    QString title;
    QString subtitle;
    QString note;
};
OutputPresentation outputPresentation(const QString& endpointId, const QJsonArray& devices);
QString cableRouteMessage(const QJsonObject& snapshot, const QJsonArray& devices);
} // namespace nodivu
