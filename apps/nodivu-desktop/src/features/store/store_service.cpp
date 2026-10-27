#include "features/store/store_service.hpp"
#include "security/checked_json.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QVersionNumber>

namespace nodivu {
namespace {
QByteArray readLimited(const QString& file, qint64 limit) {
    QFile input(file);
    const QFileInfo info(file);
    if (info.isSymbolicLink() || info.isJunction() || !input.open(QIODevice::ReadOnly) ||
        input.size() > limit)
        return {};
    return input.read(limit + 1);
}
bool boundedText(const QJsonObject& object, const QString& key, qsizetype maximum) {
    const auto value = object.value(key);
    return value.isString() && !value.toString().trimmed().isEmpty() &&
           value.toString().size() <= maximum;
}
} // namespace
QString resourceTierText(const QString& tier) {
    return tier == QStringLiteral("light")      ? QStringLiteral("Leve")
           : tier == QStringLiteral("moderate") ? QStringLiteral("Moderado")
           : tier == QStringLiteral("heavy")    ? QStringLiteral("Exigente")
                                                : QStringLiteral("Não medido");
}
CatalogPresentation catalogPresentation(const QJsonObject& entry, const StoreState& state) {
    CatalogPresentation result;
    const auto version = entry.value(QStringLiteral("version")).toString();
    bool installed = false;
    QString pending;
    for (const auto& package : state.installed) {
        if (package.id != entry.value(QStringLiteral("id")).toString())
            continue;
        installed = true;
        result.updateAvailable |=
            QVersionNumber::fromString(version) > QVersionNumber::fromString(package.version);
        pending = state.packageStatus.value(package.key).pendingVersion;
    }
    result.action = !pending.isEmpty()       ? QStringLiteral("Agendado")
                    : result.updateAvailable ? QStringLiteral("Atualizar para ") + version
                    : installed              ? QStringLiteral("Instalado")
                                             : QStringLiteral("Instalar");
    const auto platform = entry.value(QStringLiteral("platform")).toString();
    const int api = entry.value(QStringLiteral("host_api")).toInt();
    result.reason =
        platform != QStringLiteral("win32-x64") || (api != 1 && api != 2)
            ? QStringLiteral("Requer %1 / contrato %2.").arg(platform).arg(api)
        : !pending.isEmpty()
            ? QStringLiteral("Versão %1 já agendada para a próxima abertura.").arg(pending)
        : installed && !result.updateAvailable
            ? QStringLiteral("Esta versão ou uma versão mais recente já está instalada.")
        : !state.online ? QStringLiteral("Conecte-se e atualize o catálogo para instalar.")
                        : QString();
    result.detail =
        entry.value(QStringLiteral("description")).toString() +
        QStringLiteral("\nConsumo: %1 (declarado pelo publicador; pode variar neste computador).")
            .arg(resourceTierText(entry.value(QStringLiteral("resource_tier")).toString()));
    if (!result.reason.isEmpty())
        result.detail += QLatin1Char('\n') + result.reason;
    return result;
}
// Preserve the established profile/plugin-root order. Isolated store tests check both paths.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
StoreService::StoreService(QString profile, const QString& plugins, QByteArray bootstrap)
    : profile_(std::move(profile)), packages_(plugins), tuf_(profile_, std::move(bootstrap)) {}
QString StoreService::validateCatalog(const QJsonObject& catalog) {
    if (catalog.value(QStringLiteral("schema_version")).toInt() != 1 ||
        !catalog.value(QStringLiteral("plugins")).isArray() ||
        catalog.value(QStringLiteral("plugins")).toArray().size() > 500)
        return QStringLiteral("Catálogo inválido ou acima do limite.");
    static const QRegularExpression idPattern(QStringLiteral("^[a-zA-Z0-9][a-zA-Z0-9._-]{0,127}$"));
    static const QRegularExpression versionPattern(
        QStringLiteral("^(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})$"));
    QSet<QString> ids;
    for (const auto& value : catalog.value(QStringLiteral("plugins")).toArray()) {
        const auto item = value.toObject();
        const auto id = item.value(QStringLiteral("id")).toString();
        const auto size = item.value(QStringLiteral("download_bytes")).toInteger(-1);
        if (!idPattern.match(id).hasMatch() || ids.contains(id) ||
            !versionPattern.match(item.value(QStringLiteral("version")).toString()).hasMatch() ||
            !boundedText(item, QStringLiteral("title"), 128) ||
            !boundedText(item, QStringLiteral("author"), 128) ||
            !boundedText(item, QStringLiteral("publisher_id"), 128) ||
            !boundedText(item, QStringLiteral("description"), 1024) ||
            !PackageService::safeRelativePath(item.value(QStringLiteral("archive")).toString()) ||
            !PackageService::safeRelativePath(item.value(QStringLiteral("readme")).toString()) ||
            size < 1 || size > 256LL * 1024LL * 1024 ||
            (item.value(QStringLiteral("host_api")).toInt() != 1 &&
             item.value(QStringLiteral("host_api")).toInt() != 2) ||
            !boundedText(item, QStringLiteral("platform"), 32))
            return QStringLiteral("Item incompatível ou inválido no catálogo.");
        const auto tier = item.value(QStringLiteral("resource_tier")).toString();
        if (tier != QStringLiteral("light") && tier != QStringLiteral("moderate") &&
            tier != QStringLiteral("heavy") && tier != QStringLiteral("unknown"))
            return QStringLiteral("Classificação de consumo inválida.");
        const auto images = item.value(QStringLiteral("images")).toObject();
        if (images.size() > 12)
            return QStringLiteral("Imagens demais no catálogo.");
        for (auto it = images.begin(); it != images.end(); ++it)
            if (!PackageService::safeRelativePath(it.key()) ||
                !PackageService::safeRelativePath(it.value().toString()))
                return QStringLiteral("Caminho de imagem inválido.");
        if (item.contains(QStringLiteral("logo")) &&
            !PackageService::safeRelativePath(item.value(QStringLiteral("logo")).toString()))
            return QStringLiteral("Logo inválido.");
        ids.insert(id);
    }
    return {};
}
StoreState StoreService::snapshot(const PackageResult& result) const {
    StoreState state;
    state.installed = packages_.installed();
    state.catalog = catalog_;
    state.logos = logos_;
    state.configured = configured_;
    state.online = tuf_.online() && catalogAuthenticated_;
    state.error = result.error;
    state.message = result.message;
    if (!configurationError_.isEmpty())
        state.error = configurationError_;
    QString journalError;
    const auto pending = packages_.pendingVersions(journalError);
    if (!journalError.isEmpty())
        state.warnings.append(journalError);
    QJsonArray issues;
    const auto report = QDir(profile_).filePath(QStringLiteral("plugin-load-report.json"));
    if (QFileInfo::exists(report)) {
        const auto bytes = readLimited(report, 256LL * 1024);
        const auto parsed =
            checkedJson(QByteArray("{\"issues\":") + bytes + QByteArray("}"), 256LL * 1024 + 32);
        if (!parsed.ok() || !parsed.object.value(QStringLiteral("issues")).isArray())
            state.warnings.append(
                QStringLiteral("Não foi possível ler o diagnóstico de carga dos plugins."));
        else
            issues = parsed.object.value(QStringLiteral("issues")).toArray();
    }
    for (const auto& package : state.installed) {
        PackageStatus status;
        status.pendingVersion = pending.value(package.key);
        status.pending = !initialEnabled_.contains(package.key) ||
                         initialEnabled_.value(package.key) != package.enabled || package.removed ||
                         !status.pendingVersion.isEmpty();
        status.hasPrevious = !packages_.previous(package.key).isEmpty();
        for (const auto& value : issues) {
            const auto issue = value.toObject();
            const auto manifest = issue.value(QStringLiteral("manifest")).toString();
            if (QDir::isAbsolutePath(manifest) &&
                QDir::cleanPath(QFileInfo(manifest).absolutePath())
                        .compare(QDir::cleanPath(package.directory), Qt::CaseInsensitive) == 0)
                status.loadError = issue.value(QStringLiteral("message")).toString().left(4096);
        }
        state.packageStatus.insert(package.key, status);
    }
    // The badge is derived only from the authenticated source, never the package.
    const auto official = tuf_.officialPublishers();
    for (qsizetype i = 0; i < state.catalog.size(); ++i) {
        auto item = state.catalog[i].toObject();
        item.insert(QStringLiteral("authenticated_official"),
                    state.online &&
                        official.contains(item.value(QStringLiteral("publisher_id")).toString()));
        state.catalog[i] = item;
    }
    return state;
}
StoreState StoreService::initialize() {
    // Capture the activation at startup; later marker edits affect only the next run.
    initialEnabled_.clear();
    for (const auto& package : packages_.installed())
        initialEnabled_.insert(package.key, package.enabled);
    configurationError_ = tuf_.initialize();
    initialized_ = configurationError_.isEmpty();
    configured_ = initialized_;
    if (initialized_) {
        const auto display = QDir(tuf_.directory()).filePath(QStringLiteral("display.json"));
        if (QFileInfo::exists(display)) {
            const auto cached = checkedJson(readLimited(display, 1024LL * 1024), 1024LL * 1024);
            if (cached.ok() && validateCatalog(cached.object).isEmpty())
                catalog_ = cached.object.value(QStringLiteral("plugins")).toArray();
        }
    }
    loadLogos(false);
    return snapshot();
}
StoreState StoreService::refresh(TransferProgress* progress) {
    catalogAuthenticated_ = false;
    if (!initialized_)
        return snapshot({configurationError_.isEmpty() ? QStringLiteral("Loja não configurada.")
                                                       : configurationError_,
                         {}});
    const auto error = tuf_.refresh(progress);
    if (!error.isEmpty())
        return snapshot({error, {}});
    const auto transfer = tuf_.target(QStringLiteral("catalog.json"), 1024LL * 1024, progress);
    if (!transfer.ok())
        return snapshot({transfer.error, {}});
    const auto catalog = checkedJson(transfer.bytes, 1024LL * 1024);
    const auto invalid = catalog.ok() ? validateCatalog(catalog.object) : catalog.error;
    if (!invalid.isEmpty())
        return snapshot({invalid, {}});
    catalog_ = catalog.object.value(QStringLiteral("plugins")).toArray();
    catalogAuthenticated_ = true;
    loadLogos(true, progress);
    const auto destination = QDir(tuf_.directory()).filePath(QStringLiteral("display.json"));
    QSaveFile output(destination);
    output.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(catalog.object).toJson(QJsonDocument::Compact);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
        !output.commit())
        return snapshot(
            {QStringLiteral("Catálogo autenticado, mas não foi possível atualizar o cache."), {}});
    return snapshot({{}, QStringLiteral("Catálogo autenticado atualizado.")});
}
QJsonObject StoreService::entry(const QString& id) const {
    for (const auto& value : catalog_)
        if (value.toObject().value(QStringLiteral("id")).toString() == id)
            return value.toObject();
    return {};
}
void StoreService::loadLogos(bool online, TransferProgress* progress) {
    logos_.clear();
    int count = 0;
    for (const auto& value : catalog_) {
        if (++count > 32 || (progress && progress->cancelled.load()))
            break;
        const auto item = value.toObject();
        const auto name = item.value(QStringLiteral("logo")).toString();
        if (name.isEmpty())
            continue;
        const auto target = online ? tuf_.target(name, 512LL * 1024, progress)
                                   : tuf_.cachedTargetForDisplay(name, 512LL * 1024);
        if (!target.ok())
            continue;
        QFile input(target.file);
        if (!input.open(QIODevice::ReadOnly))
            continue;
        QImageReader reader(&input);
        const auto format = reader.format();
        const auto size = reader.size();
        if ((format != "png" && format != "jpeg" && format != "gif" && format != "webp") ||
            !size.isValid() || size.width() > 2048 || size.height() > 2048)
            continue;
        reader.setScaledSize(size.scaled(64, 64, Qt::KeepAspectRatio));
        const auto image = reader.read();
        if (!image.isNull())
            logos_.insert(item.value(QStringLiteral("id")).toString(), image);
    }
}
StoreState StoreService::install(const QString& id, const QString& expectedVersion,
                                 TransferProgress* progress) {
    // Match the existing store: a cached browsing session never authorizes a new
    // activation without checking current repository metadata again.
    auto updated = refresh(progress);
    if (!updated.error.isEmpty() || !updated.online)
        return updated;
    const auto item = entry(id);
    if (!catalogAuthenticated_ || item.isEmpty() ||
        item.value(QStringLiteral("version")).toString() != expectedVersion ||
        item.value(QStringLiteral("platform")).toString() != QStringLiteral("win32-x64"))
        return snapshot({QStringLiteral("Item mudou ou é incompatível com este Windows x64."), {}});
    const auto transfer =
        tuf_.target(item.value(QStringLiteral("archive")).toString(), 256LL * 1024LL * 1024,
                    progress, item.value(QStringLiteral("download_bytes")).toInteger());
    if (!transfer.ok())
        return snapshot({transfer.error, {}});
    return snapshot(packages_.importZip(transfer.file, progress ? &progress->cancelled : nullptr,
                                        id, expectedVersion, progress));
}
ReadmeContent StoreService::details(const QString& id, TransferProgress* progress) {
    ReadmeContent result;
    const auto item = entry(id);
    if (item.isEmpty()) {
        result.error = QStringLiteral("Plugin ausente do catálogo.");
        return result;
    }
    const bool online = tuf_.online() && catalogAuthenticated_;
    const auto readmeName = item.value(QStringLiteral("readme")).toString();
    const auto readme = online ? tuf_.target(readmeName, 256LL * 1024, progress)
                               : tuf_.cachedTargetForDisplay(readmeName, 256LL * 1024);
    if (!readme.ok()) {
        result.error = readme.error;
        return result;
    }
    result.markdown = QString::fromUtf8(readme.bytes);
    const auto images = item.value(QStringLiteral("images")).toObject();
    for (auto it = images.begin(); it != images.end(); ++it) {
        const auto image =
            online ? tuf_.target(it.value().toString(), 2LL * 1024LL * 1024, progress)
                   : tuf_.cachedTargetForDisplay(it.value().toString(), 2LL * 1024LL * 1024);
        if (!image.ok()) {
            result.error = image.error;
            return result;
        }
        result.images.insert(it.key(), image.file);
    }
    return result;
}
ReadmeContent StoreService::installedDetails(const QString& key) const {
    ReadmeContent result;
    for (const auto& package : packages_.installed()) {
        if (package.key != key || !package.ok())
            continue;
        result.directory = package.directory;
        const auto bytes = readLimited(
            QDir(package.directory).filePath(QStringLiteral("README.md")), 256LL * 1024);
        result.markdown = bytes.isEmpty()
                              ? QStringLiteral("Este pacote não inclui documentação local.")
                              : QString::fromUtf8(bytes);
        return result;
    }
    result.error = QStringLiteral("Pacote inválido ou ausente.");
    return result;
}
QString StoreService::cleanDownloads() {
    const auto cleaned = packages_.cleanCache();
    if (!cleaned.ok())
        return cleaned.error;
    const QDir targets(QDir(tuf_.directory()).filePath(QStringLiteral("targets")));
    if (QFileInfo(targets.absolutePath()).isSymbolicLink() ||
        QFileInfo(targets.absolutePath()).isJunction())
        return QStringLiteral("Cache não pode ser um link.");
    const auto files = targets.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    if (files.size() > 4096)
        return QStringLiteral("Cache acima do limite de limpeza automática.");
    for (const auto& file : files)
        if (!file.isSymbolicLink() && !file.isJunction() && !QFile::remove(file.absoluteFilePath()))
            return QStringLiteral("Não foi possível limpar um download.");
    return {};
}
} // namespace nodivu
