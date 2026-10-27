#include "platform/single_instance.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLocalSocket>

namespace nodivu {
SingleInstance::SingleInstance(const QString& profile, QObject* parent) : QObject(parent) {
    const auto path = QDir(profile).absolutePath();
    name_ = QStringLiteral("nodivu-") +
            QString::fromLatin1(
                QCryptographicHash::hash(path.toCaseFolded().toUtf8(), QCryptographicHash::Sha256)
                    .toHex());
    if (!QDir().mkpath(path) || QFileInfo(path).isJunction() || QFileInfo(path).isSymbolicLink()) {
        error_ = QStringLiteral("Perfil indisponível ou redirecionado por link.");
        return;
    }
    lock_ = std::make_unique<QLockFile>(QDir(path).filePath(QStringLiteral("desktop.lock")));
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection, this, [this] {
        while (auto* socket = server_.nextPendingConnection()) {
            // The pipe accepts no commands or paths, only an activation signal.
            emit activated();
            socket->disconnectFromServer();
            socket->deleteLater();
        }
    });
}
bool SingleInstance::acquire() {
    if (!lock_)
        return false;
    if (!lock_->tryLock(0)) {
        QLocalSocket socket;
        socket.connectToServer(name_);
        if (!socket.waitForConnected(500))
            error_ =
                QStringLiteral("Outra instância usa este perfil ou a trava está indisponível.");
        return false;
    }
    QLocalServer::removeServer(name_);
    if (!server_.listen(name_)) {
        error_ = QStringLiteral("Não foi possível criar o canal local de ativação.");
        lock_->unlock();
        return false;
    }
    return true;
}
} // namespace nodivu
