#include "platform/profile_backup.hpp"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace nodivu {
QString prepareQtProfileBackup(const QString& profile) {
    const QDir root(profile);
    const auto migration = root.filePath(QStringLiteral("migration"));
    const auto marker = QDir(migration).filePath(QStringLiteral("qt-profile-v1.json"));
    if (QFileInfo(migration).isJunction() || QFileInfo(migration).isSymbolicLink())
        return QStringLiteral("Diretório de migração não pode ser um link.");
    if (QFileInfo::exists(marker))
        return {};
    const auto destination =
        QDir(migration).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(destination))
        return QStringLiteral("Não foi possível criar backup do perfil anterior.");
    QStringList files;
    for (const auto& name :
         {QStringLiteral("recent-projects.json"), QStringLiteral("recovery/recovery.nodivu.json")})
        if (QFileInfo::exists(root.filePath(name)))
            files.append(root.filePath(name));
    const auto catalogs = root.filePath(QStringLiteral("plugin-catalog"));
    if (QFileInfo(catalogs).isJunction() || QFileInfo(catalogs).isSymbolicLink())
        return QStringLiteral(
            "Catálogo anterior usa um link; backup e abertura foram interrompidos.");
    const QDir catalogDirectory(catalogs);
    for (const auto& identity : catalogDirectory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (identity.isJunction() || identity.isSymbolicLink())
            return QStringLiteral("Metadados anteriores usam um link.");
        const auto metadata =
            QDir(identity.absoluteFilePath()).filePath(QStringLiteral("metadata"));
        if (QFileInfo(metadata).isJunction() || QFileInfo(metadata).isSymbolicLink())
            return QStringLiteral("Metadados anteriores usam um link.");
        QDirIterator iterator(metadata, QDir::Files | QDir::NoDotAndDotDot,
                              QDirIterator::NoIteratorFlags);
        while (iterator.hasNext()) {
            files.append(iterator.next());
            if (files.size() > 512)
                return QStringLiteral(
                    "Backup de perfil excede 512 arquivos; originais preservados.");
        }
    }
    qint64 total = 0;
    QJsonArray inventory;
    for (const auto& file : files) {
        const QFileInfo info(file);
        const auto relative = root.relativeFilePath(file);
        if (info.isJunction() || info.isSymbolicLink() || info.size() > 2 * 1024 * 1024 ||
            total > 64LL * 1024 * 1024 - info.size())
            return QStringLiteral(
                "Backup encontrou arquivo fora dos limites; originais preservados.");
        const auto target = QDir(destination).filePath(relative);
        if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::copy(file, target))
            return QStringLiteral("Falha ao copiar perfil anterior; abertura interrompida.");
        QFile copied(target);
        if (!copied.open(QIODevice::ReadOnly))
            return QStringLiteral("Falha ao verificar backup do perfil.");
        const auto contents = copied.readAll();
        total += contents.size();
        inventory.append(QJsonObject{
            {QStringLiteral("path"), relative},
            {QStringLiteral("bytes"), contents.size()},
            {QStringLiteral("sha256"),
             QString::fromLatin1(
                 QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex())}});
    }
    const QJsonObject record{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("directory"), QFileInfo(destination).fileName()},
        {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("files"), inventory}};
    QSaveFile output(marker);
    output.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(record).toJson(QJsonDocument::Indented);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
        !output.commit())
        return QStringLiteral("Backup copiado, mas seu registro não pôde ser concluído.");
    return {};
}
} // namespace nodivu
