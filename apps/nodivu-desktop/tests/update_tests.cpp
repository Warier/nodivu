#include "features/updates/update_service.hpp"
#include <QJsonDocument>
#include <QtTest>

class UpdateTests final : public QObject {
    Q_OBJECT
  private:
    static QByteArray manifest(const QString& version,
                               const QString& channel = QStringLiteral("stable")) {
        return QJsonDocument(
                   QJsonObject{
                       {QStringLiteral("schema_version"), 1},
                       {QStringLiteral("platform"), QStringLiteral("win32-x64")},
                       {QStringLiteral("channel"), channel},
                       {QStringLiteral("version"), version},
                       {QStringLiteral("installer"), QStringLiteral("windows/Nodivu-setup.exe")},
                       {QStringLiteral("bytes"), 123456},
                       {QStringLiteral("notes"), QStringLiteral("Native fixture")}})
            .toJson();
    }
  private slots:
    void versionAndChannelBoundaries() {
        auto offer = nodivu::UpdateService::parseOffer(manifest(QStringLiteral("0.2.1")),
                                                       QStringLiteral("0.2.1-dev.1"),
                                                       QStringLiteral("stable"));
        QVERIFY(offer.error.isEmpty());
        QVERIFY(offer.available);
        offer = nodivu::UpdateService::parseOffer(manifest(QStringLiteral("0.2.0")),
                                                  QStringLiteral("0.2.1-dev.1"),
                                                  QStringLiteral("stable"));
        QVERIFY(offer.error.isEmpty());
        QVERIFY(!offer.available);
        QVERIFY(!nodivu::UpdateService::parseOffer(manifest(QStringLiteral("0.3.0-beta.1")),
                                                   QStringLiteral("0.2.1"),
                                                   QStringLiteral("stable"))
                     .error.isEmpty());
        QVERIFY(!nodivu::UpdateService::parseOffer(manifest(QStringLiteral("0.3.0")),
                                                   QStringLiteral("0.2.1"), QStringLiteral("beta"))
                     .error.isEmpty());
        QVERIFY(nodivu::UpdateService::parseOffer(
                    manifest(QStringLiteral("0.3.0-beta.10"), QStringLiteral("beta")),
                    QStringLiteral("0.3.0-beta.2"), QStringLiteral("beta"))
                    .available);
    }
    void rejectsTraversalWrongPlatformAndUnboundedDownloads() {
        auto value = QJsonDocument::fromJson(manifest(QStringLiteral("0.3.0"))).object();
        for (const auto& path :
             {QStringLiteral("../installer-setup.exe"), QStringLiteral("C:/installer-setup.exe"),
              QStringLiteral("https://host/app-setup.exe")}) {
            value.insert(QStringLiteral("installer"), path);
            QVERIFY(!nodivu::UpdateService::parseOffer(QJsonDocument(value).toJson(),
                                                       QStringLiteral("0.2.1"),
                                                       QStringLiteral("stable"))
                         .error.isEmpty());
        }
        value = QJsonDocument::fromJson(manifest(QStringLiteral("0.3.0"))).object();
        value.insert(QStringLiteral("bytes"), 257LL * 1024 * 1024);
        QVERIFY(!nodivu::UpdateService::parseOffer(QJsonDocument(value).toJson(),
                                                   QStringLiteral("0.2.1"),
                                                   QStringLiteral("stable"))
                     .error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(UpdateTests)
#include "update_tests.moc"
