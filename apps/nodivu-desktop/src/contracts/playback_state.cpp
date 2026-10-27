#include "contracts/playback_state.hpp"
#include <QSet>
#include <QStringList>
#include <algorithm>

namespace nodivu {
namespace {
bool reaches(const QJsonObject& graph, const QString& source, bool outputOnly) {
    const auto nodes = graph.value(QStringLiteral("nodes")).toArray();
    const auto edges = graph.value(QStringLiteral("edges")).toArray();
    QSet<QString> seen;
    QStringList pending{source};
    while (!pending.isEmpty()) {
        const auto id = pending.takeLast();
        if (seen.contains(id))
            continue;
        seen.insert(id);
        QJsonObject block;
        for (const auto& value : nodes) {
            const auto node = value.toObject();
            if (node.value(QStringLiteral("id")).toString() == id) {
                block = node.value(QStringLiteral("block")).toObject();
                break;
            }
        }
        if (block.isEmpty())
            continue;
        const auto kind = block.value(QStringLiteral("kind")).toString();
        if (id != source && (kind == QStringLiteral("output") ||
                             (!outputOnly && (kind == QStringLiteral("meter") ||
                                              (kind == QStringLiteral("plugin") &&
                                               block.value(QStringLiteral("consumer")).toBool() &&
                                               !block.value(QStringLiteral("bypass")).toBool())))))
            return true;
        for (const auto& value : edges) {
            const auto edge = value.toObject();
            const auto to = edge.value(QStringLiteral("to")).toString();
            if (edge.value(QStringLiteral("from")).toString() == id && !seen.contains(to) &&
                !pending.contains(to))
                pending.append(to);
        }
    }
    return false;
}
QString decodeFailure(int status) {
    const auto code = static_cast<quint32>(status);
    if (code == 0x800700dfU)
        return QStringLiteral(
            "Este player aceita áudio de até 10 minutos. Escolha um trecho mais curto.");
    if (code == 0x80070002U || code == 0x80070003U)
        return QStringLiteral("Arquivo não encontrado. Escolha o áudio novamente.");
    if (code == 0x80070005U)
        return QStringLiteral("Sem acesso ao arquivo. Escolha um áudio local que possa ser lido.");
    return QStringLiteral("Não foi possível decodificar este áudio (0x%1). Tente outro arquivo: "
                          "até 10 minutos, 256 MiB, mono ou estéreo.")
        .arg(code, 8, 16, QLatin1Char('0'));
}
} // namespace
QString playbackTime(int milliseconds) {
    const int seconds = std::max(0, milliseconds) / 1000;
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
PlaybackState playbackState(const QJsonObject& snapshot, const QJsonObject& node,
                            const QJsonArray& devices, bool online, bool busy,
                            const QString& savedPath) {
    PlaybackState result;
    const auto id = node.value(QStringLiteral("id")).toString();
    const auto runtime =
        snapshot.value(QStringLiteral("plugin_states")).toObject().value(id).toObject();
    const auto status = runtime.value(QStringLiteral("status")).toInt();
    result.file = runtime.value(QStringLiteral("file")).toString();
    result.needsLoad = !savedPath.isEmpty() && (result.file.isEmpty() || status == 0);
    if (result.file.isEmpty())
        result.file = savedPath;
    result.name = QString(result.file)
                      .replace(QLatin1Char('\\'), QLatin1Char('/'))
                      .section(QLatin1Char('/'), -1);
    if (result.name.isEmpty())
        result.name = QStringLiteral("Nenhum áudio escolhido");
    const QStringList labels{QStringLiteral("Arquivo ainda não carregado"),
                             QStringLiteral("Carregando áudio…"),
                             QStringLiteral("Arquivo pronto"),
                             QStringLiteral("Reproduzindo"),
                             QStringLiteral("Terminou"),
                             QStringLiteral("Pausado")};
    result.label = status < 0
                       ? QStringLiteral("Falha ao carregar")
                       : labels.value(status, QStringLiteral("Estado do player desconhecido"));
    result.playing = status == 3;
    result.positionMs = std::max(0, runtime.value(QStringLiteral("position_ms")).toInt());
    result.durationMs = std::max(0, runtime.value(QStringLiteral("duration_ms")).toInt());
    if (!online)
        result.reason = QStringLiteral("Backend desconectado. Reconecte para usar o player.");
    else if (status < 0)
        result.reason = decodeFailure(status);
    else if (status == 1)
        result.reason = QStringLiteral("Aguarde a decodificação do áudio.");
    else if (status < 2 || status > 5 || runtime.value(QStringLiteral("file")).toString().isEmpty())
        result.reason =
            result.file.isEmpty()
                ? QStringLiteral("Escolha MP3, WAV ou M4A/AAC local para habilitar a reprodução.")
                : QStringLiteral("Áudio salvo, ainda não carregado. Use Carregar áudio salvo para "
                                 "tentar novamente.");
    const auto graph = snapshot.value(QStringLiteral("graph")).toObject();
    if (result.reason.isEmpty()) {
        QJsonValue output;
        for (const auto& value : graph.value(QStringLiteral("nodes")).toArray()) {
            const auto block = value.toObject().value(QStringLiteral("block")).toObject();
            if (block.value(QStringLiteral("kind")).toString() == QStringLiteral("output"))
                output = block.value(QStringLiteral("endpoint_id"));
        }
        bool available = false;
        for (const auto& value : devices) {
            const auto device = value.toObject();
            available |=
                device.value(QStringLiteral("endpoint_id")) == output &&
                device.value(QStringLiteral("flow")).toString() == QStringLiteral("render") &&
                device.value(QStringLiteral("state")).toString() == QStringLiteral("active");
        }
        if (output.toString().isEmpty())
            result.reason = QStringLiteral("Escolha um dispositivo no bloco Saída de áudio.");
        else if (!available)
            result.reason = QStringLiteral("A saída selecionada está indisponível. Revise a "
                                           "seleção; ela não será trocada automaticamente.");
        else if (snapshot.value(QStringLiteral("suspended")).toBool())
            result.reason = QStringLiteral("Preparando o áudio do projeto…");
        else if (snapshot.value(QStringLiteral("engine_state")).toString() !=
                 QStringLiteral("running")) {
            result.reason = snapshot.value(QStringLiteral("last_error"))
                                .toObject()
                                .value(QStringLiteral("message"))
                                .toString();
            if (result.reason.isEmpty())
                result.reason = QStringLiteral(
                    "Aguarde a abertura do áudio ou tente preparar os dispositivos novamente.");
        } else if (snapshot.value(QStringLiteral("muted")).toBool())
            result.reason = QStringLiteral("Tudo silenciado. Restaure o som para reproduzir.");
    }
    const bool connected = reaches(graph, id, false);
    if (result.reason.isEmpty() &&
        node.value(QStringLiteral("block")).toObject().value(QStringLiteral("bypass")).toBool())
        result.reason = QStringLiteral("O player está em bypass. Desmarque Bypass para tocar.");
    if (result.reason.isEmpty() && !connected)
        result.reason = QStringLiteral("Conecte este player à saída de áudio ou a um consumidor.");
    if (result.reason.isEmpty() && busy)
        result.reason = QStringLiteral("Aguarde a operação atual.");
    result.canPlay = result.reason.isEmpty();
    if (connected && !reaches(graph, id, true))
        result.warning = QStringLiteral("Este caminho chega a um consumidor, mas não à saída de "
                                        "áudio; você não ouvirá nos fones.");
    return result;
}
} // namespace nodivu
