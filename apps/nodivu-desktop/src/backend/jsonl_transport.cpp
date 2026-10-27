#include "backend/jsonl_transport.hpp"

namespace nodivu {

JsonlTransport::JsonlTransport(QObject* parent) : QObject(parent) {}

void JsonlTransport::start(const QString& executable, const QStringList& arguments,
                           const QProcessEnvironment& environment) {
    if (process_)
        return;
    process_ = new QProcess(this);
    watchdog_ = new QTimer(this);
    watchdog_->setSingleShot(true);
    watchdog_->setInterval(5000);
    shutdownTimer_ = new QTimer(this);
    shutdownTimer_->setSingleShot(true);
    shutdownTimer_->setInterval(3000);
    connect(watchdog_, &QTimer::timeout, this,
            [this] { fail(QStringLiteral("Motor sem resposta por 5 segundos.")); });
    connect(shutdownTimer_, &QTimer::timeout, process_, &QProcess::kill);
    connect(process_, &QProcess::started, this, [this] {
        emit ready();
        drain();
    });
    connect(process_, &QProcess::readyReadStandardOutput, this, &JsonlTransport::receive);
    connect(process_, &QProcess::readyReadStandardError, this,
            [this] { stderrTail_ = (stderrTail_ + process_->readAllStandardError()).right(4096); });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!closing_)
            fail(QStringLiteral("Não foi possível executar o motor: ") + process_->errorString());
        if (process_->state() == QProcess::NotRunning)
            finishShutdown();
    });
    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
                if (!closing_ && !failed_)
                    fail(QStringLiteral("Motor desconectado. ") + QString::fromUtf8(stderrTail_));
                finishShutdown();
            });
    process_->setProcessEnvironment(environment);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    process_->setProgram(executable);
    process_->setArguments(arguments);
    process_->start();
}

void JsonlTransport::enqueue(const QString& id, const QString& command, const QJsonObject& params) {
    if (closing_ || failed_) {
        emit completed(transportError(id, QStringLiteral("Motor indisponível.")));
        return;
    }
    const auto frame = encodeRequest(id, command, params);
    if (frame.isEmpty() || queue_.size() >= MaxQueuedCommands) {
        emit completed(
            transportError(id, QStringLiteral("Comando excedeu o limite ou a fila está cheia.")));
        return;
    }
    queue_.enqueue({id, command, frame});
    drain();
}

void JsonlTransport::drain() {
    if (!process_ || process_->state() != QProcess::Running || pending_ || failed_)
        return;
    if (queue_.isEmpty()) {
        if (closing_) {
            pending_ = Request{
                QStringLiteral("shutdown"), QStringLiteral("system.shutdown"),
                encodeRequest(QStringLiteral("shutdown"), QStringLiteral("system.shutdown"), {})};
        } else {
            return;
        }
    } else {
        pending_ = queue_.dequeue();
    }
    if (process_->write(pending_->frame) != pending_->frame.size()) {
        fail(QStringLiteral("Falha ao enviar comando ao motor."));
        return;
    }
    watchdog_->start();
}

void JsonlTransport::receive() {
    const auto bytes = process_->readAllStandardOutput();
    if (input_.size() + bytes.size() > MaxFrameBytes) {
        fail(QStringLiteral("Resposta excedeu 256 KiB."));
        return;
    }
    input_.append(bytes);
    const auto newline = input_.indexOf('\n');
    if (newline < 0)
        return;
    if (!pending_ || newline != input_.size() - 1) {
        fail(QStringLiteral("Resposta fora de ordem do motor."));
        return;
    }
    auto frame = input_.left(newline);
    input_.clear();
    if (frame.endsWith('\r'))
        frame.chop(1);
    const auto reply = decodeReply(frame, pending_->id);
    watchdog_->stop();
    if (reply.code == QStringLiteral("transport_error")) {
        fail(reply.message);
        return;
    }
    pending_.reset();
    if (closing_ && reply.id == QStringLiteral("shutdown")) {
        process_->closeWriteChannel();
        return;
    }
    emit completed(reply);
    drain();
}

void JsonlTransport::fail(const QString& message) {
    if (failed_)
        return;
    failed_ = true;
    if (watchdog_)
        watchdog_->stop();
    if (pending_) {
        emit completed(transportError(pending_->id, message));
        pending_.reset();
    }
    while (!queue_.isEmpty())
        emit completed(transportError(queue_.dequeue().id, message));
    emit fault(message);
    if (process_ && process_->state() != QProcess::NotRunning)
        process_->kill();
}

void JsonlTransport::shutdown() {
    if (closing_)
        return;
    closing_ = true;
    while (!queue_.isEmpty())
        emit completed(
            transportError(queue_.dequeue().id, QStringLiteral("Aplicativo encerrando.")));
    if (!process_ || process_->state() == QProcess::NotRunning) {
        finishShutdown();
        return;
    }
    shutdownTimer_->start();
    drain();
}

void JsonlTransport::finishShutdown() {
    if (stopped_)
        return;
    stopped_ = true;
    if (watchdog_)
        watchdog_->stop();
    if (shutdownTimer_)
        shutdownTimer_->stop();
    emit stopped();
}

} // namespace nodivu
