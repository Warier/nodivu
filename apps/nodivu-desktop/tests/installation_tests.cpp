#include "platform/install_transaction.hpp"
#include "platform/windows/native_desktop.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

class InstallationTests final : public QObject {
    Q_OBJECT
  private:
    static bool write(const QString& root, const QString& name, const QByteArray& bytes) {
        const auto path = QDir(root).filePath(name);
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            return false;
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static QByteArray read(const QString& root, const QString& name) {
        QFile file(QDir(root).filePath(name));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    static bool payload(const QString& root, const QByteArray& version, bool obsolete = false) {
        QJsonArray files;
        const auto add = [&](const QString& name, const QByteArray& bytes) {
            if (!write(root, name, bytes))
                return false;
            files.append(QJsonObject{
                {QStringLiteral("path"), name},
                {QStringLiteral("bytes"), bytes.size()},
                {QStringLiteral("sha256"),
                 QString::fromLatin1(
                     QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}});
            return true;
        };
        if (!add(QStringLiteral("nodivu.exe"), version) ||
            !add(QStringLiteral("bin/nodivu-app-backend.exe"), QByteArray("unchanged backend")))
            return false;
        if (obsolete && !add(QStringLiteral("obsolete.dll"), QByteArray("old runtime component")))
            return false;
        return write(root, QStringLiteral("distribution.json"),
                     QJsonDocument(QJsonObject{{QStringLiteral("schema_version"), 1},
                                               {QStringLiteral("frontend"),
                                                QStringLiteral("Qt Widgets/C++20")},
                                               {QStringLiteral("files"), files}})
                         .toJson());
    }
    static bool electron(const QString& root) {
        return write(root, QStringLiteral("nodivu-electron.exe"),
                     QByteArray("inert old executable")) &&
               write(root, QStringLiteral("resources/app/package.json"),
                     QByteArray("{\"name\":\"nodivu-electron\"}")) &&
               write(root, QStringLiteral("resources/app/node_modules/runtime/index.js"),
                     QByteArray("inert javascript")) &&
               write(root, QStringLiteral("chrome_100_percent.pak"), QByteArray("old chromium")) &&
               write(root, QStringLiteral("plugins/custom/plugin.json"),
                     QByteArray("user plugin original")) &&
               write(root, QStringLiteral("user-project.nodivu.json"),
                     QByteArray("user project original"));
    }
  private slots:
    void legacyProcessOfSameUserBlocksProfileUntilItExits() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto fixture = QDir(temporary.path()).filePath(QStringLiteral("nodivu-electron.exe"));
        QVERIFY(QFile::copy(QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("update_probe.exe")),
                            fixture));
        const auto initial = nodivu::windows::legacyProfileConflict();
        QVERIFY2(initial.isEmpty(), qPrintable(initial));
        QProcess child;
        const auto stop = qScopeGuard([&child] {
            if (child.state() != QProcess::NotRunning) {
                child.kill();
                child.waitForFinished(3000);
            }
        });
        child.start(fixture, {QStringLiteral("--hold-long")});
        QVERIFY(child.waitForStarted(3000));
        QVERIFY(nodivu::windows::legacyProfileConflict().contains(QStringLiteral("Electron")));
        child.kill();
        QVERIFY(child.waitForFinished(3000));
        QVERIFY(nodivu::windows::legacyProfileConflict().isEmpty());
    }
    void electronAndQtUpgradePreserveUnknownFilesAndRemoveActiveLegacyRuntime() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto current = QDir(temporary.path()).filePath(QStringLiteral("Nodivu"));
        const auto next = QDir(temporary.path()).filePath(QStringLiteral("payload"));
        QVERIFY(electron(current));
        QVERIFY(payload(next, QByteArray("new Qt"), true));
        nodivu::InstallTransaction transaction(current);
        auto result = transaction.apply(next, [](const QString& root) {
            return read(root, QStringLiteral("nodivu.exe")) == QByteArray("new Qt")
                       ? QString()
                       : QStringLiteral("bad installation");
        });
        QVERIFY2(result.ok(), qPrintable(result.error));
        QVERIFY(result.migratedElectron);
        QVERIFY(!QFileInfo::exists(QDir(current).filePath(QStringLiteral("nodivu-electron.exe"))));
        QVERIFY(!QFileInfo::exists(QDir(current).filePath(QStringLiteral("resources/app"))));
        QCOMPARE(read(current, QStringLiteral("plugins/custom/plugin.json")),
                 QByteArray("user plugin original"));
        QCOMPARE(read(current, QStringLiteral("user-project.nodivu.json")),
                 QByteArray("user project original"));
        QCOMPARE(read(result.backup, QStringLiteral("nodivu-electron.exe")),
                 QByteArray("inert old executable"));
        const auto update = QDir(temporary.path()).filePath(QStringLiteral("qt-update"));
        QVERIFY(payload(update, QByteArray("newer Qt")));
        result = transaction.apply(update, [](const QString&) { return QString(); });
        QVERIFY2(result.ok(), qPrintable(result.error));
        QVERIFY(!result.migratedElectron);
        QCOMPARE(read(current, QStringLiteral("nodivu.exe")), QByteArray("newer Qt"));
        QVERIFY(!QFileInfo::exists(QDir(current).filePath(QStringLiteral("obsolete.dll"))));
        QCOMPARE(read(current, QStringLiteral("user-project.nodivu.json")),
                 QByteArray("user project original"));
    }
    void invalidPayloadAndFailedValidationRestoreOriginal() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto current = QDir(temporary.path()).filePath(QStringLiteral("Nodivu"));
        const auto next = QDir(temporary.path()).filePath(QStringLiteral("payload"));
        QVERIFY(electron(current));
        QVERIFY(payload(next, QByteArray("new Qt")));
        nodivu::InstallTransaction transaction(current);
        auto result = transaction.apply(
            next, [](const QString&) { return QStringLiteral("simulated startup failure"); });
        QVERIFY(!result.ok());
        QCOMPARE(read(current, QStringLiteral("nodivu-electron.exe")),
                 QByteArray("inert old executable"));
        QCOMPARE(read(current, QStringLiteral("user-project.nodivu.json")),
                 QByteArray("user project original"));
        QVERIFY(!QFileInfo::exists(transaction.journalPath()));
        QVERIFY(write(next, QStringLiteral("nodivu.exe"), QByteArray("tampered")));
        bool executed = false;
        result = transaction.apply(next, [&](const QString&) {
            executed = true;
            return QString();
        });
        QVERIFY(!result.ok());
        QVERIFY(!executed);
        QCOMPARE(read(current, QStringLiteral("nodivu-electron.exe")),
                 QByteArray("inert old executable"));
    }
    void rejectsUnknownDirectoryAndConcurrentInstall() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto current = QDir(temporary.path()).filePath(QStringLiteral("personal-folder"));
        const auto next = QDir(temporary.path()).filePath(QStringLiteral("payload"));
        QVERIFY(write(current, QStringLiteral("precious.txt"), QByteArray("preserve")));
        QVERIFY(payload(next, QByteArray("Qt")));
        nodivu::InstallTransaction transaction(current);
        QVERIFY(!transaction.apply(next, [](const QString&) { return QString(); }).ok());
        QCOMPARE(read(current, QStringLiteral("precious.txt")), QByteArray("preserve"));
        QLockFile lock(transaction.journalPath() + QStringLiteral(".lock"));
        QVERIFY(lock.tryLock(0));
        QVERIFY(!transaction.apply(next, [](const QString&) { return QString(); }).ok());
    }
    void recoversAfterProcessDiesWithNewVersionPublishedButUnverified() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto current = QDir(temporary.path()).filePath(QStringLiteral("Nodivu"));
        const auto next = QDir(temporary.path()).filePath(QStringLiteral("payload"));
        QVERIFY(electron(current));
        QVERIFY(payload(next, QByteArray("new Qt")));
        QProcess child;
        child.start(QDir(QCoreApplication::applicationDirPath())
                        .filePath(QStringLiteral("install_crash_probe.exe")),
                    {current, next});
        QVERIFY(child.waitForFinished(10000));
        QCOMPARE(child.exitCode(), 73);
        QCOMPARE(read(current, QStringLiteral("nodivu.exe")), QByteArray("new Qt"));
        nodivu::InstallTransaction transaction(current);
        QVERIFY(QFileInfo::exists(transaction.journalPath()));
        const auto error = transaction.recover();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(read(current, QStringLiteral("nodivu-electron.exe")),
                 QByteArray("inert old executable"));
        QCOMPARE(read(current, QStringLiteral("user-project.nodivu.json")),
                 QByteArray("user project original"));
        QVERIFY(!QFileInfo::exists(transaction.journalPath()));
    }
};
QTEST_GUILESS_MAIN(InstallationTests)
#include "installation_tests.moc"
