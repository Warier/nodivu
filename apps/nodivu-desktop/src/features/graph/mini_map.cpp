#include "features/graph/mini_map.hpp"
#include "features/graph/graph_scene.hpp"
#include "features/graph/graph_view.hpp"
#include "ui/theme/studio_icons.hpp"
#include "ui/theme/theme.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QToolButton>
#include <algorithm>

namespace nodivu {
MiniMap::MiniMap(GraphView& view, GraphScene& scene)
    : QWidget(view.parentWidget()), view_(view), scene_(scene) {
    setObjectName(QStringLiteral("graphMiniMap"));
    setAccessibleName(QStringLiteral("Minimapa do projeto"));
    setAccessibleDescription(QStringLiteral("Clique para navegar. Setas movem a câmera; Home "
                                            "enquadra todos os blocos; Enter enquadra a seleção."));
    setToolTip(accessibleDescription());
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(180, 124);
    auto* close = new QToolButton(this);
    close->setGeometry(150, 2, 26, 26);
    close->setIcon(studioIcon(StudioIcon::Close));
    close->setAccessibleName(QStringLiteral("Ocultar minimapa"));
    close->setToolTip(close->accessibleName());
    close->setStyleSheet(QStringLiteral("QToolButton { border: 0; background: transparent; } "
                                        "QToolButton:focus { border: 2px solid #70d9bd; }"));
    connect(close, &QToolButton::clicked, this, &MiniMap::closeRequested);
    view.viewport()->installEventFilter(this);
    view.installEventFilter(this);
    auto* refresh = new QTimer(this);
    refresh->setInterval(250);
    connect(refresh, &QTimer::timeout, this, [this] {
        if (isVisible())
            update();
    });
    refresh->start();
    reposition();
    hide();
}
void MiniMap::reposition() {
    // A viewport child is translated by QGraphicsView's scroll optimization.
    // Anchor this sibling in workspace coordinates instead.
    move(view_.viewport()->mapTo(
        parentWidget(), QPoint(12, std::max(12, view_.viewport()->height() - height() - 12))));
}
bool MiniMap::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Resize || event->type() == QEvent::Move ||
        event->type() == QEvent::Show)
        reposition();
    return QWidget::eventFilter(watched, event);
}
QTransform MiniMap::diagramTransform() const {
    QRectF bounds = view_.mapToScene(view_.viewport()->rect()).boundingRect();
    for (auto* item : scene_.items())
        if (dynamic_cast<NodeItem*>(item))
            bounds = bounds.united(item->sceneBoundingRect());
    bounds.adjust(-30, -30, 30, 30);
    const QRectF area(8, 30, width() - 16, height() - 38);
    const double scale = std::min(area.width() / bounds.width(), area.height() / bounds.height());
    QTransform transform;
    transform.translate(area.center().x(), area.center().y());
    transform.scale(scale, scale);
    transform.translate(-bounds.center().x(), -bounds.center().y());
    return transform;
}
void MiniMap::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(hasFocus() ? Theme::mint() : Theme::muted(), 1));
    p.setBrush(Theme::headerBackground());
    p.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 7, 7);
    auto font = p.font();
    font.setPixelSize(11);
    p.setFont(font);
    p.setPen(Theme::text());
    p.drawText(QRect(10, 3, 138, 22), Qt::AlignVCenter, QStringLiteral("Visão do projeto"));
    p.setClipRect(QRect(6, 29, width() - 12, height() - 35));
    const auto transform = diagramTransform();
    p.setPen(QPen(Theme::muted(), 1));
    for (auto* item : scene_.items()) {
        if (!item->data(3).isValid())
            continue;
        const auto edge = item->data(3).toJsonObject();
        auto* from = scene_.nodeItem(edge.value(QStringLiteral("from")).toString());
        auto* to = scene_.nodeItem(edge.value(QStringLiteral("to")).toString());
        if (from && to)
            p.drawLine(transform.map(from->sceneBoundingRect().center()),
                       transform.map(to->sceneBoundingRect().center()));
    }
    for (auto* item : scene_.items()) {
        if (!dynamic_cast<NodeItem*>(item))
            continue;
        p.setPen(Qt::NoPen);
        p.setBrush(item->isSelected() ? Theme::mint() : Theme::muted());
        p.drawRoundedRect(transform.mapRect(item->sceneBoundingRect()), 1.5, 1.5);
    }
    p.setPen(QPen(Theme::warningText(), 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(transform.mapRect(view_.mapToScene(view_.viewport()->rect()).boundingRect()));
}
void MiniMap::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && event->position().y() >= 29) {
        setFocus(Qt::MouseFocusReason);
        view_.focusPosition(diagramTransform().inverted().map(event->position()));
        update();
        event->accept();
    } else
        QWidget::mousePressEvent(event);
}
void MiniMap::keyPressEvent(QKeyEvent* event) {
    const auto visible = view_.mapToScene(view_.viewport()->rect()).boundingRect();
    QPointF delta;
    if (event->key() == Qt::Key_Left)
        delta.setX(-visible.width() / 4);
    if (event->key() == Qt::Key_Right)
        delta.setX(visible.width() / 4);
    if (event->key() == Qt::Key_Up)
        delta.setY(-visible.height() / 4);
    if (event->key() == Qt::Key_Down)
        delta.setY(visible.height() / 4);
    if (!delta.isNull())
        view_.focusPosition(visible.center() + delta);
    else if (event->key() == Qt::Key_Home)
        view_.fitNodes();
    else if (event->key() == Qt::Key_Return)
        view_.fitSelection();
    else {
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
    update();
}
} // namespace nodivu
