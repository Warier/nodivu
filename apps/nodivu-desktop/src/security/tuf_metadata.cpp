#include "security/tuf_metadata.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QSet>
extern "C" {
#include "monocypher-ed25519.h"
}

namespace nodivu {
namespace {
QByteArray strictHex(const QString& text, qsizetype size) {
    if (text.size() != size * 2)
        return {};
    const auto encoded = text.toLatin1();
    for (const auto byte : encoded)
        if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') ||
              (byte >= 'A' && byte <= 'F')))
            return {};
    return QByteArray::fromHex(encoded);
}
} // namespace
JsonResult TufMetadata::decode(const QByteArray& bytes, const QString& role) {
    auto result = checkedJson(bytes, 2LL * 1024 * 1024, true);
    if (!result.ok())
        return result;
    const auto signedData = result.object.value(QStringLiteral("signed")).toObject();
    const auto spec = signedData.value(QStringLiteral("spec_version")).toString();
    if (signedData.value(QStringLiteral("_type")).toString() != role ||
        !result.object.value(QStringLiteral("signatures")).isArray() ||
        result.object.value(QStringLiteral("signatures")).toArray().size() > 128 ||
        !spec.startsWith(QStringLiteral("1.")) || version(result.object) < 1)
        return {{}, QStringLiteral("Envelope TUF incompatível.")};
    return result;
}
qint64 TufMetadata::version(const QJsonObject& metadata) {
    return metadata.value(QStringLiteral("signed"))
        .toObject()
        .value(QStringLiteral("version"))
        .toInteger(-1);
}
QString TufMetadata::verifyRole(const QJsonObject& metadata, const QJsonObject& root,
                                const QString& role) {
    const auto rootData = root.value(QStringLiteral("signed")).toObject();
    const auto roleData = rootData.value(QStringLiteral("roles")).toObject().value(role).toObject();
    const auto authorized = roleData.value(QStringLiteral("keyids")).toArray();
    const qint64 threshold = roleData.value(QStringLiteral("threshold")).toInteger(-1);
    if (threshold < 1 || threshold > authorized.size() || threshold > 128)
        return QStringLiteral("Limiar TUF inválido.");
    const auto keys = rootData.value(QStringLiteral("keys")).toObject();
    const auto message = canonicalJson(metadata.value(QStringLiteral("signed")));
    QSet<QString> valid;
    for (const auto& entry : metadata.value(QStringLiteral("signatures")).toArray()) {
        const auto signature = entry.toObject();
        const auto id = signature.value(QStringLiteral("keyid")).toString();
        if (valid.contains(id) || !authorized.contains(id))
            continue;
        const auto key = keys.value(id).toObject();
        if (key.value(QStringLiteral("keytype")).toString() != QStringLiteral("ed25519") ||
            key.value(QStringLiteral("scheme")).toString() != QStringLiteral("ed25519"))
            continue;
        const auto publicKey = strictHex(key.value(QStringLiteral("keyval"))
                                             .toObject()
                                             .value(QStringLiteral("public"))
                                             .toString(),
                                         32);
        const auto bytes = strictHex(signature.value(QStringLiteral("sig")).toString(), 64);
        if (publicKey.size() == 32 && bytes.size() == 64 &&
            crypto_ed25519_check(reinterpret_cast<const uint8_t*>(bytes.constData()),
                                 reinterpret_cast<const uint8_t*>(publicKey.constData()),
                                 reinterpret_cast<const uint8_t*>(message.constData()),
                                 static_cast<size_t>(message.size())) == 0)
            valid.insert(id);
    }
    return valid.size() >= threshold
               ? QString()
               : QStringLiteral("Assinaturas TUF insuficientes ou inválidas.");
}
QString TufMetadata::verifyRootRotation(const QJsonObject& previous, const QJsonObject& next) {
    if (version(next) != version(previous) + 1)
        return QStringLiteral("Rotação de root fora de sequência.");
    auto oldApproval = verifyRole(next, previous, QStringLiteral("root"));
    if (!oldApproval.isEmpty())
        return oldApproval;
    return verifyRole(next, next, QStringLiteral("root"));
}
QString TufMetadata::checkFreshness(const QJsonObject& metadata, qint64 minimumVersion,
                                    const QDateTime& updateStart) {
    if (version(metadata) < minimumVersion)
        return QStringLiteral("Tentativa de retornar metadados TUF antigos.");
    const auto text = metadata.value(QStringLiteral("signed"))
                          .toObject()
                          .value(QStringLiteral("expires"))
                          .toString();
    const auto expiration = QDateTime::fromString(text, Qt::ISODate);
    if (!text.endsWith(QLatin1Char('Z')) || !expiration.isValid() || expiration <= updateStart)
        return QStringLiteral("Metadados TUF expirados ou com data inválida.");
    return {};
}
QString TufMetadata::checkFile(const QByteArray& bytes, const QJsonObject& description,
                               qint64 maximumBytes) {
    if (bytes.size() > maximumBytes)
        return QStringLiteral("Arquivo acima do limite.");
    if (description.contains(QStringLiteral("length")) &&
        description.value(QStringLiteral("length")).toInteger(-1) != bytes.size())
        return QStringLiteral("Tamanho autenticado incompatível.");
    const auto hashes = description.value(QStringLiteral("hashes")).toObject();
    bool checked = false;
    for (const auto& algorithm : {QStringLiteral("sha256"), QStringLiteral("sha512")}) {
        if (!hashes.contains(algorithm))
            continue;
        const auto hashType = algorithm == QStringLiteral("sha256") ? QCryptographicHash::Sha256
                                                                    : QCryptographicHash::Sha512;
        const auto expected = strictHex(hashes.value(algorithm).toString(),
                                        algorithm == QStringLiteral("sha256") ? 32 : 64);
        if (expected.isEmpty() || QCryptographicHash::hash(bytes, hashType) != expected)
            return QStringLiteral("Hash autenticado incompatível.");
        checked = true;
    }
    return checked ? QString() : QStringLiteral("Arquivo sem hash suportado autenticado.");
}
} // namespace nodivu
