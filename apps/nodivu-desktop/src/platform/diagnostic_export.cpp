#include "platform/diagnostic_export.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
namespace nodivu {
QString redactDiagnostic(QString text) {
    // Prefer dropping excess detail to retaining names inside an unquoted path.
    static const QRegularExpression path(QStringLiteral(R"((?:[A-Za-z]:[\\/]|\\\\)[^\r\n]*\b)"));
    static const QRegularExpression id(
        QStringLiteral(R"(\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression sid(QStringLiteral(R"(\bS-1-5-21(?:-\d+){3,4}\b)"),
                                        QRegularExpression::CaseInsensitiveOption);
    return text.replace(path, QStringLiteral("<caminho-local>"))
        .replace(id, QStringLiteral("<id>"))
        .replace(sid, QStringLiteral("<id>"));
}
DiagnosticReport diagnosticReport(const QString& profile) {
    const auto directory = QDir(profile).filePath(QStringLiteral("logs"));
    if (QFileInfo(directory).isSymbolicLink() || QFileInfo(directory).isJunction())
        return {{}, QStringLiteral("A pasta de logs usa um link; exportação recusada.")};
    QJsonArray records;
    for (const auto& name : {QStringLiteral("desktop.jsonl.1"), QStringLiteral("desktop.jsonl")}) {
        const auto path = QDir(directory).filePath(name);
        if (!QFile::exists(path))
            continue;
        QFile file(path);
        if (QFileInfo(path).isSymbolicLink() || !file.open(QIODevice::ReadOnly))
            return {{}, QStringLiteral("Não foi possível ler os logs locais.")};
        const auto bytes = file.read(1024 * 1024 + 16385);
        if (bytes.size() > 1024 * 1024 + 16384)
            return {{}, QStringLiteral("Log excede o limite de exportação.")};
        for (const auto& line : bytes.split('\n')) {
            if (line.trimmed().isEmpty())
                continue;
            QJsonParseError error;
            const auto row = QJsonDocument::fromJson(line, &error).object();
            if (error.error != QJsonParseError::NoError || row.isEmpty())
                return {{}, QStringLiteral("Log inválido; arquivo original preservado.")};
            QJsonObject record;
            for (const auto& field :
                 {QStringLiteral("time"), QStringLiteral("category"), QStringLiteral("message")})
                record.insert(field, redactDiagnostic(row.value(field).toString().left(4096)));
            records.append(record);
        }
    }
    return {
        QJsonDocument(
            QJsonObject{{QStringLiteral("format"), QStringLiteral("nodivu-qt-diagnostics")},
                        {QStringLiteral("version"), 1},
                        {QStringLiteral("app_version"), QCoreApplication::applicationVersion()},
                        {QStringLiteral("notice"),
                         QStringLiteral(
                             "Caminhos e IDs conhecidos foram removidos. Revise nomes livres antes "
                             "de compartilhar. Sem projeto, áudio ou arquivos de plugins.")},
                        {QStringLiteral("records"), records}})
            .toJson(),
        {}};
}
} // namespace nodivu
