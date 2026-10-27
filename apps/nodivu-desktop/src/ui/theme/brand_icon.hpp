#pragma once
#include <QImage>
#include <QPainter>
#include <QPainterPath>

namespace nodivu {
// Vector geometry shared by the window icon and the build-time Windows ICO.
inline QImage brandImage(int size) {
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size / 64.0, size / 64.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(29, 48, 48));
    painter.drawRoundedRect(QRectF(1, 1, 62, 62), 14, 14);
    QPainterPath letter;
    letter.moveTo(18, 47);
    letter.lineTo(18, 20);
    letter.lineTo(25, 20);
    letter.lineTo(25, 24);
    letter.cubicTo(30, 17, 47, 17, 47, 32);
    letter.lineTo(47, 47);
    letter.lineTo(39, 47);
    letter.lineTo(39, 32);
    letter.cubicTo(39, 23, 26, 24, 26, 33);
    letter.lineTo(26, 47);
    letter.closeSubpath();
    painter.setBrush(QColor(112, 217, 189));
    painter.drawPath(letter);
    return image;
}
} // namespace nodivu
