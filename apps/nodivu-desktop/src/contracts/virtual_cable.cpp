#include "contracts/virtual_cable.hpp"

namespace nodivu {
namespace {
QJsonObject endpoint(const QString& id, const QJsonArray& devices) {
    if (id.isEmpty())
        return {};
    for (const auto& value : devices)
        if (value.toObject().value(QStringLiteral("endpoint_id")).toString() == id)
            return value.toObject();
    return {};
}
bool cable(const QJsonObject& device, bool render) {
    // Friendly names are advisory; this never selects an endpoint or proves driver identity.
    return device.value(QStringLiteral("flow")).toString() ==
               (render ? QStringLiteral("render") : QStringLiteral("capture")) &&
           device.value(QStringLiteral("name")).toString() ==
               (render ? QStringLiteral("CABLE Input (VB-Audio Virtual Cable)")
                       : QStringLiteral("CABLE Output (VB-Audio Virtual Cable)"));
}
} // namespace
OutputPresentation outputPresentation(const QString& endpointId, const QJsonArray& devices) {
    if (cable(endpoint(endpointId, devices), true))
        return {QStringLiteral("Microfone virtual"),
                QStringLiteral("VB-CABLE · envio para outros apps"),
                QStringLiteral("No outro app, escolha CABLE Output como microfone. Esta barra mede "
                               "o envio; confirme a recepção no app de destino.")};
    return {QStringLiteral("Saída de áudio"), QStringLiteral("Fones ou microfone virtual"),
            QStringLiteral("Para enviar a outros apps, selecione CABLE Input. Para ouvir "
                           "localmente, selecione seus fones. Uma saída por projeto.")};
}
QString cableRouteMessage(const QJsonObject& snapshot, const QJsonArray& devices) {
    QString output, input;
    for (const auto& value : snapshot.value(QStringLiteral("graph"))
                                 .toObject()
                                 .value(QStringLiteral("nodes"))
                                 .toArray()) {
        const auto block = value.toObject().value(QStringLiteral("block")).toObject();
        if (block.value(QStringLiteral("kind")).toString() == QStringLiteral("output"))
            output = block.value(QStringLiteral("endpoint_id")).toString();
        if (block.value(QStringLiteral("kind")).toString() == QStringLiteral("capture"))
            input = block.value(QStringLiteral("endpoint_id")).toString();
    }
    const auto destination = endpoint(output, devices);
    if (destination.isEmpty())
        return output.isEmpty()
                   ? QStringLiteral("Destino: selecione CABLE Input no bloco de saída.")
                   : QStringLiteral("Destino salvo indisponível. Nenhum outro dispositivo será "
                                    "escolhido automaticamente.");
    if (destination.value(QStringLiteral("state")).toString() != QStringLiteral("active"))
        return QStringLiteral(
            "Destino salvo indisponível. Nenhum outro dispositivo será escolhido automaticamente.");
    if (!cable(destination, true))
        return QStringLiteral("Destino atual: %1. Para VB-CABLE, escolha CABLE Input; cabos "
                              "renomeados exigem identificação manual.")
            .arg(destination.value(QStringLiteral("name")).toString());
    if (cable(endpoint(input, devices), false))
        return QStringLiteral("Atenção: capturar CABLE Output e devolver ao CABLE Input pode criar "
                              "retorno. Use seu microfone real como entrada.");
    if (snapshot.value(QStringLiteral("suspended")).toBool())
        return QStringLiteral("Preparando o áudio do projeto automaticamente. Se houver falha, "
                              "consulte o aviso acima do canvas.");
    if (snapshot.value(QStringLiteral("engine_state")).toString() != QStringLiteral("running"))
        return QStringLiteral(
            "CABLE Input selecionado; aguarde a abertura ou confira o erro de dispositivo.");
    if (snapshot.value(QStringLiteral("muted")).toBool())
        return QStringLiteral("Envio silenciado. Use Restaurar som.");
    return snapshot.value(QStringLiteral("signal")).toInt()
               ? QStringLiteral(
                     "Envio ao cabo ativo. Confirme CABLE Output no aplicativo que vai receber.")
               : QStringLiteral(
                     "Cabo aberto em silêncio. Conecte o microfone, player ou Mixer à saída.");
}
} // namespace nodivu
