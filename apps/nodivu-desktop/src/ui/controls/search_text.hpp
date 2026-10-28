#pragma once
#include "contracts/node_descriptor.hpp"
#include <QChar>
#include <QStringView>
namespace nodivu {
inline QString normalizedSearchText(const QString& text) {
    QString result;
    for (const auto c : text.normalized(QString::NormalizationForm_D))
        if (c.category() != QChar::Mark_NonSpacing)
            result.append(c.toLower());
    return result;
}
inline bool matchesSearchText(const QString& text, QStringView query) {
    const auto normalized = normalizedSearchText(text);
    for (const auto& word :
         normalizedSearchText(query.toString()).split(QLatin1Char(' '), Qt::SkipEmptyParts))
        if (!normalized.contains(word))
            return false;
    return true;
}
inline QString blockSearchText(const NodeDescriptor& d) {
    QString terms = d.title + QLatin1Char(' ') + d.subtitle + QLatin1Char(' ') + d.description;
    if (d.kind == QStringLiteral("capture"))
        terms += QStringLiteral(" microfone entrada voz mic");
    if (d.kind == QStringLiteral("output"))
        terms += QStringLiteral(" destino saida fones alto-falante cabo");
    if (d.kind == QStringLiteral("gain"))
        terms += QStringLiteral(" volume ganho amplificar");
    if (d.filePlayer)
        terms += QStringLiteral(" arquivo musica mp3 wav m4a player");
    if (d.captureSource)
        terms += QStringLiteral(" aplicativo programa jogo captura audio");
    return terms;
}
} // namespace nodivu
