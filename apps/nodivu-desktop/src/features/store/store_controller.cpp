#include "features/store/store_controller.hpp"
#include <QFile>
#include <QFutureWatcher>
#include <QtConcurrentRun>

namespace nodivu {
StoreController::StoreController(const AppPaths& paths, QObject* parent) : QObject(parent) {
    pool_.setMaxThreadCount(1);
    QFile source(paths.storeSource);
    QByteArray bootstrap;
    if (source.open(QIODevice::ReadOnly) && source.size() <= 1024 * 1024)
        bootstrap = source.readAll();
    service_ = std::make_shared<StoreService>(paths.profile, paths.plugins, bootstrap);
    run([](StoreService& service) { return service.initialize(); });
    QTimer::singleShot(20000, this, [this] { refresh(false); });
    periodic_.setInterval(6 * 60 * 60 * 1000);
    connect(&periodic_, &QTimer::timeout, this, [this] { refresh(false); });
    periodic_.start();
}
StoreController::~StoreController() {
    if (progress_)
        progress_->cancel();
    pool_.waitForDone();
}
void StoreController::run(std::function<StoreState(StoreService&)> work,
                          std::shared_ptr<TransferProgress> progress) {
    if (busy_)
        return;
    busy_ = true;
    const bool cancellable = static_cast<bool>(progress);
    progress_ = progress ? std::move(progress) : std::make_shared<TransferProgress>();
    if (!cancellable)
        progress_->beginCommit();
    auto* watcher = new QFutureWatcher<StoreState>(this);
    connect(watcher, &QFutureWatcher<StoreState>::finished, this, [this, watcher] {
        state_ = watcher->result();
        watcher->deleteLater();
        busy_ = false;
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run(
        &pool_, [service = service_, work = std::move(work)] { return work(*service); }));
    emit changed();
}
void StoreController::refresh(bool force) {
    if (busy_ || (!force && lastAttempt_.isValid() &&
                  lastAttempt_.secsTo(QDateTime::currentDateTimeUtc()) < 15 * 60))
        return;
    lastAttempt_ = QDateTime::currentDateTimeUtc();
    auto progress = std::make_shared<TransferProgress>();
    run([progress](StoreService& service) { return service.refresh(progress.get()); }, progress);
}
void StoreController::refreshInstalled() {
    run([](StoreService& service) { return service.snapshot(); });
}
void StoreController::install(const QString& id, const QString& version) {
    auto progress = std::make_shared<TransferProgress>();
    run([id, version,
         progress](StoreService& service) { return service.install(id, version, progress.get()); },
        progress);
}
void StoreController::importFolder(const QString& path) {
    auto progress = std::make_shared<TransferProgress>();
    run(
        [path, progress](StoreService& service) {
            return service.snapshot(
                service.packages().importFolder(path, &progress->cancelled, progress.get()));
        },
        progress);
}
void StoreController::importZip(const QString& path) {
    auto progress = std::make_shared<TransferProgress>();
    run(
        [path, progress](StoreService& service) {
            return service.snapshot(
                service.packages().importZip(path, &progress->cancelled, {}, {}, progress.get()));
        },
        progress);
}
void StoreController::setEnabled(const QString& key, bool enabled) {
    run([key, enabled](StoreService& service) {
        return service.snapshot(service.packages().setEnabled(key, enabled));
    });
}
void StoreController::markRemoved(const QString& key, bool removed) {
    run([key, removed](StoreService& service) {
        return service.snapshot(service.packages().markRemoved(key, removed));
    });
}
void StoreController::cancelUpdate(const QString& key) {
    run([key](StoreService& service) {
        return service.snapshot(service.packages().cancelUpdate(key));
    });
}
void StoreController::rollback(const QString& key, const QString& archivedKey) {
    run([key, archivedKey](StoreService& service) {
        return service.snapshot(service.packages().rollback(key, archivedKey));
    });
}
void StoreController::listVersions(const QString& key) {
    if (busy_)
        return;
    busy_ = true;
    auto* watcher = new QFutureWatcher<QVector<PackageRecord>>(this);
    connect(watcher, &QFutureWatcher<QVector<PackageRecord>>::finished, this, [this, watcher, key] {
        const auto versions = watcher->result();
        watcher->deleteLater();
        busy_ = false;
        emit changed();
        emit versionsReady(key, versions);
    });
    watcher->setFuture(QtConcurrent::run(
        &pool_, [service = service_, key] { return service->packages().previous(key); }));
    emit changed();
}
void StoreController::cleanDownloads() {
    run([](StoreService& service) {
        return service.snapshot({service.cleanDownloads(),
                                 QStringLiteral("Downloads e staging não referenciado limpos.")});
    });
}
void StoreController::cancel() {
    if (progress_ && !progress_->cancel()) {
        state_.message =
            QStringLiteral("A instalação já está na cópia final; aguarde a conclusão.");
        emit changed();
    }
}
void StoreController::details(const QString& keyOrId, bool installed) {
    if (busy_)
        return;
    busy_ = true;
    progress_ = std::make_shared<TransferProgress>();
    auto* watcher = new QFutureWatcher<ReadmeContent>(this);
    connect(watcher, &QFutureWatcher<ReadmeContent>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        watcher->deleteLater();
        busy_ = false;
        emit readmeReady(result);
        emit changed();
    });
    watcher->setFuture(
        QtConcurrent::run(&pool_, [service = service_, progress = progress_, keyOrId, installed] {
            return installed ? service->installedDetails(keyOrId)
                             : service->details(keyOrId, progress.get());
        }));
    emit changed();
}
} // namespace nodivu
