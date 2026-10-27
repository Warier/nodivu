#include "features/store/tuf_client.hpp"
#include "features/plugins/package_service.hpp"
#include "security/tuf_metadata.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

namespace nodivu {
namespace {
constexpr qsizetype MaxTrustStateBytes = 24LL * 1024 * 1024;
QUrl appendPath(QUrl base, const QString& path) {
    base.setPath(base.path() + path);
    return base;
}
QByteArray boundedRead(const QString& path, qsizetype limit) {
    QFile input(path);
    const QFileInfo info(path);
    if (info.isSymbolicLink() || info.isJunction() || !input.open(QIODevice::ReadOnly) ||
        input.size() > limit)
        return {};
    return input.read(limit + 1);
}
QString saveJson(const QString& path, const QJsonObject& object) {
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxTrustStateBytes || QFileInfo(path).isSymbolicLink() ||
        QFileInfo(path).isJunction())
        return QStringLiteral("Estado de confiança excede limite ou usa um link.");
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
        !output.commit())
        return QStringLiteral("Não foi possível persistir o estado de confiança TUF.");
    return {};
}
QJsonObject signedData(const QJsonObject& role) {
    return role.value(QStringLiteral("signed")).toObject();
}
QByteArray roleIdentity(const QJsonObject& root, const QString& name) {
    const auto data = signedData(root);
    const auto role = data.value(QStringLiteral("roles")).toObject().value(name).toObject();
    QJsonObject keys;
    for (const auto& id : role.value(QStringLiteral("keyids")).toArray())
        keys.insert(id.toString(),
                    data.value(QStringLiteral("keys")).toObject().value(id.toString()));
    return canonicalJson(
        QJsonObject{{QStringLiteral("role"), role}, {QStringLiteral("keys"), keys}});
}
bool validCachedTarget(const QString& file, const QJsonObject& description) {
    const QFileInfo info(file);
    QFile input(file);
    if (info.isSymbolicLink() || info.isJunction() || !input.open(QIODevice::ReadOnly) ||
        input.size() != description.value(QStringLiteral("length")).toInteger(-1))
        return false;
    QCryptographicHash sha256(QCryptographicHash::Sha256);
    QCryptographicHash sha512(QCryptographicHash::Sha512);
    while (!input.atEnd()) {
        const auto bytes = input.read(1024LL * 1024);
        if (bytes.isEmpty() || input.error() != QFileDevice::NoError)
            return false;
        sha256.addData(bytes);
        sha512.addData(bytes);
    }
    const auto hashes = description.value(QStringLiteral("hashes")).toObject();
    bool supported = false;
    for (const auto& name : {QStringLiteral("sha256"), QStringLiteral("sha512")}) {
        if (!hashes.contains(name))
            continue;
        const auto hash = name == QStringLiteral("sha256") ? sha256.result() : sha512.result();
        if (QString::fromLatin1(hash.toHex()) != hashes.value(name).toString().toLower())
            return false;
        supported = true;
    }
    return supported;
}
bool matchesDelegation(const QJsonObject& delegation, const QString& path) {
    if (delegation.contains(QStringLiteral("path_hash_prefixes"))) {
        const auto hash = QString::fromLatin1(
            QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
        for (const auto& prefix : delegation.value(QStringLiteral("path_hash_prefixes")).toArray())
            if (!prefix.toString().isEmpty() && hash.startsWith(prefix.toString()))
                return true;
    } else {
        for (const auto& value : delegation.value(QStringLiteral("paths")).toArray()) {
            const auto pattern = value.toString();
            if (pattern.size() > 4096)
                continue;
            const QRegularExpression expression(QRegularExpression::wildcardToRegularExpression(
                pattern, QRegularExpression::NonPathWildcardConversion));
            if (expression.match(path).hasMatch())
                return true;
        }
    }
    return false;
}
// Cache location and signed target name are independent protocol fields.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
QString verifiedCachedTarget(const QString& directory, const QString& name,
                             const QJsonObject& info) {
    const auto hashed =
        QString::fromLatin1(
            QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex()) +
        QStringLiteral(".target");
    auto native = QDir(directory).filePath(QStringLiteral("targets/") + hashed);
    if (validCachedTarget(native, info))
        return native;
    // tuf-js uses encodeURIComponent(target.path), including its punctuation set.
    // An Electron cache is reused only after its signed hash and length pass.
    const auto legacyName =
        QString::fromLatin1(QUrl::toPercentEncoding(name, QByteArray("!~*'()")));
    const auto legacy = QDir(directory).filePath(QStringLiteral("targets/") + legacyName);
    return validCachedTarget(legacy, info) ? legacy : QString();
}
} // namespace

TufClient::TufClient(QString profile, QByteArray bootstrap, bool allowLoopback)
    : profile_(std::move(profile)), bootstrapBytes_(std::move(bootstrap)),
      allowLoopback_(allowLoopback) {}
QByteArray TufClient::compactBootstrap(const QByteArray& bytes) {
    QByteArray result;
    bool quoted = false;
    bool escaped = false;
    for (const auto byte : bytes) {
        if (quoted) {
            result.append(byte);
            if (escaped)
                escaped = false;
            else if (byte == '\\')
                escaped = true;
            else if (byte == '"')
                quoted = false;
        } else if (byte == '"') {
            quoted = true;
            result.append(byte);
        } else if (byte != ' ' && byte != '\r' && byte != '\n' && byte != '\t') {
            result.append(byte);
        }
    }
    return result;
}
QString TufClient::validateUrls() const {
    if (source_.value(QStringLiteral("schema_version")).toInt() != 1 ||
        !source_.value(QStringLiteral("official_publishers")).isArray())
        return QStringLiteral("Configuração da loja incompatível.");
    for (const auto& field : {QStringLiteral("metadata_url"), QStringLiteral("target_url")}) {
        const QUrl url(source_.value(field).toString());
        if (!url.isValid() || !url.userInfo().isEmpty() || !url.query().isEmpty() ||
            !url.fragment().isEmpty() || !url.path().endsWith(QLatin1Char('/')) ||
            (url.scheme() != QStringLiteral("https") &&
             !(allowLoopback_ && url.scheme() == QStringLiteral("http") &&
               url.host() == QStringLiteral("127.0.0.1"))))
            return QStringLiteral("Origem da loja exige HTTPS e caminho terminado em /.");
    }
    return {};
}
QString TufClient::initialize() {
    const auto source = checkedJson(bootstrapBytes_, 1024LL * 1024);
    if (!source.ok())
        return source.error;
    source_ = source.object;
    auto policyError = validateUrls();
    if (!policyError.isEmpty())
        return policyError;
    const auto bootstrap = TufMetadata::decode(
        source_.value(QStringLiteral("root")).toString().toUtf8(), QStringLiteral("root"));
    if (!bootstrap.ok())
        return bootstrap.error;
    auto bootstrapError =
        TufMetadata::verifyRole(bootstrap.object, bootstrap.object, QStringLiteral("root"));
    if (!bootstrapError.isEmpty())
        return bootstrapError;
    // The shipped source is byte-for-byte the existing JSON.stringify bootstrap;
    // retaining property order preserves the Electron trust-domain directory.
    const auto identity = QString::fromLatin1(
        QCryptographicHash::hash(compactBootstrap(bootstrapBytes_), QCryptographicHash::Sha256)
            .toHex());
    directory_ = QDir(profile_).filePath(QStringLiteral("plugin-catalog/") + identity);
    if (QFileInfo(QDir(profile_).filePath(QStringLiteral("plugin-catalog"))).isSymbolicLink() ||
        QFileInfo(directory_).isSymbolicLink() || QFileInfo(directory_).isJunction() ||
        !QDir().mkpath(QDir(directory_).filePath(QStringLiteral("metadata"))) ||
        !QDir().mkpath(QDir(directory_).filePath(QStringLiteral("targets"))))
        return QStringLiteral("Diretório de confiança inválido.");
    const auto native = QDir(directory_).filePath(QStringLiteral("qt-trusted.json"));
    if (QFileInfo::exists(native)) {
        const auto cached =
            checkedJson(boundedRead(native, MaxTrustStateBytes), MaxTrustStateBytes, true);
        if (!cached.ok() || cached.object.value(QStringLiteral("schema")).toInt() != 1)
            return QStringLiteral("Estado TUF local inválido; preservado para diagnóstico.");
        state_ = cached.object;
    } else {
        state_ = {{QStringLiteral("schema"), 1}, {QStringLiteral("root"), bootstrap.object}};
        const QDir metadata(QDir(directory_).filePath(QStringLiteral("metadata")));
        if (QFileInfo::exists(metadata.filePath(QStringLiteral("root.json")))) {
            const auto cached = TufMetadata::decode(
                boundedRead(metadata.filePath(QStringLiteral("root.json")), 512000),
                QStringLiteral("root"));
            if (!cached.ok() ||
                TufMetadata::version(cached.object) < TufMetadata::version(bootstrap.object))
                return QStringLiteral("Root persistida inválida ou anterior à raiz distribuída.");
            state_.insert(QStringLiteral("root"), cached.object);
        }
        for (const auto& name :
             {QStringLiteral("timestamp"), QStringLiteral("snapshot"), QStringLiteral("targets")}) {
            if (!QFileInfo::exists(metadata.filePath(name + QStringLiteral(".json"))))
                continue;
            const auto cached = TufMetadata::decode(
                boundedRead(metadata.filePath(name + QStringLiteral(".json")), 2LL * 1024 * 1024),
                name);
            if (!cached.ok() ||
                !TufMetadata::verifyRole(cached.object,
                                         state_.value(QStringLiteral("root")).toObject(), name)
                     .isEmpty())
                return QStringLiteral(
                    "Metadados persistidos inválidos; confiança anterior preservada.");
            state_.insert(name, cached.object);
        }
    }
    const auto root = state_.value(QStringLiteral("root")).toObject();
    if (TufMetadata::version(root) < TufMetadata::version(bootstrap.object) ||
        !TufMetadata::verifyRole(root, root, QStringLiteral("root")).isEmpty())
        return QStringLiteral("Root confiável local inválida.");
    for (const auto& name :
         {QStringLiteral("timestamp"), QStringLiteral("snapshot"), QStringLiteral("targets")})
        if (state_.contains(name) &&
            !TufMetadata::verifyRole(state_.value(name).toObject(), root, name).isEmpty())
            return QStringLiteral("Assinatura do estado TUF local inválida.");
    return persist();
}
QString TufClient::persist() {
    return saveJson(QDir(directory_).filePath(QStringLiteral("qt-trusted.json")), state_);
}
// Role identity and its versioned URL filename are independently authenticated.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
JsonResult TufClient::fetchRole(const QString& role, const QString& file,
                                const QJsonObject& description, qint64 maximumBytes,
                                const QJsonObject& authority, TransferProgress* progress) {
    if (!PackageService::safeRelativePath(file))
        return {{}, QStringLiteral("Nome de metadado inválido.")};
    const auto metadataUrl = QUrl::fromEncoded(
        QUrl(source_.value(QStringLiteral("metadata_url")).toString()).toEncoded() +
        QUrl::toPercentEncoding(file, QByteArray("!~*'()")));
    const auto transfer = NetworkTransfer::get(metadataUrl, maximumBytes, progress, allowLoopback_);
    if (!transfer.ok())
        return {{}, transfer.error};
    if (description.contains(QStringLiteral("length")) &&
        description.value(QStringLiteral("length")).toInteger(-1) != transfer.bytes.size())
        return {{}, QStringLiteral("Tamanho de metadado incompatível.")};
    if (description.contains(QStringLiteral("hashes"))) {
        const auto error = TufMetadata::checkFile(transfer.bytes, description, maximumBytes);
        if (!error.isEmpty())
            return {{}, error};
    }
    auto metadata = TufMetadata::decode(transfer.bytes, role == QStringLiteral("timestamp") ||
                                                                role == QStringLiteral("snapshot")
                                                            ? role
                                                            : QStringLiteral("targets"));
    if (!metadata.ok())
        return metadata;
    auto verification = TufMetadata::verifyRole(metadata.object, authority, role);
    if (!verification.isEmpty())
        return {{}, verification};
    if (description.contains(QStringLiteral("version")) &&
        TufMetadata::version(metadata.object) !=
            description.value(QStringLiteral("version")).toInteger(-1))
        return {{}, QStringLiteral("Versão de metadado difere da versão autenticada.")};
    const auto previous = state_.value(role).toObject();
    const auto freshness = TufMetadata::checkFreshness(
        metadata.object, previous.isEmpty() ? 1 : TufMetadata::version(previous), updateStart_);
    if (!freshness.isEmpty())
        return {{}, freshness};
    if (!previous.isEmpty() &&
        TufMetadata::version(metadata.object) == TufMetadata::version(previous) &&
        canonicalJson(metadata.object.value(QStringLiteral("signed"))) !=
            canonicalJson(previous.value(QStringLiteral("signed"))))
        return {{},
                QStringLiteral("A mesma versão de metadado foi reutilizada com outro conteúdo.")};
    return metadata;
}
QString TufClient::refresh(TransferProgress* progress) {
    online_ = false;
    updateStart_ = QDateTime::currentDateTimeUtc();
    auto root = state_.value(QStringLiteral("root")).toObject();
    if (root.isEmpty())
        return QStringLiteral("Cliente TUF não inicializado.");
    bool rotationLimit = true;
    for (int rotation = 0; rotation < 16; ++rotation) {
        const auto nextVersion = TufMetadata::version(root) + 1;
        const auto result = NetworkTransfer::get(
            appendPath(QUrl(source_.value(QStringLiteral("metadata_url")).toString()),
                       QString::number(nextVersion) + QStringLiteral(".root.json")),
            512000, progress, allowLoopback_);
        if (result.status == 404) {
            rotationLimit = false;
            break;
        }
        if (!result.ok())
            return result.error;
        const auto next = TufMetadata::decode(result.bytes, QStringLiteral("root"));
        if (!next.ok())
            return next.error;
        auto verification = TufMetadata::verifyRootRotation(root, next.object);
        if (!verification.isEmpty())
            return verification;
        bool changed = false;
        for (const auto& role :
             {QStringLiteral("timestamp"), QStringLiteral("snapshot"), QStringLiteral("targets")})
            changed = changed || roleIdentity(root, role) != roleIdentity(next.object, role);
        root = next.object;
        state_.insert(QStringLiteral("root"), root);
        if (changed)
            for (const auto& role : {QStringLiteral("timestamp"), QStringLiteral("snapshot"),
                                     QStringLiteral("targets"), QStringLiteral("delegated")})
                state_.remove(role);
        const auto saved = persist();
        if (!saved.isEmpty())
            return saved;
    }
    if (rotationLimit)
        return QStringLiteral("Limite de 16 rotações atingido. Atualize novamente para continuar.");
    auto error = TufMetadata::checkFreshness(root, 1, updateStart_);
    if (!error.isEmpty())
        return error;
    const auto timestamp = fetchRole(QStringLiteral("timestamp"), QStringLiteral("timestamp.json"),
                                     {}, 16384, root, progress);
    if (!timestamp.ok())
        return timestamp.error;
    state_.insert(QStringLiteral("timestamp"), timestamp.object);
    error = persist();
    if (!error.isEmpty())
        return error;
    const bool consistent = signedData(root).value(QStringLiteral("consistent_snapshot")).toBool();
    const auto snapshotDescription = signedData(timestamp.object)
                                         .value(QStringLiteral("meta"))
                                         .toObject()
                                         .value(QStringLiteral("snapshot.json"))
                                         .toObject();
    const qint64 snapshotVersion =
        snapshotDescription.value(QStringLiteral("version")).toInteger(-1);
    if (snapshotVersion < 1)
        return QStringLiteral("Timestamp sem versão de snapshot válida.");
    const auto snapshot =
        fetchRole(QStringLiteral("snapshot"),
                  (consistent ? QString::number(snapshotVersion) + QLatin1Char('.') : QString()) +
                      QStringLiteral("snapshot.json"),
                  snapshotDescription, 1024LL * 1024, root, progress);
    if (!snapshot.ok())
        return snapshot.error;
    const auto oldMeta = signedData(state_.value(QStringLiteral("snapshot")).toObject())
                             .value(QStringLiteral("meta"))
                             .toObject();
    const auto newMeta = signedData(snapshot.object).value(QStringLiteral("meta")).toObject();
    for (auto it = oldMeta.begin(); it != oldMeta.end(); ++it)
        if (newMeta.value(it.key()).toObject().value(QStringLiteral("version")).toInteger(-1) <
            it.value().toObject().value(QStringLiteral("version")).toInteger())
            return QStringLiteral(
                "Snapshot tentou retornar ou remover uma versão de metadado confiável.");
    state_.insert(QStringLiteral("snapshot"), snapshot.object);
    error = persist();
    if (!error.isEmpty())
        return error;
    const auto targetsDescription = newMeta.value(QStringLiteral("targets.json")).toObject();
    const qint64 targetsVersion = targetsDescription.value(QStringLiteral("version")).toInteger(-1);
    if (targetsVersion < 1)
        return QStringLiteral("Snapshot sem versão de targets válida.");
    const auto targets =
        fetchRole(QStringLiteral("targets"),
                  (consistent ? QString::number(targetsVersion) + QLatin1Char('.') : QString()) +
                      QStringLiteral("targets.json"),
                  targetsDescription, 2LL * 1024 * 1024, root, progress);
    if (!targets.ok())
        return targets.error;
    state_.insert(QStringLiteral("targets"), targets.object);
    error = persist();
    if (!error.isEmpty())
        return error;
    online_ = true;
    return {};
}
JsonResult TufClient::findTarget(const QJsonObject& role, const QString& target,
                                 QSet<QString>& visited, TransferProgress* progress, int depth) {
    const auto data = signedData(role);
    const auto targets = data.value(QStringLiteral("targets")).toObject();
    if (targets.contains(target))
        return {targets.value(target).toObject(), {}};
    if (depth >= 8 || visited.size() >= 8)
        return {{}, QStringLiteral("Limite de delegações TUF atingido.")};
    const auto delegations = data.value(QStringLiteral("delegations")).toObject();
    for (const auto& entry : delegations.value(QStringLiteral("roles")).toArray()) {
        const auto delegation = entry.toObject();
        const auto name = delegation.value(QStringLiteral("name")).toString();
        if (!matchesDelegation(delegation, target) || visited.contains(name))
            continue;
        if (!PackageService::safeRelativePath(name) || visited.size() >= 8 ||
            name == QStringLiteral("root") || name == QStringLiteral("timestamp") ||
            name == QStringLiteral("snapshot") || name == QStringLiteral("targets"))
            return {{}, QStringLiteral("Delegação inválida ou limite excedido.")};
        visited.insert(name);
        const auto description = signedData(state_.value(QStringLiteral("snapshot")).toObject())
                                     .value(QStringLiteral("meta"))
                                     .toObject()
                                     .value(name + QStringLiteral(".json"))
                                     .toObject();
        const auto version = description.value(QStringLiteral("version")).toInteger(-1);
        if (version < 1)
            return {{}, QStringLiteral("Delegação ausente no snapshot autenticado.")};
        const bool consistent = signedData(state_.value(QStringLiteral("root")).toObject())
                                    .value(QStringLiteral("consistent_snapshot"))
                                    .toBool();
        const auto path = (consistent ? QString::number(version) + QLatin1Char('.') : QString()) +
                          name + QStringLiteral(".json");
        const QJsonObject authority{
            {QStringLiteral("signed"),
             QJsonObject{{QStringLiteral("keys"), delegations.value(QStringLiteral("keys"))},
                         {QStringLiteral("roles"), QJsonObject{{name, delegation}}}}}};
        auto delegated = fetchRole(name, path, description, 2LL * 1024 * 1024, authority, progress);
        if (!delegated.ok())
            return delegated;
        auto cached = state_.value(QStringLiteral("delegated")).toObject();
        const auto previous = cached.value(name).toObject();
        if (!previous.isEmpty() &&
            TufMetadata::version(delegated.object) < TufMetadata::version(previous))
            return {{}, QStringLiteral("Rollback em metadados delegados.")};
        if (!previous.isEmpty() &&
            TufMetadata::version(delegated.object) == TufMetadata::version(previous) &&
            canonicalJson(delegated.object.value(QStringLiteral("signed"))) !=
                canonicalJson(previous.value(QStringLiteral("signed"))))
            return {{},
                    QStringLiteral("A mesma versão delegada foi reutilizada com outro conteúdo.")};
        const auto fresh =
            TufMetadata::checkFreshness(delegated.object, 1, QDateTime::currentDateTimeUtc());
        if (!fresh.isEmpty())
            return {{}, fresh};
        cached.insert(name, delegated.object);
        state_.insert(QStringLiteral("delegated"), cached);
        const auto saved = persist();
        if (!saved.isEmpty())
            return {{}, saved};
        const auto result = findTarget(delegated.object, target, visited, progress, depth + 1);
        if (!result.object.isEmpty() || !result.error.isEmpty())
            return result;
        if (delegation.value(QStringLiteral("terminating")).toBool())
            break;
    }
    return {};
}
TransferResult TufClient::target(const QString& name, qint64 maximumBytes,
                                 TransferProgress* progress, qint64 expectedLength) {
    TransferResult rejected;
    if (!online_ || !PackageService::safeRelativePath(name) || maximumBytes < 0 ||
        maximumBytes > 256LL * 1024LL * 1024) {
        rejected.error =
            QStringLiteral("Atualize um catálogo autenticado antes de baixar arquivos.");
        return rejected;
    }
    const auto now = QDateTime::currentDateTimeUtc();
    for (const auto& role : {QStringLiteral("root"), QStringLiteral("timestamp"),
                             QStringLiteral("snapshot"), QStringLiteral("targets")}) {
        const auto error = TufMetadata::checkFreshness(state_.value(role).toObject(), 1, now);
        if (!error.isEmpty()) {
            online_ = false;
            rejected.error = error;
            return rejected;
        }
    }
    QSet<QString> visited;
    const auto info =
        findTarget(state_.value(QStringLiteral("targets")).toObject(), name, visited, progress);
    const auto length = info.object.value(QStringLiteral("length")).toInteger(-1);
    if (!info.ok() || info.object.isEmpty() || length < 0 || length > maximumBytes ||
        (expectedLength >= 0 && length != expectedLength)) {
        rejected.error =
            info.error.isEmpty()
                ? QStringLiteral(
                      "Arquivo ausente ou tamanho incompatível com o catálogo autenticado.")
                : info.error;
        return rejected;
    }
    const auto fileKey =
        QString::fromLatin1(
            QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex()) +
        QStringLiteral(".target");
    const auto destination = QDir(directory_).filePath(QStringLiteral("targets/") + fileKey);
    const auto cached = verifiedCachedTarget(directory_, name, info.object);
    if (!cached.isEmpty())
        return {length <= 2LL * 1024 * 1024 ? boundedRead(cached, length) : QByteArray(),
                cached,
                {},
                200,
                info.object};
    auto remote = name;
    if (signedData(state_.value(QStringLiteral("root")).toObject())
            .value(QStringLiteral("consistent_snapshot"))
            .toBool()) {
        const auto hashes = info.object.value(QStringLiteral("hashes")).toObject();
        const auto hash = hashes.value(QStringLiteral("sha256"))
                              .toString(hashes.value(QStringLiteral("sha512")).toString());
        if (hash.isEmpty()) {
            rejected.error = QStringLiteral("Target sem hash para consistent snapshot.");
            return rejected;
        }
        const auto slash = name.lastIndexOf(QLatin1Char('/'));
        remote = name.left(slash + 1) + hash + QLatin1Char('.') + name.mid(slash + 1);
    }
    auto result = NetworkTransfer::get(
        appendPath(QUrl(source_.value(QStringLiteral("target_url")).toString()), remote), length,
        progress, allowLoopback_, destination, info.object);
    if (result.ok() && length <= 2LL * 1024 * 1024)
        result.bytes = boundedRead(destination, length);
    if (result.ok())
        result.authenticated = info.object;
    return result;
}
QStringList TufClient::officialPublishers() const {
    QStringList result;
    for (const auto& value : source_.value(QStringLiteral("official_publishers")).toArray())
        result.append(value.toString());
    return result;
}
TransferResult TufClient::cachedTargetForDisplay(const QString& name, qint64 maximumBytes) const {
    TransferResult result;
    if (!PackageService::safeRelativePath(name) || maximumBytes < 0 ||
        maximumBytes > 2LL * 1024 * 1024) {
        result.error = QStringLiteral("Documento de cache inválido.");
        return result;
    }
    const auto info = signedData(state_.value(QStringLiteral("targets")).toObject())
                          .value(QStringLiteral("targets"))
                          .toObject()
                          .value(name)
                          .toObject();
    const auto length = info.value(QStringLiteral("length")).toInteger(-1);
    const auto file = length >= 0 && length <= maximumBytes
                          ? verifiedCachedTarget(directory_, name, info)
                          : QString();
    if (length < 0 || length > maximumBytes || file.isEmpty()) {
        result.error = QStringLiteral(
            "Documento não está disponível no cache. Atualize a loja quando houver conexão.");
        return result;
    }
    // Display only: this API never authorizes installing an executable package.
    return {boundedRead(file, length), file, {}, 200, info};
}
} // namespace nodivu
