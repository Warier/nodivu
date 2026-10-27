#pragma once
#include "contracts/operation_control.hpp"
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <atomic>

namespace nodivu {
struct PackageRecord {
    QString key;
    QString id;
    QString title;
    QString version;
    QString author;
    QString error;
    QString directory;
    bool enabled = true;
    bool removed = false;
    bool managed = false;
    QJsonObject manifest;
    bool ok() const {
        return error.isEmpty();
    }
};
struct PackageResult {
    QString error;
    QString message;
    bool ok() const {
        return error.isEmpty();
    }
};
class PackageService {
  public:
    explicit PackageService(const QString& plugins);
    static PackageRecord inspect(const QString& directory);
    QVector<PackageRecord> installed() const;
    QVector<PackageRecord> previous(const QString& key) const;
    QHash<QString, QString> pendingVersions(QString& error) const;
    static bool isProtected(const QString& key);
    PackageResult importFolder(const QString& folder, const std::atomic_bool* cancelled = nullptr,
                               OperationControl* operation = nullptr);
    PackageResult importZip(const QString& file, const std::atomic_bool* cancelled = nullptr,
                            const QString& expectedId = {}, const QString& expectedVersion = {},
                            OperationControl* operation = nullptr);
    PackageResult setEnabled(const QString& key, bool enabled);
    PackageResult markRemoved(const QString& key, bool removed);
    PackageResult applyPending();
    PackageResult prepareBundled(const QString& bundled);
    PackageResult migrateLegacy(const QString& legacyRoot);
    PackageResult cancelUpdate(const QString& key);
    PackageResult rollback(const QString& key, const QString& archivedKey);
    PackageResult cleanCache();
    QString pluginRoot() const {
        return root_;
    }
    static bool safeRelativePath(const QString& name);

  private:
    QString directoryFor(const QString& key) const;
    QString root_;
    QString profile_;
};
} // namespace nodivu
