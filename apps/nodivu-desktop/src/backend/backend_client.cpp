#include "backend/backend_client.hpp"

namespace nodivu {

BackendClient::BackendClient(QObject* parent) : QObject(parent) {
    qRegisterMetaType<BackendReply>();
    qRegisterMetaType<QProcessEnvironment>();
    workerThread_.setObjectName(QStringLiteral("nodivu-control"));
    connect(&workerThread_, &QThread::finished, this, [this] {
        transport_ = nullptr;
        emit stopped();
    });
    createTransport();
}

void BackendClient::createTransport() {
    transport_ = new JsonlTransport;
    transport_->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::finished, transport_, &QObject::deleteLater);
    connect(this, &BackendClient::startRequested, transport_, &JsonlTransport::start);
    connect(this, &BackendClient::requestQueued, transport_, &JsonlTransport::enqueue);
    connect(this, &BackendClient::shutdownRequested, transport_, &JsonlTransport::shutdown);
    connect(transport_, &JsonlTransport::ready, this, &BackendClient::ready);
    connect(transport_, &JsonlTransport::fault, this, &BackendClient::fault);
    // Direct quit is safe: QThread::quit is thread-safe, including during teardown.
    connect(transport_, &JsonlTransport::stopped, &workerThread_, &QThread::quit,
            Qt::DirectConnection);
    connect(transport_, &JsonlTransport::completed, this, [this](const BackendReply& reply) {
        const auto entry = callbacks_.find(reply.id);
        if (entry == callbacks_.end())
            return;
        const auto pending = entry.value();
        callbacks_.erase(entry);
        if (pending.context)
            pending.callback(reply);
    });
    workerThread_.start();
}

BackendClient::~BackendClient() {
    shutdown();
    // Normal shutdown is asynchronous before QApplication quits. This bounded join
    // is the final ownership barrier; only our worker/child can be affected.
    workerThread_.wait();
}

void BackendClient::start(const QString& executable, const QProcessEnvironment& environment) {
    if (!transport_) {
        closing_ = false;
        createTransport();
    }
    emit startRequested(executable, {}, environment);
}

QString BackendClient::request(const QString& command, const QJsonObject& params, QObject* context,
                               Callback callback) {
    const auto id = QString::number(++serial_);
    if (closing_ || callbacks_.size() >= MaxQueuedCommands + 1) {
        QTimer::singleShot(0, context, [callback = std::move(callback), id] {
            callback(transportError(id, QStringLiteral("Aguarde as operações pendentes.")));
        });
        return id;
    }
    callbacks_.insert(id, {context, std::move(callback)});
    emit requestQueued(id, command, params);
    return id;
}

void BackendClient::shutdown() {
    if (closing_)
        return;
    closing_ = true;
    if (!workerThread_.isRunning()) {
        QTimer::singleShot(0, this, &BackendClient::stopped);
        return;
    }
    emit shutdownRequested();
}

} // namespace nodivu
