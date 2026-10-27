#include "features/plugins/package_service.hpp"
#include "security/checked_json.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>
extern "C" {
#include "miniz.h"
}
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace nodivu {
namespace {
constexpr qint64 MaxPackageBytes = 256LL * 1024LL * 1024;
bool linked(const QString& path) {
    const QFileInfo info(path);
    if (info.isSymbolicLink() || info.isJunction())
        return true;
#ifdef Q_OS_WIN
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return false;
#endif
}
bool validKey(const QString& key) {
    static const QRegularExpression expression(QStringLiteral("^[a-zA-Z0-9_.-]{1,160}$"));
    return expression.match(key).hasMatch() && key != QStringLiteral(".") &&
           key != QStringLiteral("..");
}
bool versionValid(const QString& version) {
    static const QRegularExpression expression(
        QStringLiteral("^(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})$"));
    return expression.match(version).hasMatch();
}
bool newer(const QString& next, const QString& previous) {
    const auto a = next.split(QLatin1Char('.'));
    const auto b = previous.split(QLatin1Char('.'));
    for (qsizetype i = 0; i < 3; ++i)
        if (a[i].toInt() != b[i].toInt())
            return a[i].toInt() > b[i].toInt();
    return false;
}
QByteArray readBounded(const QString& file, qint64 maximum) {
    QFile input(file);
    if (linked(file) || !input.open(QIODevice::ReadOnly) || input.size() > maximum)
        return {};
    const auto bytes = input.read(maximum + 1);
    return bytes.size() <= maximum && input.error() == QFileDevice::NoError ? bytes : QByteArray();
}
bool writeBytes(const QString& file, const QByteArray& bytes) {
    if (linked(file))
        return false;
    QSaveFile output(file);
    output.setDirectWriteFallback(false);
    return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() &&
           output.commit();
}
QVector<QJsonObject> readJournals(const QString& profile, QString& error) {
    QVector<QJsonObject> result;
    const QDir directory(QDir(profile).filePath(QStringLiteral("plugin-updates")));
    if (linked(directory.absolutePath())) {
        error = QStringLiteral("Pasta de transações não pode ser um link.");
        return {};
    }
    const auto files = directory.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    if (files.size() > 128) {
        error = QStringLiteral("Há transações demais para recuperar com segurança.");
        return {};
    }
    for (const auto& file : files) {
        const auto parsed = checkedJson(readBounded(directory.filePath(file), 16384), 16384);
        const auto object = parsed.object;
        if (!parsed.ok() || object.value(QStringLiteral("schema")).toInt() != 1 ||
            !validKey(object.value(QStringLiteral("key")).toString()) ||
            !validKey(object.value(QStringLiteral("stage")).toString()) ||
            !validKey(object.value(QStringLiteral("backup")).toString()) ||
            object.value(QStringLiteral("pluginId")).toString().trimmed().isEmpty() ||
            object.value(QStringLiteral("pluginId")).toString().size() > 128 ||
            !versionValid(object.value(QStringLiteral("version")).toString())) {
            error = QStringLiteral("Registro de atualização inválido: ") + file;
            return {};
        }
        auto journal = object;
        journal.insert(QStringLiteral("file"), directory.filePath(file));
        result.append(journal);
    }
    return result;
}
bool removeOwnedTree(const QString& parent, const QString& key) {
    if (!validKey(key) || linked(parent))
        return false;
    const auto path = QDir(parent).absoluteFilePath(key);
    const auto prefix = QDir::cleanPath(QDir(parent).absolutePath()) + QLatin1Char('/');
    if (!QDir::cleanPath(path).startsWith(prefix, Qt::CaseInsensitive) || linked(path))
        return false;
    int entries = 0;
    QDirIterator iterator(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
        if (linked(iterator.next()) || ++entries > 4096)
            return false;
    return !QFileInfo::exists(path) || QDir(path).removeRecursively();
}
// Source/destination follow QFile::copy order; both are checked before copying.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
PackageResult copyPackage(const QString& source, const QString& destination,
                          const std::atomic_bool* cancelled) {
    if (QFileInfo::exists(destination) || !QDir().mkpath(destination))
        return {QStringLiteral("Destino de staging inválido."), {}};
    QDirIterator iterator(source, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                          QDirIterator::Subdirectories);
    qint64 total = 0;
    int count = 0;
    while (iterator.hasNext()) {
        const auto path = iterator.next();
        const auto relative = QDir(source).relativeFilePath(path);
        const auto info = iterator.fileInfo();
        if ((cancelled && cancelled->load()) || ++count > 2048 || linked(path) ||
            !PackageService::safeRelativePath(relative))
            return {QStringLiteral("Cópia cancelada ou estrutura de pacote inválida."), {}};
        const auto target = QDir(destination).filePath(relative);
        if (info.isDir()) {
            if (!QDir().mkpath(target))
                return {QStringLiteral("Falha ao criar pasta de staging."), {}};
            continue;
        }
        if (!info.isFile())
            return {QStringLiteral("Entrada do pacote não é um arquivo regular."), {}};
        total += info.size();
        if (total > MaxPackageBytes)
            return {QStringLiteral("Pacote excede o limite."), {}};
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile input(path);
        QSaveFile output(target);
        output.setDirectWriteFallback(false);
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
            return {QStringLiteral("Não foi possível copiar o pacote."), {}};
        qint64 copied = 0;
        while (!input.atEnd()) {
            if (cancelled && cancelled->load())
                return {QStringLiteral("Instalação cancelada."), {}};
            const auto chunk = input.read(1024LL * 1024);
            copied += chunk.size();
            if (chunk.isEmpty() || copied > info.size() || output.write(chunk) != chunk.size())
                return {QStringLiteral("Falha ou alteração durante a cópia do pacote."), {}};
        }
        if (copied != info.size() || !output.commit())
            return {QStringLiteral("Pacote mudou durante a cópia ou não pôde ser gravado."), {}};
    }
    return {};
}
size_t zipRead(void* opaque, mz_uint64 offset, void* buffer, size_t length) {
    auto* file = static_cast<QFile*>(opaque);
    if (offset > static_cast<mz_uint64>(MaxPackageBytes) ||
        length > static_cast<size_t>(MaxPackageBytes) || !file->seek(static_cast<qint64>(offset)))
        return 0;
    const auto received = file->read(static_cast<char*>(buffer), static_cast<qint64>(length));
    return received < 0 ? 0 : static_cast<size_t>(received);
}
struct ZipOutput {
    QSaveFile* file;
    const std::atomic_bool* cancelled;
    qint64 maximum;
    qint64 written = 0;
};
size_t zipWrite(void* opaque, mz_uint64 offset, const void* bytes, size_t length) {
    auto* output = static_cast<ZipOutput*>(opaque);
    if ((output->cancelled && output->cancelled->load()) ||
        offset != static_cast<mz_uint64>(output->written) ||
        length > static_cast<size_t>(output->maximum - output->written))
        return 0;
    const auto written =
        output->file->write(static_cast<const char*>(bytes), static_cast<qint64>(length));
    if (written < 0)
        return 0;
    output->written += written;
    return static_cast<size_t>(written);
}
} // namespace

PackageService::PackageService(const QString& plugins)
    : root_(QDir::cleanPath(plugins)), profile_(QFileInfo(root_).absolutePath()) {}
QString PackageService::directoryFor(const QString& key) const {
    if (key == QStringLiteral("."))
        return root_;
    return validKey(key) ? QDir(root_).filePath(key) : QString();
}
bool PackageService::safeRelativePath(const QString& name) {
    if (name.isEmpty() || name.size() > 4096 || QDir::isAbsolutePath(name) ||
        name.contains(QLatin1Char('\\')))
        return false;
    const auto parts = name.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty() || parts.size() > 12 || name.startsWith(QLatin1Char('/')) ||
        name.contains(QStringLiteral("//")))
        return false;
    static const QRegularExpression forbidden(QStringLiteral("[<>:\"\\\\|?*\\x00-\\x1f]"));
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),
        QRegularExpression::CaseInsensitiveOption);
    for (const auto& part : parts)
        if (part == QStringLiteral(".") || part == QStringLiteral("..") ||
            part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' ')) ||
            forbidden.match(part).hasMatch() || reserved.match(part).hasMatch())
            return false;
    return true;
}
PackageRecord PackageService::inspect(const QString& directory) {
    PackageRecord record;
    record.directory = directory;
    record.key = QFileInfo(directory).fileName();
    const auto fail = [&](const QString& error) {
        auto rejected = record;
        rejected.error = error;
        return rejected;
    };
    if (linked(directory) || !QFileInfo(directory).isDir())
        return fail(QStringLiteral("Pasta de pacote inválida."));
    QDirIterator iterator(directory, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                          QDirIterator::Subdirectories);
    qint64 total = 0;
    int count = 0;
    QSet<QString> names;
    while (iterator.hasNext()) {
        const auto path = iterator.next();
        const auto relative = QDir(directory).relativeFilePath(path);
        const auto info = iterator.fileInfo();
        const auto key = relative.toCaseFolded();
        if (++count > 2048 || linked(path) || !safeRelativePath(relative) || names.contains(key) ||
            (!info.isDir() && !info.isFile()))
            return fail(QStringLiteral("Estrutura, caminho ou quantidade de arquivos inválidos."));
        names.insert(key);
        if (info.isFile())
            total += info.size();
        if (total > MaxPackageBytes)
            return fail(QStringLiteral("Pacote excede 256 MiB."));
    }
    const auto manifest = checkedJson(
        readBounded(QDir(directory).filePath(QStringLiteral("plugin.json")), 65536), 65536);
    if (!manifest.ok())
        return fail(manifest.error);
    record.manifest = manifest.object;
    record.id = manifest.object.value(QStringLiteral("plugin_id")).toString();
    record.title = manifest.object.value(QStringLiteral("title")).toString();
    const auto library = manifest.object.value(QStringLiteral("library")).toString();
    if (manifest.object.value(QStringLiteral("schema_version")).toInt() != 1 ||
        record.id.trimmed().isEmpty() || record.id.size() > 128 ||
        record.id.contains(QChar::Null) || record.title.trimmed().isEmpty() ||
        record.title.size() > 128 || !safeRelativePath(library) ||
        !library.endsWith(QStringLiteral(".clap")) ||
        !QFileInfo(QDir(directory).filePath(library)).isFile() ||
        QFileInfo::exists(QDir(directory).filePath(QStringLiteral("worker.json"))))
        return fail(QStringLiteral("Manifesto local incompatível com o contrato CLAP."));
    const auto envelopeFile = QDir(directory).filePath(QStringLiteral("nodivu-package.json"));
    if (QFileInfo::exists(envelopeFile)) {
        const auto envelope = checkedJson(readBounded(envelopeFile, 16384), 16384);
        if (!envelope.ok())
            return fail(envelope.error);
        const auto data = envelope.object;
        record.version = data.value(QStringLiteral("version")).toString();
        record.author = data.value(QStringLiteral("author")).toString();
        if (data.value(QStringLiteral("schema_version")).toInt() != 1 ||
            data.value(QStringLiteral("plugin_id")).toString() != record.id ||
            !versionValid(record.version) || record.author.trimmed().isEmpty() ||
            record.author.size() > 128 ||
            (data.contains(QStringLiteral("host_api")) &&
             data.value(QStringLiteral("host_api")).toInt() != 1 &&
             data.value(QStringLiteral("host_api")).toInt() != 2) ||
            (data.contains(QStringLiteral("platform")) &&
             data.value(QStringLiteral("platform")).toString() != QStringLiteral("win32-x64")))
            return fail(QStringLiteral("Envelope de pacote, API ou plataforma incompatível."));
    }
    record.enabled =
        !QFileInfo::exists(QDir(directory).filePath(QStringLiteral(".nodivu-disabled")));
    record.removed = QFileInfo::exists(QDir(directory).filePath(QStringLiteral(".nodivu-removed")));
    record.managed = QFileInfo::exists(QDir(directory).filePath(QStringLiteral(".nodivu-managed")));
    return record;
}
QVector<PackageRecord> PackageService::installed() const {
    QVector<PackageRecord> result;
    const QDir root(root_);
    if (QFileInfo::exists(root.filePath(QStringLiteral("plugin.json")))) {
        auto legacy = inspect(root_);
        legacy.key = QStringLiteral(".");
        result.append(legacy);
    }
    for (const auto& folder : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!validKey(folder))
            continue;
        const auto directory = root.filePath(folder);
        const auto manifest = QDir(directory).filePath(QStringLiteral("plugin.json"));
        const auto worker = QDir(directory).filePath(QStringLiteral("worker.json"));
        if (!QFileInfo::exists(manifest) && !QFileInfo::exists(worker))
            continue;
        auto entry = inspect(directory);
        if (!QFileInfo::exists(manifest) && QFileInfo::exists(worker)) {
            // Existing workers remain manageable; importing native packages still
            // goes through the stricter CLAP-only inspect() contract above.
            entry = {};
            entry.directory = directory;
            const auto data = checkedJson(readBounded(worker, 65536), 65536);
            if (!data.ok() || linked(directory) || linked(worker))
                entry.error = QStringLiteral("Manifesto do worker inválido.");
            else {
                entry.manifest = data.object;
                entry.id = data.object.value(QStringLiteral("plugin_id")).toString();
                entry.title = data.object.value(QStringLiteral("title")).toString(folder);
                if (entry.id.isEmpty() || entry.id.size() > 128)
                    entry.error = QStringLiteral("ID do worker inválido.");
            }
            entry.enabled =
                !QFileInfo::exists(QDir(directory).filePath(QStringLiteral(".nodivu-disabled")));
            entry.removed =
                QFileInfo::exists(QDir(directory).filePath(QStringLiteral(".nodivu-removed")));
        }
        entry.key = folder;
        result.append(entry);
    }
    return result;
}
PackageResult PackageService::importFolder(const QString& folder, const std::atomic_bool* cancelled,
                                           OperationControl* operation) {
    const auto candidate = inspect(folder);
    if (!candidate.ok())
        return {candidate.error, {}};
    if (!QDir().mkpath(root_) || linked(root_))
        return {QStringLiteral("Pasta de plugins inválida."), {}};
    const auto stageKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto staging = QDir(profile_).filePath(QStringLiteral("plugin-staging"));
    if (linked(staging) || !QDir().mkpath(staging))
        return {QStringLiteral("Pasta de staging inválida."), {}};
    const auto stage = QDir(staging).filePath(stageKey);
    auto copied = copyPackage(folder, stage, cancelled);
    if (!copied.ok())
        return copied;
    const auto checked = inspect(stage);
    if (!checked.ok() || checked.id != candidate.id || checked.version != candidate.version)
        return {QStringLiteral("Pacote mudou durante a importação."), {}};
    if (!writeBytes(QDir(stage).filePath(QStringLiteral(".nodivu-managed")),
                    QByteArray("managed\n")))
        return {QStringLiteral("Não foi possível registrar o pacote."), {}};
    for (const auto& current : installed()) {
        if (!current.ok() || current.id != checked.id)
            continue;
        if (current.key == QStringLiteral(".") || current.version.isEmpty() ||
            checked.version.isEmpty() || !newer(checked.version, current.version))
            return {
                QStringLiteral("Atualização exige envelopes e uma versão maior para o mesmo ID."),
                {}};
        const auto journals = QDir(profile_).filePath(QStringLiteral("plugin-updates"));
        QString journalError;
        const auto pending = readJournals(profile_, journalError);
        if (!journalError.isEmpty())
            return {journalError, {}};
        for (const auto& transaction : pending)
            if (transaction.value(QStringLiteral("key")).toString() == current.key)
                return {QStringLiteral("Este pacote já tem uma atualização pendente."), {}};
        if (linked(journals) || !QDir().mkpath(journals))
            return {QStringLiteral("Pasta de transações inválida."), {}};
        const auto backup = current.key.left(100) + QLatin1Char('-') +
                            QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QJsonObject journal{{QStringLiteral("schema"), 1},
                                  {QStringLiteral("key"), current.key},
                                  {QStringLiteral("stage"), stageKey},
                                  {QStringLiteral("pluginId"), checked.id},
                                  {QStringLiteral("version"), checked.version},
                                  {QStringLiteral("backup"), backup}};
        const auto path = QDir(journals).filePath(stageKey + QStringLiteral(".json"));
        if ((cancelled && cancelled->load()) || (operation && !operation->beginCommit()))
            return {QStringLiteral("Instalação cancelada antes de publicar a atualização."), {}};
        if (!writeBytes(path, QJsonDocument(journal).toJson(QJsonDocument::Compact)))
            return {QStringLiteral("Falha ao agendar atualização."), {}};
        return {{}, QStringLiteral("Atualização agendada para a próxima abertura.")};
    }
    const auto packageKey = QStringLiteral("package-") +
                            QString::fromLatin1(QCryptographicHash::hash(checked.id.toUtf8(),
                                                                         QCryptographicHash::Sha256)
                                                    .toHex()
                                                    .left(24));
    const auto destination = directoryFor(packageKey);
    qsizetype enabled = 0;
    for (const auto& record : installed())
        enabled += record.ok() && record.enabled && !record.removed ? 1 : 0;
    if (enabled >= 8 && !writeBytes(QDir(stage).filePath(QStringLiteral(".nodivu-disabled")),
                                    QByteArray("limit\n")))
        return {QStringLiteral("Falha ao preservar o limite de oito tipos ativos."), {}};
    if ((cancelled && cancelled->load()) || (operation && !operation->beginCommit()))
        return {QStringLiteral("Instalação cancelada antes de publicar o pacote."), {}};
    if (destination.isEmpty() || QFileInfo::exists(destination) ||
        !QDir().rename(stage, destination))
        return {QStringLiteral("Destino ocupado ou não foi possível publicar o pacote."), {}};
    return {{},
            QStringLiteral("Pacote instalado. Feche e abra o Nodivu para disponibilizar o bloco.")};
}
PackageResult PackageService::setEnabled(const QString& key, bool enabled) {
    const auto directory = directoryFor(key);
    if (directory.isEmpty() || linked(directory) || !QFileInfo(directory).isDir())
        return {QStringLiteral("Pacote inválido."), {}};
    if (enabled) {
        const auto entries = installed();
        const auto item = std::find_if(entries.begin(), entries.end(),
                                       [&key](const auto& value) { return value.key == key; });
        if (item == entries.end() || !item->ok())
            return {QStringLiteral("Corrija o manifesto antes de habilitar este pacote."), {}};
    }
    const auto marker = QDir(directory).filePath(QStringLiteral(".nodivu-disabled"));
    if (enabled && QFileInfo::exists(marker)) {
        qsizetype active = 0;
        for (const auto& package : installed())
            active += package.ok() && package.enabled && !package.removed ? 1 : 0;
        if (active >= 8)
            return {QStringLiteral("Até oito tipos de plugins podem ficar habilitados."), {}};
    }
    if (enabled ? (QFileInfo::exists(marker) && !QFile::remove(marker))
                : !writeBytes(marker, QByteArray("disabled\n")))
        return {QStringLiteral("Não foi possível alterar a ativação."), {}};
    return {{}, QStringLiteral("Ativação será aplicada ao reabrir o aplicativo.")};
}
PackageResult PackageService::markRemoved(const QString& key, bool removed) {
    if (isProtected(key))
        return {QStringLiteral("Este pacote acompanha o aplicativo. Você pode desativá-lo."), {}};
    const auto directory = directoryFor(key);
    if (directory.isEmpty() || linked(directory) || !QFileInfo(directory).isDir())
        return {QStringLiteral("Pacote inválido."), {}};
    const auto marker = QDir(directory).filePath(QStringLiteral(".nodivu-removed"));
    if (removed ? !writeBytes(marker, QByteArray("removed\n"))
                : (QFileInfo::exists(marker) && !QFile::remove(marker)))
        return {QStringLiteral("Não foi possível agendar a remoção."), {}};
    return {{}, QStringLiteral("Remoção alterada; a versão será arquivada na próxima abertura.")};
}
bool PackageService::isProtected(const QString& key) {
    return key == QStringLiteral(".") || key == QStringLiteral("mp3") ||
           key == QStringLiteral("windows-audio") || key == QStringLiteral("voice-cleanup") ||
           key == QStringLiteral("peak-limiter");
}
QHash<QString, QString> PackageService::pendingVersions(QString& error) const {
    QHash<QString, QString> versions;
    for (const auto& journal : readJournals(profile_, error))
        versions.insert(journal.value(QStringLiteral("key")).toString(),
                        journal.value(QStringLiteral("version")).toString());
    return versions;
}
PackageResult PackageService::importZip(const QString& file, const std::atomic_bool* cancelled,
                                        const QString& expectedId, const QString& expectedVersion,
                                        OperationControl* operation) {
    QFile input(file);
    if (linked(file) || !input.open(QIODevice::ReadOnly) || input.size() > MaxPackageBytes)
        return {QStringLiteral("ZIP inválido ou acima de 256 MiB."), {}};
    mz_zip_archive archive{};
    archive.m_pIO_opaque = &input;
    archive.m_pRead = zipRead;
    if (!mz_zip_reader_init(&archive, static_cast<mz_uint64>(input.size()), 0))
        return {QStringLiteral("Não foi possível ler o ZIP."), {}};
    struct ZipGuard {
        mz_zip_archive* archive;
        ~ZipGuard() {
            mz_zip_reader_end(archive);
        }
    } guard{&archive};
    const auto count = mz_zip_reader_get_num_files(&archive);
    if (count == 0 || count > 2048)
        return {QStringLiteral("ZIP vazio ou com entradas demais."), {}};
    qint64 expanded = 0;
    QSet<QString> names;
    QVector<QString> paths;
    for (mz_uint index = 0; index < count; ++index) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&archive, index, &stat))
            return {QStringLiteral("Índice ZIP inválido."), {}};
        const auto needed = mz_zip_reader_get_filename(&archive, index, nullptr, 0);
        if (needed == 0 || needed > 4097)
            return {QStringLiteral("Nome de entrada ZIP inválido."), {}};
        QByteArray bytes(static_cast<qsizetype>(needed), '\0');
        mz_zip_reader_get_filename(&archive, index, bytes.data(), needed);
        bytes.chop(1);
        const auto name = QString::fromUtf8(bytes);
        const auto mode = (stat.m_external_attr >> 16) & 0170000;
        if (name.toUtf8() != bytes || !safeRelativePath(name) ||
            names.contains(name.toCaseFolded()) || stat.m_is_encrypted ||
            (mode != 0 && mode != 0100000 && mode != 0040000) ||
            stat.m_uncomp_size > static_cast<mz_uint64>(MaxPackageBytes - expanded))
            return {QStringLiteral("Caminho, tipo, duplicata ou tamanho inválido no ZIP."), {}};
        names.insert(name.toCaseFolded());
        paths.append(name);
        expanded += static_cast<qint64>(stat.m_uncomp_size);
    }
    const auto unpackRoot = QDir(profile_).filePath(QStringLiteral("plugin-unpack"));
    if (linked(unpackRoot) || !QDir().mkpath(unpackRoot))
        return {QStringLiteral("Pasta temporária inválida."), {}};
    const auto destination =
        QDir(unpackRoot).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QDir().mkpath(destination);
    for (mz_uint index = 0; index < count; ++index) {
        if (cancelled && cancelled->load())
            return {QStringLiteral("Extração cancelada."), {}};
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&archive, index, &stat))
            return {QStringLiteral("Entrada ZIP inválida."), {}};
        const auto path = QDir(destination).filePath(paths[static_cast<qsizetype>(index)]);
        if (stat.m_is_directory) {
            if (!QDir().mkpath(path))
                return {QStringLiteral("Falha ao criar pasta do ZIP."), {}};
            continue;
        }
        if (!QDir().mkpath(QFileInfo(path).absolutePath()) || linked(path))
            return {QStringLiteral("Destino de extração inválido."), {}};
        QSaveFile output(path);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly))
            return {QStringLiteral("Falha ao abrir destino do ZIP."), {}};
        ZipOutput context{&output, cancelled, static_cast<qint64>(stat.m_uncomp_size)};
        if (!mz_zip_reader_extract_to_callback(&archive, index, zipWrite, &context, 0) ||
            context.written != static_cast<qint64>(stat.m_uncomp_size) || !output.commit())
            return {QStringLiteral("Falha na extração, CRC ou cancelamento."), {}};
    }
    auto package = destination;
    if (!QFileInfo::exists(QDir(package).filePath(QStringLiteral("plugin.json")))) {
        const auto children =
            QDir(package).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        if (children.size() != 1 || !QFileInfo(QDir(package).filePath(children.first())).isDir())
            return {
                QStringLiteral("ZIP precisa conter um pacote na raiz ou uma única pasta externa."),
                {}};
        package = QDir(package).filePath(children.first());
    }
    const auto inspected = inspect(package);
    if (!inspected.ok() || (!expectedId.isEmpty() && inspected.id != expectedId) ||
        (!expectedVersion.isEmpty() && inspected.version != expectedVersion))
        return {QStringLiteral(
                    "ID/versão internos diferem do catálogo autenticado ou o pacote é inválido."),
                {}};
    return importFolder(package, cancelled, operation);
}
PackageResult PackageService::prepareBundled(const QString& bundled) {
    const auto bundledCanonical = QFileInfo(bundled).canonicalFilePath();
    if (!bundledCanonical.isEmpty() && bundledCanonical == QFileInfo(root_).canonicalFilePath())
        return {};
    if (linked(root_) || linked(bundled) || !QDir().mkpath(root_))
        return {QStringLiteral("Pasta de plugins embarcados inválida."), {}};
    for (const auto& key : {QStringLiteral("mp3"), QStringLiteral("windows-audio"),
                            QStringLiteral("voice-cleanup"), QStringLiteral("peak-limiter")}) {
        const auto source = QDir(bundled).filePath(key);
        const auto destination = QDir(root_).filePath(key);
        if (!QFileInfo::exists(source))
            return {QStringLiteral("Pacote obrigatório ausente: ") + key, {}};
        if (!QFileInfo::exists(destination)) {
            const auto sourceInfo = inspect(source);
            if (!sourceInfo.ok())
                return {sourceInfo.error, {}};
            const auto staging = QDir(profile_).filePath(QStringLiteral("plugin-staging"));
            if (linked(staging) || !QDir().mkpath(staging))
                return {QStringLiteral("Pasta de staging inválida."), {}};
            const auto stage =
                QDir(staging).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
            auto copied = copyPackage(source, stage, nullptr);
            if (!copied.ok())
                return copied;
            const auto verified = inspect(stage);
            if (!verified.ok() || verified.id != sourceInfo.id ||
                verified.version != sourceInfo.version || !QDir().rename(stage, destination))
                return {QStringLiteral("Falha ao publicar o pacote embarcado validado."), {}};
        }
    }
    return {};
}
PackageResult PackageService::migrateLegacy(const QString& legacyRoot) {
    if (!QFileInfo::exists(legacyRoot) ||
        QFileInfo(legacyRoot).canonicalFilePath() == QFileInfo(root_).canonicalFilePath())
        return {};
    if (linked(legacyRoot) || linked(root_) || !QDir().mkpath(root_))
        return {QStringLiteral("Origem/destino de plugins antigos usa link ou está indisponível."),
                {}};
    QSet<QString> existingIds;
    int enabled = 0;
    for (const auto& record : installed()) {
        if (!record.id.isEmpty())
            existingIds.insert(record.id);
        if (record.enabled && !record.removed)
            ++enabled;
    }
    QStringList warnings;
    for (const auto& record : PackageService(legacyRoot).installed()) {
        if (!record.id.isEmpty() && existingIds.contains(record.id))
            continue;
        auto key = record.key;
        if (key == QStringLiteral(".") || !validKey(key) ||
            QFileInfo::exists(QDir(root_).filePath(key)))
            key = QStringLiteral("legacy-") +
                  QString::fromLatin1(
                      QCryptographicHash::hash((record.directory + record.id).toUtf8(),
                                               QCryptographicHash::Sha256)
                          .toHex()
                          .left(24));
        const auto destination = QDir(root_).filePath(key);
        if (QFileInfo::exists(destination))
            continue; // A partial migration never overwrites its previous copy.
        const auto staging = QDir(profile_).filePath(QStringLiteral("plugin-staging"));
        if (linked(staging) || !QDir().mkpath(staging))
            return {QStringLiteral("Staging de migração inválido."), {}};
        const auto stage =
            QDir(staging).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
        auto copied = copyPackage(record.directory, stage, nullptr);
        if (!copied.ok())
            return copied;
        if (record.ok() && QFileInfo::exists(QDir(stage).filePath(QStringLiteral("plugin.json")))) {
            const auto candidate = inspect(stage);
            if (!candidate.ok() || candidate.id != record.id)
                return {
                    QStringLiteral("Plugin antigo mudou durante a migração; original preservado."),
                    {}};
        }
        if (record.enabled && !record.removed && enabled >= 8) {
            if (!writeBytes(QDir(stage).filePath(QStringLiteral(".nodivu-disabled")),
                            QByteArray("migration type limit\n")))
                return {
                    QStringLiteral("Não foi possível preservar o limite de plugins habilitados."),
                    {}};
            warnings.append(QStringLiteral("%1 foi preservado desativado: limite de oito tipos.")
                                .arg(record.title));
        } else if (record.enabled && !record.removed)
            ++enabled;
        if (!QDir().rename(stage, destination))
            return {QStringLiteral("Não foi possível publicar o plugin migrado."), {}};
        if (!record.id.isEmpty())
            existingIds.insert(record.id);
        if (!record.ok())
            warnings.append(
                QStringLiteral("%1 foi preservado com erro: %2").arg(record.key, record.error));
    }
    return {{}, warnings.join(QLatin1Char('\n'))};
}
PackageResult PackageService::applyPending() {
    QString error;
    const auto journals = readJournals(profile_, error);
    if (!error.isEmpty())
        return {error, {}};
    const auto archive = QDir(profile_).filePath(QStringLiteral("plugin-archive"));
    const auto staging = QDir(profile_).filePath(QStringLiteral("plugin-staging"));
    if (linked(root_) || linked(archive) || linked(staging) || !QDir().mkpath(root_) ||
        !QDir().mkpath(archive))
        return {QStringLiteral("Pastas de plugins ou arquivo inválidas."), {}};
    QStringList warnings;
    for (const auto& journal : journals) {
        const auto current = directoryFor(journal.value(QStringLiteral("key")).toString());
        const auto staged =
            QDir(staging).filePath(journal.value(QStringLiteral("stage")).toString());
        const auto backup =
            QDir(archive).filePath(journal.value(QStringLiteral("backup")).toString());
        const auto id = journal.value(QStringLiteral("pluginId")).toString();
        const auto version = journal.value(QStringLiteral("version")).toString();
        QString failure;
        if (linked(current) || linked(staged) || linked(backup)) {
            failure = QStringLiteral("Link encontrado na transação.");
        } else if (QFileInfo::exists(staged)) {
            const auto next = inspect(staged);
            if (!next.ok() || next.id != id || next.version != version) {
                failure = QStringLiteral("ID/versão da atualização foram alterados.");
            } else {
                if (!QFileInfo::exists(backup)) {
                    const auto old = inspect(current);
                    if (!old.ok() || old.id != id) {
                        failure = QStringLiteral("Pacote original ausente ou inválido.");
                    } else {
                        for (const auto& marker : {QStringLiteral(".nodivu-disabled"),
                                                   QStringLiteral(".nodivu-managed")})
                            if (QFileInfo::exists(QDir(current).filePath(marker)) &&
                                !writeBytes(QDir(staged).filePath(marker),
                                            QByteArray("preserved\n")))
                                failure = QStringLiteral("Não foi possível preservar a ativação.");
                        if (failure.isEmpty() && !QDir().rename(current, backup))
                            failure =
                                QStringLiteral("Não foi possível arquivar a versão anterior.");
                    }
                }
                if (failure.isEmpty() &&
                    (QFileInfo::exists(current) || !QDir().rename(staged, current)))
                    failure = QStringLiteral("Destino ocupado ou versão em uso.");
            }
        } else {
            const auto installed = inspect(current);
            if (!installed.ok() || installed.id != id || installed.version != version ||
                !QFileInfo::exists(backup))
                failure = QStringLiteral("Arquivos da transação estão incompletos.");
        }
        if (failure.isEmpty()) {
            if (!QFile::remove(journal.value(QStringLiteral("file")).toString()))
                warnings.append(
                    QStringLiteral("Versão ativada, mas o journal não pôde ser removido."));
        } else {
            if (!QFileInfo::exists(current) && QFileInfo::exists(backup) && !linked(backup)) {
                if (!QDir().rename(backup, current))
                    failure += QStringLiteral(" A restauração também falhou.");
            }
            warnings.append(id + QStringLiteral(": ") + failure);
        }
    }
    for (const auto& record : installed()) {
        if (!record.removed || record.key == QStringLiteral(".") ||
            record.key == QStringLiteral("mp3") || record.key == QStringLiteral("windows-audio") ||
            record.key == QStringLiteral("voice-cleanup") ||
            record.key == QStringLiteral("peak-limiter") || linked(record.directory))
            continue;
        const auto destination =
            QDir(archive).filePath(record.key.left(100) + QLatin1Char('-') +
                                   QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (!QDir().rename(record.directory, destination))
            warnings.append(QStringLiteral("Remoção aguarda liberação dos arquivos: ") +
                            record.title);
    }
    return {warnings.join(QLatin1Char('\n')), {}};
}
PackageResult PackageService::cancelUpdate(const QString& key) {
    QString error;
    const auto journals = readJournals(profile_, error);
    if (!error.isEmpty())
        return {error, {}};
    for (const auto& journal : journals) {
        if (journal.value(QStringLiteral("key")).toString() != key)
            continue;
        const auto backup = QDir(QDir(profile_).filePath(QStringLiteral("plugin-archive")))
                                .filePath(journal.value(QStringLiteral("backup")).toString());
        if (QFileInfo::exists(backup) || !QFileInfo::exists(directoryFor(key)))
            return {
                QStringLiteral(
                    "Transação iniciada: reabra o aplicativo para recuperar antes de cancelar."),
                {}};
        if (!QFile::remove(journal.value(QStringLiteral("file")).toString()))
            return {QStringLiteral("Não foi possível cancelar a atualização."), {}};
        return {{},
                QStringLiteral("Atualização cancelada; staging pode ser limpo pelo gerenciador.")};
    }
    return {QStringLiteral("Não há atualização pendente para este pacote."), {}};
}
QVector<PackageRecord> PackageService::previous(const QString& key) const {
    QVector<PackageRecord> result;
    const auto current = inspect(directoryFor(key));
    if (!current.ok())
        return result;
    const QDir archive(QDir(profile_).filePath(QStringLiteral("plugin-archive")));
    if (linked(archive.absolutePath()))
        return result;
    for (const auto& folder : archive.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!validKey(folder))
            continue;
        auto record = inspect(archive.filePath(folder));
        if (record.ok() && record.id == current.id) {
            record.key = folder;
            result.append(record);
        }
    }
    return result;
}
PackageResult PackageService::rollback(const QString& key, const QString& archivedKey) {
    if (!validKey(key) || !validKey(archivedKey))
        return {QStringLiteral("Versão arquivada inválida."), {}};
    const auto current = inspect(directoryFor(key));
    const auto archive =
        QDir(QDir(profile_).filePath(QStringLiteral("plugin-archive"))).filePath(archivedKey);
    const auto previous = inspect(archive);
    if (!current.ok() || !previous.ok() || current.id != previous.id || previous.version.isEmpty())
        return {QStringLiteral("Arquivo incompatível com o pacote atual."), {}};
    QString error;
    const auto journals = readJournals(profile_, error);
    if (!error.isEmpty())
        return {error, {}};
    for (const auto& journal : journals)
        if (journal.value(QStringLiteral("key")).toString() == key)
            return {QStringLiteral("Já existe uma troca pendente."), {}};
    const auto stage = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto staging = QDir(profile_).filePath(QStringLiteral("plugin-staging"));
    QDir().mkpath(staging);
    auto copied = copyPackage(archive, QDir(staging).filePath(stage), nullptr);
    if (!copied.ok())
        return copied;
    const auto removedMarker =
        QDir(QDir(staging).filePath(stage)).filePath(QStringLiteral(".nodivu-removed"));
    if (QFileInfo::exists(removedMarker) && !QFile::remove(removedMarker))
        return {QStringLiteral("Não foi possível reativar a versão arquivada."), {}};
    const QJsonObject journal{
        {QStringLiteral("schema"), 1},
        {QStringLiteral("key"), key},
        {QStringLiteral("stage"), stage},
        {QStringLiteral("pluginId"), previous.id},
        {QStringLiteral("version"), previous.version},
        {QStringLiteral("backup"),
         key.left(100) + QLatin1Char('-') + QUuid::createUuid().toString(QUuid::WithoutBraces)}};
    const auto transactions = QDir(profile_).filePath(QStringLiteral("plugin-updates"));
    if (linked(transactions) || !QDir().mkpath(transactions) ||
        !writeBytes(QDir(transactions).filePath(stage + QStringLiteral(".json")),
                    QJsonDocument(journal).toJson(QJsonDocument::Compact)))
        return {QStringLiteral("Falha ao agendar retorno de versão."), {}};
    return {{}, QStringLiteral("Retorno agendado para a próxima abertura.")};
}
PackageResult PackageService::cleanCache() {
    QString error;
    const auto journals = readJournals(profile_, error);
    if (!error.isEmpty())
        return {error, {}};
    QSet<QString> retained;
    for (const auto& journal : journals)
        retained.insert(journal.value(QStringLiteral("stage")).toString());
    for (const auto& name : {QStringLiteral("plugin-staging"), QStringLiteral("plugin-unpack")}) {
        const QDir directory(QDir(profile_).filePath(name));
        if (linked(directory.absolutePath()))
            return {QStringLiteral("Cache não pode ser um link."), {}};
        for (const auto& key : directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (!retained.contains(key) && !removeOwnedTree(directory.absolutePath(), key))
                return {QStringLiteral("Não foi possível limpar staging com segurança."), {}};
    }
    return {{},
            QStringLiteral(
                "Staging não referenciado limpo. Projetos, arquivos e metadados TUF preservados.")};
}
} // namespace nodivu
