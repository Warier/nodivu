#include "features/hotkeys/hotkey_service.hpp"
#include "features/projects/project_controller.hpp"
#include "features/store/tuf_client.hpp"
#include "features/workspace/main_window.hpp"
#include "platform/diagnostic_export.hpp"
#include <QDialog>
#include <QTemporaryDir>
#include <QtTest>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace nodivu;
class HotkeyTests final : public QObject {
    Q_OBJECT
    static QJsonObject binding(QString action, QString key = QStringLiteral("F23"),
                               QString scope = QStringLiteral("global")) {
        return {{QStringLiteral("id"), QStringLiteral("test")},
                {QStringLiteral("enabled"), true},
                {QStringLiteral("action"), action},
                {QStringLiteral("accelerator"), key},
                {QStringLiteral("scope"), scope},
                {QStringLiteral("label"), QStringLiteral("Teste")}};
    }
    static QJsonObject config(QJsonObject b) {
        return {{QStringLiteral("version"), 1},
                {QStringLiteral("enabled"), true},
                {QStringLiteral("bindings"), QJsonArray{b}}};
    }
  private slots:
    void diagnosticExportRedactsPathsAndKeepsOriginal() {
        QTemporaryDir profile;
        QVERIFY(QDir().mkpath(QDir(profile.path()).filePath(QStringLiteral("logs"))));
        const auto path = QDir(profile.path()).filePath(QStringLiteral("logs/desktop.jsonl"));
        const QByteArray original = "{\"time\":\"2026-11-05\",\"category\":\"backend\",\"message\":"
                                    "\"0x80070005 C:/Users/Person/private.wav\"}\n";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(original), original.size());
        file.close();
        const auto report = diagnosticReport(profile.path());
        QVERIFY(report.error.isEmpty());
        QVERIFY(!report.bytes.contains("Person"));
        QVERIFY(!report.bytes.contains("private.wav"));
        QVERIFY(report.bytes.contains("0x80070005"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
    }
    void keyPolicyAndPersistenceValidation() {
        for (const auto& key :
             {"F12", "Alt+F4", "Ctrl+Esc", "Ctrl+Shift+Esc", "Meta+R", "Ctrl+Alt+Del", "Ctrl+S",
              "Ctrl+Shift+P", "A", "Shift+A", "Ctrl+F1, Ctrl+F2"})
            QVERIFY2(!HotkeyService::validate(
                          config(binding(QStringLiteral("app.show"), QString::fromLatin1(key))))
                          .isEmpty(),
                     key);
        for (const auto& key : {"F13", "F24", "Control+Shift+F8", "Alt+B", "Ctrl+Home"})
            QVERIFY2(HotkeyService::validate(
                         config(binding(QStringLiteral("app.show"), QString::fromLatin1(key))))
                         .isEmpty(),
                     key);
        auto c = config(binding(QStringLiteral("app.show")));
        auto b = binding(QStringLiteral("app.mute.toggle"));
        b.insert(QStringLiteral("id"), QStringLiteral("second"));
        auto all = c.value(QStringLiteral("bindings")).toArray();
        all.append(b);
        c.insert(QStringLiteral("bindings"), all);
        QVERIFY(!HotkeyService::validate(c).isEmpty());
    }
    void updateReleaseOriginIsPinnedAndQtOnly() {
        const auto valid = QStringLiteral("https://github.com/Warier/nodivu/releases/download/"
                                          "v0.2.2-qt.1/Nodivu-0.2.2-windows-x64-qt-setup.exe");
        QVERIFY(TufClient::validReleaseUrl(QUrl(valid), QStringLiteral("Warier/nodivu")));
        for (const auto& invalid :
             {valid + QStringLiteral("?token=x"), valid + QStringLiteral("#x"),
              QString(valid).replace(QStringLiteral("https:"), QStringLiteral("http:")),
              QString(valid).replace(QStringLiteral("Warier"), QStringLiteral("Other")),
              QString(valid).replace(QStringLiteral("-qt.1"), QString()),
              QString(valid).replace(QStringLiteral("github.com"),
                                     QStringLiteral("github.com.evil.test"))})
            QVERIFY(!TufClient::validReleaseUrl(QUrl(invalid), QStringLiteral("Warier/nodivu")));
    }
    void registrationSuspensionEscapeAndMissingActions() {
        QTemporaryDir profile;
        QWidget window;
        BackendClient backend;
        SessionController session(backend);
        HotkeyService service(session, profile.path(), &window);
        QVERIFY(service.save(config(binding(QStringLiteral("app.show")))).isEmpty());
#ifdef Q_OS_WIN
        QCOMPARE(service.state(QStringLiteral("test")), QStringLiteral("Global ativo"));
        QVERIFY(!RegisterHotKey(nullptr, 0x5001, MOD_NOREPEAT, VK_F23));
        service.suspend(true);
        QVERIFY(RegisterHotKey(nullptr, 0x5001, MOD_NOREPEAT, VK_F23));
        service.suspend(false);
        QVERIFY(service.state(QStringLiteral("test")).startsWith(QStringLiteral("Conflito")));
        QVERIFY(UnregisterHotKey(nullptr, 0x5001));
        service.suspend(false);
        QTimer::singleShot(50, [] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
                QTest::keyClick(dialog, Qt::Key_Escape);
        });
        showHotkeys(service, &window);
        QCOMPARE(service.state(QStringLiteral("test")), QStringLiteral("Global ativo"));
        QSignalSpy executed(&service, &HotkeyService::result);
        MSG msg{};
        msg.message = WM_HOTKEY;
        msg.wParam = 0x5100;
        qintptr result = 1;
        QVERIFY(
            service.nativeEventFilter(QByteArrayLiteral("windows_dispatcher_MSG"), &msg, &result));
        QTRY_COMPARE(executed.size(), 1);
        QVERIFY(window.isVisible());
#endif
        QVERIFY(
            service.save(config(binding(QStringLiteral("absent/plugin/player.play")))).isEmpty());
        QCOMPARE(service.state(QStringLiteral("test")),
                 QStringLiteral("Bloco/ação ausente neste projeto"));
    }
    void gainAgainstRealBackendAndDisabledBinding() {
        QTemporaryDir temporary;
        auto paths = AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = temporary.path();
        paths.plugins = QDir(temporary.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        paths.updatesSource.clear();
        MainWindow window(paths);
        auto& session = window.session();
        auto* projects = window.findChild<ProjectController*>();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !projects->busy(), 10000);
        session.addNode(QStringLiteral("gain"));
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 5000);
        auto* service = window.findChild<HotkeyService*>();
        QVERIFY(service);
        QString action, node;
        for (const auto& a : service->actions())
            if (a.field == QStringLiteral("gain_db") && a.operation == QStringLiteral("adjust")) {
                action = a.id;
                node = a.node;
                break;
            }
        QVERIFY(!action.isEmpty());
        auto b = binding(action, QStringLiteral("F24"), QStringLiteral("local"));
        b.insert(QStringLiteral("value"), -6.0);
        QVERIFY(service->save(config(b)).isEmpty());
        QSignalSpy spy(service, &HotkeyService::result);
        service->fire(QStringLiteral("test"), true);
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 5000);
        QVERIFY(spy.first().first().toBool());
        QCOMPARE(session.node(node)
                     .value(QStringLiteral("block"))
                     .toObject()
                     .value(QStringLiteral("gain_db"))
                     .toDouble(),
                 -6.0);
        service->fire(QStringLiteral("test"), true);
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 2, 5000);
        QCOMPARE(session.node(node)
                     .value(QStringLiteral("block"))
                     .toObject()
                     .value(QStringLiteral("gain_db"))
                     .toDouble(),
                 -12.0);
        auto c = config(b);
        c.insert(QStringLiteral("enabled"), false);
        QVERIFY(service->save(c).isEmpty());
        service->fire(QStringLiteral("test"));
        QTest::qWait(100);
        QCOMPARE(spy.size(), 2);
        session.removeNode(node);
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 5000);
        QCOMPARE(service->actions().size(), 5);
        // Avoid modal unsaved prompts in the test; the backend's destructor owns shutdown.
    }
};
QTEST_MAIN(HotkeyTests)
#include "hotkey_tests.moc"
