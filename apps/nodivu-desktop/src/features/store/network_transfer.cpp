#include "features/store/network_transfer.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QThread>
#include <QTimer>

namespace nodivu {
TransferResult NetworkTransfer::get(const QUrl& source, qint64 maximumBytes,
                                    TransferProgress* progress, bool allowLoopback,
                                    const QString& outputFile,
                                    const QJsonObject& authenticatedDescription) {
    TransferResult result;
    if (QCoreApplication::instance() &&
        QThread::currentThread() == QCoreApplication::instance()->thread()) {
        result.error = QStringLiteral("Transferência de rede não pode bloquear a interface.");
        return result;
    }
    QUrl url = source;
    for (int redirect = 0; redirect <= 5; ++redirect) {
        if (!url.isValid() || !url.userInfo().isEmpty() || !url.fragment().isEmpty() ||
            (url.scheme() != QStringLiteral("https") &&
             !(allowLoopback && url.scheme() == QStringLiteral("http") &&
               url.host() == QStringLiteral("127.0.0.1")))) {
            result.error = QStringLiteral("A origem precisa usar HTTPS sem credenciais.");
            return result;
        }
        QNetworkAccessManager manager;
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(30000);
        auto* reply = manager.get(request);
        reply->setReadBufferSize(1024 * 1024);
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        deadline.setInterval(120000);
        QTimer cancellation;
        cancellation.setInterval(50);
        QSaveFile output(outputFile);
        output.setDirectWriteFallback(false);
        qint64 received = 0;
        QCryptographicHash sha256(QCryptographicHash::Sha256);
        QCryptographicHash sha512(QCryptographicHash::Sha512);
        QByteArray buffer;
        QString failure;
        bool outputOpened = false;
        const auto consume = [&] {
            const auto bytes = reply->readAll();
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
                return;
            if (received > maximumBytes - bytes.size()) {
                failure = QStringLiteral("Download excedeu o limite autenticado.");
                reply->abort();
                return;
            }
            received += bytes.size();
            if (progress) {
                progress->received.store(received);
                progress->expected.store(authenticatedDescription.value(QStringLiteral("length"))
                                             .toInteger(maximumBytes));
            }
            sha256.addData(bytes);
            sha512.addData(bytes);
            if (outputFile.isEmpty()) {
                buffer.append(bytes);
            } else {
                if (!outputOpened) {
                    outputOpened = output.open(QIODevice::WriteOnly);
                    if (!outputOpened)
                        failure = output.errorString();
                }
                if (outputOpened && output.write(bytes) != bytes.size())
                    failure = output.errorString();
                if (!failure.isEmpty())
                    reply->abort();
            }
        };
        QObject::connect(reply, &QNetworkReply::readyRead, &loop, consume);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
            failure = QStringLiteral("Tempo de download excedido.");
            reply->abort();
        });
        QObject::connect(&cancellation, &QTimer::timeout, &loop, [&] {
            if (progress && progress->cancelled.load()) {
                failure = QStringLiteral("Download cancelado.");
                reply->abort();
            }
        });
        deadline.start();
        cancellation.start();
        loop.exec();
        consume();
        result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (result.status >= 300 && result.status < 400) {
            const auto next = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
            if (!next.isValid() || next.isEmpty()) {
                result.error = QStringLiteral("Redirecionamento inválido.");
                return result;
            }
            url = url.resolved(next);
            continue;
        }
        if (!failure.isEmpty() || result.status != 200 ||
            reply->error() != QNetworkReply::NoError) {
            result.error =
                !failure.isEmpty()
                    ? failure
                    : QStringLiteral("HTTP %1: %2").arg(result.status).arg(reply->errorString());
            return result;
        }
        if (!authenticatedDescription.isEmpty()) {
            if (authenticatedDescription.value(QStringLiteral("length")).toInteger(-1) !=
                received) {
                result.error = QStringLiteral("Tamanho autenticado incompatível.");
                return result;
            }
            const auto hashes = authenticatedDescription.value(QStringLiteral("hashes")).toObject();
            bool supported = false;
            for (const auto& name : {QStringLiteral("sha256"), QStringLiteral("sha512")}) {
                if (!hashes.contains(name))
                    continue;
                const auto actual = name == QStringLiteral("sha256") ? sha256.result().toHex()
                                                                     : sha512.result().toHex();
                if (hashes.value(name).toString().toLatin1().toLower() != actual) {
                    result.error =
                        QStringLiteral("Hash do download não corresponde aos metadados assinados.");
                    return result;
                }
                supported = true;
            }
            if (!supported) {
                result.error = QStringLiteral("Download sem hash autenticado suportado.");
                return result;
            }
        }
        if (!outputFile.isEmpty()) {
            if (!outputOpened)
                outputOpened = output.open(QIODevice::WriteOnly);
            if (!outputOpened || !output.commit()) {
                result.error = QStringLiteral("Não foi possível publicar o download verificado.");
                return result;
            }
            result.file = outputFile;
        }
        result.bytes = buffer;
        return result;
    }
    result.error = QStringLiteral("Redirecionamentos demais.");
    return result;
}
} // namespace nodivu
