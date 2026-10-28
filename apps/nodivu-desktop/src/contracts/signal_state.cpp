#include "contracts/signal_state.hpp"
#include <cmath>
namespace nodivu {
QString nodeSignalStatus(const QJsonObject& snapshot, const NodeDescriptor& d,
                         const QJsonObject& node, const QJsonArray& devices, bool online,
                         double peak) {
    const auto block = node.value(QStringLiteral("block")).toObject();
    const auto id = node.value(QStringLiteral("id")).toString();
    const auto runtime =
        snapshot.value(QStringLiteral("plugin_states")).toObject().value(id).toObject();
    if (!online)
        return QStringLiteral("Motor desconectado");
    if (d.missing)
        return QStringLiteral("Plugin indisponível");
    if (d.kind == QStringLiteral("capture") || d.kind == QStringLiteral("output")) {
        const auto endpoint = block.value(QStringLiteral("endpoint_id")).toString();
        if (endpoint.isEmpty())
            return d.kind == QStringLiteral("capture") ? QStringLiteral("Selecione uma entrada")
                                                       : QStringLiteral("Selecione uma saída");
        bool active = false;
        for (const auto& value : devices) {
            const auto item = value.toObject();
            if (item.value(QStringLiteral("endpoint_id")) == endpoint &&
                item.value(QStringLiteral("state")).toString() == QStringLiteral("active"))
                active = true;
        }
        if (!active)
            return QStringLiteral("Dispositivo indisponível");
    }
    if (snapshot.value(QStringLiteral("muted")).toBool())
        return QStringLiteral("Projeto silenciado");
    if (block.value(QStringLiteral("bypass")).toBool())
        return d.inputs == 0    ? QStringLiteral("Fonte silenciada")
               : d.outputs == 0 ? QStringLiteral("Consumidor suspenso")
                                : QStringLiteral("Bypass · áudio original");
    if (d.kind == QStringLiteral("tone") && !block.value(QStringLiteral("enabled")).toBool())
        return QStringLiteral("Gerador desligado");
    if (d.filePlayer && runtime.value(QStringLiteral("status")).toInt() != 3)
        return runtime.value(QStringLiteral("file")).toString().isEmpty()
                   ? QStringLiteral("Escolha um arquivo")
                   : QStringLiteral("Arquivo parado");
    if (d.captureSource && runtime.value(QStringLiteral("capture_selection")).toObject().isEmpty())
        return QStringLiteral("Selecione um aplicativo");
    bool connected = false;
    for (const auto& value : snapshot.value(QStringLiteral("graph"))
                                 .toObject()
                                 .value(QStringLiteral("edges"))
                                 .toArray()) {
        const auto edge = value.toObject();
        connected |=
            edge.value(d.inputs == 0 ? QStringLiteral("from") : QStringLiteral("to")) == id;
    }
    if (!connected)
        return d.inputs == 0 ? QStringLiteral("Conecte esta fonte")
                             : QStringLiteral("Sem conexão de entrada");
    if (snapshot.value(QStringLiteral("engine_state")).toString() != QStringLiteral("running"))
        return QStringLiteral("Áudio ainda não ativo");
    if (std::isfinite(peak) && peak > 0.000001)
        return QStringLiteral("%1 dBFS · sinal").arg(20 * std::log10(peak), 0, 'f', 1);
    return QStringLiteral("Sem sinal detectado");
}
} // namespace nodivu
