#include "contracts/monitor_state.hpp"
#include <algorithm>
namespace nodivu {
MonitorState monitorState(const QJsonObject& snapshot, const QJsonArray& devices, bool online,
                          bool busy, const QString& selection) {
    MonitorState result;
    QString primary;
    for (const auto& value : snapshot.value(QStringLiteral("graph"))
                                 .toObject()
                                 .value(QStringLiteral("nodes"))
                                 .toArray()) {
        const auto block = value.toObject().value(QStringLiteral("block")).toObject();
        if (block.value(QStringLiteral("kind")).toString() == QStringLiteral("output"))
            primary = block.value(QStringLiteral("endpoint_id")).toString();
    }
    const auto monitor = snapshot.value(QStringLiteral("monitor")).toObject();
    const auto runtime = monitor.value(QStringLiteral("runtime")).toObject();
    result.selectedId = monitor.value(QStringLiteral("endpoint_id")).toString();
    result.active = !result.selectedId.isEmpty();
    for (const auto& value : devices) {
        const auto device = value.toObject();
        if (device.value(QStringLiteral("flow")).toString() == QStringLiteral("render") &&
            device.value(QStringLiteral("state")).toString() == QStringLiteral("active") &&
            device.value(QStringLiteral("endpoint_id")).toString() != primary)
            result.devices.append(device);
    }
    const auto unavailable = !online ? QStringLiteral("O motor de áudio está desconectado.")
                             : busy  ? QStringLiteral("Aguarde a operação atual do projeto.")
                             : snapshot.value(QStringLiteral("demo")).toBool()
                                 ? QStringLiteral("O modo de demonstração não reproduz áudio.")
                                 : QString();
    result.toggleReason = unavailable;
    result.selectionReason = unavailable;
    if (unavailable.isEmpty()) {
        if (result.active)
            result.selectionReason = QStringLiteral("Desligue a escuta antes de trocar os fones.");
        else {
            if (primary.isEmpty())
                result.selectionReason = result.toggleReason =
                    QStringLiteral("Selecione primeiro o dispositivo no bloco Saída de áudio.");
            else if (result.devices.isEmpty())
                result.selectionReason = result.toggleReason =
                    QStringLiteral("Conecte outra saída de áudio para escutar; ela deve ser "
                                   "diferente da saída principal.");
            else if (std::none_of(result.devices.cbegin(), result.devices.cend(),
                                  [&](const QJsonValue& value) {
                                      return value.toObject()
                                                 .value(QStringLiteral("endpoint_id"))
                                                 .toString() == selection;
                                  }))
                result.toggleReason =
                    QStringLiteral("Selecione os fones para ligar a escuta local.");
        }
    }
    const auto error = runtime.value(QStringLiteral("error"))
                           .toObject()
                           .value(QStringLiteral("message"))
                           .toString();
    const auto phase = runtime.value(QStringLiteral("state")).toString();
    result.failed = !error.isEmpty();
    result.message = !online ? QStringLiteral("Backend desconectado.")
                     : snapshot.value(QStringLiteral("demo")).toBool()
                         ? QStringLiteral("Demonstração: sem áudio.")
                     : !error.isEmpty() ? QStringLiteral("Falha somente na escuta: ") + error
                     : result.active
                         ? (phase == QStringLiteral("running")
                                ? QStringLiteral("Escutando a saída final nos fones.")
                            : phase == QStringLiteral("starting")
                                ? QStringLiteral("Abrindo saída de escuta…")
                                : QStringLiteral("Aguardando áudio principal."))
                         : QStringLiteral("Desligada. O áudio principal continua normal.");
    return result;
}
} // namespace nodivu
