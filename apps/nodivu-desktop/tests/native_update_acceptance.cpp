// Real installer handoff against an ephemeral, loopback-only test authority.
// This target is never packaged and never creates production update keys.
#include "features/updates/update_service.hpp"
#include "security/checked_json.hpp"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QtConcurrentRun>
#include <array>
#include <functional>
#include <memory>
#include <stdexcept>
// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on
extern "C" {
#include "monocypher-ed25519.h"
#include "monocypher.h"
}
namespace {
void require(bool success, const char* message) {
    if (!success)
        throw std::runtime_error(message);
}
bool wait(const std::function<bool()>& ready, int timeout = 60000) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(10);
    }
    return ready();
}
QByteArray read(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error((QStringLiteral("Test input unavailable: ") + path +
                                  QStringLiteral(": ") + file.errorString())
                                     .toStdString());
    require(file.size() <= 256LL * 1024 * 1024, "test input too large");
    return file.readAll();
}
QJsonObject description(const QByteArray& bytes, bool metadata = false) {
    QJsonObject result{
        {QStringLiteral("length"), bytes.size()},
        {QStringLiteral("hashes"),
         QJsonObject{{QStringLiteral("sha256"),
                      QString::fromLatin1(
                          QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}}}};
    if (metadata)
        result.insert(QStringLiteral("version"), 1);
    return result;
}
struct Key {
    std::array<uint8_t, 64> secret{};
    std::array<uint8_t, 32> publicKey{};
    Key() {
        std::array<uint8_t, 32> seed{};
        require(BCryptGenRandom(nullptr, seed.data(), static_cast<ULONG>(seed.size()),
                                BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0,
                "test key RNG failed");
        crypto_ed25519_key_pair(secret.data(), publicKey.data(), seed.data());
        crypto_wipe(seed.data(), seed.size());
    }
    ~Key() {
        crypto_wipe(secret.data(), secret.size());
    }
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
    QJsonObject publicValue() const {
        return {
            {QStringLiteral("keytype"), QStringLiteral("ed25519")},
            {QStringLiteral("scheme"), QStringLiteral("ed25519")},
            {QStringLiteral("keyval"),
             QJsonObject{
                 {QStringLiteral("public"),
                  QString::fromLatin1(
                      QByteArray(reinterpret_cast<const char*>(publicKey.data()), 32).toHex())}}}};
    }
    QString id() const {
        return QString::fromLatin1(QCryptographicHash::hash(nodivu::canonicalJson(publicValue()),
                                                            QCryptographicHash::Sha256)
                                       .toHex());
    }
    QByteArray sign(const QJsonObject& data) const {
        const auto message = nodivu::canonicalJson(data);
        std::array<uint8_t, 64> signature{};
        crypto_ed25519_sign(signature.data(), secret.data(),
                            reinterpret_cast<const uint8_t*>(message.constData()),
                            static_cast<size_t>(message.size()));
        return QJsonDocument(
                   QJsonObject{
                       {QStringLiteral("signed"), data},
                       {QStringLiteral("signatures"),
                        QJsonArray{QJsonObject{
                            {QStringLiteral("keyid"), id()},
                            {QStringLiteral("sig"),
                             QString::fromLatin1(
                                 QByteArray(reinterpret_cast<const char*>(signature.data()), 64)
                                     .toHex())}}}}})
            .toJson(QJsonDocument::Compact);
    }
};
class Repository final : public QObject {
  public:
    QTcpServer server;
    QHash<QString, QByteArray> files;
    QByteArray root;
    Repository(const QByteArray& installer, const QString& version) {
        std::array<Key, 4> keys;
        QJsonObject publicKeys, roles;
        const QStringList names{QStringLiteral("root"), QStringLiteral("targets"),
                                QStringLiteral("snapshot"), QStringLiteral("timestamp")};
        for (int i = 0; i < 4; ++i) {
            publicKeys.insert(keys[i].id(), keys[i].publicValue());
            roles.insert(names[i], QJsonObject{{QStringLiteral("keyids"), QJsonArray{keys[i].id()}},
                                               {QStringLiteral("threshold"), 1}});
        }
        const auto base = [](const QString& type) {
            return QJsonObject{{QStringLiteral("_type"), type},
                               {QStringLiteral("version"), 1},
                               {QStringLiteral("spec_version"), QStringLiteral("1.0.31")},
                               {QStringLiteral("expires"),
                                QDateTime::currentDateTimeUtc().addDays(1).toString(Qt::ISODate)}};
        };
        auto data = base(QStringLiteral("root"));
        data.insert(QStringLiteral("keys"), publicKeys);
        data.insert(QStringLiteral("roles"), roles);
        data.insert(QStringLiteral("consistent_snapshot"), false);
        root = keys[0].sign(data);
        const auto offer =
            QJsonDocument(
                QJsonObject{
                    {QStringLiteral("schema_version"), 1},
                    {QStringLiteral("platform"), QStringLiteral("win32-x64")},
                    {QStringLiteral("version"), version},
                    {QStringLiteral("channel"), QStringLiteral("beta")},
                    {QStringLiteral("installer"), QStringLiteral("windows/Nodivu-setup.exe")},
                    {QStringLiteral("bytes"), installer.size()},
                    {QStringLiteral("notes"),
                     QStringLiteral("Isolated reinstall acceptance; not a published release")}})
                .toJson();
        data = base(QStringLiteral("targets"));
        data.insert(
            QStringLiteral("targets"),
            QJsonObject{{QStringLiteral("windows-x64/beta.json"), description(offer)},
                        {QStringLiteral("windows/Nodivu-setup.exe"), description(installer)}});
        const auto targets = keys[1].sign(data);
        data = base(QStringLiteral("snapshot"));
        data.insert(QStringLiteral("meta"),
                    QJsonObject{{QStringLiteral("targets.json"), description(targets, true)}});
        const auto snapshot = keys[2].sign(data);
        data = base(QStringLiteral("timestamp"));
        data.insert(QStringLiteral("meta"),
                    QJsonObject{{QStringLiteral("snapshot.json"), description(snapshot, true)}});
        files = {{QStringLiteral("/metadata/timestamp.json"), keys[3].sign(data)},
                 {QStringLiteral("/metadata/snapshot.json"), snapshot},
                 {QStringLiteral("/metadata/targets.json"), targets},
                 {QStringLiteral("/targets/windows-x64/beta.json"), offer},
                 {QStringLiteral("/targets/windows/Nodivu-setup.exe"), installer}};
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    auto request = socket->property("request").toByteArray() + socket->readAll();
                    if (request.size() > 8192) {
                        socket->abort();
                        return;
                    }
                    if (!request.contains("\r\n\r\n")) {
                        socket->setProperty("request", request);
                        return;
                    }
                    const auto parts = request.left(request.indexOf("\r\n")).split(' ');
                    const auto path = parts.size() >= 2 ? QString::fromUtf8(parts[1]) : QString();
                    const auto bytes = files.value(path);
                    socket->write(files.contains(path) ? "HTTP/1.1 200 OK\r\n"
                                                       : "HTTP/1.1 404 Not Found\r\n");
                    socket->write(QByteArray("Content-Length: ") +
                                  QByteArray::number(bytes.size()) +
                                  QByteArray("\r\nConnection: close\r\n\r\n"));
                    socket->write(bytes);
                    socket->disconnectFromHost();
                });
            }
        });
        require(server.listen(QHostAddress::LocalHost, 0), "test server failed");
    }
    QByteArray source() const {
        const auto base = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        return QJsonDocument(
                   QJsonObject{
                       {QStringLiteral("schema_version"), 1},
                       {QStringLiteral("root"), QString::fromUtf8(root)},
                       {QStringLiteral("official_publishers"), QJsonArray{}},
                       {QStringLiteral("metadata_url"), base + QStringLiteral("/metadata/")},
                       {QStringLiteral("target_url"), base + QStringLiteral("/targets/")}})
            .toJson();
    }
};
void run(const QCommandLineParser& parser, QJsonObject& evidence) {
    const auto directory =
        QFileInfo(parser.value(QStringLiteral("install-dir"))).canonicalFilePath();
    QSettings settings(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Nodivu.QtMigration.IsolatedTest"),
        QSettings::NativeFormat);
    require(directory.contains(QStringLiteral("/.local/qt-install-test-")) &&
                QFileInfo(settings.value(QStringLiteral("InstallLocation")).toString())
                        .canonicalFilePath() == directory,
            "only an existing isolated migration-test installation is allowed");
    const auto installer = QFileInfo(parser.value(QStringLiteral("installer")));
    require(installer.fileName() == QStringLiteral("Nodivu-isolated-install-test.exe"),
            "isolated installer required");
    auto profile = std::make_shared<QTemporaryDir>();
    require(profile->isValid(), "temporary update profile failed");
    Repository repository(read(installer.absoluteFilePath()),
                          parser.value(QStringLiteral("version")));
    evidence.insert(QStringLiteral("stage"), QStringLiteral("authenticated_download"));
    auto service = std::make_shared<nodivu::UpdateService>(profile->path(), repository.source(),
                                                           QStringLiteral("0.2.0"),
                                                           QStringLiteral("beta"), true);
    auto pending = std::make_shared<nodivu::UpdateOffer>();
    QFutureWatcher<void> watcher;
    watcher.setFuture(QtConcurrent::run([service, pending, profile] {
        nodivu::TransferProgress progress;
        *pending = service->check(&progress);
        if (pending->error.isEmpty() && pending->available)
            *pending = service->download(&progress);
    }));
    require(wait([&] { return watcher.isFinished(); }), "authenticated download timed out");
    const auto offer = *pending;
    if (!offer.error.isEmpty())
        throw std::runtime_error(offer.error.toStdString());
    require(offer.available && !offer.installer.isEmpty(), "no authenticated installer offer");
    require(read(offer.installer) == read(installer.absoluteFilePath()),
            "download differs from installer");
    const auto receipt = QDir(directory).filePath(QStringLiteral("migration-receipt.json"));
    const auto previous = read(receipt);
    evidence.insert(QStringLiteral("stage"), QStringLiteral("parent_shutdown"));
    const auto screenshot = QDir(profile->path()).filePath(QStringLiteral("parent-ready.png"));
    QProcess parent;
    parent.start(QDir(directory).filePath(QStringLiteral("nodivu.exe")),
                 {QStringLiteral("--portable"), QStringLiteral("--smoke-test"),
                  QStringLiteral("--benchmark-ms"), QStringLiteral("6000"),
                  QStringLiteral("--screenshot"), screenshot});
    require(parent.waitForStarted(3000) &&
                wait([&] { return QFileInfo::exists(screenshot); }, 15000),
            "installed Qt parent did not become ready");
    const auto pid = parent.processId();
    QProcess helper;
    helper.start(QDir(directory).filePath(QStringLiteral("nodivu-update-helper.exe")),
                 {QString::number(pid), offer.installer, offer.sha256});
    require(helper.waitForStarted(3000), "native helper did not start");
    require(wait([&] { return parent.state() == QProcess::NotRunning; }, 20000) &&
                parent.exitCode() == 0,
            "installed parent did not shut down normally");
    require(wait([&] { return helper.state() == QProcess::NotRunning; }, 15000) &&
                helper.exitCode() == 0,
            "helper failed after parent exit");
    evidence.insert(QStringLiteral("stage"), QStringLiteral("installation_receipt"));
    require(wait([&] {
                // A directory swap can occur between exists() and open(). A missing
                // receipt is transient until the bounded installation deadline.
                QFile marker(receipt);
                if (!marker.open(QIODevice::ReadOnly))
                    return false;
                require(marker.size() <= 64 * 1024, "installation receipt exceeds limit");
                const auto bytes = marker.readAll();
                const auto value = QJsonDocument::fromJson(bytes).object();
                return bytes != previous &&
                       value.value(QStringLiteral("schema_version")).toInt() == 1 &&
                       value.value(QStringLiteral("previous_frontend")).toString() ==
                           QStringLiteral("Qt");
            }),
            "real installer did not commit a new transaction");
    evidence.insert(QStringLiteral("version"), offer.version);
    evidence.insert(QStringLiteral("installer_sha256"), offer.sha256);
    evidence.insert(QStringLiteral("installer_bytes"), offer.bytes);
    evidence.insert(QStringLiteral("parent_exit_code"), parent.exitCode());
    evidence.insert(QStringLiteral("helper_exit_code"), helper.exitCode());
    evidence.insert(QStringLiteral("receipt"), QJsonDocument::fromJson(read(receipt)).object());
    evidence.insert(QStringLiteral("scope"),
                    QStringLiteral("Ephemeral authority, loopback transport, real NSIS reinstall, "
                                   "isolated test identity; no public update channel"));
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    for (const auto& option : {QStringLiteral("install-dir"), QStringLiteral("installer"),
                               QStringLiteral("version"), QStringLiteral("evidence")})
        parser.addOption({option, option, QStringLiteral("value")});
    parser.process(app);
    QJsonObject evidence{{QStringLiteral("status"), QStringLiteral("failed")}};
    try {
        run(parser, evidence);
        evidence.insert(QStringLiteral("status"), QStringLiteral("passed"));
    } catch (const std::exception& error) {
        evidence.insert(QStringLiteral("error"), QString::fromUtf8(error.what()));
    }
    QFile output(parser.value(QStringLiteral("evidence")));
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return 2;
    const auto bytes = QJsonDocument(evidence).toJson();
    if (output.write(bytes) != bytes.size())
        return 2;
    return evidence.value(QStringLiteral("status")).toString() == QStringLiteral("passed") ? 0 : 1;
}
