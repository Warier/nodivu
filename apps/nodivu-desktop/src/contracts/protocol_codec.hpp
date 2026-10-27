#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace nodivu {

inline constexpr qsizetype MaxFrameBytes = 256 * 1024;
inline constexpr int MaxQueuedCommands = 16;

struct BackendReply {
    QString id;
    bool ok = false;
    QJsonObject result;
    QString code;
    QString message;
};

// Framing and envelope checks belong here; graph validation stays in Rust.
QByteArray encodeRequest(const QString& id, const QString& command, const QJsonObject& params);
BackendReply decodeReply(const QByteArray& frame, const QString& expectedId);
BackendReply transportError(const QString& id, const QString& message);

} // namespace nodivu
