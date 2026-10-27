#include "features/graph/graph_scene.hpp"
#include "contracts/virtual_cable.hpp"
#include "features/graph/capture_source_widget.hpp"
#include "features/graph/node_content.hpp"
#include "ui/theme/theme.hpp"

#include <QFocusEvent>
#include <QGraphicsProxyWidget>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>
#include <algorithm>
#include <cmath>

namespace nodivu {
namespace {
QRectF headerActionRect(int action) {
    return QRectF(action == 1 ? 173 : 195, 9, 18, 24);
}
QPainterPath wirePath(const QPointF& from, const QPointF& to) {
    const qreal bend = std::max(70.0, std::abs(to.x() - from.x()) * 0.45);
    QPainterPath path(from);
    path.cubicTo(from + QPointF(bend, 0), to - QPointF(bend, 0), to);
    return path;
}
QString edgeKey(const QJsonObject& edge) {
    return edge.value(QStringLiteral("from")).toString() + QLatin1Char(':') +
           QString::number(edge.value(QStringLiteral("from_port")).toInt()) + QLatin1Char(':') +
           edge.value(QStringLiteral("to")).toString() + QLatin1Char(':') +
           QString::number(edge.value(QStringLiteral("to_port")).toInt());
}
class ConnectionItem final : public QGraphicsPathItem {
  public:
    ConnectionItem() {
        setFlags(ItemIsSelectable | ItemIsFocusable);
        setZValue(-1);
        setPen(QPen(QColor(112, 217, 189, 170), 2.3));
    }
    void setEndpoints(const QPointF& from, const QPointF& to) {
        if (initialized_ && from == from_ && to == to_)
            return;
        const auto next = wirePath(from, to);
        prepareGeometryChange();
        from_ = from;
        to_ = to;
        initialized_ = true;
        bounds_ = next.controlPointRect().adjusted(-9, -9, 9, 9);
        shapeDirty_ = true;
        setPath(next);
    }
    QRectF boundingRect() const override {
        return bounds_;
    }
    QPainterPath shape() const override {
        if (shapeDirty_) {
            QPainterPathStroker stroker;
            stroker.setWidth(18);
            shape_ = stroker.createStroke(path());
            shapeDirty_ = false;
        }
        return shape_;
    }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
        painter->setPen(QPen(isSelected() ? Theme::mint() : QColor(112, 217, 189, 170),
                             isSelected() ? 3.2 : 2.3));
        painter->drawPath(path());
    }

  private:
    QPointF from_, to_;
    QRectF bounds_;
    mutable QPainterPath shape_;
    mutable bool shapeDirty_ = true;
    bool initialized_ = false;
};
} // namespace

NodeItem::NodeItem(QString id, NodeDescriptor descriptor, SessionController& session)
    : id_(std::move(id)), descriptor_(std::move(descriptor)) {
    setFlags(ItemIsMovable | ItemIsSelectable | ItemIsFocusable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setCursor(Qt::ArrowCursor);
    // Reuse native-resolution painting during translations. Qt invalidates the
    // cache on zoom and update(), preserving readable text and live meters.
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    setToolTip(descriptor_.title + QLatin1Char('\n') + descriptor_.description);
    setProperty("accessibleName", descriptor_.title);
    content_ = new NodeContent(id_, descriptor_);
    auto* proxy = new QGraphicsProxyWidget(this);
    proxy->setWidget(content_);
    const bool mixer = descriptor_.kind == QStringLiteral("mixer");
    proxy->setPos(mixer ? 25 : 12, mixer ? 80 : 56);
    content_->setFixedWidth(mixer ? 168 : Theme::NodeWidth - 24);
    const auto resizeBody = [this, proxy] {
        const int ports = std::max(descriptor_.inputs, descriptor_.outputs);
        const qreal height = std::max({descriptor_.kind == QStringLiteral("mixer") ? 175.0 : 112.0,
                                       90.0 + std::max(0, ports - 1) * 26.0,
                                       proxy->pos().y() + content_->height() + 11});
        if (height != bodyHeight_) {
            prepareGeometryChange();
            bodyHeight_ = height;
            update();
        }
    };
    connect(content_, &NodeContent::geometryChanged, this, resizeBody);
    if (descriptor_.captureSource) {
        auto* capture = new CaptureSourceWidget(session, id_, true, content_);
        content_->addCaptureWidget(capture);
        connect(capture, &CaptureSourceWidget::geometryChanged, content_,
                &NodeContent::resizeToContents);
    }
    resizeBody();
    connect(content_, &NodeContent::fieldEdited, this,
            [this](const QString& field, const QJsonValue& value) {
                emit fieldEdited(id_, field, value);
            });
    connect(content_, &NodeContent::actionRequested, this,
            [this](const QString& action) { emit actionRequested(id_, action, {}); });
    connect(content_, &NodeContent::interactionStarted, this, [this] {
        if (scene())
            scene()->clearSelection();
        setSelected(true);
    });
    connect(content_, &NodeContent::gestureRequested, this,
            [this](quint32 parameter, bool pressed) {
                if (!pressed) {
                    releaseGesture();
                    return;
                }
                releaseGesture();
                gestureParameter_ = parameter;
                holdingGesture_ = true;
                emit gestureRequested(id_, parameter, true);
                gestureTimer_.start();
            });
    gestureTimer_.setInterval(100);
    connect(&gestureTimer_, &QTimer::timeout, this,
            [this] { emit gestureRequested(id_, gestureParameter_, true); });
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state != Qt::ApplicationActive)
                    releaseGesture();
            });
    for (const bool output : {false, true}) {
        const int count = output ? descriptor_.outputs : descriptor_.inputs;
        for (int index = 0; index < count; ++index) {
            const qreal diameter = count > 1 ? 17 : 14;
            auto* port = new QGraphicsEllipseItem(
                QRectF(-diameter / 2, -diameter / 2, diameter, diameter), this);
            port->setPos(portPosition(output, index));
            const QColor color = output ? Theme::mint() : QColor(178, 150, 250);
            port->setPen(QPen(color, 2));
            port->setBrush(Theme::background());
            port->setData(0, id_);
            port->setData(1, output ? QStringLiteral("source") : QStringLiteral("sink"));
            port->setData(2, index);
            port->setToolTip(output ? QStringLiteral("Saída %1").arg(index + 1)
                                    : descriptor_.inputLabels.value(
                                          index, QStringLiteral("Entrada %1").arg(index + 1)));
            port->setCursor(Qt::CrossCursor);
            {
                port->setPen(QPen(Theme::panel(), count > 1 ? 2 : 3));
                port->setBrush(color);
                const qreal outer = count > 1 ? 20 : 18;
                auto* ring =
                    new QGraphicsEllipseItem(QRectF(-outer / 2, -outer / 2, outer, outer), port);
                ring->setPen(QPen(color, 1.5));
                ring->setBrush(Qt::NoBrush);
                ring->setData(0, id_);
                ring->setData(1, output ? QStringLiteral("source") : QStringLiteral("sink"));
                ring->setData(2, index);
                ring->setCursor(Qt::CrossCursor);
                if (count > 1) {
                    auto* label = new QGraphicsSimpleTextItem(QString::number(index + 1), port);
                    auto font = label->font();
                    font.setPixelSize(9);
                    label->setFont(font);
                    label->setBrush(QColor(16, 19, 25));
                    label->setPos(-label->boundingRect().width() / 2,
                                  -label->boundingRect().height() / 2);
                }
            }
        }
    }
}
NodeItem::~NodeItem() {
    releaseGesture();
    disconnect(content_, nullptr, this, nullptr);
}
void NodeItem::releaseGesture() {
    if (!holdingGesture_)
        return;
    holdingGesture_ = false;
    gestureTimer_.stop();
    emit gestureRequested(id_, gestureParameter_, false);
}
void NodeItem::focusOutEvent(QFocusEvent* event) {
    releaseGesture();
    QGraphicsObject::focusOutEvent(event);
}
void NodeItem::updateHeaderHover(const QPointF& position) {
    const int action = headerActionRect(1).contains(position)   ? 1
                       : headerActionRect(2).contains(position) ? 2
                                                                : 0;
    if (hoveredHeaderAction_ == action)
        return;
    hoveredHeaderAction_ = action;
    update(QRectF(172, 8, 42, 26));
}
void NodeItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    updateHeaderHover(event->pos());
    QGraphicsObject::hoverEnterEvent(event);
}
void NodeItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event) {
    updateHeaderHover(event->pos());
    QGraphicsObject::hoverMoveEvent(event);
}
void NodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    updateHeaderHover(QPointF(-1, -1));
    QGraphicsObject::hoverLeaveEvent(event);
}
void NodeItem::paintHeaderActions(QPainter* painter) const {
    painter->save();
    if (hoveredHeaderAction_ != 0) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(53, 65, 83));
        painter->drawRoundedRect(headerActionRect(hoveredHeaderAction_), 4, 4);
    }
    auto font = painter->font();
    font.setPixelSize(17);
    painter->setFont(font);
    painter->setPen(hoveredHeaderAction_ == 1 ? Theme::mint() : QColor(162, 179, 199));
    painter->drawText(headerActionRect(1), Qt::AlignCenter, QStringLiteral("⋯"));
    painter->setPen(QColor(162, 179, 199));
    painter->drawText(headerActionRect(2), Qt::AlignCenter, QStringLiteral("×"));
    painter->restore();
}
QRectF NodeItem::boundingRect() const {
    return QRectF(-11, -6, Theme::NodeWidth + 22, bodyHeight_ + 12);
}
QPointF NodeItem::portPosition(bool output, int index) const {
    const int count = output ? descriptor_.outputs : descriptor_.inputs;
    return {output ? qreal(Theme::NodeWidth) : 0.0, (count > 1 ? 64.0 : 63.0) + index * 26.0};
}
void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    const QRectF body(0, 0, Theme::NodeWidth, bodyHeight_);
    painter->setRenderHint(QPainter::Antialiasing);
    if (isSelected()) {
        painter->setPen(QPen(QColor(131, 221, 197), 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(body.adjusted(-4, -4, 4, 4), 12, 12);
    }
    painter->setPen(QPen(QColor(61, 75, 90), 1));
    painter->setBrush(Theme::panel());
    painter->drawRoundedRect(body, 9, 9);
    QPainterPath accent(QPointF(0.5, 9));
    accent.quadTo(0.5, 0.5, 9, 0.5);
    accent.lineTo(Theme::NodeWidth - 9, 0.5);
    accent.quadTo(Theme::NodeWidth - 0.5, 0.5, Theme::NodeWidth - 0.5, 9);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(descriptor_.accent, 2));
    painter->drawPath(accent);
    auto font = painter->font();
    font.setPixelSize(19);
    font.setWeight(QFont::Normal);
    painter->setFont(font);
    painter->setPen(descriptor_.kind == QStringLiteral("output") ? Theme::mint()
                                                                 : descriptor_.accent);
    painter->drawText(QRectF(10, 7, 20, 30), Qt::AlignCenter, descriptor_.icon());
    font.setPixelSize(12);
    painter->setFont(font);
    painter->setPen(QColor(219, 227, 239));
    painter->drawText(QRectF(37, 7, 133, 17), Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(descriptor_.title, Qt::ElideRight, 133));
    font.setPixelSize(8);
    painter->setFont(font);
    painter->setPen(Theme::muted());
    painter->drawText(QRectF(37, 27, 133, 12), Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(descriptor_.subtitle, Qt::ElideRight, 133));
    paintHeaderActions(painter);
    painter->setPen(QColor(48, 58, 71));
    painter->drawLine(QPointF(1, 45), QPointF(Theme::NodeWidth - 1, 45));
}
void NodeItem::setState(const QJsonObject& block, double peak) {
    block_ = block;
    peak_ = peak;
    // Native children repaint their changed controls/meters. The cached frame
    // does not depend on telemetry and need not be repainted on every poll.
}
void NodeItem::setPlayback(const PlaybackState& state) {
    content_->setPlayback(state, controlsEnabled_);
    setToolTip(state.name + QLatin1Char('\n') + state.label + QLatin1Char('\n') + state.reason +
               QLatin1Char('\n') + state.warning);
}
void NodeItem::synchronizeControls(const QJsonArray& devices, bool enabled,
                                   const QJsonObject& runtime) {
    if (descriptor_.kind == QStringLiteral("output")) {
        const auto presentation =
            outputPresentation(block_.value(QStringLiteral("endpoint_id")).toString(), devices);
        if (descriptor_.title != presentation.title ||
            descriptor_.subtitle != presentation.subtitle) {
            descriptor_.title = presentation.title;
            descriptor_.subtitle = presentation.subtitle;
            descriptor_.description = presentation.note;
            setToolTip(presentation.title + QLatin1Char('\n') + presentation.note);
            setProperty("accessibleName", presentation.title);
            update();
        }
    }
    controlsEnabled_ = enabled;
    content_->synchronize(block_, devices, enabled, peak_, runtime);
}
void NodeItem::focusPrimaryControl() {
    content_->focusPrimaryControl();
}
QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionHasChanged)
        emit moved();
    return QGraphicsObject::itemChange(change, value);
}
void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    const auto point = event->pos();
    if (event->button() == Qt::LeftButton && point.y() < 44 && point.x() > 172) {
        if (point.x() > 194)
            emit removeRequested(id_);
        else
            emit inspectRequested(id_);
        event->accept();
        return;
    }
    setSelected(true);
    if (needsInspector())
        emit inspectRequested(id_);
    QGraphicsObject::mousePressEvent(event);
}
void NodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    emit inspectRequested(id_);
    event->accept();
}

GraphScene::GraphScene(SessionController& session, QObject* parent)
    : QGraphicsScene(parent), session_(session) {
    // At most 32 nodes and 64 wires: a linear scan avoids rebuilding a spatial
    // index on each drag, with cached wire hit areas for precise selection.
    setItemIndexMethod(QGraphicsScene::NoIndex);
    setSceneRect(0, 0, 1600, 1100);
    preview_ = addPath({}, QPen(Theme::mint(), 2, Qt::DashLine));
    preview_->setZValue(-0.5);
    connect(&session_, &SessionController::changed, this, &GraphScene::synchronize);
    connect(&session_, &SessionController::devicesChanged, this, &GraphScene::synchronize);
}
void GraphScene::synchronize() {
    synchronizing_ = true;
    const auto graph = session_.snapshot().value(QStringLiteral("graph")).toObject();
    const auto values = graph.value(QStringLiteral("nodes")).toArray();
    QSet<QString> existing;
    int index = 0;
    for (const auto& value : values) {
        const auto node = value.toObject();
        const auto id = node.value(QStringLiteral("id")).toString();
        existing.insert(id);
        if (!nodes_.contains(id)) {
            auto* item = new NodeItem(id, session_.catalog().describe(node), session_);
            addItem(item);
            nodes_.insert(id, item);
            const int column = index % 4;
            qreal y = 50;
            if (index >= 4) {
                const int previousRow = (index / 4 - 1) * 4;
                for (int i = previousRow; i < previousRow + 4; ++i) {
                    const auto previousId =
                        values[i].toObject().value(QStringLiteral("id")).toString();
                    if (const auto* previous = nodes_.value(previousId))
                        y = std::max(y, previous->sceneBoundingRect().bottom() + 45);
                }
            }
            item->setPos(savedPositions_.value(id, QPointF(40 + column * 275, y)));
            connect(item, &NodeItem::inspectRequested, this, &GraphScene::inspectRequested);
            connect(item, &NodeItem::removeRequested, &session_, &SessionController::removeNode);
            connect(item, &NodeItem::fieldEdited, &session_, &SessionController::updateField);
            connect(item, &NodeItem::actionRequested, this, &GraphScene::nodeActionRequested);
            connect(item, &NodeItem::gestureRequested, &session_, &SessionController::gesture);
            connect(item, &NodeItem::moved, this, [this, item, id] {
                savedPositions_.insert(id, item->pos());
                setSceneRect(
                    sceneRect().united(item->sceneBoundingRect().adjusted(-180, -180, 180, 180)));
                updateConnections(id);
                if (!synchronizing_)
                    emit positionsEdited();
            });
        }
        const auto block = node.value(QStringLiteral("block")).toObject();
        double peak = 0;
        const auto metrics = session_.snapshot()
                                 .value(QStringLiteral("metrics"))
                                 .toObject()
                                 .value(QStringLiteral("route_nodes"))
                                 .toArray();
        for (const auto& metric : metrics)
            if (metric.toObject().value(QStringLiteral("node_id")).toString() == id)
                peak = metric.toObject().value(QStringLiteral("peak")).toDouble();
        if (!session_.online() ||
            session_.snapshot().value(QStringLiteral("engine_state")).toString() !=
                QStringLiteral("running"))
            peak = 0;
        nodes_.value(id)->setState(block, peak);
        nodes_.value(id)->synchronizeControls(session_.devices(),
                                              session_.online() && !session_.busy(),
                                              session_.snapshot()
                                                  .value(QStringLiteral("plugin_states"))
                                                  .toObject()
                                                  .value(id)
                                                  .toObject());
        if (session_.catalog().describe(node).filePlayer)
            nodes_.value(id)->setPlayback(
                playbackState(session_.snapshot(), node, session_.devices(), session_.online(),
                              session_.busy(), savedResources_.value(id)));
        ++index;
    }
    for (auto it = nodes_.begin(); it != nodes_.end();) {
        if (!existing.contains(it.key())) {
            savedPositions_.remove(it.key());
            delete it.value();
            it = nodes_.erase(it);
        } else {
            ++it;
        }
    }
    QSet<QString> currentEdges;
    for (const auto& value : graph.value(QStringLiteral("edges")).toArray()) {
        const auto edge = value.toObject();
        const auto key = edgeKey(edge);
        currentEdges.insert(key);
        if (!edges_.contains(key)) {
            auto* item = new ConnectionItem;
            item->setData(3, edge);
            addItem(item);
            edges_.insert(key, item);
        }
    }
    for (auto it = edges_.begin(); it != edges_.end();) {
        if (!currentEdges.contains(it.key())) {
            delete it.value();
            it = edges_.erase(it);
        } else {
            ++it;
        }
    }
    updateConnections();
    synchronizing_ = false;
}
void GraphScene::setSavedResources(const QHash<QString, QString>& resources) {
    if (savedResources_ == resources)
        return;
    savedResources_ = resources;
    synchronize();
}
void GraphScene::updateConnections(const QString& movedNode) {
    for (auto* item : edges_) {
        const auto edge = item->data(3).toJsonObject();
        const auto fromId = edge.value(QStringLiteral("from")).toString();
        const auto toId = edge.value(QStringLiteral("to")).toString();
        if (!movedNode.isEmpty() && fromId != movedNode && toId != movedNode)
            continue;
        auto* from = nodes_.value(fromId);
        auto* to = nodes_.value(toId);
        if (from && to)
            static_cast<ConnectionItem*>(item)->setEndpoints(
                from->mapToScene(
                    from->portPosition(true, edge.value(QStringLiteral("from_port")).toInt())),
                to->mapToScene(
                    to->portPosition(false, edge.value(QStringLiteral("to_port")).toInt())));
    }
}
void GraphScene::placeNode(const QString& id, const QPointF& position) {
    if (auto* item = nodes_.value(id))
        item->setPos(position);
}
bool GraphScene::usePort(QGraphicsItem* item) {
    while (item && item->data(1).toString().isEmpty())
        item = item->parentItem();
    if (!item)
        return false;
    const auto id = item->data(0).toString();
    const int index = item->data(2).toInt();
    if (item->data(1).toString() == QStringLiteral("source")) {
        armedNode_ = id;
        armedPort_ = index;
    } else if (!armedNode_.isEmpty()) {
        session_.connectNodes(armedNode_, armedPort_, id, index);
        armedNode_.clear();
        preview_->setPath({});
    }
    return true;
}
void GraphScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton && usePort(itemAt(event->scenePos(), QTransform()))) {
        event->accept();
        return;
    }
    QGraphicsScene::mousePressEvent(event);
}
void GraphScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (auto* from = nodes_.value(armedNode_)) {
        preview_->setPath(
            wirePath(from->mapToScene(from->portPosition(true, armedPort_)), event->scenePos()));
        event->accept();
        return;
    }
    QGraphicsScene::mouseMoveEvent(event);
}
void GraphScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (!armedNode_.isEmpty() && event->button() == Qt::LeftButton)
        usePort(itemAt(event->scenePos(), QTransform()));
    QGraphicsScene::mouseReleaseEvent(event);
}
void GraphScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    auto* item = itemAt(event->scenePos(), QTransform());
    if (item && item->data(3).isValid()) {
        item->setSelected(true);
        QMenu menu;
        auto* remove = menu.addAction(QStringLiteral("Remover conexão"));
        if (menu.exec(event->screenPos()) == remove)
            session_.disconnectEdge(item->data(3).toJsonObject());
        event->accept();
        return;
    }
    QGraphicsScene::contextMenuEvent(event);
}
void GraphScene::keyPressEvent(QKeyEvent* event) {
    const auto selected = selectedItems();
    // Embedded controls own navigation while focused. In particular, changing
    // a device with arrows must not move or delete its graph node.
    if (auto* proxy = dynamic_cast<QGraphicsProxyWidget*>(focusItem())) {
        auto* owner = proxy->parentItem();
        while (owner && !dynamic_cast<NodeItem*>(owner))
            owner = owner->parentItem();
        if (selected.size() == 1 && selected.first() == owner) {
            QGraphicsScene::keyPressEvent(event);
            return;
        }
    }
    if (selected.size() == 1) {
        if (auto* node = dynamic_cast<NodeItem*>(selected.first())) {
            if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
                if (node->needsInspector())
                    emit inspectRequested(node->nodeId());
                else
                    node->focusPrimaryControl();
                event->accept();
                return;
            }
            const int step = event->modifiers().testFlag(Qt::ShiftModifier) ? 40 : 10;
            QPointF delta;
            if (event->key() == Qt::Key_Left)
                delta.setX(-step);
            if (event->key() == Qt::Key_Right)
                delta.setX(step);
            if (event->key() == Qt::Key_Up)
                delta.setY(-step);
            if (event->key() == Qt::Key_Down)
                delta.setY(step);
            if (!delta.isNull()) {
                node->setPos(node->pos() + delta);
                event->accept();
                return;
            }
        }
    }
    if (event->key() == Qt::Key_Escape) {
        armedNode_.clear();
        preview_->setPath({});
        clearSelection();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete) {
        if (selected.size() == 1) {
            if (const auto* node = dynamic_cast<NodeItem*>(selected.first()))
                session_.removeNode(node->nodeId());
            else if (selected.first()->data(3).isValid())
                session_.disconnectEdge(selected.first()->data(3).toJsonObject());
        }
        event->accept();
        return;
    }
    QGraphicsScene::keyPressEvent(event);
}
QJsonArray GraphScene::positions() const {
    QJsonArray result;
    for (auto it = nodes_.begin(); it != nodes_.end(); ++it)
        result.append(QJsonObject{{QStringLiteral("node_id"), it.key()},
                                  {QStringLiteral("x"), it.value()->pos().x()},
                                  {QStringLiteral("y"), it.value()->pos().y()}});
    return result;
}
void GraphScene::restorePositions(const QJsonArray& positions) {
    synchronizing_ = true;
    savedPositions_.clear();
    for (const auto& value : positions) {
        const auto entry = value.toObject();
        const auto id = entry.value(QStringLiteral("node_id")).toString();
        const QPointF position(entry.value(QStringLiteral("x")).toDouble(),
                               entry.value(QStringLiteral("y")).toDouble());
        savedPositions_.insert(id, position);
        if (auto* node = nodes_.value(id)) {
            node->setPos(position);
            setSceneRect(
                sceneRect().united(node->sceneBoundingRect().adjusted(-180, -180, 180, 180)));
        }
    }
    updateConnections();
    synchronizing_ = false;
}
void GraphScene::organize() {
    int index = 0;
    qreal y = 35;
    qreal rowHeight = 0;
    for (const auto& value : session_.snapshot()
                                 .value(QStringLiteral("graph"))
                                 .toObject()
                                 .value(QStringLiteral("nodes"))
                                 .toArray()) {
        const int column = index % 4;
        if (column == 0 && index > 0) {
            y += rowHeight + 45;
            rowHeight = 0;
        }
        if (auto* item = nodes_.value(value.toObject().value(QStringLiteral("id")).toString())) {
            item->setPos(35 + column * 275, y);
            rowHeight = std::max(rowHeight, item->boundingRect().height());
        }
        ++index;
    }
}
void GraphScene::selectNode(const QString& id) {
    clearSelection();
    if (auto* item = nodes_.value(id)) {
        item->setSelected(true);
        item->setFocus();
        if (!item->needsInspector()) {
            item->focusPrimaryControl();
            return;
        }
    }
    emit inspectRequested(id);
}
} // namespace nodivu
