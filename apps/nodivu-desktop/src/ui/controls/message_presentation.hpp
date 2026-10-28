#pragma once
#include <QString>
namespace nodivu {
inline QString friendlyError(const QString& details) {
    if (details.contains(QStringLiteral("JSON"), Qt::CaseInsensitive) ||
        details.contains(QStringLiteral("schema"), Qt::CaseInsensitive))
        return QStringLiteral("Não foi possível ler os dados recebidos. Atualize a lista ou tente "
                              "abrir novamente. Consulte os detalhes se o problema continuar.");
    if (details.contains(QStringLiteral("signature"), Qt::CaseInsensitive) ||
        details.contains(QStringLiteral("hash"), Qt::CaseInsensitive) ||
        details.contains(QStringLiteral("assinatura"), Qt::CaseInsensitive))
        return QStringLiteral("A verificação de segurança falhou. A operação foi interrompida; "
                              "confira a origem do pacote e tente atualizar novamente.");
    if (details.contains(QStringLiteral("HTTP"), Qt::CaseInsensitive) ||
        details.contains(QStringLiteral("TLS"), Qt::CaseInsensitive))
        return QStringLiteral("Não foi possível acessar o serviço com segurança. Confira sua "
                              "conexão e tente novamente.");
    return details;
}
} // namespace nodivu
