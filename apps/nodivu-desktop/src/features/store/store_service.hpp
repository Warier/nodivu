#pragma once
#include "features/plugins/package_service.hpp"
#include "features/store/tuf_client.hpp"
#include <QImage>
#include <QJsonArray>
#include <memory>

namespace nodivu {
struct PackageStatus {
    QString pendingVersion;
    QString loadError;
    bool pending = false;
    bool hasPrevious = false;
};
struct StoreState {
    QVector<PackageRecord> installed;
    QHash<QString, PackageStatus> packageStatus;
    QJsonArray catalog;
    QHash<QString, QImage> logos;
    bool configured = false;
    bool online = false;
    QString message;
    QString error;
    QStringList warnings;
};
struct ReadmeContent {
    QString markdown;
    QString directory;
    QHash<QString, QString> images;
    QString error;
};
struct CatalogPresentation {
    bool updateAvailable = false;
    QString action;
    QString reason;
    QString detail;
};
CatalogPresentation catalogPresentation(const QJsonObject& entry, const StoreState& state);
QString resourceTierText(const QString& tier);
class StoreService {
  public:
    StoreService(QString profile, const QString& plugins, QByteArray bootstrap);
    StoreState initialize();
    StoreState refresh(TransferProgress* progress);
    StoreState install(const QString& id, const QString& expectedVersion,
                       TransferProgress* progress);
    ReadmeContent details(const QString& id, TransferProgress* progress);
    ReadmeContent installedDetails(const QString& key) const;
    StoreState snapshot(const PackageResult& result = {}) const;
    PackageService& packages() {
        return packages_;
    }
    QString cleanDownloads();
    static QString validateCatalog(const QJsonObject& catalog);

  private:
    QJsonObject entry(const QString& id) const;
    void loadLogos(bool online, TransferProgress* progress = nullptr);
    QString profile_;
    PackageService packages_;
    TufClient tuf_;
    QJsonArray catalog_;
    QHash<QString, QImage> logos_;
    bool initialized_ = false;
    bool configured_ = false;
    bool catalogAuthenticated_ = false;
    QString configurationError_;
    QHash<QString, bool> initialEnabled_;
};
} // namespace nodivu
