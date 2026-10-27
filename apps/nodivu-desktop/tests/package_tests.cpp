#include "features/plugins/package_service.hpp"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
extern "C" {
#include "miniz.h"
}

namespace {
void writeFixture(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        qFatal("Could not create isolated test fixture");
}
QString inertPackage(const QString& parent, const QString& version) {
    const auto folder = QDir(parent).filePath(QStringLiteral("source-") + version);
    QDir().mkpath(folder);
    const QJsonObject manifest{{QStringLiteral("schema_version"), 1},
                               {QStringLiteral("plugin_id"), QStringLiteral("org.example.inert")},
                               {QStringLiteral("title"), QStringLiteral("Inert test package")},
                               {QStringLiteral("library"), QStringLiteral("inert.clap")}};
    const QJsonObject envelope{{QStringLiteral("schema_version"), 1},
                               {QStringLiteral("plugin_id"), QStringLiteral("org.example.inert")},
                               {QStringLiteral("version"), version},
                               {QStringLiteral("author"), QStringLiteral("Test only")}};
    writeFixture(QDir(folder).filePath(QStringLiteral("plugin.json")),
                 QJsonDocument(manifest).toJson());
    writeFixture(QDir(folder).filePath(QStringLiteral("nodivu-package.json")),
                 QJsonDocument(envelope).toJson());
    // Never loaded by the backend or GUI: package tests exercise files only.
    writeFixture(QDir(folder).filePath(QStringLiteral("inert.clap")),
                 QByteArray("not executable; isolated fixture"));
    return folder;
}
} // namespace
class PackageTests final : public QObject {
    Q_OBJECT
  private slots:
    void cancellationAndCommitAreMutuallyExclusive() {
        QTemporaryDir directory;
        const auto source = inertPackage(directory.path(), QStringLiteral("1.0.0"));
        nodivu::PackageService packages(
            QDir(directory.path()).filePath(QStringLiteral("profile/plugins")));
        nodivu::OperationControl cancelled;
        QVERIFY(cancelled.cancel());
        QVERIFY(!packages.importFolder(source, &cancelled.cancelled, &cancelled).ok());
        QVERIFY(packages.installed().isEmpty());
        nodivu::OperationControl active;
        QVERIFY(packages.importFolder(source, &active.cancelled, &active).ok());
        QCOMPARE(active.phase.load(), 2);
        QVERIFY(!active.cancel());
        QVERIFY(!active.cancelled.load());
        QCOMPARE(packages.installed().size(), 1);
    }
    void rejectsWindowsTraversalAndAmbiguousNames() {
        for (const auto& path :
             {QStringLiteral("../escape"), QStringLiteral("C:/escape"),
              QStringLiteral("folder/../../escape"), QStringLiteral("folder\\escape"),
              QStringLiteral("CON.txt"), QStringLiteral("folder/name:stream"),
              QStringLiteral("folder/name."), QStringLiteral("//server/share")})
            QVERIFY2(!nodivu::PackageService::safeRelativePath(path), qPrintable(path));
        QVERIFY(nodivu::PackageService::safeRelativePath(QStringLiteral("images/logo.png")));
    }
    void updatesAreDeferredRecoverableAndPreserveActivation() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto first = inertPackage(directory.path(), QStringLiteral("1.0.0"));
        const auto next = inertPackage(directory.path(), QStringLiteral("1.1.0"));
        nodivu::PackageService packages(
            QDir(directory.path()).filePath(QStringLiteral("profile/plugins")));
        QVERIFY(packages.importFolder(first).ok());
        auto installed = packages.installed();
        QCOMPARE(installed.size(), 1);
        const auto key = installed.first().key;
        QVERIFY(packages.setEnabled(key, false).ok());
        QVERIFY(packages.importFolder(next).ok());
        QCOMPARE(packages.installed().first().version, QStringLiteral("1.0.0"));
        QVERIFY(!packages.importFolder(next).ok());
        QVERIFY(packages.applyPending().ok());
        installed = packages.installed();
        QCOMPARE(installed.first().version, QStringLiteral("1.1.0"));
        QVERIFY(!installed.first().enabled);
        const auto previous = packages.previous(key);
        QCOMPARE(previous.size(), 1);
        QCOMPARE(previous.first().version, QStringLiteral("1.0.0"));
        QVERIFY(packages.rollback(key, previous.first().key).ok());
        QVERIFY(packages.applyPending().ok());
        QCOMPARE(packages.installed().first().version, QStringLiteral("1.0.0"));
        QVERIFY(!packages.installed().first().enabled);
    }
    void legacyMigrationPreservesDisabledPackageAndDoesNotReplaceProfileVersion() {
        QTemporaryDir directory;
        const auto source = inertPackage(directory.path(), QStringLiteral("1.0.0"));
        const auto legacy = QDir(directory.path()).filePath(QStringLiteral("legacy"));
        QVERIFY(QDir().mkpath(legacy));
        QVERIFY(QDir().rename(source, QDir(legacy).filePath(QStringLiteral("custom"))));
        writeFixture(QDir(legacy).filePath(QStringLiteral("custom/.nodivu-disabled")),
                     QByteArray("disabled\n"));
        nodivu::PackageService packages(
            QDir(directory.path()).filePath(QStringLiteral("profile/plugins")));
        auto result = packages.migrateLegacy(legacy);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(packages.installed().size(), 1);
        QVERIFY(!packages.installed().first().enabled);
        QCOMPARE(packages.installed().first().version, QStringLiteral("1.0.0"));
        result = packages.migrateLegacy(legacy);
        QVERIFY(result.ok());
        QCOMPARE(packages.installed().size(), 1);
        const auto newer = inertPackage(directory.path(), QStringLiteral("2.0.0"));
        result = packages.importFolder(newer);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QVERIFY(packages.applyPending().ok());
        QVERIFY(packages.migrateLegacy(legacy).ok());
        QCOMPARE(packages.installed().size(), 1);
        QCOMPARE(packages.installed().first().version, QStringLiteral("2.0.0"));
        QVERIFY(QFileInfo::exists(QDir(legacy).filePath(QStringLiteral("custom/plugin.json"))));
    }
    void corruptedJournalBlocksDestructiveCleanup() {
        QTemporaryDir directory;
        const auto profile = QDir(directory.path()).filePath(QStringLiteral("profile"));
        QDir().mkpath(QDir(profile).filePath(QStringLiteral("plugin-updates")));
        const auto journal = QDir(profile).filePath(QStringLiteral("plugin-updates/bad.json"));
        writeFixture(journal, QByteArray(R"({"schema":1,"key":"../outside"})"));
        nodivu::PackageService packages(QDir(profile).filePath(QStringLiteral("plugins")));
        QVERIFY(!packages.applyPending().ok());
        QVERIFY(!packages.cleanCache().ok());
        QVERIFY(QFileInfo::exists(journal));
    }
    void rejectsZipTraversalBeforeExtracting() {
        QTemporaryDir directory;
        const auto zip = QDir(directory.path()).filePath(QStringLiteral("attack.zip"));
        mz_zip_archive archive{};
        QVERIFY(mz_zip_writer_init_heap(&archive, 0, 0));
        const QByteArray content("unsafe");
        QVERIFY(mz_zip_writer_add_mem(&archive, "../escape", content.constData(),
                                      static_cast<size_t>(content.size()), MZ_DEFAULT_COMPRESSION));
        void* bytes = nullptr;
        size_t size = 0;
        QVERIFY(mz_zip_writer_finalize_heap_archive(&archive, &bytes, &size));
        writeFixture(zip,
                     QByteArray(static_cast<const char*>(bytes), static_cast<qsizetype>(size)));
        mz_free(bytes);
        mz_zip_writer_end(&archive);
        nodivu::PackageService packages(
            QDir(directory.path()).filePath(QStringLiteral("profile/plugins")));
        QVERIFY(!packages.importZip(zip).ok());
        QVERIFY(!QFileInfo::exists(QDir(directory.path()).filePath(QStringLiteral("escape"))));
        QVERIFY(packages.installed().isEmpty());
    }
};
QTEST_GUILESS_MAIN(PackageTests)
#include "package_tests.moc"
