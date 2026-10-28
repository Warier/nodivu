#include "features/workspace/session_controller.hpp"
#include <cmath>
#include <utility>

namespace nodivu {
SessionController::SessionController(BackendClient& backend, QObject* parent)
    : QObject(parent), backend_(backend) {
    pollTimer_.setInterval(100);
    editTimer_.setSingleShot(true);
    editTimer_.setInterval(60);
    connect(&pollTimer_, &QTimer::timeout, this, &SessionController::poll);
    connect(&editTimer_, &QTimer::timeout, this, &SessionController::flushEdits);
    connect(&backend_, &BackendClient::ready, this, &SessionController::initialize);
    connect(&backend_, &BackendClient::fault, this, [this](const QString& message) {
        online_ = false;
        pollTimer_.stop();
        editTimer_.stop();
        edits_.clear();
        busy_ = false;
        polling_ = false;
        emit disconnected();
        emit error(message);
        emit changed();
    });
}
void SessionController::beginShutdown() {
    online_ = false;
    pollTimer_.stop();
    editTimer_.stop();
    edits_.clear();
}

void SessionController::initialize() {
    backend_.request(QStringLiteral("system.hello"), {}, this, [this](const BackendReply& hello) {
        if (!hello.ok ||
            hello.result.value(QStringLiteral("api")).toString() != QStringLiteral("graph-v2")) {
            emit error(hello.ok ? QStringLiteral("Backend incompatível: API graph-v2 necessária.")
                                : hello.message);
            return;
        }
        backend_.request(
            QStringLiteral("plugins.list"), {}, this, [this](const BackendReply& reply) {
                if (!reply.ok) {
                    emit error(reply.message);
                    return;
                }
                catalog_.setPlugins(reply.result.value(QStringLiteral("plugins")).toArray());
                emit catalogChanged();
                backend_.request(QStringLiteral("session.snapshot"), {}, this,
                                 [this](const BackendReply& state) {
                                     if (!state.ok) {
                                         emit error(state.message);
                                         return;
                                     }
                                     online_ = true;
                                     acceptSnapshot(state.result);
                                     refreshDevices();
                                     pollTimer_.start();
                                 });
            });
    });
}

void SessionController::acceptSnapshot(const QJsonObject& snapshot) {
    const auto revision = snapshot.value(QStringLiteral("revision"));
    const auto graph = snapshot.value(QStringLiteral("graph")).toObject();
    if (!revision.isDouble() || revision.toDouble() < 0 ||
        revision.toDouble() > 9007199254740991.0 ||
        std::floor(revision.toDouble()) != revision.toDouble() ||
        !graph.value(QStringLiteral("nodes")).isArray() ||
        !graph.value(QStringLiteral("edges")).isArray()) {
        emit error(QStringLiteral("Snapshot incompatível: estado anterior preservado."));
        return;
    }
    snapshot_ = snapshot;
    emit changed();
}

QJsonObject SessionController::node(const QString& id) const {
    for (const auto& value : snapshot_.value(QStringLiteral("graph"))
                                 .toObject()
                                 .value(QStringLiteral("nodes"))
                                 .toArray()) {
        const auto entry = value.toObject();
        if (entry.value(QStringLiteral("id")).toString() == id)
            return entry;
    }
    return {};
}

void SessionController::refreshDevices() {
    if (!online_ || devicesRefreshing_)
        return;
    devicesRefreshing_ = true;
    emit activityChanged();
    backend_.request(QStringLiteral("devices.list"), {}, this, [this](const BackendReply& reply) {
        devicesRefreshing_ = false;
        if (!reply.ok) {
            deviceIssue_ = reply.message;
            emit devicesChanged();
            emit activityChanged();
            emit error(reply.message);
            emit devicesRefreshed(false);
            return;
        }
        devices_ = reply.result.value(QStringLiteral("devices")).toArray();
        const auto warnings = reply.result.value(QStringLiteral("warnings")).toArray();
        QStringList messages;
        for (const auto& value : warnings)
            messages.append(value.toString());
        deviceIssue_ = messages.join(QStringLiteral("\n"));
        emit devicesChanged();
        emit activityChanged();
        if (!warnings.isEmpty())
            emit error(warnings.first().toString());
        emit devicesRefreshed(true);
    });
}

void SessionController::poll() {
    if (!online_ || busy_ || polling_ || !edits_.isEmpty())
        return;
    polling_ = true;
    backend_.request(QStringLiteral("session.snapshot"), {}, this,
                     [this](const BackendReply& reply) {
                         polling_ = false;
                         if (reply.ok)
                             acceptSnapshot(reply.result);
                         else
                             emit error(reply.message);
                     });
}

void SessionController::command(const QString& name, const QJsonObject& params,
                                BackendClient::Callback callback) {
    if (!online_ || busy_) {
        const auto failure =
            transportError({}, QStringLiteral("Aguarde a operação atual ou reconecte o motor."));
        emit error(failure.message);
        if (callback)
            QTimer::singleShot(0, this,
                               [callback = std::move(callback), failure] { callback(failure); });
        return;
    }
    busy_ = true;
    emit activityChanged();
    backend_.request(name, params, this,
                     [this, callback = std::move(callback)](const BackendReply& reply) {
                         busy_ = false;
                         if (!reply.ok) {
                             emit error(reply.message);
                             if (reply.code == QStringLiteral("revision_conflict"))
                                 poll();
                         } else if (reply.result.contains(QStringLiteral("graph"))) {
                             acceptSnapshot(reply.result);
                         }
                         if (callback)
                             callback(reply);
                         emit activityChanged();
                         if (!edits_.isEmpty())
                             editTimer_.start();
                     });
}

void SessionController::mutateGraph(const std::function<void(QJsonObject&)>& mutation) {
    if (!online_ || busy_)
        return;
    auto graph = snapshot_.value(QStringLiteral("graph")).toObject();
    mutation(graph);
    command(QStringLiteral("graph.apply"),
            {{QStringLiteral("expected_revision"), snapshot_.value(QStringLiteral("revision"))},
             {QStringLiteral("graph"), graph}},
            [this](const BackendReply& reply) {
                if (reply.ok)
                    emit documentEdited();
            });
}

void SessionController::addNode(const QString& kind, const QString& pluginId) {
    QJsonObject params{
        {QStringLiteral("kind"), kind},
        {QStringLiteral("expected_revision"), snapshot_.value(QStringLiteral("revision"))}};
    if (!pluginId.isEmpty())
        params.insert(QStringLiteral("plugin_id"), pluginId);
    const auto previous = snapshot_.value(QStringLiteral("graph"))
                              .toObject()
                              .value(QStringLiteral("nodes"))
                              .toArray();
    command(QStringLiteral("node.add"), params, [this, previous](const BackendReply& reply) {
        if (!reply.ok)
            return;
        emit documentEdited();
        for (const auto& value : reply.result.value(QStringLiteral("graph"))
                                     .toObject()
                                     .value(QStringLiteral("nodes"))
                                     .toArray()) {
            bool found = false;
            for (const auto& old : previous)
                found = found || old.toObject().value(QStringLiteral("id")) ==
                                     value.toObject().value(QStringLiteral("id"));
            if (!found)
                emit nodeAdded(value.toObject().value(QStringLiteral("id")).toString());
        }
    });
}

void SessionController::addInitialNodes(const NodeDescriptor& source) {
    if (!source.kind.isEmpty() && (source.inputs != 0 || source.outputs < 1)) {
        emit error(QStringLiteral("O início rápido precisa de uma fonte de áudio."));
        return;
    }
    if (!settled() || !snapshot_.value(QStringLiteral("graph"))
                           .toObject()
                           .value(QStringLiteral("nodes"))
                           .toArray()
                           .isEmpty())
        return;
    QJsonObject params{
        {QStringLiteral("kind"), source.kind.isEmpty() ? QStringLiteral("capture") : source.kind},
        {QStringLiteral("expected_revision"), snapshot_.value(QStringLiteral("revision"))}};
    if (!source.pluginId.isEmpty())
        params.insert(QStringLiteral("plugin_id"), source.pluginId);
    // Explicit starter choice builds a connected template, with no endpoint chosen.
    command(QStringLiteral("node.add"), params, [this](const BackendReply& first) {
        if (!first.ok)
            return;
        const auto source = first.result.value(QStringLiteral("graph"))
                                .toObject()
                                .value(QStringLiteral("nodes"))
                                .toArray()
                                .first()
                                .toObject()
                                .value(QStringLiteral("id"))
                                .toString();
        emit documentEdited();
        emit nodeAdded(source);
        command(
            QStringLiteral("node.add"),
            {{QStringLiteral("kind"), QStringLiteral("output")},
             {QStringLiteral("expected_revision"), snapshot_.value(QStringLiteral("revision"))}},
            [this, source](const BackendReply& second) {
                if (!second.ok)
                    return;
                emit documentEdited();
                for (const auto& node : second.result.value(QStringLiteral("graph"))
                                            .toObject()
                                            .value(QStringLiteral("nodes"))
                                            .toArray()) {
                    const auto id = node.toObject().value(QStringLiteral("id")).toString();
                    if (id != source) {
                        emit nodeAdded(id);
                        connectNodes(source, 0, id, 0);
                        break;
                    }
                }
            });
    });
}

void SessionController::removeNode(const QString& id) {
    mutateGraph([&](QJsonObject& graph) {
        QJsonArray nodes, edges;
        for (const auto& value : graph.value(QStringLiteral("nodes")).toArray())
            if (value.toObject().value(QStringLiteral("id")).toString() != id)
                nodes.append(value);
        for (const auto& value : graph.value(QStringLiteral("edges")).toArray()) {
            const auto edge = value.toObject();
            if (edge.value(QStringLiteral("from")).toString() != id &&
                edge.value(QStringLiteral("to")).toString() != id)
                edges.append(edge);
        }
        graph.insert(QStringLiteral("nodes"), nodes);
        graph.insert(QStringLiteral("edges"), edges);
    });
}
void SessionController::connectNodes(const QString& from, int fromPort, const QString& to,
                                     int toPort) {
    mutateGraph([&](QJsonObject& graph) {
        auto edges = graph.value(QStringLiteral("edges")).toArray();
        edges.append(QJsonObject{{QStringLiteral("from"), from},
                                 {QStringLiteral("from_port"), fromPort},
                                 {QStringLiteral("to"), to},
                                 {QStringLiteral("to_port"), toPort}});
        graph.insert(QStringLiteral("edges"), edges);
    });
}
void SessionController::disconnectEdge(const QJsonObject& edge) {
    mutateGraph([&](QJsonObject& graph) {
        QJsonArray remaining;
        for (const auto& value : graph.value(QStringLiteral("edges")).toArray())
            if (value.toObject() != edge)
                remaining.append(value);
        graph.insert(QStringLiteral("edges"), remaining);
    });
}
void SessionController::updateField(const QString& id, const QString& field,
                                    const QJsonValue& value) {
    if (!online_ || node(id).isEmpty())
        return;
    edits_[id].insert(field, value);
    editTimer_.start();
}
void SessionController::flushEdits() {
    if (busy_ || !online_) {
        editTimer_.start();
        return;
    }
    const auto edits = std::exchange(edits_, {});
    if (edits.isEmpty())
        return;
    mutateGraph([&](QJsonObject& graph) {
        auto nodes = graph.value(QStringLiteral("nodes")).toArray();
        for (qsizetype i = 0; i < nodes.size(); ++i) {
            auto entry = nodes[i].toObject();
            const auto changes = edits.value(entry.value(QStringLiteral("id")).toString());
            auto block = entry.value(QStringLiteral("block")).toObject();
            for (auto it = changes.begin(); it != changes.end(); ++it)
                setFieldValue(block, it.key(), it.value());
            entry.insert(QStringLiteral("block"), block);
            nodes[i] = entry;
        }
        graph.insert(QStringLiteral("nodes"), nodes);
    });
}
void SessionController::setMinimized(bool minimized) {
    pollTimer_.setInterval(minimized ? 1000 : 100);
}
void SessionController::gesture(const QString& id, quint32 parameter, bool pressed) {
    if (!online_)
        return;
    backend_.request(QStringLiteral("plugin.gesture"),
                     {{QStringLiteral("node_id"), id},
                      {QStringLiteral("parameter"), static_cast<qint64>(parameter)},
                      {QStringLiteral("pressed"), pressed}},
                     this, [this](const BackendReply& reply) {
                         if (!reply.ok)
                             emit error(reply.message);
                     });
}
} // namespace nodivu
