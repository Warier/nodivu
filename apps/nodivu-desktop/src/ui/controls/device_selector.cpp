#include "ui/controls/device_selector.hpp"
#include <QJsonObject>
#include <QPainter>
#include <QSignalBlocker>
#include <algorithm>

namespace nodivu {
DeviceSelector::DeviceSelector(QWidget* parent) : QComboBox(parent) {
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(1);
    setCursor(Qt::ArrowCursor);
    setStyleSheet(QStringLiteral("QComboBox::drop-down { border: 0; width: 21px; }"
                                 "QComboBox::down-arrow { image: none; }"));
}
void DeviceSelector::paintEvent(QPaintEvent* event) {
    QComboBox::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(isEnabled() ? QColor(219, 227, 239) : QColor(117, 129, 147), 1.4,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const QPointF center(width() - 12, height() / 2.0);
    const QPointF points[]{center + QPointF(-3, -1.5), center + QPointF(0, 1.5),
                           center + QPointF(3, -1.5)};
    painter.drawPolyline(points, 3);
}
void DeviceSelector::synchronize(const QJsonArray& devices, const QString& flow,
                                 const QString& endpointId) {
    QList<QPair<QString, QString>> choices{
        {QStringLiteral("Nenhum · liberar dispositivo"), QString()}};
    for (const auto& value : devices) {
        const auto device = value.toObject();
        if (device.value(QStringLiteral("flow")).toString() == flow &&
            device.value(QStringLiteral("state")).toString() == QStringLiteral("active"))
            choices.append({device.value(QStringLiteral("name")).toString(),
                            device.value(QStringLiteral("endpoint_id")).toString()});
    }
    if (!endpointId.isEmpty() &&
        std::none_of(choices.cbegin(), choices.cend(),
                     [&](const auto& choice) { return choice.second == endpointId; }))
        choices.append({QStringLiteral("Selecionado indisponível"), endpointId});
    // Telemetry never rebuilds an unchanged popup; identity is always opaque.
    const QSignalBlocker blocker(this);
    bool same = count() == choices.size();
    for (int index = 0; same && index < count(); ++index)
        same = itemText(index) == choices[index].first &&
               itemData(index).toString() == choices[index].second;
    if (!same) {
        clear();
        for (const auto& choice : choices)
            addItem(choice.first, choice.second);
    }
    setCurrentIndex(std::max(0, findData(endpointId)));
    setToolTip(currentText());
}
} // namespace nodivu
