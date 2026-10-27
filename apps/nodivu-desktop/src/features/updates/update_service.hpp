#pragma once
#include "features/store/tuf_client.hpp"

namespace nodivu {
struct UpdateOffer {
    QString version;
    QString target;
    QString notes;
    QString installer;
    QString sha256;
    QString error;
    qint64 bytes = 0;
    bool available = false;
};
class UpdateService {
  public:
    UpdateService(QString profile, QByteArray bootstrap, QString currentVersion, QString channel,
                  bool allowLoopbackForTests = false);
    UpdateOffer check(TransferProgress* progress);
    UpdateOffer download(TransferProgress* progress);
    static UpdateOffer parseOffer(const QByteArray& contents, const QString& currentVersion,
                                  const QString& channel);

  private:
    TufClient repository_;
    QString profile_;
    QString currentVersion_;
    QString channel_;
    UpdateOffer offer_;
};
} // namespace nodivu
