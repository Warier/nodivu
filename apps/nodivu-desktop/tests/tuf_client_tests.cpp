#include "features/plugins/package_service.hpp"
#include "features/store/tuf_client.hpp"
#include "features/updates/update_service.hpp"
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtConcurrentRun>
#include <QtTest>

namespace {
class InertRepository final : public QObject {
  public:
    explicit InertRepository(const QString& name = QStringLiteral("tuf-repository.json")) {
        QFile input(QStringLiteral(NODIVU_REPOSITORY_DIR) +
                    QStringLiteral("/apps/nodivu-desktop/tests/fixtures/") + name);
        if (!input.open(QIODevice::ReadOnly))
            qFatal("Missing public TUF fixture");
        fixture = QJsonDocument::fromJson(input.readAll()).object();
        use(QStringLiteral("first"));
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    auto request = socket->property("request").toByteArray() + socket->readAll();
                    if (!request.contains("\r\n\r\n")) {
                        socket->setProperty("request", request);
                        return;
                    }
                    const auto parts = request.left(request.indexOf("\r\n")).split(' ');
                    const auto path = parts.size() >= 2 ? QString::fromUtf8(parts[1]) : QString();
                    const bool found = files.contains(path);
                    const auto bytes = files.value(path);
                    socket->write(found ? QByteArray("HTTP/1.1 200 OK\r\n")
                                        : QByteArray("HTTP/1.1 404 Not Found\r\n"));
                    socket->write(QByteArray("Content-Length: ") +
                                  QByteArray::number(bytes.size()) +
                                  QByteArray("\r\nConnection: close\r\n\r\n"));
                    socket->write(bytes);
                    socket->disconnectFromHost();
                });
            }
        });
        if (!server.listen(QHostAddress::LocalHost, 0))
            qFatal("Could not listen on isolated test server");
    }
    void use(const QString& revision) {
        files.clear();
        const auto entries = fixture.value(revision).toObject();
        for (auto it = entries.begin(); it != entries.end(); ++it)
            files.insert(it.key(), QByteArray::fromBase64(it.value().toString().toLatin1()));
    }
    QByteArray source() const {
        const auto base = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        return QJsonDocument(
                   QJsonObject{
                       {QStringLiteral("schema_version"), 1},
                       {QStringLiteral("root"),
                        QString::fromUtf8(QByteArray::fromBase64(
                            fixture.value(QStringLiteral("root")).toString().toLatin1()))},
                       {QStringLiteral("metadata_url"), base + QStringLiteral("/metadata/")},
                       {QStringLiteral("target_url"), base + QStringLiteral("/targets/")},
                       {QStringLiteral("official_publishers"),
                        QJsonArray{QStringLiteral("nodivu")}}})
            .toJson(QJsonDocument::Compact);
    }
    QTcpServer server;
    QJsonObject fixture;
    QHash<QString, QByteArray> files;
};
QString runIo(std::function<QString()> work) {
    QFutureWatcher<QString> watcher;
    QSignalSpy finished(&watcher, &QFutureWatcher<QString>::finished);
    watcher.setFuture(QtConcurrent::run(std::move(work)));
    if (!watcher.isFinished() && !finished.wait(15000))
        qFatal("Native TUF fixture operation timed out");
    return watcher.result();
}
} // namespace

class TufClientTests final : public QObject {
    Q_OBJECT
  private slots:
    void rootRotationConsistentSnapshotsDelegationAndApplicationUpdate() {
        QTemporaryDir profile;
        InertRepository repository(QStringLiteral("advanced-tuf.json"));
        nodivu::TufClient client(profile.path(), repository.source(), true);
        auto error = runIo([&] {
            auto failure = client.initialize();
            if (!failure.isEmpty())
                return failure;
            failure = client.refresh();
            if (!failure.isEmpty())
                return failure;
            const auto target = client.target(QStringLiteral("delegated/data.txt"), 1024);
            if (!target.ok())
                return target.error;
            return target.bytes == QByteArray("delegated authenticated content")
                       ? QString()
                       : QStringLiteral("Delegated fixture content differs");
        });
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!runIo([&] {
                     return client.target(QStringLiteral("outside/data.txt"), 1024).error;
                 }).isEmpty());
        nodivu::UpdateService updates(profile.path(), repository.source(),
                                      QStringLiteral("0.2.1-dev.1"), QStringLiteral("stable"),
                                      true);
        nodivu::UpdateOffer offer;
        error = runIo([&] {
            nodivu::TransferProgress progress;
            offer = updates.check(&progress);
            if (!offer.error.isEmpty())
                return offer.error;
            if (!offer.available)
                return QStringLiteral("Expected fixture update offer");
            offer = updates.download(&progress);
            return offer.error;
        });
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QFile installer(offer.installer);
        QVERIFY(installer.open(QIODevice::ReadOnly));
        QCOMPARE(installer.readAll(), QByteArray("INERT UPDATE FIXTURE; NOT AN EXECUTABLE"));
        QCOMPARE(offer.sha256.size(), 64);
        QVERIFY(offer.installer.startsWith(profile.path() + QStringLiteral("/updates/downloads/")));
    }
    void nativeClientAcceptsReferenceSignaturesAndInstallsInertZip() {
        QTemporaryDir profile;
        InertRepository repository;
        nodivu::TufClient client(profile.path(), repository.source(), true);
        auto error = runIo([&] {
            auto failure = client.initialize();
            if (!failure.isEmpty())
                return failure;
            failure = client.refresh();
            if (!failure.isEmpty())
                return failure;
            const auto catalog = client.target(QStringLiteral("catalog.json"), 1024 * 1024);
            if (!catalog.ok())
                return catalog.error;
            const auto package =
                client.target(QStringLiteral("org.example.test/1.0.0.zip"), 256 * 1024 * 1024);
            if (!package.ok())
                return package.error;
            nodivu::PackageService packages(
                QDir(profile.path()).filePath(QStringLiteral("plugins")));
            return packages.importZip(package.file).error;
        });
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(client.online());
        nodivu::PackageService packages(QDir(profile.path()).filePath(QStringLiteral("plugins")));
        QCOMPARE(packages.installed().size(), 1);
        QCOMPARE(packages.installed().first().id, QStringLiteral("org.example.test"));
    }
    void persistsVersionHistoryAndRejectsRollbackAndExpiredMetadata() {
        QTemporaryDir profile;
        InertRepository repository;
        nodivu::TufClient client(profile.path(), repository.source(), true);
        auto error = runIo([&] {
            auto init = client.initialize();
            return init.isEmpty() ? client.refresh() : init;
        });
        QVERIFY2(error.isEmpty(), qPrintable(error));
        repository.use(QStringLiteral("second"));
        error = runIo([&] { return client.refresh(); });
        QVERIFY2(error.isEmpty(), qPrintable(error));
        repository.use(QStringLiteral("first"));
        QVERIFY(!runIo([&] { return client.refresh(); }).isEmpty());
        QVERIFY(!client.online());
        nodivu::TufClient reopened(profile.path(), repository.source(), true);
        QVERIFY(runIo([&] { return reopened.initialize(); }).isEmpty());
        QVERIFY(!runIo([&] { return reopened.refresh(); }).isEmpty());
        repository.use(QStringLiteral("expired"));
        QVERIFY(!runIo([&] { return reopened.refresh(); }).isEmpty());
        QVERIFY(!reopened.online());
    }
    void refusesCorruptTargetAndSignature() {
        QTemporaryDir profile;
        InertRepository repository;
        nodivu::TufClient client(profile.path(), repository.source(), true);
        QVERIFY(runIo([&] { return client.initialize(); }).isEmpty());
        QVERIFY(runIo([&] { return client.refresh(); }).isEmpty());
        repository.files[QStringLiteral("/targets/catalog.json")].append('!');
        QVERIFY(!runIo([&] {
                     return client.target(QStringLiteral("catalog.json"), 1024 * 1024).error;
                 }).isEmpty());
        auto timestamp = QJsonDocument::fromJson(
                             repository.files.value(QStringLiteral("/metadata/timestamp.json")))
                             .object();
        auto signedData = timestamp.value(QStringLiteral("signed")).toObject();
        signedData.insert(QStringLiteral("version"), 90);
        timestamp.insert(QStringLiteral("signed"), signedData);
        repository.files[QStringLiteral("/metadata/timestamp.json")] =
            QJsonDocument(timestamp).toJson();
        QVERIFY(!runIo([&] { return client.refresh(); }).isEmpty());
    }
    void productionPolicyRejectsHttpAndOfflineInstallation() {
        QTemporaryDir profile;
        InertRepository repository;
        nodivu::TufClient production(profile.path(), repository.source());
        QVERIFY(!runIo([&] { return production.initialize(); }).isEmpty());
        nodivu::TufClient client(profile.path(), repository.source(), true);
        QVERIFY(runIo([&] { return client.initialize(); }).isEmpty());
        QVERIFY(!runIo([&] {
                     return client.target(QStringLiteral("catalog.json"), 1024 * 1024).error;
                 }).isEmpty());
    }
};
QTEST_GUILESS_MAIN(TufClientTests)
#include "tuf_client_tests.moc"
