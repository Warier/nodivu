#include "platform/diagnostic_log.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace nodivu {
DiagnosticLog::DiagnosticLog(QString profile)
    : directory_(QDir(std::move(profile)).filePath(QStringLiteral("logs"))) {}
void DiagnosticLog::record(const QString& category, const QString& message) const {
    const auto path = QDir(directory_).filePath(QStringLiteral("desktop.jsonl"));
    if (!QDir().mkpath(directory_) || QFileInfo(directory_).isJunction() ||
        QFileInfo(directory_).isSymbolicLink() || QFileInfo(path).isSymbolicLink())
        return;
    if (QFileInfo(path).size() > 1024 * 1024) {
        const auto previous = path + QStringLiteral(".1");
        if (QFileInfo(previous).exists() && !QFile::remove(previous))
            return;
        if (!QFile::rename(path, previous))
            return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return;
    const QJsonObject entry{
        {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("category"), category.left(64)},
        {QStringLiteral("message"), message.left(4096)}};
    file.write(QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n');
}
} // namespace nodivu
