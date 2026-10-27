#pragma once

#include "contracts/protocol_codec.hpp"

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQueue>
#include <QTimer>
#include <optional>

namespace nodivu {

// Lives on a dedicated event-loop thread. It never transfers PCM.
class JsonlTransport final : public QObject {
    Q_OBJECT
  public:
    explicit JsonlTransport(QObject* parent = nullptr);

  public slots:
    void start(const QString& executable, const QStringList& arguments,
               const QProcessEnvironment& environment);
    void enqueue(const QString& id, const QString& command, const QJsonObject& params);
    void shutdown();

  signals:
    void ready();
    void completed(const nodivu::BackendReply& reply);
    void fault(const QString& message);
    void stopped();

  private:
    struct Request {
        QString id;
        QString command;
        QByteArray frame;
    };
    void drain();
    void receive();
    void fail(const QString& message);
    void finishShutdown();

    QProcess* process_ = nullptr;
    QTimer* watchdog_ = nullptr;
    QTimer* shutdownTimer_ = nullptr;
    QQueue<Request> queue_;
    std::optional<Request> pending_;
    QByteArray input_;
    QByteArray stderrTail_;
    bool closing_ = false;
    bool failed_ = false;
    bool stopped_ = false;
};

} // namespace nodivu
Q_DECLARE_METATYPE(nodivu::BackendReply)
