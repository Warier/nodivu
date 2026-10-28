#include "ui/theme/studio_icons.hpp"
#include "contracts/node_descriptor.hpp"
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
namespace nodivu {
StudioIcon blockIcon(const NodeDescriptor& d) {
    if (d.missing)
        return StudioIcon::Warning;
    if (d.filePlayer)
        return StudioIcon::File;
    if (d.captureSource)
        return StudioIcon::Apps;
    if (d.kind == QStringLiteral("capture"))
        return StudioIcon::Microphone;
    if (d.kind == QStringLiteral("output"))
        return StudioIcon::Output;
    if (d.kind == QStringLiteral("gain"))
        return StudioIcon::Gain;
    if (d.kind == QStringLiteral("mixer"))
        return StudioIcon::Mixer;
    if (d.kind == QStringLiteral("meter"))
        return StudioIcon::Meter;
    if (d.kind == QStringLiteral("tone"))
        return StudioIcon::Wave;
    return StudioIcon::Plugin;
}
void paintStudioIcon(QPainter& p, StudioIcon icon, const QRectF& rect, const QColor& color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const qreal side = qMin(rect.width(), rect.height());
    p.translate(rect.center() - QPointF(side / 2, side / 2));
    p.scale(side / 24, side / 24);
    p.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const auto line = [&p](qreal x, qreal y, qreal a, qreal b) {
        p.drawLine(QPointF(x, y), QPointF(a, b));
    };
    switch (icon) {
    case StudioIcon::Microphone:
        p.drawRoundedRect(QRectF(9, 3, 6, 11), 3, 3);
        p.drawArc(QRectF(6, 6, 12, 12), 180 * 16, 180 * 16);
        line(6, 10, 6, 12);
        line(18, 10, 18, 12);
        line(12, 18, 12, 21);
        line(8, 21, 16, 21);
        break;
    case StudioIcon::Output: {
        QPainterPath path;
        path.moveTo(4, 9);
        path.lineTo(8, 9);
        path.lineTo(13, 5);
        path.lineTo(13, 19);
        path.lineTo(8, 15);
        path.lineTo(4, 15);
        path.closeSubpath();
        p.drawPath(path);
        p.drawArc(QRectF(10, 7, 10, 10), -65 * 16, 130 * 16);
        p.drawArc(QRectF(9, 3, 14, 18), -50 * 16, 100 * 16);
        break;
    }
    case StudioIcon::File: {
        QPainterPath path;
        path.moveTo(5, 3);
        path.lineTo(14, 3);
        path.lineTo(19, 8);
        path.lineTo(19, 21);
        path.lineTo(5, 21);
        path.closeSubpath();
        p.drawPath(path);
        line(14, 3, 14, 8);
        line(14, 8, 19, 8);
        QPainterPath play;
        play.moveTo(8, 11);
        play.lineTo(14, 15);
        play.lineTo(8, 18);
        play.closeSubpath();
        p.drawPath(play);
        break;
    }
    case StudioIcon::Apps:
        p.drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
        line(3, 9, 21, 9);
        p.drawPoint(QPointF(6, 6.5));
        p.drawPoint(QPointF(9, 6.5));
        line(8, 15, 10, 13);
        line(10, 13, 13, 17);
        line(13, 17, 16, 12);
        break;
    case StudioIcon::Gain:
        for (int x : {6, 12, 18}) {
            line(x, 4, x, 20);
            p.drawEllipse(QPointF(x, x == 12 ? 15 : 9), 2, 2);
        }
        break;
    case StudioIcon::Mixer:
        line(3, 5, 7, 5);
        line(7, 5, 12, 12);
        line(3, 12, 12, 12);
        line(3, 19, 7, 19);
        line(7, 19, 12, 12);
        line(12, 12, 21, 12);
        break;
    case StudioIcon::Meter:
        p.drawRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
        line(7, 17, 7, 12);
        line(12, 17, 12, 7);
        line(17, 17, 17, 10);
        break;
    case StudioIcon::Plugin:
        p.drawRoundedRect(QRectF(6, 7, 12, 12), 3, 3);
        line(9, 3, 9, 7);
        line(15, 3, 15, 7);
        line(12, 19, 12, 22);
        break;
    case StudioIcon::Wave: {
        QPainterPath path;
        path.moveTo(2, 12);
        path.cubicTo(5, 0, 9, 0, 12, 12);
        path.cubicTo(15, 24, 19, 24, 22, 12);
        p.drawPath(path);
        break;
    }
    case StudioIcon::More:
        for (int x : {5, 12, 19}) {
            p.setBrush(color);
            p.drawEllipse(QPointF(x, 12), 1.2, 1.2);
        }
        break;
    case StudioIcon::Close:
        line(6, 6, 18, 18);
        line(18, 6, 6, 18);
        break;
    case StudioIcon::Menu:
        for (int y : {6, 12, 18})
            line(4, y, 20, y);
        break;
    case StudioIcon::Plus:
        line(5, 12, 19, 12);
        line(12, 5, 12, 19);
        break;
    case StudioIcon::Minus:
        line(5, 12, 19, 12);
        break;
    case StudioIcon::Target:
        p.drawRoundedRect(QRectF(5, 5, 14, 14), 2, 2);
        line(12, 2, 12, 8);
        line(12, 16, 12, 22);
        line(2, 12, 8, 12);
        line(16, 12, 22, 12);
        break;
    case StudioIcon::Grip:
        for (int x : {8, 16})
            for (int y : {5, 12, 19}) {
                p.setBrush(color);
                p.drawEllipse(QPointF(x, y), 1, 1);
            }
        break;
    case StudioIcon::Play: {
        QPainterPath path;
        path.moveTo(7, 4);
        path.lineTo(20, 12);
        path.lineTo(7, 20);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case StudioIcon::Pause:
        line(8, 5, 8, 19);
        line(16, 5, 16, 19);
        break;
    case StudioIcon::Search:
        p.drawEllipse(QRectF(4, 3, 12, 12));
        line(14, 14, 21, 21);
        break;
    case StudioIcon::Check:
        line(4, 12, 9, 17);
        line(9, 17, 20, 6);
        break;
    case StudioIcon::Undo:
    case StudioIcon::Redo: {
        if (icon == StudioIcon::Redo) {
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        line(8, 5, 3, 10);
        line(3, 10, 8, 15);
        QPainterPath path;
        path.moveTo(3, 10);
        path.lineTo(14, 10);
        path.cubicTo(22, 10, 22, 20, 14, 20);
        p.drawPath(path);
        break;
    }
    case StudioIcon::Folder:
        p.drawRoundedRect(QRectF(3, 7, 18, 13), 2, 2);
        line(3, 7, 3, 4);
        line(3, 4, 10, 4);
        line(10, 4, 13, 7);
        break;
    case StudioIcon::Star: {
        const QPolygonF star{QPointF(12, 3),  QPointF(15, 9),  QPointF(22, 10), QPointF(17, 15),
                             QPointF(18, 22), QPointF(12, 19), QPointF(6, 22),  QPointF(7, 15),
                             QPointF(2, 10),  QPointF(9, 9)};
        p.drawPolygon(star);
        break;
    }
    case StudioIcon::ChevronRight:
        line(9, 6, 15, 12);
        line(15, 12, 9, 18);
        break;
    case StudioIcon::Warning: {
        QPainterPath path;
        path.moveTo(12, 3);
        path.lineTo(22, 21);
        path.lineTo(2, 21);
        path.closeSubpath();
        p.drawPath(path);
        line(12, 9, 12, 14);
        p.drawPoint(QPointF(12, 18));
        break;
    }
    }
    p.restore();
}
namespace {
class StudioIconEngine final : public QIconEngine {
  public:
    StudioIconEngine(StudioIcon icon, QColor color) : icon_(icon), color_(color) {}
    QIconEngine* clone() const override {
        return new StudioIconEngine(icon_, color_);
    }
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        auto color = color_;
        if (mode == QIcon::Disabled)
            color.setAlpha(110);
        paintStudioIcon(*painter, icon_, rect, color);
    }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }

  private:
    StudioIcon icon_;
    QColor color_;
};
} // namespace
QIcon studioIcon(StudioIcon icon, const QColor& color) {
    return QIcon(new StudioIconEngine(icon, color));
}
} // namespace nodivu
