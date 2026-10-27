#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class UpdateHelperTests final : public QObject {
    Q_OBJECT
  private:
    static QString helper() {
        return QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(QStringLiteral("../nodivu-update-helper.exe"));
    }
  private slots:
    void waitsForParentThenRechecksAndStartsVerifiedExecutable() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto probe = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("update_probe.exe"));
        const auto installer =
            QDir(temporary.path()).filePath(QStringLiteral("inert installer-setup.exe"));
        QVERIFY(QFile::copy(probe, installer));
        QFile input(installer);
        QVERIFY(input.open(QIODevice::ReadOnly));
        const auto sha256 =
            QCryptographicHash::hash(input.readAll(), QCryptographicHash::Sha256).toHex();
        input.close();
        const auto marker = QDir(temporary.path()).filePath(QStringLiteral("installed.marker"));
        QProcess parent;
        parent.start(probe, {QStringLiteral("--hold")});
        QVERIFY(parent.waitForStarted());
        QProcess handoff;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("NODIVU_UPDATE_PROBE_MARKER"), marker);
        handoff.setProcessEnvironment(environment);
        handoff.start(helper(), {QString::number(parent.processId()), installer,
                                 QString::fromLatin1(sha256)});
        QVERIFY(handoff.waitForStarted());
        QTest::qWait(100);
        QVERIFY(!QFileInfo::exists(marker));
        if (parent.state() != QProcess::NotRunning)
            QVERIFY(parent.waitForFinished(5000));
        if (handoff.state() != QProcess::NotRunning)
            QVERIFY(handoff.waitForFinished(5000));
        QCOMPARE(handoff.exitCode(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(marker), 5000);
        QFile result(marker);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("verified installer fixture launched"));
    }
    void rejectsOverflowAndMalformedPidBeforeLaunchingAnything() {
        for (const auto& pid :
             {QStringLiteral("4294967296"), QStringLiteral("-1"), QStringLiteral("0")}) {
            QProcess handoff;
            handoff.start(helper(), {pid, QStringLiteral("C:/inert-setup.exe"),
                                     QString(64, QLatin1Char('0'))});
            QVERIFY(handoff.waitForFinished(5000));
            QCOMPARE(handoff.exitCode(), 2);
        }
    }
};
QTEST_GUILESS_MAIN(UpdateHelperTests)
#include "update_helper_tests.moc"
