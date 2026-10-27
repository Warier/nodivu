#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>

namespace nodivu {
struct FileResult {
    QByteArray contents;
    QString error;
    QDateTime writtenAt;
    bool ok() const {
        return error.isEmpty();
    }
};
class ProjectRepository {
  public:
    static constexpr qint64 MaxProjectBytes = 64 * 1024;
    static FileResult read(const QString& file);
    static FileResult write(const QString& file, const QJsonObject& project);
    static FileResult removeRecovery(const QString& file);
    static QJsonObject resolveResources(QJsonObject project, const QString& file, bool opening);
};
} // namespace nodivu
