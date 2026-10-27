#include "contracts/protocol_codec.hpp"

#include <QJsonDocument>
#include <QJsonParseError>

namespace nodivu {

BackendReply transportError(const QString& id, const QString& message) {
    return {id, false, {}, QStringLiteral("transport_error"), message};
}

QByteArray encodeRequest(const QString& id, const QString& command, const QJsonObject& params) {
    if (id.isEmpty() || id.size() > 64 || command.isEmpty())
        return {};
    const QJsonObject request{{QStringLiteral("protocol_version"), 1},
                              {QStringLiteral("id"), id},
                              {QStringLiteral("command"), command},
                              {QStringLiteral("params"), params}};
    auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
    if (bytes.size() >= MaxFrameBytes)
        return {};
    bytes.append('\n');
    return bytes;
}

BackendReply decodeReply(const QByteArray& frame, const QString& expectedId) {
    const auto invalid = [&] {
        return transportError(expectedId, QStringLiteral("Resposta inválida do motor."));
    };
    if (frame.isEmpty() || frame.size() > MaxFrameBytes)
        return invalid();
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(frame, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return invalid();
    const auto object = document.object();
    if (object.value(QStringLiteral("protocol_version")).toInt(-1) != 1 ||
        !object.value(QStringLiteral("id")).isString() ||
        object.value(QStringLiteral("id")).toString() != expectedId ||
        !object.value(QStringLiteral("ok")).isBool() ||
        object.contains(QStringLiteral("result")) == object.contains(QStringLiteral("error")))
        return invalid();
    BackendReply reply;
    reply.id = expectedId;
    reply.ok = object.value(QStringLiteral("ok")).toBool();
    if (reply.ok) {
        if (!object.value(QStringLiteral("result")).isObject())
            return invalid();
        reply.result = object.value(QStringLiteral("result")).toObject();
    } else {
        const auto error = object.value(QStringLiteral("error")).toObject();
        if (!error.value(QStringLiteral("code")).isString() ||
            !error.value(QStringLiteral("message")).isString())
            return invalid();
        reply.code = error.value(QStringLiteral("code")).toString();
        reply.message = error.value(QStringLiteral("message")).toString();
    }
    return reply;
}

} // namespace nodivu
