#pragma once

#include "backend/jsonl_transport.hpp"

#include <QHash>
#include <QPointer>
#include <QThread>
#include <functional>

namespace nodivu {

class BackendClient final : public QObject {
    Q_OBJECT
  public:
    using Callback = std::function<void(const BackendReply&)>;
    explicit BackendClient(QObject* parent = nullptr);
    ~BackendClient() override;
    void start(const QString& executable, const QProcessEnvironment& environment);
    QString request(const QString& command, const QJsonObject& params, QObject* context,
                    Callback callback);
    void shutdown();

  signals:
    void ready();
    void fault(const QString& message);
    void stopped();
    void startRequested(const QString& executable, const QStringList& arguments,
                        const QProcessEnvironment& environment);
    void requestQueued(const QString& id, const QString& command, const QJsonObject& params);
    void shutdownRequested();

  private:
    void createTransport();
    struct Pending {
        QPointer<QObject> context;
        Callback callback;
    };
    QThread workerThread_;
    JsonlTransport* transport_ = nullptr;
    QHash<QString, Pending> callbacks_;
    quint64 serial_ = 0;
    bool closing_ = false;
};

} // namespace nodivu
