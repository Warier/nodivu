#include "features/projects/project_repository.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStringDecoder>

namespace nodivu {
FileResult ProjectRepository::read(const QString& file) {
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly))
        return {{}, QStringLiteral("Não foi possível ler o arquivo: ") + input.errorString()};
    const auto bytes = input.read(MaxProjectBytes + 1);
    if (bytes.size() > MaxProjectBytes)
        return {{}, QStringLiteral("Projeto excede 64 KiB.")};
    if (input.error() != QFileDevice::NoError)
        return {{}, input.errorString()};
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString decoded = decoder(bytes);
    Q_UNUSED(decoded);
    if (decoder.hasError())
        return {{}, QStringLiteral("Arquivo não contém UTF-8 válido.")};
    return {bytes, {}, QFileInfo(file).lastModified()};
}
FileResult ProjectRepository::write(const QString& file, const QJsonObject& project) {
    const auto bytes = QJsonDocument(project).toJson(QJsonDocument::Indented);
    if (bytes.size() > MaxProjectBytes)
        return {{}, QStringLiteral("Projeto excede 64 KiB.")};
    if (!QDir().mkpath(QFileInfo(file).absolutePath()))
        return {{}, QStringLiteral("Não foi possível criar a pasta do projeto.")};
    QSaveFile output(file);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
        !output.commit())
        return {{},
                QStringLiteral("Falha ao salvar; o original foi preservado: ") +
                    output.errorString()};
    return {bytes, {}, QFileInfo(file).lastModified()};
}
FileResult ProjectRepository::removeRecovery(const QString& file) {
    if (QFileInfo::exists(file) && !QFile::remove(file))
        return {{}, QStringLiteral("Não foi possível remover a cópia de recuperação.")};
    return {};
}
QJsonObject ProjectRepository::resolveResources(QJsonObject project, const QString& file,
                                                bool opening) {
    const QDir directory(QFileInfo(file).absolutePath());
    auto resources = project.value(QStringLiteral("resources")).toArray();
    for (qsizetype i = 0; i < resources.size(); ++i) {
        auto resource = resources[i].toObject();
        const auto path = resource.value(QStringLiteral("path")).toString();
        const auto absolute = QDir::cleanPath(directory.absoluteFilePath(path));
        const auto relative = directory.relativeFilePath(absolute);
        resource.insert(QStringLiteral("path"),
                        opening || relative.startsWith(QStringLiteral("../")) ||
                                QDir::isAbsolutePath(relative)
                            ? absolute
                            : relative);
        resources[i] = resource;
    }
    project.insert(QStringLiteral("resources"), resources);
    return project;
}
} // namespace nodivu
