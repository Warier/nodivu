#include "contracts/node_descriptor.hpp"

#include <algorithm>

namespace nodivu {
namespace {
ControlDescriptor field(const QString& key, const QString& label, ControlKind kind, double low = 0,
                        double high = 1, double step = 0.01) {
    ControlDescriptor result;
    result.key = key;
    result.label = label;
    result.kind = kind;
    result.minimum = low;
    result.maximum = high;
    result.step = step;
    result.quick = true;
    return result;
}
NodeDescriptor internalNode(const QString& kind, const QString& title, int inputs, int outputs) {
    NodeDescriptor result;
    result.kind = kind;
    result.title = title;
    result.inputs = inputs;
    result.outputs = outputs;
    return result;
}
} // namespace

QString NodeDescriptor::catalogKey() const {
    return pluginId.isEmpty() ? kind : QStringLiteral("plugin:") + pluginId;
}
QString NodeDescriptor::icon() const {
    if (missing)
        return QStringLiteral("!");
    if (kind == QStringLiteral("capture"))
        return QStringLiteral("↗");
    if (kind == QStringLiteral("output"))
        return QStringLiteral("↙");
    if (kind == QStringLiteral("gain"))
        return QStringLiteral("±");
    if (kind == QStringLiteral("tone"))
        return QStringLiteral("∿");
    if (kind == QStringLiteral("mixer"))
        return QStringLiteral("Σ");
    if (kind == QStringLiteral("meter"))
        return QStringLiteral("▥");
    return QStringLiteral("◇");
}
bool NodeDescriptor::needsInspector() const {
    return missing || std::any_of(controls.cbegin(), controls.cend(), [](const auto& control) {
               return !control.quick || control.advanced;
           });
}

NodeCatalog::NodeCatalog() {
    auto capture =
        internalNode(QStringLiteral("capture"), QStringLiteral("Entrada do sistema"), 0, 1);
    capture.subtitle = QStringLiteral("Microfone · captura");
    capture.description = QStringLiteral(
        "A captura abre ao selecionar entrada e saída. Retire o dispositivo para liberá-la.");
    auto input = field(QStringLiteral("endpoint_id"), QStringLiteral("Microfone ativo"),
                       ControlKind::Device);
    input.flow = QStringLiteral("capture");
    capture.controls.append(input);
    auto output = internalNode(QStringLiteral("output"), QStringLiteral("Saída de áudio"), 1, 0);
    output.subtitle = QStringLiteral("Fones ou microfone virtual");
    output.description = QStringLiteral("Para enviar a outros apps, selecione CABLE Input. "
                                        "Para ouvir localmente, selecione seus fones. "
                                        "Uma saída por projeto.");
    output.accent = QColor(178, 150, 250);
    auto destination = field(QStringLiteral("endpoint_id"),
                             QStringLiteral("Destino · fones ou CABLE Input"), ControlKind::Device);
    destination.flow = QStringLiteral("render");
    output.controls.append(destination);
    auto gain = internalNode(QStringLiteral("gain"), QStringLiteral("Ganho"), 1, 1);
    gain.subtitle = QStringLiteral("Processador · volume");
    gain.description = QStringLiteral("0 dB = 100%; +6 dB ≈ 200%; −6 dB ≈ 50%. Ganhos em série "
                                      "multiplicam. Bypass mantém o fio e ignora este ganho.");
    gain.accent = QColor(231, 188, 105);
    auto volume = field(QStringLiteral("gain_db"), QStringLiteral("Ganho (dB / amplitude)"),
                        ControlKind::Slider, -60, 24, 0.1);
    volume.unit = QStringLiteral("dB");
    gain.controls = {volume, field(QStringLiteral("bypass"),
                                   QStringLiteral("Bypass · deixar passar sem efeito"),
                                   ControlKind::Toggle)};
    auto tone = internalNode(QStringLiteral("tone"), QStringLiteral("Tom de teste"), 0, 1);
    tone.subtitle = QStringLiteral("Gerador · 440 Hz");
    tone.description =
        QStringLiteral("Senoide em volume baixo para conferir a saída sem microfone.");
    tone.accent = QColor(107, 185, 255);
    tone.controls = {
        field(QStringLiteral("enabled"), QStringLiteral("Gerar tom"), ControlKind::Toggle)};
    auto mixer = internalNode(QStringLiteral("mixer"), QStringLiteral("Mixer"), 4, 1);
    mixer.subtitle = QStringLiteral("Somar até quatro sinais");
    mixer.description = QStringLiteral(
        "Entradas 1–4 somam sem reduzir o volume. Use Ganho antes "
        "de cada entrada para ajustar a contribuição. Uma saída pode alimentar vários destinos.");
    mixer.accent = QColor(223, 159, 238);
    auto meter = internalNode(QStringLiteral("meter"), QStringLiteral("Medidor"), 1, 0);
    meter.subtitle = QStringLiteral("Consumidor de áudio");
    meter.description = QStringLiteral("Recebe e mede o sinal sem enviá-lo adiante. Pode "
                                       "compartilhar a fonte com outros destinos. O relógio de "
                                       "áudio requer uma saída do sistema selecionada.");
    meter.accent = QColor(117, 200, 229);
    for (const auto& descriptor : {capture, output, gain, tone, mixer, meter})
        internal_.insert(descriptor.kind, descriptor);
}

void NodeCatalog::setPlugins(const QJsonArray& packages) {
    plugins_.clear();
    pluginOrder_.clear();
    for (const auto& value : packages) {
        const auto package = value.toObject();
        const auto appearance = package.value(QStringLiteral("appearance")).toObject();
        NodeDescriptor descriptor;
        descriptor.kind = QStringLiteral("plugin");
        descriptor.pluginId = package.value(QStringLiteral("id")).toString();
        descriptor.title = appearance.value(QStringLiteral("title")).toString();
        descriptor.description = appearance.value(QStringLiteral("description")).toString();
        descriptor.layout =
            appearance.value(QStringLiteral("layout")).toString(QStringLiteral("stack"));
        descriptor.accent = QColor(appearance.value(QStringLiteral("accent")).toString());
        if (!descriptor.accent.isValid())
            descriptor.accent = QColor(112, 217, 189);
        descriptor.inputs =
            static_cast<int>(package.value(QStringLiteral("inputs")).toArray().size());
        descriptor.outputs =
            static_cast<int>(package.value(QStringLiteral("outputs")).toArray().size());
        for (const auto& label : package.value(QStringLiteral("input_labels")).toArray())
            descriptor.inputLabels.append(label.toString());
        descriptor.filePlayer = package.value(QStringLiteral("file_player")).toBool();
        descriptor.fileTransport = package.value(QStringLiteral("file_transport")).toBool();
        descriptor.captureSource = package.value(QStringLiteral("capture_source")).toBool();
        descriptor.worker = package.value(QStringLiteral("worker")).toBool() ||
                            package.value(QStringLiteral("worker")).isObject();
        descriptor.subtitle = descriptor.worker ? QStringLiteral("Worker Python · +20 ms")
                                                : QStringLiteral("Plugin local · CLAP");
        const double latency = package.value(QStringLiteral("latency_frames")).toDouble() / 48.0;
        if (!descriptor.worker && latency > 0) {
            auto text = QString::number(latency, 'f', 2);
            while (text.endsWith(QLatin1Char('0')))
                text.chop(1);
            if (text.endsWith(QLatin1Char('.')))
                text.chop(1);
            descriptor.subtitle += QStringLiteral(" · +%1 ms").arg(text);
        }
        const auto parameters = package.value(QStringLiteral("parameters")).toArray();
        auto controls = appearance.value(QStringLiteral("controls")).toArray();
        for (const auto& parameterValue : parameters) {
            const auto parameter = parameterValue.toObject();
            const auto id = parameter.value(QStringLiteral("id"));
            const bool declared =
                std::any_of(controls.begin(), controls.end(), [&](const QJsonValue& control) {
                    return control.toObject().value(QStringLiteral("parameter")) == id;
                });
            if (!declared)
                controls.append(QJsonObject{
                    {QStringLiteral("parameter"), id},
                    {QStringLiteral("label"), QStringLiteral("Parâmetro %1").arg(id.toInteger())},
                    {QStringLiteral("widget"), QStringLiteral("number")}});
        }
        const bool declaredQuick =
            std::any_of(controls.cbegin(), controls.cend(), [](const auto& c) {
                return c.toObject().value(QStringLiteral("quick")).toBool();
            });
        int controlIndex = 0;
        for (const auto& controlValue : controls) {
            const auto control = controlValue.toObject();
            const auto parameterId = control.value(QStringLiteral("parameter"));
            const auto parameterIt =
                std::find_if(parameters.begin(), parameters.end(), [&](const QJsonValue& p) {
                    return p.toObject().value(QStringLiteral("id")) == parameterId;
                });
            if (parameterIt == parameters.end())
                continue;
            const auto parameter = parameterIt->toObject();
            const auto widget = control.value(QStringLiteral("widget")).toString();
            const auto kind = widget == QStringLiteral("slider")      ? ControlKind::Slider
                              : widget == QStringLiteral("toggle")    ? ControlKind::Toggle
                              : widget == QStringLiteral("select")    ? ControlKind::Select
                              : widget == QStringLiteral("momentary") ? ControlKind::Momentary
                                                                      : ControlKind::Number;
            auto item = field(QStringLiteral("parameter:%1").arg(parameterId.toInteger()),
                              control.value(QStringLiteral("label")).toString(), kind,
                              parameter.value(QStringLiteral("min")).toDouble(),
                              parameter.value(QStringLiteral("max")).toDouble());
            item.step = parameter.value(QStringLiteral("stepped")).toBool()
                            ? 1
                            : std::max((item.maximum - item.minimum) / 1000, 0.000001);
            item.readonly = parameter.value(QStringLiteral("readonly")).toBool();
            item.advanced = control.value(QStringLiteral("advanced")).toBool();
            item.quick = control.value(QStringLiteral("quick")).toBool() ||
                         (!declaredQuick && controlIndex == 0 && !item.advanced);
            ++controlIndex;
            item.unit = control.value(QStringLiteral("unit")).toString();
            item.page = control.value(QStringLiteral("page")).toString();
            item.group = control.value(QStringLiteral("group")).toString();
            item.help = control.value(QStringLiteral("help")).toString();
            item.options = control.value(QStringLiteral("options")).toArray();
            descriptor.controls.append(item);
        }
        auto bypass = field(QStringLiteral("bypass"),
                            descriptor.inputs == 0    ? QStringLiteral("Silenciar fonte")
                            : descriptor.outputs == 0 ? QStringLiteral("Suspender consumidor")
                            : descriptor.worker || latency > 0
                                ? QStringLiteral("Bypass · original com atraso alinhado")
                                : QStringLiteral("Bypass · manter o áudio original"),
                            ControlKind::Toggle);
        bypass.group = QStringLiteral("Rota");
        descriptor.controls.append(bypass);
        if (!plugins_.contains(descriptor.pluginId))
            pluginOrder_.append(descriptor.pluginId);
        plugins_.insert(descriptor.pluginId, descriptor);
    }
}

QVector<NodeDescriptor> NodeCatalog::entries() const {
    QVector<NodeDescriptor> result;
    for (const auto& kind :
         {QStringLiteral("capture"), QStringLiteral("output"), QStringLiteral("gain"),
          QStringLiteral("tone"), QStringLiteral("mixer"), QStringLiteral("meter")})
        result.append(internal_.value(kind));
    for (const auto& id : pluginOrder_)
        result.append(plugins_.value(id));
    return result;
}

NodeDescriptor NodeCatalog::describe(const QJsonObject& node) const {
    const auto block = node.value(QStringLiteral("block")).toObject();
    const auto kind = block.value(QStringLiteral("kind")).toString();
    if (kind != QStringLiteral("plugin"))
        return internal_.value(kind);
    const auto id = block.value(QStringLiteral("plugin_id")).toString();
    if (plugins_.contains(id))
        return plugins_.value(id);
    auto missing = internalNode(kind, QStringLiteral("Plugin indisponível"),
                                block.value(QStringLiteral("source")).toBool()      ? 0
                                : block.value(QStringLiteral("sidechain")).toBool() ? 2
                                                                                    : 1,
                                block.value(QStringLiteral("consumer")).toBool() ? 0 : 1);
    missing.pluginId = id;
    missing.subtitle = id;
    missing.missing = true;
    missing.description = QStringLiteral(
        "Pacote ausente ou desativado. Reinstale/ative em Gerenciar plugins e reabra o app. "
        "Parâmetros, arquivos e fios estão preservados; este caminho fica silencioso.");
    missing.accent = QColor(230, 170, 104);
    return missing;
}

QJsonValue fieldValue(const QJsonObject& block, const QString& key) {
    return key.startsWith(QStringLiteral("parameter:"))
               ? block.value(QStringLiteral("parameters")).toObject().value(key.mid(10))
               : block.value(key);
}
void setFieldValue(QJsonObject& block, const QString& key, const QJsonValue& value) {
    if (key.startsWith(QStringLiteral("parameter:"))) {
        auto parameters = block.value(QStringLiteral("parameters")).toObject();
        parameters.insert(key.mid(10), value);
        block.insert(QStringLiteral("parameters"), parameters);
    } else {
        block.insert(key, value);
    }
}
} // namespace nodivu
