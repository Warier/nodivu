#include "ui/controls/inline_check_box.hpp"
#include <QPainter>
#include <QStyleOptionButton>

namespace nodivu {
InlineCheckBox::InlineCheckBox(const QString& text, QWidget* parent) : QCheckBox(text, parent) {
    setStyleSheet(QStringLiteral("QCheckBox { color: #97a6bb; font-size: 9px; spacing: 6px; }"));
    setCursor(Qt::ArrowCursor);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}
void InlineCheckBox::paintEvent(QPaintEvent* event) {
    QCheckBox::paintEvent(event);
    QStyleOptionButton option;
    initStyleOption(&option);
    const auto rect =
        style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, this).adjusted(1, 1, -1, -1);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(isEnabled() ? 1.0 : 0.45);
    painter.setPen(QPen(isChecked() ? QColor(112, 217, 189) : QColor(132, 147, 169), 1));
    painter.setBrush(isChecked() ? QColor(112, 217, 189) : QColor(41, 45, 53));
    painter.drawRoundedRect(rect, 2, 2);
    if (isChecked()) {
        painter.setPen(QPen(QColor(19, 36, 30), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF points[]{
            QPointF(rect.left() + rect.width() * .22, rect.top() + rect.height() * .52),
            QPointF(rect.left() + rect.width() * .43, rect.top() + rect.height() * .73),
            QPointF(rect.left() + rect.width() * .8, rect.top() + rect.height() * .25)};
        painter.drawPolyline(points, 3);
    }
}
} // namespace nodivu
