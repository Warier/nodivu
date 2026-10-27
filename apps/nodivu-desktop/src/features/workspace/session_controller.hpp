#pragma once

#include "backend/backend_client.hpp"
#include "contracts/node_descriptor.hpp"
#include <QJsonArray>
#include <QTimer>

namespace nodivu {
class SessionController final : public QObject {
    Q_OBJECT
  public:
    explicit SessionController(BackendClient& backend, QObject* parent = nullptr);
    const QJsonObject& snapshot() const {
        return snapshot_;
    }
    const QJsonArray& devices() const {
        return devices_;
    }
    const NodeCatalog& catalog() const {
        return catalog_;
    }
    bool online() const {
        return online_;
    }
    bool busy() const {
        return busy_;
    }
    bool settled() const {
        return online_ && !busy_ && edits_.isEmpty();
    }
    QString deviceIssue() const {
        return deviceIssue_;
    }
    bool devicesRefreshing() const {
        return devicesRefreshing_;
    }
    void restoreSnapshot(const QJsonObject& snapshot) {
        acceptSnapshot(snapshot);
    }
    QJsonObject node(const QString& id) const;
    void initialize();
    void beginShutdown();
    void refreshDevices();
    void addNode(const QString& kind, const QString& pluginId = {});
    void addInitialNodes();
    void removeNode(const QString& id);
    void connectNodes(const QString& from, int fromPort, const QString& to, int toPort);
    void disconnectEdge(const QJsonObject& edge);
    void updateField(const QString& id, const QString& field, const QJsonValue& value);
    void setMinimized(bool minimized);
    void gesture(const QString& id, quint32 parameter, bool pressed);
    void command(const QString& name, const QJsonObject& params,
                 BackendClient::Callback callback = {});

  signals:
    void changed();
    void activityChanged();
    void catalogChanged();
    void devicesChanged();
    void devicesRefreshed(bool success);
    void error(const QString& message);
    void documentEdited();
    void disconnected();
    void nodeAdded(const QString& id);

  private:
    void acceptSnapshot(const QJsonObject& snapshot);
    void mutateGraph(const std::function<void(QJsonObject&)>& mutation);
    void poll();
    void flushEdits();
    BackendClient& backend_;
    NodeCatalog catalog_;
    QJsonObject snapshot_;
    QJsonArray devices_;
    QHash<QString, QHash<QString, QJsonValue>> edits_;
    QTimer pollTimer_;
    QTimer editTimer_;
    bool online_ = false;
    bool busy_ = false;
    bool polling_ = false;
    bool devicesRefreshing_ = false;
    QString deviceIssue_;
};
} // namespace nodivu
