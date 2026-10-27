#include "platform/install_transaction.hpp"
#include "security/checked_json.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

namespace nodivu {
namespace {
constexpr qint64 MaxTreeBytes = 4LL * 1024 * 1024 * 1024;
struct Entry {
    QString relative;
    qint64 bytes;
    bool directory;
};
bool linked(const QString& path) {
    const QFileInfo file(path);
    return file.isSymbolicLink() || file.isJunction();
}
bool relativePath(const QString& path) {
    static const QRegularExpression reserved(
        QStringLiteral("^(con|prn|aux|nul|com[0-9]|lpt[0-9])($|\\.)"),
        QRegularExpression::CaseInsensitiveOption);
    if (path.isEmpty() || path.size() > 4096 || QDir::isAbsolutePath(path) ||
        path.contains(QLatin1Char('\\')))
        return false;
    for (const auto& part : path.split(QLatin1Char('/'))) {
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..") ||
            part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' ')) ||
            reserved.match(part).hasMatch())
            return false;
        for (const auto character : part)
            if (character.unicode() < 32 || QStringLiteral("<>:\"|?*").contains(character))
                return false;
    }
    return true;
}
QString walk(const QString& root, const QString& relative, QList<Entry>& entries, qint64& total,
             int depth = 0) {
    if (depth > 32 || linked(QDir(root).filePath(relative)))
        return QStringLiteral("Instalação usa link ou profundidade excessiva.");
    const QDir directory(QDir(root).filePath(relative));
    for (const auto& info : directory.entryInfoList(
             QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name)) {
        const auto path =
            relative.isEmpty() ? info.fileName() : relative + QLatin1Char('/') + info.fileName();
        if (!relativePath(path) || linked(info.absoluteFilePath()) || entries.size() >= 32768 ||
            (!info.isDir() && !info.isFile()))
            return QStringLiteral(
                "Arquivo, link ou quantidade de entradas inválidos na instalação.");
        const qint64 bytes = info.isFile() ? info.size() : 0;
        if (bytes < 0 || total > MaxTreeBytes - bytes)
            return QStringLiteral("A pasta da instalação excede 4 GiB; originais preservados.");
        total += bytes;
        entries.append({path, bytes, info.isDir()});
        if (info.isDir()) {
            const auto error = walk(root, path, entries, total, depth + 1);
            if (!error.isEmpty())
                return error;
        }
    }
    return {};
}
JsonResult readObject(const QString& path, qsizetype limit = 2LL * 1024 * 1024) {
    QFile file(path);
    if (linked(path) || !file.open(QIODevice::ReadOnly) || file.size() > limit)
        return {{}, QStringLiteral("Inventário/journal ausente ou fora dos limites: ") + path};
    return checkedJson(file.read(limit + 1), limit);
}
QString writeObject(const QString& path, const QJsonObject& object) {
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (linked(path) || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
        !file.commit())
        return QStringLiteral("Não foi possível registrar a transação de instalação.");
    return {};
}
QString fileHash(const QString& path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}
QString copyFile(const QString& source, const QString& destination, qint64 expected) {
    if (!QDir().mkpath(QFileInfo(destination).absolutePath()))
        return QStringLiteral("Não foi possível preparar pasta da nova instalação.");
    QFile input(source);
    QSaveFile output(destination);
    output.setDirectWriteFallback(false);
    if (linked(source) || linked(destination) || !input.open(QIODevice::ReadOnly) ||
        !output.open(QIODevice::WriteOnly))
        return QStringLiteral("Não foi possível copiar um arquivo da instalação.");
    qint64 count = 0;
    while (!input.atEnd()) {
        const auto block = input.read(64LL * 1024);
        count += block.size();
        if (block.isEmpty() || count > expected || output.write(block) != block.size())
            return QStringLiteral("Arquivo alterado ou erro durante a cópia da instalação.");
    }
    if (count != expected || !output.commit())
        return QStringLiteral("Cópia da instalação incompleta; original preservado.");
    return {};
}
bool electronRuntime(const QString& path) {
    const auto lower = path.toLower();
    for (const auto& prefix :
         {QStringLiteral("locales/"), QStringLiteral("resources/app/"),
          QStringLiteral("resources/app.asar.unpacked/"), QStringLiteral("third-party/")})
        if (lower.startsWith(prefix) || lower == prefix.chopped(1))
            return true;
    static const QSet<QString> names{QStringLiteral("nodivu-electron.exe"),
                                     QStringLiteral("chrome_100_percent.pak"),
                                     QStringLiteral("chrome_200_percent.pak"),
                                     QStringLiteral("d3dcompiler_47.dll"),
                                     QStringLiteral("dxcompiler.dll"),
                                     QStringLiteral("dxil.dll"),
                                     QStringLiteral("ffmpeg.dll"),
                                     QStringLiteral("icudtl.dat"),
                                     QStringLiteral("resources.pak"),
                                     QStringLiteral("snapshot_blob.bin"),
                                     QStringLiteral("v8_context_snapshot.bin"),
                                     QStringLiteral("vk_swiftshader_icd.json"),
                                     QStringLiteral("vk_swiftshader.dll"),
                                     QStringLiteral("vulkan-1.dll"),
                                     QStringLiteral("resources/app.asar"),
                                     QStringLiteral("resources/app-update.yml"),
                                     QStringLiteral("resources/elevate.exe"),
                                     QStringLiteral("license"),
                                     QStringLiteral("license-nodivu.txt"),
                                     QStringLiteral("licenses.chromium.html"),
                                     QStringLiteral("third-party-notices.md"),
                                     QStringLiteral("version"),
                                     QStringLiteral("unins000.exe"),
                                     QStringLiteral("unins000.dat"),
                                     QStringLiteral("uninstall nodivu.exe")};
    return names.contains(lower);
}
QString validateInventory(const QString& root, QSet<QString>& owned, bool verifyHashes) {
    const auto inventory = readObject(QDir(root).filePath(QStringLiteral("distribution.json")));
    if (!inventory.ok())
        return inventory.error;
    const auto files = inventory.object.value(QStringLiteral("files")).toArray();
    if (inventory.object.value(QStringLiteral("schema_version")).toInt() != 1 ||
        inventory.object.value(QStringLiteral("frontend")).toString() !=
            QStringLiteral("Qt Widgets/C++20") ||
        files.isEmpty() || files.size() > 8192)
        return QStringLiteral("Inventário de instalação incompatível.");
    for (const auto& value : files) {
        const auto file = value.toObject();
        const auto path = file.value(QStringLiteral("path")).toString();
        const auto key = path.toCaseFolded();
        const auto bytes = file.value(QStringLiteral("bytes")).toInteger(-1);
        const auto hash = file.value(QStringLiteral("sha256")).toString();
        if (!relativePath(path) || owned.contains(key) || bytes < 0 ||
            bytes > 512LL * 1024 * 1024 || hash.size() != 64)
            return QStringLiteral("Caminho, duplicata ou limite inválido no inventário.");
        owned.insert(key);
        if (verifyHashes && (QFileInfo(QDir(root).filePath(path)).size() != bytes ||
                             fileHash(QDir(root).filePath(path)) != hash))
            return QStringLiteral("Hash/comprimento divergente no payload: ") + path;
    }
    owned.insert(QStringLiteral("distribution.json"));
    owned.insert(QStringLiteral("migration-receipt.json"));
    owned.insert(QStringLiteral("uninstall nodivu.exe"));
    if (!owned.contains(QStringLiteral("nodivu.exe")) ||
        !owned.contains(QStringLiteral("bin/nodivu-app-backend.exe")))
        return QStringLiteral("Payload não contém frontend/backend esperados.");
    return {};
}
bool recognizedElectron(const QString& target) {
    if (!QFileInfo::exists(QDir(target).filePath(QStringLiteral("nodivu-electron.exe"))))
        return false;
    const auto package =
        readObject(QDir(target).filePath(QStringLiteral("resources/app/package.json")), 65536);
    return package.ok() && package.object.value(QStringLiteral("name")).toString() ==
                               QStringLiteral("nodivu-electron");
}
} // namespace
InstallTransaction::InstallTransaction(const QString& target)
    : target_(QDir::cleanPath(QFileInfo(target).absoluteFilePath())),
      parent_(QFileInfo(target_).absolutePath()),
      identity_(QString::fromLatin1(QCryptographicHash::hash(target_.toCaseFolded().toUtf8(),
                                                             QCryptographicHash::Sha256)
                                        .toHex())
                    .left(16)),
      journal_(QDir(parent_).filePath(QStringLiteral(".nodivu-install-") + identity_ +
                                      QStringLiteral(".json"))) {}
QString InstallTransaction::checkTargetParent() const {
    if (target_ == parent_ || linked(target_) || linked(parent_) || !QFileInfo(parent_).isDir() ||
        QFileInfo(parent_).canonicalFilePath().compare(parent_, Qt::CaseInsensitive) != 0)
        return QStringLiteral("Pasta de instalação inválida, indisponível ou redirecionada.");
    for (const auto location : {QStandardPaths::HomeLocation, QStandardPaths::DocumentsLocation,
                                QStandardPaths::DesktopLocation,
                                QStandardPaths::GenericDataLocation, QStandardPaths::TempLocation})
        if (QDir::cleanPath(QStandardPaths::writableLocation(location))
                .compare(target_, Qt::CaseInsensitive) == 0)
            return QStringLiteral("Não use uma pasta pessoal/sistema como raiz da instalação.");
    return {};
}
QString InstallTransaction::recover() {
    auto policyError = checkTargetParent();
    if (!policyError.isEmpty())
        return policyError;
    QLockFile lock(journal_ + QStringLiteral(".lock"));
    if (!lock.tryLock(0))
        return QStringLiteral("Outra instalação está em andamento.");
    return recoverLocked();
}
QString InstallTransaction::recoverLocked() {
    auto error = checkTargetParent();
    if (!error.isEmpty() || !QFileInfo::exists(journal_))
        return error;
    const auto parsed = readObject(journal_, 65536);
    if (!parsed.ok())
        return parsed.error;
    const auto data = parsed.object;
    const auto preparedName = data.value(QStringLiteral("prepared")).toString();
    const auto backupName = data.value(QStringLiteral("backup")).toString();
    const auto phase = data.value(QStringLiteral("phase")).toString();
    const auto stagePrefix = QStringLiteral(".nodivu-stage-") + identity_ + QLatin1Char('-');
    const auto backupPrefix = QStringLiteral(".nodivu-backup-") + identity_ + QLatin1Char('-');
    static const QRegularExpression uuid(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    if (data.value(QStringLiteral("schema_version")).toInt() != 1 ||
        data.value(QStringLiteral("target")).toString() != QFileInfo(target_).fileName() ||
        !data.value(QStringLiteral("had_previous")).isBool() ||
        !preparedName.startsWith(stagePrefix) ||
        !uuid.match(preparedName.mid(stagePrefix.size())).hasMatch() ||
        !backupName.startsWith(backupPrefix) ||
        !uuid.match(backupName.mid(backupPrefix.size())).hasMatch() ||
        (phase != QStringLiteral("swapping") && phase != QStringLiteral("checking") &&
         phase != QStringLiteral("committed")))
        return QStringLiteral(
            "Journal de instalação inválido; arquivos preservados para inspeção.");
    const auto backup = QDir(parent_).filePath(backupName);
    const auto prepared = QDir(parent_).filePath(preparedName);
    if (linked(backup) || linked(prepared))
        return QStringLiteral("Transação redirecionada por link.");
    if (phase != QStringLiteral("committed")) {
        const bool previous = data.value(QStringLiteral("had_previous")).toBool();
        const bool backupExists = QFileInfo::exists(backup);
        if (previous && !backupExists &&
            (phase != QStringLiteral("swapping") || !QFileInfo::exists(target_)))
            return QStringLiteral(
                "Backup da instalação interrompida não está disponível. Originais preservados.");
        if (QFileInfo::exists(target_) && (backupExists || !previous)) {
            const auto failed = QDir(parent_).filePath(
                QStringLiteral(".nodivu-failed-") + identity_ + QLatin1Char('-') +
                QUuid::createUuid().toString(QUuid::WithoutBraces));
            if (!QDir().rename(target_, failed))
                return QStringLiteral("Não foi possível recolher a versão incompleta.");
        }
        if (backupExists && !QDir().rename(backup, target_))
            return QStringLiteral("Não foi possível restaurar a instalação anterior.");
    }
    // An unused staging tree is retained after interruption for diagnosis. No
    // recursive deletion can mistake user data for an incomplete installation.
    if (!QFile::remove(journal_))
        return QStringLiteral("Recuperação pronta, mas o journal não pôde ser finalizado.");
    return {};
}
InstallResult InstallTransaction::apply(const QString& payload, const Verifier& verify) {
    InstallResult result;
    result.error = checkTargetParent();
    if (!result.ok())
        return result;
    QLockFile lock(journal_ + QStringLiteral(".lock"));
    if (!lock.tryLock(0))
        return {QStringLiteral("Outra instalação está em andamento."), {}, false};
    result.error = recoverLocked();
    if (!result.ok())
        return result;
    QList<Entry> payloadEntries;
    qint64 payloadBytes = 0;
    if (QFileInfo(payload).canonicalFilePath().isEmpty() || linked(payload) ||
        QDir::cleanPath(QFileInfo(payload).absoluteFilePath())
                .compare(QFileInfo(payload).canonicalFilePath(), Qt::CaseInsensitive) != 0)
        return {QStringLiteral("Payload indisponível ou redirecionado."), {}, false};
    const auto payloadPath = QDir::cleanPath(QFileInfo(payload).absoluteFilePath());
    if (payloadPath.compare(target_, Qt::CaseInsensitive) == 0 ||
        payloadPath.startsWith(target_ + QLatin1Char('/'), Qt::CaseInsensitive) ||
        target_.startsWith(payloadPath + QLatin1Char('/'), Qt::CaseInsensitive))
        return {QStringLiteral("Payload e instalação devem ser pastas independentes."), {}, false};
    result.error = walk(payload, {}, payloadEntries, payloadBytes);
    if (!result.ok())
        return result;
    QSet<QString> newOwned;
    result.error = validateInventory(payload, newOwned, true);
    if (!result.ok())
        return result;
    for (const auto& entry : payloadEntries)
        if (!entry.directory && !newOwned.contains(entry.relative.toCaseFolded()))
            return {QStringLiteral("Payload contém arquivo fora do inventário: ") + entry.relative,
                    {},
                    false};
    const bool previous = QFileInfo::exists(target_);
    QList<Entry> previousEntries;
    QSet<QString> oldOwned;
    if (previous) {
        qint64 bytes = 0;
        result.error = walk(target_, {}, previousEntries, bytes);
        if (!result.ok())
            return result;
        result.migratedElectron = recognizedElectron(target_);
        if (!previousEntries.isEmpty() && !result.migratedElectron) {
            result.error = validateInventory(target_, oldOwned, false);
            if (!result.ok())
                return {
                    QStringLiteral(
                        "A pasta não é uma instalação Nodivu reconhecida; conteúdo preservado. ") +
                        result.error,
                    {},
                    false};
        }
        for (const auto& name :
             {QStringLiteral("nodivu.exe"), QStringLiteral("nodivu-electron.exe"),
              QStringLiteral("bin/nodivu-app-backend.exe"),
              QStringLiteral("resources/app/bin/nodivu-app-backend.exe")}) {
            const auto path = QDir(target_).filePath(name);
            if (!QFileInfo::exists(path))
                continue;
            QFile probe(path);
            if (!probe.open(QIODevice::ReadWrite))
                return {QStringLiteral("Encerre o Nodivu antes de instalar; nenhum processo será "
                                       "finalizado à força."),
                        {},
                        false};
        }
    }
    const auto nonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto preparedName =
        QStringLiteral(".nodivu-stage-") + identity_ + QLatin1Char('-') + nonce;
    const auto backupName =
        QStringLiteral(".nodivu-backup-") + identity_ + QLatin1Char('-') + nonce;
    const auto prepared = QDir(parent_).filePath(preparedName);
    result.backup = previous ? QDir(parent_).filePath(backupName) : QString();
    if (!QDir().mkdir(prepared))
        return {QStringLiteral("Não foi possível criar a nova instalação."), {}, false};
    for (const auto& entry : payloadEntries) {
        if (entry.directory)
            continue;
        result.error = copyFile(QDir(payload).filePath(entry.relative),
                                QDir(prepared).filePath(entry.relative), entry.bytes);
        if (!result.ok())
            return result;
    }
    QSet<QString> verified;
    result.error = validateInventory(prepared, verified, true);
    if (!result.ok())
        return result;
    for (const auto& entry : previousEntries) {
        const auto key = entry.relative.toCaseFolded();
        if (newOwned.contains(key) || oldOwned.contains(key) ||
            (result.migratedElectron && electronRuntime(entry.relative)))
            continue;
        const auto destination = QDir(prepared).filePath(entry.relative);
        if (entry.directory) {
            if (!QDir().mkpath(destination))
                return {QStringLiteral("Não foi possível preservar a pasta do usuário."),
                        result.backup, result.migratedElectron};
        } else {
            result.error =
                copyFile(QDir(target_).filePath(entry.relative), destination, entry.bytes);
            if (!result.ok())
                return result;
        }
    }
    QJsonObject journal{{QStringLiteral("schema_version"), 1},
                        {QStringLiteral("target"), QFileInfo(target_).fileName()},
                        {QStringLiteral("prepared"), preparedName},
                        {QStringLiteral("backup"), backupName},
                        {QStringLiteral("had_previous"), previous},
                        {QStringLiteral("phase"), QStringLiteral("swapping")}};
    result.error = writeObject(journal_, journal);
    if (!result.ok())
        return result;
    if (previous && !QDir().rename(target_, result.backup)) {
        result.error = QStringLiteral("Não foi possível reservar a instalação anterior.");
        const auto recoveryError = recoverLocked();
        if (!recoveryError.isEmpty())
            result.error += QLatin1Char(' ') + recoveryError;
        return result;
    }
    if (!QDir().rename(prepared, target_)) {
        result.error = QStringLiteral("Não foi possível publicar a nova instalação.");
    } else {
        journal.insert(QStringLiteral("phase"), QStringLiteral("checking"));
        result.error = writeObject(journal_, journal);
        if (result.ok())
            result.error = verify(target_);
    }
    if (!result.ok()) {
        const auto recoveryError = recoverLocked();
        if (!recoveryError.isEmpty())
            result.error += QLatin1Char(' ') + recoveryError;
        return result;
    }
    result.error = writeObject(
        QDir(target_).filePath(QStringLiteral("migration-receipt.json")),
        {{QStringLiteral("schema_version"), 1},
         {QStringLiteral("backup"), previous ? backupName : QString()},
         {QStringLiteral("previous_frontend"),
          result.migratedElectron ? QStringLiteral("Electron") : QStringLiteral("Qt")}});
    if (result.ok()) {
        journal.insert(QStringLiteral("phase"), QStringLiteral("committed"));
        result.error = writeObject(journal_, journal);
    }
    if (!result.ok()) {
        const auto recoveryError = recoverLocked();
        if (!recoveryError.isEmpty())
            result.error += QLatin1Char(' ') + recoveryError;
        return result;
    }
    if (!QFile::remove(journal_))
        result.error = QStringLiteral(
            "Nova instalação validada; journal de commit será finalizado na próxima execução.");
    return result;
}
} // namespace nodivu
