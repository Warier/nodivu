#include "contracts/audio_state.hpp"
#include <QStringList>
namespace nodivu {
AudioState audioState(const QJsonObject& snapshot, const QJsonArray& devices, bool online,
                      const QString& deviceIssue) {
    if (snapshot.value(QStringLiteral("demo")).toBool())
        return {QStringLiteral("Demonstração visual — sem áudio"),
                QStringLiteral("Edite blocos, fios e projetos. Dispositivos, captura e reprodução "
                               "reais exigem o backend Windows.")};
    if (!online)
        return {QStringLiteral("Backend desconectado"),
                QStringLiteral("Reconecte o backend para controlar o áudio. A cópia de recuperação "
                               "do projeto é preservada."),
                QStringLiteral("Reconectar backend"), AudioAction::Reconnect};
    const auto phase = snapshot.value(QStringLiteral("engine_state")).toString();
    if (!deviceIssue.isEmpty())
        return {QStringLiteral("Falha ao detectar dispositivos de áudio"),
                QStringLiteral("Você pode continuar editando e salvando o projeto. Atualize os "
                               "dispositivos. Detalhes: ") +
                    deviceIssue,
                QStringLiteral("Tentar detectar novamente"), AudioAction::Refresh,
                phase == QStringLiteral("running") &&
                    !snapshot.value(QStringLiteral("muted")).toBool()};
    if (snapshot.value(QStringLiteral("graph"))
            .toObject()
            .value(QStringLiteral("nodes"))
            .toArray()
            .isEmpty())
        return {QStringLiteral("Comece pelo seu áudio"),
                QStringLiteral("Escolha microfone, aplicativo ou arquivo na área de projeto.")};
    QString output;
    for (const auto& value : snapshot.value(QStringLiteral("graph"))
                                 .toObject()
                                 .value(QStringLiteral("nodes"))
                                 .toArray()) {
        const auto block = value.toObject().value(QStringLiteral("block")).toObject();
        if (block.value(QStringLiteral("kind")).toString() == QStringLiteral("output"))
            output = block.value(QStringLiteral("endpoint_id")).toString();
    }
    if (output.isEmpty())
        return {QStringLiteral("Escolha uma saída"),
                QStringLiteral("Selecione seus fones ou CABLE Input no bloco Saída de áudio."),
                QStringLiteral("Selecionar saída"), AudioAction::Output};
    QJsonObject endpoint;
    for (const auto& value : devices) {
        const auto device = value.toObject();
        if (device.value(QStringLiteral("endpoint_id")).toString() == output &&
            device.value(QStringLiteral("flow")).toString() == QStringLiteral("render"))
            endpoint = device;
    }
    if (endpoint.value(QStringLiteral("state")).toString() != QStringLiteral("active"))
        return {QStringLiteral("Saída indisponível"),
                QStringLiteral("O dispositivo salvo não está ativo. Atualize a lista ou escolha "
                               "outra saída; não trocamos automaticamente."),
                QStringLiteral("Revisar saída"), AudioAction::Output};
    if (snapshot.value(QStringLiteral("suspended")).toBool())
        return {QStringLiteral("Preparando áudio do projeto…"),
                QStringLiteral("O áudio e os arquivos salvos são preparados automaticamente. Se a "
                               "preparação falhar, tente novamente."),
                QStringLiteral("Tentar preparar novamente"), AudioAction::Retry};
    if (phase == QStringLiteral("starting"))
        return {QStringLiteral("Abrindo dispositivos…"),
                QStringLiteral("Aguarde a abertura da saída selecionada.")};
    const auto error = snapshot.value(QStringLiteral("last_error"))
                           .toObject()
                           .value(QStringLiteral("message"))
                           .toString();
    if (phase == QStringLiteral("error") || !error.isEmpty())
        return {QStringLiteral("Não foi possível abrir o áudio"),
                error.isEmpty() ? QStringLiteral("Confira os dispositivos e tente novamente.")
                                : error,
                QStringLiteral("Tentar ativar novamente"), AudioAction::Retry};
    if (phase != QStringLiteral("running"))
        return {QStringLiteral("Áudio indisponível"),
                QStringLiteral("Não foi possível manter os dispositivos abertos. Confira a seleção "
                               "e tente novamente."),
                QStringLiteral("Tentar abrir novamente"), AudioAction::Retry};
    if (snapshot.value(QStringLiteral("muted")).toBool())
        return {QStringLiteral("Tudo silenciado"),
                QStringLiteral("O áudio está ativo, mas o envio foi silenciado."),
                QStringLiteral("Restaurar som"), AudioAction::Unmute};
    const auto captureError = snapshot.value(QStringLiteral("capture_error"))
                                  .toObject()
                                  .value(QStringLiteral("message"))
                                  .toString();
    if (!captureError.isEmpty())
        return {QStringLiteral("Saída ativa · microfone indisponível"),
                captureError + QStringLiteral(" O player ainda pode tocar. Confira a entrada e "
                                              "tente reabrir os dispositivos."),
                QStringLiteral("Reabrir dispositivos"),
                AudioAction::Retry,
                true,
                true};
    const auto missing = snapshot.value(QStringLiteral("missing_plugins")).toArray();
    if (!missing.isEmpty()) {
        QStringList names;
        for (const auto& value : missing) {
            const auto id = value.toObject().value(QStringLiteral("plugin_id")).toString();
            if (!names.contains(id))
                names.append(id);
        }
        return {QStringLiteral("Projeto com plugins indisponíveis"),
                QStringLiteral(
                    "Caminhos com plugins ausentes estão silenciosos; os demais continuam ativos. "
                    "Reinstale ou ative em Gerenciar plugins e reabra o app. Preservados: ") +
                    names.join(QStringLiteral(", ")),
                {},
                AudioAction::None,
                true,
                true};
    }
    if (snapshot.value(QStringLiteral("signal")).toInt() == 0)
        return {snapshot.value(QStringLiteral("graph"))
                        .toObject()
                        .value(QStringLiteral("edges"))
                        .toArray()
                        .isEmpty()
                    ? QStringLiteral("Saída pronta · conecte os blocos")
                    : QStringLiteral("Saída pronta · configure a fonte"),
                QStringLiteral(
                    "O dispositivo está aberto. Conecte uma fonte à saída ou a um consumidor; o "
                    "áudio passa automaticamente quando o caminho estiver pronto."),
                {},
                AudioAction::None,
                true};
    const bool measured = snapshot.value(QStringLiteral("metrics"))
                              .toObject()
                              .value(QStringLiteral("output_peak"))
                              .toDouble() > 0.000001;
    return {measured ? QStringLiteral("Sinal detectado na saída")
                     : QStringLiteral("Saída ativa · sem sinal"),
            QStringLiteral("Destino: ") + endpoint.value(QStringLiteral("name")).toString() +
                QStringLiteral(". Medição do aplicativo; confirme a recepção no destino."),
            {},
            AudioAction::None,
            true};
}
} // namespace nodivu
