#pragma once
#include "security/checked_json.hpp"
#include <QDateTime>

namespace nodivu {
class TufMetadata {
  public:
    static JsonResult decode(const QByteArray& bytes, const QString& role);
    static QString verifyRole(const QJsonObject& metadata, const QJsonObject& root,
                              const QString& role);
    static QString verifyRootRotation(const QJsonObject& previous, const QJsonObject& next);
    static QString checkFreshness(const QJsonObject& metadata, qint64 minimumVersion,
                                  const QDateTime& updateStart);
    static QString checkFile(const QByteArray& bytes, const QJsonObject& description,
                             qint64 maximumBytes);
    static qint64 version(const QJsonObject& metadata);
};
} // namespace nodivu
