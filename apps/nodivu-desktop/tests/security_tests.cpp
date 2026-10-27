#include "security/checked_json.hpp"
#include "security/tuf_metadata.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

class SecurityTests final : public QObject {
    Q_OBJECT
  private slots:
    void rejectsDuplicateKeysBeforeQtCanOverwriteThem() {
        QVERIFY(!nodivu::checkedJson(QByteArray(R"({"version":1,"version":2})"), 1024).ok());
        QVERIFY(!nodivu::checkedJson(QByteArray(R"({"x":{"a":1,"\u0061":2}})"), 1024).ok());
        QVERIFY(!nodivu::checkedJson(QByteArray(R"({"version":1.5})"), 1024, true).ok());
        QVERIFY(
            !nodivu::checkedJson(QByteArray(R"({"version":9007199254740993})"), 1024, true).ok());
        QVERIFY(nodivu::checkedJson(QByteArray(R"({"version":1,"controls":[{"a":2}]})"), 1024, true)
                    .ok());
    }
    void rejectsUnboundedOrMalformedDocuments() {
        QVERIFY(!nodivu::checkedJson(QByteArray("{\"x\":") + QByteArray(70, '[') + QByteArray("1") +
                                         QByteArray(70, ']') + QByteArray("}"),
                                     1024)
                     .ok());
        QVERIFY(!nodivu::checkedJson(QByteArray::fromHex("7b22ff223a317d"), 1024).ok());
        QVERIFY(!nodivu::checkedJson(QByteArray("{\"x\":\"\\ud800\"}"), 1024).ok());
        QVERIFY(!nodivu::checkedJson(QByteArray(R"({"x":1})"), 3).ok());
    }
    void verifiesRealBootstrapSignedByTheExistingPublisher() {
        QFile source(QStringLiteral(NODIVU_REPOSITORY_DIR) +
                     QStringLiteral("/apps/nodivu-desktop/resources/store/source.json"));
        QVERIFY(source.open(QIODevice::ReadOnly));
        const auto bootstrap = QJsonDocument::fromJson(source.readAll()).object();
        const auto root = nodivu::TufMetadata::decode(
            bootstrap.value(QStringLiteral("root")).toString().toUtf8(), QStringLiteral("root"));
        QVERIFY2(root.ok(), qPrintable(root.error));
        QVERIFY(nodivu::TufMetadata::verifyRole(root.object, root.object, QStringLiteral("root"))
                    .isEmpty());
        auto corrupted = root.object;
        auto signedData = corrupted.value(QStringLiteral("signed")).toObject();
        signedData.insert(QStringLiteral("version"), 2);
        corrupted.insert(QStringLiteral("signed"), signedData);
        QVERIFY(!nodivu::TufMetadata::verifyRole(corrupted, root.object, QStringLiteral("root"))
                     .isEmpty());
        QVERIFY(!nodivu::TufMetadata::verifyRootRotation(root.object, corrupted).isEmpty());
        QVERIFY(nodivu::TufMetadata::checkFreshness(
                    root.object, 1,
                    QDateTime::fromString(QStringLiteral("2026-10-16T00:00:00Z"), Qt::ISODate))
                    .isEmpty());
        QVERIFY(!nodivu::TufMetadata::checkFreshness(
                     root.object, 2,
                     QDateTime::fromString(QStringLiteral("2026-10-16T00:00:00Z"), Qt::ISODate))
                     .isEmpty());
        QVERIFY(!nodivu::TufMetadata::checkFreshness(
                     root.object, 1,
                     QDateTime::fromString(QStringLiteral("2030-01-01T00:00:00Z"), Qt::ISODate))
                     .isEmpty());
    }
    void enforcesSignedLengthAndHash() {
        const QByteArray file("authenticated target");
        const QJsonObject description{
            {QStringLiteral("length"), file.size()},
            {QStringLiteral("hashes"),
             QJsonObject{
                 {QStringLiteral("sha256"),
                  QString::fromLatin1(
                      QCryptographicHash::hash(file, QCryptographicHash::Sha256).toHex())}}}};
        QVERIFY(nodivu::TufMetadata::checkFile(file, description, 1024).isEmpty());
        QVERIFY(
            !nodivu::TufMetadata::checkFile(file + QByteArray("!"), description, 1024).isEmpty());
        QVERIFY(!nodivu::TufMetadata::checkFile(file, description, 2).isEmpty());
        QVERIFY(!nodivu::TufMetadata::checkFile(file, {}, 1024).isEmpty());
    }
};
QTEST_GUILESS_MAIN(SecurityTests)
#include "security_tests.moc"
