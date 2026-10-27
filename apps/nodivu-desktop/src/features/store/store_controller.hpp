#pragma once
#include "features/store/store_service.hpp"
#include "platform/app_paths.hpp"
#include <QObject>
#include <QThreadPool>
#include <QTimer>

namespace nodivu {
class StoreController final : public QObject {
    Q_OBJECT
  public:
    explicit StoreController(const AppPaths& paths, QObject* parent = nullptr);
    ~StoreController() override;
    const StoreState& state() const {
        return state_;
    }
    bool busy() const {
        return busy_;
    }
    bool committing() const {
        return progress_ && progress_->phase.load() == 2;
    }
    std::shared_ptr<TransferProgress> progress() const {
        return progress_;
    }
    void refresh(bool force = true);
    void refreshInstalled();
    void install(const QString& id, const QString& version);
    void importFolder(const QString& path);
    void importZip(const QString& path);
    void setEnabled(const QString& key, bool enabled);
    void markRemoved(const QString& key, bool removed);
    void cancelUpdate(const QString& key);
    void listVersions(const QString& key);
    void rollback(const QString& key, const QString& archivedKey);
    void cleanDownloads();
    void details(const QString& keyOrId, bool installed);
    void cancel();
  signals:
    void changed();
    void readmeReady(const nodivu::ReadmeContent& content);
    void versionsReady(const QString& key, const QVector<nodivu::PackageRecord>& versions);

  private:
    void run(std::function<StoreState(StoreService&)> work,
             std::shared_ptr<TransferProgress> progress = {});
    QThreadPool pool_;
    std::shared_ptr<StoreService> service_;
    std::shared_ptr<TransferProgress> progress_;
    StoreState state_;
    QDateTime lastAttempt_;
    QTimer periodic_;
    bool busy_ = false;
};
} // namespace nodivu
