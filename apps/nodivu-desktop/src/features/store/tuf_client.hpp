#pragma once
#include "features/store/network_transfer.hpp"
#include "security/checked_json.hpp"
#include <QDateTime>
#include <QSet>

namespace nodivu {
// A native client for the existing repository. No signing keys belong in this class.
class TufClient {
  public:
    TufClient(QString profile, QByteArray bootstrap, bool allowLoopback = false);
    QString initialize();
    QString refresh(TransferProgress* progress = nullptr);
    TransferResult target(const QString& name, qint64 maximumBytes,
                          TransferProgress* progress = nullptr, qint64 expectedLength = -1);
    bool online() const {
        return online_;
    }
    QString directory() const {
        return directory_;
    }
    QStringList officialPublishers() const;
    TransferResult cachedTargetForDisplay(const QString& name, qint64 maximumBytes) const;
    static QByteArray compactBootstrap(const QByteArray& bytes);

  private:
    QString persist();
    JsonResult fetchRole(const QString& role, const QString& file, const QJsonObject& description,
                         qint64 maximumBytes, const QJsonObject& authority,
                         TransferProgress* progress);
    JsonResult findTarget(const QJsonObject& role, const QString& target, QSet<QString>& visited,
                          TransferProgress* progress, int depth = 0);
    QString validateUrls() const;
    QString profile_;
    QByteArray bootstrapBytes_;
    QJsonObject source_;
    QJsonObject state_;
    QString directory_;
    QDateTime updateStart_;
    bool allowLoopback_ = false;
    bool online_ = false;
};
} // namespace nodivu
