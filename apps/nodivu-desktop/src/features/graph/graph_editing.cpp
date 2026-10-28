#include "features/graph/graph_scene.hpp"
#include <QPointer>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace nodivu {
QStringList GraphScene::selectedNodeIds() const {
    QStringList ids;
    for (auto* item : selectedItems())
        if (const auto* node = dynamic_cast<NodeItem*>(item))
            ids.append(node->nodeId());
    std::sort(ids.begin(), ids.end());
    return ids;
}
void GraphScene::selectNodes(const QStringList& ids) {
    clearSelection();
    for (const auto& id : ids)
        if (auto* node = nodes_.value(id))
            node->setSelected(true);
}
void GraphScene::selectAllNodes() {
    selectNodes(nodes_.keys());
}
QString GraphScene::nodeTitle(const QString& id) const {
    if (auto* item = nodes_.value(id))
        return item->displayName();
    return session_.catalog().describe(session_.node(id)).title;
}
QJsonObject GraphScene::presentation() const {
    QJsonObject names;
    for (auto it = names_.cbegin(); it != names_.cend(); ++it)
        if (nodes_.contains(it.key()) && !it.value().isEmpty())
            names.insert(it.key(), it.value());
    return {{QStringLiteral("names"), names}};
}
void GraphScene::restorePresentation(const QJsonObject& presentation) {
    names_.clear();
    const auto names = presentation.value(QStringLiteral("names")).toObject();
    for (auto it = names.begin(); it != names.end(); ++it)
        if (nodes_.contains(it.key()) && it.value().isString() &&
            it.value().toString().size() <= 80)
            names_.insert(it.key(), it.value().toString().simplified());
    for (auto it = nodes_.begin(); it != nodes_.end(); ++it)
        it.value()->setDisplayName(names_.value(it.key()));
    updateConnections();
    emit presentationEdited();
}
void GraphScene::renameNode(QStringView nodeId, const QString& name) {
    const auto id = nodeId.toString();
    if (!nodes_.contains(id) || !session_.settled())
        return;
    const auto trimmed = name.simplified().left(80);
    if (trimmed == names_.value(id))
        return;
    names_.insert(id, trimmed);
    nodes_.value(id)->setDisplayName(trimmed);
    updateConnections();
    emit presentationEdited();
}
void GraphScene::setCompact(bool compact) {
    compact_ = compact;
    for (auto* item : nodes_)
        item->setCompact(compact);
    updateConnections();
}
void GraphScene::alignSelection(bool horizontally) {
    auto ids = selectedNodeIds();
    if (ids.size() < 2 || !session_.settled())
        return;
    // Preserve spatial order while distributing full card bounds, including
    // ports. Setting only one coordinate can stack cards on top of each other.
    std::stable_sort(ids.begin(), ids.end(),
                     [this, horizontally](const auto& left, const auto& right) {
                         const auto a = nodes_.value(left)->pos(), b = nodes_.value(right)->pos();
                         const auto primaryA = horizontally ? a.x() : a.y();
                         const auto primaryB = horizontally ? b.x() : b.y();
                         return primaryA != primaryB ? primaryA < primaryB
                                : horizontally       ? a.y() < b.y()
                                                     : a.x() < b.x();
                     });
    QRectF bounds;
    for (const auto& id : ids)
        bounds = bounds.united(nodes_.value(id)->sceneBoundingRect());
    QList<QRectF> obstacles;
    for (auto it = nodes_.cbegin(); it != nodes_.cend(); ++it)
        if (!ids.contains(it.key()))
            obstacles.append(it.value()->sceneBoundingRect());
    constexpr qreal gap = 48;
    qreal cursor = horizontally ? bounds.left() : bounds.top();
    synchronizing_ = true;
    for (const auto& id : ids) {
        auto* node = nodes_.value(id);
        const auto original = node->sceneBoundingRect();
        QRectF target(horizontally ? QPointF(cursor, bounds.top()) : QPointF(bounds.left(), cursor),
                      original.size());
        // Skip unselected cards instead of moving them or covering them.
        for (qsizetype attempt = 0; attempt <= obstacles.size(); ++attempt) {
            bool occupied = false;
            for (const auto& obstacle : obstacles)
                if (target.intersects(obstacle)) {
                    if (horizontally)
                        target.moveLeft(obstacle.right() + gap);
                    else
                        target.moveTop(obstacle.bottom() + gap);
                    occupied = true;
                    break;
                }
            if (!occupied)
                break;
        }
        node->setPos(node->pos() + target.topLeft() - original.topLeft());
        cursor = (horizontally ? target.right() : target.bottom()) + gap;
    }
    synchronizing_ = false;
    updateConnections();
    emit positionsEdited();
}
void GraphScene::removeSelection() {
    if (!session_.settled())
        return;
    const auto ids = selectedNodeIds();
    QJsonArray selectedEdges;
    for (auto* item : selectedItems())
        if (item->data(3).isValid())
            selectedEdges.append(item->data(3).toJsonObject());
    if (ids.isEmpty() && selectedEdges.isEmpty())
        return;
    auto graph = session_.snapshot().value(QStringLiteral("graph")).toObject();
    QJsonArray nodes, edges;
    for (const auto& node : graph.value(QStringLiteral("nodes")).toArray())
        if (!ids.contains(node.toObject().value(QStringLiteral("id")).toString()))
            nodes.append(node);
    for (const auto& value : graph.value(QStringLiteral("edges")).toArray()) {
        const auto edge = value.toObject();
        bool selected = false;
        for (const auto& candidate : selectedEdges)
            selected |= candidate == value;
        if (!selected && !ids.contains(edge.value(QStringLiteral("from")).toString()) &&
            !ids.contains(edge.value(QStringLiteral("to")).toString()))
            edges.append(edge);
    }
    graph.insert(QStringLiteral("nodes"), nodes);
    graph.insert(QStringLiteral("edges"), edges);
    session_.command(QStringLiteral("graph.apply"),
                     {{QStringLiteral("expected_revision"),
                       session_.snapshot().value(QStringLiteral("revision"))},
                      {QStringLiteral("graph"), graph}});
}
bool GraphScene::canConnect(const QString& from, const QString& to, int toPort) const {
    if (from == to || !nodes_.contains(from) || !nodes_.contains(to))
        return false;
    const auto descriptor = session_.catalog().describe(session_.node(to));
    if (toPort < 0 || toPort >= descriptor.inputs)
        return false;
    const auto edges = session_.snapshot()
                           .value(QStringLiteral("graph"))
                           .toObject()
                           .value(QStringLiteral("edges"))
                           .toArray();
    for (const auto& value : edges) {
        const auto edge = value.toObject();
        if (edge.value(QStringLiteral("to")).toString() == to &&
            edge.value(QStringLiteral("to_port")).toInt() == toPort)
            return false;
    }
    QSet<QString> visited;
    QStringList pending{to};
    while (!pending.isEmpty()) {
        const auto id = pending.takeLast();
        if (id == from)
            return false;
        if (visited.contains(id))
            continue;
        visited.insert(id);
        for (const auto& value : edges)
            if (value.toObject().value(QStringLiteral("from")).toString() == id)
                pending.append(value.toObject().value(QStringLiteral("to")).toString());
    }
    return true;
}
QJsonObject GraphScene::selectedFragment() const {
    const auto ids = selectedNodeIds();
    const auto graph = session_.snapshot().value(QStringLiteral("graph")).toObject();
    QJsonArray nodes, edges, positions;
    QJsonObject names;
    QRectF bounds;
    for (const auto& id : ids) {
        bounds = bounds.united(nodes_.value(id)->sceneBoundingRect());
        if (!names_.value(id).isEmpty())
            names.insert(id, names_.value(id));
    }
    for (const auto& value : graph.value(QStringLiteral("nodes")).toArray()) {
        auto node = value.toObject();
        const auto id = node.value(QStringLiteral("id")).toString();
        if (!ids.contains(id))
            continue;
        auto block = node.value(QStringLiteral("block")).toObject();
        // Reuse processing settings, never live input/device/file selections.
        if (block.contains(QStringLiteral("endpoint_id")))
            block.insert(QStringLiteral("endpoint_id"), QJsonValue(QJsonValue::Null));
        if (block.value(QStringLiteral("kind")).toString() == QStringLiteral("tone"))
            block.insert(QStringLiteral("enabled"), false);
        node.insert(QStringLiteral("block"), block);
        nodes.append(node);
        const auto point = nodes_.value(id)->pos() - bounds.topLeft();
        positions.append(QJsonObject{{QStringLiteral("node_id"), id},
                                     {QStringLiteral("x"), point.x()},
                                     {QStringLiteral("y"), point.y()}});
    }
    for (const auto& value : graph.value(QStringLiteral("edges")).toArray()) {
        const auto edge = value.toObject();
        if (ids.contains(edge.value(QStringLiteral("from")).toString()) &&
            ids.contains(edge.value(QStringLiteral("to")).toString()))
            edges.append(edge);
    }
    return {{QStringLiteral("format"), QStringLiteral("nodivu-chain")},
            {QStringLiteral("version"), 1},
            {QStringLiteral("nodes"), nodes},
            {QStringLiteral("edges"), edges},
            {QStringLiteral("positions"), positions},
            {QStringLiteral("presentation"), QJsonObject{{QStringLiteral("names"), names}}}};
}
void GraphScene::insertFragment(const QJsonObject& fragment, const QPointF& origin) {
    if (!session_.settled())
        return;
    const auto fail = [this] {
        emit session_.error(
            QStringLiteral("Modelo inválido. Use um modelo de cadeia salvo pelo Nodivu."));
    };
    const auto sourceNodes = fragment.value(QStringLiteral("nodes")).toArray();
    const auto sourceEdges = fragment.value(QStringLiteral("edges")).toArray();
    if (fragment.value(QStringLiteral("format")).toString() != QStringLiteral("nodivu-chain") ||
        fragment.value(QStringLiteral("version")).toInt() != 1 || sourceNodes.isEmpty() ||
        sourceNodes.size() > 32 || sourceEdges.size() > 64 || !std::isfinite(origin.x()) ||
        !std::isfinite(origin.y())) {
        fail();
        return;
    }
    auto graph = session_.snapshot().value(QStringLiteral("graph")).toObject();
    auto nodes = graph.value(QStringLiteral("nodes")).toArray();
    auto edges = graph.value(QStringLiteral("edges")).toArray();
    QSet<QString> singletons;
    for (const auto& node : nodes)
        singletons.insert(node.toObject()
                              .value(QStringLiteral("block"))
                              .toObject()
                              .value(QStringLiteral("kind"))
                              .toString());
    QHash<QString, QString> mapping;
    for (const auto& value : sourceNodes) {
        auto node = value.toObject();
        const auto old = node.value(QStringLiteral("id")).toString();
        if (old.isEmpty() || mapping.contains(old)) {
            fail();
            return;
        }
        auto block = node.value(QStringLiteral("block")).toObject();
        const auto kind = block.value(QStringLiteral("kind")).toString();
        if (kind == QStringLiteral("output") || kind == QStringLiteral("capture") ||
            kind == QStringLiteral("tone")) {
            if (singletons.contains(kind)) {
                emit session_.error(
                    QStringLiteral("O projeto admite apenas uma instância de %1. Selecione ou "
                                   "salve a cadeia sem esse bloco para inseri-la aqui.")
                        .arg(session_.catalog().describe(node).title));
                return;
            }
            singletons.insert(kind);
        }
        if (block.contains(QStringLiteral("endpoint_id")))
            block.insert(QStringLiteral("endpoint_id"), QJsonValue(QJsonValue::Null));
        if (block.value(QStringLiteral("kind")).toString() == QStringLiteral("tone"))
            block.insert(QStringLiteral("enabled"), false);
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        mapping.insert(old, id);
        node.insert(QStringLiteral("id"), id);
        node.insert(QStringLiteral("block"), block);
        nodes.append(node);
    }
    for (const auto& value : sourceEdges) {
        auto edge = value.toObject();
        const auto from = edge.value(QStringLiteral("from")).toString(),
                   to = edge.value(QStringLiteral("to")).toString();
        if (!mapping.contains(from) || !mapping.contains(to)) {
            fail();
            return;
        }
        edge.insert(QStringLiteral("from"), mapping.value(from));
        edge.insert(QStringLiteral("to"), mapping.value(to));
        edges.append(edge);
    }
    QHash<QString, QPointF> positions;
    for (const auto& value : fragment.value(QStringLiteral("positions")).toArray()) {
        const auto entry = value.toObject();
        const auto old = entry.value(QStringLiteral("node_id")).toString();
        const double x = entry.value(QStringLiteral("x")).toDouble(1e20),
                     y = entry.value(QStringLiteral("y")).toDouble(1e20);
        if (!mapping.contains(old) || positions.contains(mapping.value(old)) || !std::isfinite(x) ||
            !std::isfinite(y) || std::abs(x + origin.x()) > 1e7 || std::abs(y + origin.y()) > 1e7) {
            fail();
            return;
        }
        positions.insert(mapping.value(old), origin + QPointF(x, y));
    }
    if (positions.size() != mapping.size()) {
        fail();
        return;
    }
    graph.insert(QStringLiteral("nodes"), nodes);
    graph.insert(QStringLiteral("edges"), edges);
    const QPointer<GraphScene> self(this);
    session_.command(QStringLiteral("graph.apply"),
                     {{QStringLiteral("expected_revision"),
                       session_.snapshot().value(QStringLiteral("revision"))},
                      {QStringLiteral("graph"), graph}},
                     [self, mapping, positions, fragment](const BackendReply& reply) {
                         if (!self || !reply.ok)
                             return;
                         const auto names = fragment.value(QStringLiteral("presentation"))
                                                .toObject()
                                                .value(QStringLiteral("names"))
                                                .toObject();
                         self->synchronizing_ = true;
                         for (auto it = positions.begin(); it != positions.end(); ++it)
                             self->placeNode(it.key(), it.value());
                         self->synchronizing_ = false;
                         for (auto it = mapping.begin(); it != mapping.end(); ++it)
                             self->renameNode(it.value(), names.value(it.key()).toString());
                         self->selectNodes(mapping.values());
                         emit self->positionsEdited();
                         emit self->fragmentInserted(mapping.values());
                     });
}
} // namespace nodivu
