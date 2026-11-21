#include "features/hotkeys/hotkey_service.hpp"
#include "features/inspector/inspector_panel.hpp"
#include "features/projects/project_controller.hpp"
#include "features/store/tuf_client.hpp"
#include "features/workspace/main_window.hpp"
#include "platform/diagnostic_export.hpp"
#include "ui/theme/theme.hpp"
#include <QDataStream>
#include <QDialog>
#include <QPushButton>
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
    void soundboardProjectSlotsPresetAndClear() {
        if (!qEnvironmentVariableIsEmpty("NODIVU_SOUNDBOARD_OUTPUT"))
            Theme::apply(*qobject_cast<QApplication*>(qApp));
        QTemporaryDir temporary;
        auto paths = AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = temporary.path();
        paths.plugins = QDir(temporary.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        paths.updatesSource.clear();
        const auto audio = QDir(temporary.path()).filePath(QStringLiteral("sound.wav"));
        QFile wave(audio);
        QVERIFY(wave.open(QIODevice::WriteOnly));
        QDataStream out(&wave);
        out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF", 4);
        out << quint32(36 + 288000);
        out.writeRawData("WAVEfmt ", 8);
        out << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000)
            << quint16(2) << quint16(16);
        out.writeRawData("data", 4);
        out << quint32(288000);
        for (int i = 0; i < 144000; ++i)
            out << qint16(i % 100 < 50 ? 1000 : -1000);
        wave.close();
        MainWindow window(paths);
        window.show();
        auto& session = window.session();
        auto* projects = window.findChild<ProjectController*>();
        auto* service = window.findChild<HotkeyService*>();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !projects->busy(), 10000);
        session.addNode(QStringLiteral("plugin"), QStringLiteral("org.nodivu.soundboard"));
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 5000);
        QString id;
        for (const auto& a : service->actions())
            if (a.operation == QStringLiteral("bank.trigger")) {
                id = a.node;
                break;
            }
        QVERIFY(!id.isEmpty());
        auto* inspector = window.findChild<InspectorPanel*>();
        QVERIFY(inspector);
        inspector->inspect(id);
        QVERIFY(inspector->findChild<QPushButton*>(QStringLiteral("soundboardFile9")));
        auto loaded = [&](int slot) {
            return session.snapshot()
                       .value(QStringLiteral("plugin_states"))
                       .toObject()
                       .value(id)
                       .toObject()
                       .value(QStringLiteral("bank"))
                       .toArray()[slot]
                       .toObject()
                       .value(QStringLiteral("status"))
                       .toInt() == 2;
        };
        for (int slot = 0; slot < 10; ++slot) {
            session.command(QStringLiteral("plugin.bank"),
                            {{QStringLiteral("node_id"), id},
                             {QStringLiteral("slot"), slot},
                             {QStringLiteral("action"), QStringLiteral("load")},
                             {QStringLiteral("path"), audio}});
            QTRY_VERIFY_WITH_TIMEOUT(session.settled() && loaded(slot), 5000);
        }
        QCOMPARE(projects->document().value(QStringLiteral("resources")).toArray().size(), 10);
        QVERIFY(service->soundboardPreset(id).isEmpty());
        QCOMPARE(service->config().value(QStringLiteral("bindings")).toArray().size(), 10);
        QVERIFY(service->soundboardPreset(id).isEmpty());
        QCOMPARE(service->config().value(QStringLiteral("bindings")).toArray().size(), 10);
        // Optional hardware gate: the ordinary CTest case above uses no endpoint.
        const auto endpoint = qEnvironmentVariable("NODIVU_SOUNDBOARD_OUTPUT");
        if (!endpoint.isEmpty()) {
            session.addNode(QStringLiteral("output"));
            QTRY_VERIFY(session.settled());
            QString output;
            for (const auto& n : session.snapshot()
                                     .value(QStringLiteral("graph"))
                                     .toObject()
                                     .value(QStringLiteral("nodes"))
                                     .toArray())
                if (n.toObject()
                        .value(QStringLiteral("block"))
                        .toObject()
                        .value(QStringLiteral("kind"))
                        .toString() == QStringLiteral("output"))
                    output = n.toObject().value(QStringLiteral("id")).toString();
            QVERIFY(!output.isEmpty());
            session.updateField(output, QStringLiteral("endpoint_id"), endpoint);
            QTRY_VERIFY(session.settled());
            session.connectNodes(id, 0, output, 0);
            QTRY_VERIFY(session.settled());
            QTRY_COMPARE(session.snapshot().value(QStringLiteral("engine_state")).toString(),
                         QStringLiteral("running"));
            const auto generation = session.snapshot().value(QStringLiteral("generation"));
            const auto bindings = service->config().value(QStringLiteral("bindings")).toArray();
            QSignalSpy fired(service, &HotkeyService::result);
            window.showMinimized();
            service->fire(bindings[0].toObject().value(QStringLiteral("id")).toString(), true);
            service->fire(bindings[1].toObject().value(QStringLiteral("id")).toString(), true);
            QTRY_COMPARE_WITH_TIMEOUT(fired.size(), 2, 3000);
            QVERIFY(fired[0][0].toBool());
            QVERIFY(fired[1][0].toBool());
            QTRY_VERIFY(session.snapshot()
                            .value(QStringLiteral("metrics"))
                            .toObject()
                            .value(QStringLiteral("output_peak"))
                            .toDouble() > 0.01);
            const auto playing = [&](int slot) {
                return session.snapshot()
                           .value(QStringLiteral("plugin_states"))
                           .toObject()
                           .value(id)
                           .toObject()
                           .value(QStringLiteral("bank"))
                           .toArray()[slot]
                           .toObject()
                           .value(QStringLiteral("status"))
                           .toInt() == 3;
            };
            QTRY_VERIFY_WITH_TIMEOUT(playing(0) && playing(1), 2000);
            QCOMPARE(session.snapshot().value(QStringLiteral("generation")), generation);
            window.showNormal();
            QTest::qWait(1100);
            qInfo("PASS: Qt minimized, two queued soundboard hotkeys, real WASAPI output, stable "
                  "generation");
        }
        const auto file = QDir(temporary.path()).filePath(QStringLiteral("bank.nodivu.json"));
        projects->save(file);
        QTRY_VERIFY_WITH_TIMEOUT(!projects->busy() && session.settled() && QFile::exists(file),
                                 5000);
        projects->open(file);
        QTRY_VERIFY_WITH_TIMEOUT(!projects->busy() && session.settled() && loaded(9), 10000);
        QCOMPARE(projects->document().value(QStringLiteral("resources")).toArray().size(), 10);
        QVERIFY(loaded(0));
        session.command(QStringLiteral("plugin.bank"),
                        {{QStringLiteral("node_id"), id},
                         {QStringLiteral("slot"), 0},
                         {QStringLiteral("action"), QStringLiteral("clear")}});
        QTRY_VERIFY(session.settled());
        QCOMPARE(projects->document().value(QStringLiteral("resources")).toArray().size(), 9);
        projects->save(file);
        QTRY_VERIFY(!projects->busy() && session.settled());
        projects->open(file);
        QTRY_VERIFY_WITH_TIMEOUT(!projects->busy() && session.settled() && loaded(9), 10000);
        QCOMPARE(projects->document().value(QStringLiteral("resources")).toArray().size(), 9);
        QVERIFY(!loaded(0));
        if (!endpoint.isEmpty()) {
            inspector->inspect(id);
            window.showNormal();
            QTest::qWait(80);
            QVERIFY(window.grab().save(QStringLiteral(NODIVU_REPOSITORY_DIR) +
                                       QStringLiteral("/.local/qt-build/soundboard-qt.png")));
        }
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
