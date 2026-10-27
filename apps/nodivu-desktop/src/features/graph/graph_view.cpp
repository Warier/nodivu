#include "features/graph/graph_view.hpp"
#include <QAbstractButton>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QGraphicsItem>
#include <QGraphicsProxyWidget>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextEdit>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>

namespace nodivu {
namespace {
bool editableWidget(const QWidget* widget) {
    for (auto* current = widget; current; current = current->parentWidget())
        if (qobject_cast<const QAbstractButton*>(current) ||
            qobject_cast<const QAbstractSlider*>(current) ||
            qobject_cast<const QAbstractSpinBox*>(current) ||
            qobject_cast<const QComboBox*>(current) || qobject_cast<const QLineEdit*>(current) ||
            qobject_cast<const QTextEdit*>(current))
            return true;
    return false;
}
} // namespace
GraphView::GraphView(QGraphicsScene* scene, QWidget* parent) : QGraphicsView(scene, parent) {
    setFrameShape(QFrame::NoFrame);
    setRenderHint(QPainter::Antialiasing);
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAcceptDrops(true);
    setAccessibleName(QStringLiteral("Editor do grafo de áudio"));
    setMouseTracking(true);
    qApp->installEventFilter(this);
}
void GraphView::drawBackground(QPainter* painter, const QRectF& rect) {
    painter->fillRect(rect, QColor(16, 22, 29));
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QPen(QColor(52, 64, 80), 0));
    qreal step = transform().m11() < 0.5 ? 44 : 22;
    // Keep the grid legible when zoomed out, with bounded batches rather than
    // thousands of individual painter calls for a large visible scene.
    while (step * transform().m11() < 12)
        step *= 2;
    const qreal left = std::floor(rect.left() / step) * step;
    const qreal top = std::floor(rect.top() / step) * step;
    std::array<QPointF, 1024> points;
    int count = 0;
    for (qreal x = left; x < rect.right(); x += step)
        for (qreal y = top; y < rect.bottom(); y += step) {
            points[count++] = QPointF(x, y);
            if (count == static_cast<int>(points.size())) {
                painter->drawPoints(points.data(), count);
                count = 0;
            }
        }
    if (count)
        painter->drawPoints(points.data(), count);
    painter->restore();
}
void GraphView::setOverlayWidget(QWidget* overlay) {
    overlays_.clear();
    addOverlayWidget(overlay);
}
void GraphView::addOverlayWidget(QWidget* overlay) {
    overlays_.append(overlay);
}
QRect GraphView::availableViewportRect() const {
    QRect available = viewport()->rect().adjusted(16, 16, -16, -16);
    for (const auto& overlay : overlays_) {
        if (!overlay || !overlay->isVisible())
            continue;
        const QRect obstacle(viewport()->mapFromGlobal(overlay->mapToGlobal(QPoint())),
                             overlay->size());
        if (!available.intersects(obstacle))
            continue;
        const auto clipped = available.intersected(obstacle.adjusted(-12, -12, 12, 12));
        const QList<QRect> candidates{
            QRect(available.topLeft(), QPoint(clipped.left() - 1, available.bottom())),
            QRect(QPoint(clipped.right() + 1, available.top()), available.bottomRight()),
            QRect(available.topLeft(), QPoint(available.right(), clipped.top() - 1)),
            QRect(QPoint(available.left(), clipped.bottom() + 1), available.bottomRight())};
        QRect largest;
        for (const auto& candidate : candidates)
            if (candidate.isValid() &&
                candidate.width() * candidate.height() > largest.width() * largest.height())
                largest = candidate;
        if (largest.width() > 80 && largest.height() > 80)
            available = largest;
    }
    return available;
}
void GraphView::revealNode(QGraphicsItem* item, bool placeInView) {
    if (!item || item->scene() != scene())
        return;
    const auto previousCamera = camera();
    const auto available = availableViewportRect();
    const auto bounds = item->sceneBoundingRect();
    if (bounds.isEmpty() || !available.isValid())
        return;
    const qreal zoom = std::clamp(std::min({transform().m11(), available.width() / bounds.width(),
                                            available.height() / bounds.height()}),
                                  .1, 2.0);
    if (zoom < transform().m11()) {
        const auto anchor = transformationAnchor();
        setTransformationAnchor(QGraphicsView::NoAnchor);
        scale(zoom / transform().m11(), zoom / transform().m11());
        setTransformationAnchor(anchor);
        emit zoomChanged(zoom);
    }
    if (placeInView) {
        const QSizeF size(bounds.width() * zoom, bounds.height() * zoom);
        QList<QRectF> candidates;
        // Bounded search around the visible center; keep existing nodes in place.
        for (int row = -4; row <= 4; ++row)
            for (int column = -4; column <= 4; ++column) {
                const QPointF center =
                    QPointF(available.center()) +
                    QPointF(column * (size.width() + 24), row * (size.height() + 24));
                const QRectF candidate(center - QPointF(size.width() / 2, size.height() / 2), size);
                if (QRectF(available).contains(candidate))
                    candidates.append(candidate);
            }
        std::stable_sort(candidates.begin(), candidates.end(),
                         [&](const QRectF& a, const QRectF& b) {
                             const auto x = a.center() - QPointF(available.center());
                             const auto y = b.center() - QPointF(available.center());
                             return QPointF::dotProduct(x, x) < QPointF::dotProduct(y, y);
                         });
        QRectF existing;
        QList<QRectF> obstacles;
        for (auto* other : scene()->items())
            if (other != item && !other->parentItem() &&
                other->flags().testFlag(QGraphicsItem::ItemIsMovable)) {
                existing = existing.united(other->sceneBoundingRect());
                obstacles.append(QRectF(mapFromScene(other->sceneBoundingRect()).boundingRect())
                                     .adjusted(-12, -12, 12, 12));
            }
        QPointF target;
        bool found = false;
        for (const auto& candidate : candidates) {
            const bool occupied =
                std::any_of(obstacles.cbegin(), obstacles.cend(),
                            [&](const QRectF& obstacle) { return obstacle.intersects(candidate); });
            if (!occupied) {
                target = mapToScene(candidate.topLeft().toPoint());
                found = true;
                break;
            }
        }
        // If the viewport is full, append beside the graph and pan only to the new node.
        if (!found)
            target = QPointF(existing.right() + 40,
                             mapToScene(available.center()).y() - bounds.height() / 2);
        item->setPos(target - (item->sceneBoundingRect().topLeft() - item->pos()));
    }
    const auto actual = item->sceneBoundingRect();
    if (!available.contains(mapFromScene(actual).boundingRect())) {
        scene()->setSceneRect(scene()->sceneRect().united(
            actual.adjusted(-viewport()->width() / zoom, -viewport()->height() / zoom,
                            viewport()->width() / zoom, viewport()->height() / zoom)));
        centerOn(actual.center());
        const auto delta = mapFromScene(actual.center()) - available.center();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() + delta.y());
    }
    if (camera() != previousCamera)
        emit cameraEdited();
}
void GraphView::fitNodes() {
    QRectF bounds;
    for (auto* item : scene()->items())
        if (item->parentItem() == nullptr && item->flags().testFlag(QGraphicsItem::ItemIsMovable))
            bounds = bounds.united(item->sceneBoundingRect());
    if (!bounds.isEmpty()) {
        bounds.adjust(-40, -40, 40, 40);
        const auto available = availableViewportRect();
        const qreal zoom = std::clamp(
            std::min(available.width() / bounds.width(), available.height() / bounds.height()), 0.1,
            1.5);
        resetTransform();
        scale(zoom, zoom);
        // Permit centering near the original scene boundary without changing
        // document positions or pushing nodes underneath the floating inspector.
        scene()->setSceneRect(scene()->sceneRect().united(
            bounds.adjusted(-viewport()->width() / zoom, -viewport()->height() / zoom,
                            viewport()->width() / zoom, viewport()->height() / zoom)));
        centerOn(bounds.center());
        const auto delta = mapFromScene(bounds.center()) - available.center();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() + delta.y());
        emit zoomChanged(transform().m11());
        emit cameraEdited();
    }
}
void GraphView::zoomBy(double factor) {
    if (!zoomEnabled_)
        return;
    const double target = std::clamp(transform().m11() * factor, 0.1, 2.0);
    scale(target / transform().m11(), target / transform().m11());
    emit zoomChanged(target);
    emit cameraEdited();
}
void GraphView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        zoomBy(std::pow(1.0015, event->angleDelta().y()));
        event->accept();
    } else if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        const auto delta =
            event->pixelDelta().isNull() ? event->angleDelta() / 2 : event->pixelDelta();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() -
                                        (delta.x() ? delta.x() : delta.y()));
        emit cameraEdited();
        event->accept();
    } else {
        QGraphicsView::wheelEvent(event);
        emit cameraEdited();
    }
}
void GraphView::mousePressEvent(QMouseEvent* event) {
    auto* item = itemAt(event->pos());
    bool control = false;
    for (auto* current = item; current; current = current->parentItem()) {
        control |= current->data(1).toString() == QStringLiteral("source") ||
                   current->data(1).toString() == QStringLiteral("sink");
        if (auto* proxy = dynamic_cast<QGraphicsProxyWidget*>(current))
            control |= editableWidget(
                proxy->widget()->childAt(proxy->mapFromScene(mapToScene(event->pos())).toPoint()));
    }
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && (!item || (spacePan_ && !control)))) {
        setFocus(Qt::MouseFocusReason);
        panning_ = true;
        previous_ = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else {
        QGraphicsView::mousePressEvent(event);
    }
}
void GraphView::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        const auto delta = event->pos() - previous_;
        previous_ = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        emit cameraEdited();
        event->accept();
    } else {
        QGraphicsView::mouseMoveEvent(event);
    }
}
void GraphView::mouseReleaseEvent(QMouseEvent* event) {
    if (panning_) {
        panning_ = false;
        viewport()->setCursor(spacePan_ ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
    } else {
        QGraphicsView::mouseReleaseEvent(event);
    }
}
void GraphView::cancelPan() {
    panning_ = false;
    spacePan_ = false;
    viewport()->unsetCursor();
}
bool GraphView::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ApplicationDeactivate ||
        event->type() == QEvent::WindowDeactivate ||
        ((watched == this || watched == viewport()) && event->type() == QEvent::FocusOut))
        cancelPan();
    return QGraphicsView::eventFilter(watched, event);
}
void GraphView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && (spacePan_ || panning_)) {
        cancelPan();
        event->accept();
        return;
    }
    QWidget* focused = QApplication::focusWidget();
    if (auto* proxy = dynamic_cast<QGraphicsProxyWidget*>(scene()->focusItem()))
        focused = proxy->widget()->focusWidget();
    if (event->key() == Qt::Key_Space && !editableWidget(focused)) {
        spacePan_ = true;
        if (!panning_)
            viewport()->setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}
void GraphView::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat() && spacePan_) {
        spacePan_ = false;
        if (!panning_)
            viewport()->unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}
QJsonObject GraphView::camera() const {
    const auto origin = mapFromScene(QPointF(0, 0));
    return {{QStringLiteral("x"), origin.x()},
            {QStringLiteral("y"), origin.y()},
            {QStringLiteral("scale"), transform().m11()}};
}
void GraphView::restoreCamera(const QJsonObject& camera) {
    const double zoom = std::clamp(camera.value(QStringLiteral("scale")).toDouble(1), 0.1, 2.0);
    const double x = camera.value(QStringLiteral("x")).toDouble();
    const double y = camera.value(QStringLiteral("y")).toDouble();
    // Electron stores the pixel translation, including positive offsets. Qt
    // scrollbars need scene space on that side to represent the same camera.
    const QRectF visible(-x / zoom, -y / zoom, viewport()->width() / zoom,
                         viewport()->height() / zoom);
    scene()->setSceneRect(scene()->sceneRect().united(visible.adjusted(-16, -16, 16, 16)));
    resetTransform();
    scale(zoom, zoom);
    centerOn((viewport()->width() / 2.0 - x) / zoom, (viewport()->height() / 2.0 - y) / zoom);
    emit zoomChanged(zoom);
}
void GraphView::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-nodivu-block")))
        event->acceptProposedAction();
}
void GraphView::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-nodivu-block")))
        event->acceptProposedAction();
}
void GraphView::dropEvent(QDropEvent* event) {
    const auto block = QJsonDocument::fromJson(
                           event->mimeData()->data(QStringLiteral("application/x-nodivu-block")))
                           .object();
    emit blockDropped(block.value(QStringLiteral("kind")).toString(),
                      block.value(QStringLiteral("plugin_id")).toString(),
                      mapToScene(event->position().toPoint()));
    event->acceptProposedAction();
}
} // namespace nodivu
