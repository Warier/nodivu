#include "features/projects/project_repository.hpp"
#include "platform/profile_backup.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

class ProjectTests final : public QObject {
    Q_OBJECT
  private slots:
    void profileBackupPreservesDocumentsAndExistingTrustMetadata() {
        QTemporaryDir directory;
        const QDir profile(directory.path());
        const auto recents = profile.filePath(QStringLiteral("recent-projects.json"));
        QVERIFY(nodivu::ProjectRepository::write(recents, {{QStringLiteral("version"), 1},
                                                           {QStringLiteral("files"), QJsonArray{}}})
                    .ok());
        const auto metadata =
            profile.filePath(QStringLiteral("plugin-catalog/fixture/metadata/root.json"));
        QVERIFY(
            nodivu::ProjectRepository::write(metadata, {{QStringLiteral("fixture"), true}}).ok());
        const auto original = nodivu::ProjectRepository::read(metadata).contents;
        QVERIFY(nodivu::prepareQtProfileBackup(directory.path()).isEmpty());
        const auto marker = nodivu::ProjectRepository::read(
            profile.filePath(QStringLiteral("migration/qt-profile-v1.json")));
        QVERIFY(marker.ok());
        const auto record = QJsonDocument::fromJson(marker.contents).object();
        QCOMPARE(record.value(QStringLiteral("files")).toArray().size(), 2);
        const auto copy = profile.filePath(
            QStringLiteral("migration/") + record.value(QStringLiteral("directory")).toString() +
            QStringLiteral("/plugin-catalog/fixture/metadata/root.json"));
        QCOMPARE(nodivu::ProjectRepository::read(copy).contents, original);
        QCOMPARE(nodivu::ProjectRepository::read(metadata).contents, original);
        QVERIFY(nodivu::prepareQtProfileBackup(directory.path()).isEmpty());
    }
    void refusesOversizeWithoutDestroyingOriginal() {
        QTemporaryDir directory;
        const auto path = QDir(directory.path()).filePath(QStringLiteral("Voz.nodivu.json"));
        QVERIFY(nodivu::ProjectRepository::write(path, {{QStringLiteral("version"), 1}}).ok());
        const auto original = nodivu::ProjectRepository::read(path).contents;
        const auto result = nodivu::ProjectRepository::write(
            path, {{QStringLiteral("too_big"), QString(65536, QLatin1Char('x'))}});
        QVERIFY(!result.ok());
        QCOMPARE(nodivu::ProjectRepository::read(path).contents, original);
    }
    void refusesInvalidUtf8() {
        QTemporaryDir directory;
        const auto path = QDir(directory.path()).filePath(QStringLiteral("invalid.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::fromHex("7b22ff223a317d"));
        file.close();
        QVERIFY(!nodivu::ProjectRepository::read(path).ok());
        QVERIFY(QFileInfo::exists(path));
    }
    void resourcePathsRoundTrip() {
        QTemporaryDir directory;
        const auto file = QDir(directory.path()).filePath(QStringLiteral("Projeto.json"));
        const auto audio = QDir(directory.path()).filePath(QStringLiteral("Voz/áudio.wav"));
        const QJsonObject project{
            {QStringLiteral("resources"),
             QJsonArray{QJsonObject{{QStringLiteral("node_id"), QStringLiteral("uuid")},
                                    {QStringLiteral("path"), audio}}}}};
        const auto saved = nodivu::ProjectRepository::resolveResources(project, file, false);
        QCOMPARE(saved.value(QStringLiteral("resources"))
                     .toArray()
                     .first()
                     .toObject()
                     .value(QStringLiteral("path"))
                     .toString(),
                 QStringLiteral("Voz/áudio.wav"));
        QCOMPARE(nodivu::ProjectRepository::resolveResources(saved, file, true), project);
    }
};
QTEST_GUILESS_MAIN(ProjectTests)
#include "project_tests.moc"
