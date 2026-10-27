#pragma once
#include "contracts/operation_control.hpp"
#include <QByteArray>
#include <QJsonObject>
#include <QUrl>
#include <atomic>

namespace nodivu {
using TransferProgress = OperationControl;
struct TransferResult {
    QByteArray bytes;
    QString file;
    QString error;
    int status = 0;
    QJsonObject authenticated;
    bool ok() const {
        return error.isEmpty() && status == 200;
    }
};
class NetworkTransfer {
  public:
    // Synchronous only on a bounded I/O worker, never on the widget thread.
    static TransferResult get(const QUrl& url, qint64 maximumBytes,
                              TransferProgress* progress = nullptr, bool allowLoopback = false,
                              const QString& outputFile = {},
                              const QJsonObject& authenticatedDescription = {});
};
} // namespace nodivu
