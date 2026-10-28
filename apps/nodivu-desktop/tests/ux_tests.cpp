#include "contracts/audio_state.hpp"
#include "contracts/signal_state.hpp"
#include "features/graph/empty_graph_widget.hpp"
#include "features/graph/graph_scene.hpp"
#include "features/graph/graph_view.hpp"
#include "features/library/library_block_button.hpp"
#include "features/library/library_panel.hpp"
#include "features/projects/project_controller.hpp"
#include "features/workspace/main_window.hpp"
#include "features/workspace/system_panel.hpp"
#include "ui/controls/feedback_banner.hpp"
#include "ui/theme/studio_icons.hpp"
#include "ui/theme/theme.hpp"
#include <QAction>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDataStream>
#include <QFileDialog>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>
#include <cmath>
namespace {
nodivu::AppPaths pathsFor(const QTemporaryDir& profile) {
    auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
    paths.profile = profile.path();
    paths.plugins = profile.filePath(QStringLiteral("plugins"));
    paths.storeSource.clear();
    return paths;
}
QJsonArray nodes(nodivu::SessionController& session) {
    return session.snapshot()
        .value(QStringLiteral("graph"))
        .toObject()
        .value(QStringLiteral("nodes"))
        .toArray();
}
QJsonArray edges(nodivu::SessionController& session) {
    return session.snapshot()
        .value(QStringLiteral("graph"))
        .toObject()
        .value(QStringLiteral("edges"))
        .toArray();
}
QString idFor(nodivu::SessionController& session, const QString& kind) {
    for (const auto& value : nodes(session))
        if (value.toObject()
                .value(QStringLiteral("block"))
                .toObject()
                .value(QStringLiteral("kind"))
                .toString() == kind)
            return value.toObject().value(QStringLiteral("id")).toString();
    return {};
}
void capture(QWidget& widget, const QString& name) {
    QCoreApplication::processEvents();
    const auto dir = qEnvironmentVariable("NODIVU_WORKSPACE_SCREENSHOTS");
    if (!dir.isEmpty())
        widget.grab().save(QDir(dir).filePath(name));
}
} // namespace
class UxTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeuib.ttf"));
        nodivu::Theme::apply(*qobject_cast<QApplication*>(QCoreApplication::instance()));
    }
    void saveUsesCurrentPathAndSaveAsKeepsTheOriginal() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        nodivu::MainWindow window(pathsFor(profile));
        window.show();
        auto& session = window.session();
        auto* projects = window.findChild<nodivu::ProjectController*>();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !projects->busy(), 10000);
        session.addNode(QStringLiteral("gain"));
        QTRY_VERIFY(session.settled() && nodes(session).size() == 1);
        const auto id = idFor(session, QStringLiteral("gain"));
        const auto first = profile.filePath(QStringLiteral("Primeiro.nodivu.json")),
                   second = profile.filePath(QStringLiteral("Cópia.nodivu.json"));
        const bool previous = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restore = qScopeGuard(
            [previous] { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous); });
        const auto choose = [&](const QString& file) {
            QTimer::singleShot(0, &window, [&, file] {
                auto* picker = window.findChild<QFileDialog*>();
                if (!picker)
                    qFatal("Expected project file picker");
                picker->setDirectory(QFileInfo(file).absolutePath());
                picker->selectFile(QFileInfo(file).fileName());
                QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection);
            });
        };
        window.activateWindow();
        window.setFocus();
        choose(first);
        QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY(!projects->busy() && projects->path() == first && !projects->dirty());
        QVERIFY(projects->savedAt().isValid());
        session.updateField(id, QStringLiteral("gain_db"), -12.0);
        QTRY_VERIFY(session.settled() && projects->dirty());
        QSignalSpy saved(&window, &nodivu::MainWindow::saveRequested);
        window.activateWindow();
        window.setFocus();
        QTRY_VERIFY(window.isActiveWindow());
        QTRY_VERIFY(window.findChild<QAction*>(QStringLiteral("projectSave"))->isEnabled());
        QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);
        QCOMPARE(saved.count(), 1);
        QTRY_VERIFY(!projects->busy() && !projects->dirty());
        QVERIFY(!window.findChild<QFileDialog*>());
        const auto original = nodivu::ProjectRepository::read(first).contents;
        choose(second);
        window.findChild<QAction*>(QStringLiteral("projectSaveAs"))->trigger();
        QTRY_VERIFY(!projects->busy() && projects->path() == second && !projects->dirty());
        QCOMPARE(nodivu::ProjectRepository::read(first).contents, original);
        session.updateField(id, QStringLiteral("gain_db"), -18.0);
        QTRY_VERIFY(session.settled() && projects->dirty());
        QSignalSpy errors(projects, &nodivu::ProjectController::error);
        projects->save(profile.path());
        QTRY_VERIFY(!projects->busy() && !errors.isEmpty());
        QVERIFY(projects->dirty());
        QCOMPARE(projects->path(), second);
        QCOMPARE(nodivu::ProjectRepository::read(first).contents, original);
    }
    void historyRestoresTopologyParametersAndPositionsWithoutChangingTheCamera() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        nodivu::MainWindow window(pathsFor(profile));
        window.show();
        auto& session = window.session();
        auto* projects = window.findChild<nodivu::ProjectController*>();
        auto* scene = window.findChild<nodivu::GraphScene*>();
        auto* view = window.findChild<nodivu::GraphView*>();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !projects->busy(), 10000);
        QVERIFY(!projects->canUndo());
        auto* undoButton = window.findChild<QToolButton*>(QStringLiteral("canvasUndo"));
        auto* redoButton = window.findChild<QToolButton*>(QStringLiteral("canvasRedo"));
        QVERIFY(undoButton && redoButton);
        QVERIFY(undoButton->isVisible() && redoButton->isVisible());
        QVERIFY(!undoButton->isEnabled() && !redoButton->isEnabled());
        session.addNode(QStringLiteral("gain"));
        QTRY_VERIFY(session.settled() && nodes(session).size() == 1);
        const auto gain = idFor(session, QStringLiteral("gain"));
        QTRY_VERIFY(undoButton->isEnabled());
        QVERIFY(undoButton->toolTip().contains(QStringLiteral("Ctrl+Z")));
        QTest::mouseClick(undoButton, Qt::LeftButton);
        QTRY_VERIFY(!projects->busy() && session.settled() && nodes(session).isEmpty());
        QVERIFY(projects->canRedo());
        QTRY_VERIFY(!undoButton->isEnabled() && redoButton->isEnabled());
        QTest::mouseClick(redoButton, Qt::LeftButton);
        QTRY_VERIFY(!projects->busy() && session.settled() && !session.node(gain).isEmpty());
        QTRY_VERIFY(undoButton->isEnabled() && !redoButton->isEnabled());
        capture(window, QStringLiteral("ux-canvas-history-controls.png"));
        session.addNode(QStringLiteral("meter"));
        QTRY_VERIFY(session.settled() && nodes(session).size() == 2);
        const auto meter = idFor(session, QStringLiteral("meter"));
        session.connectNodes(gain, 0, meter, 0);
        QTRY_VERIFY(session.settled() && edges(session).size() == 1);
        const auto file = profile.filePath(QStringLiteral("history.nodivu.json"));
        projects->save(file);
        QTRY_VERIFY(!projects->busy() && !projects->dirty());
        const double oldGain = session.node(gain)
                                   .value(QStringLiteral("block"))
                                   .toObject()
                                   .value(QStringLiteral("gain_db"))
                                   .toDouble();
        session.updateField(gain, QStringLiteral("gain_db"), -6.0);
        QTRY_VERIFY(session.settled());
        session.updateField(gain, QStringLiteral("gain_db"), -18.0);
        QTRY_VERIFY(session.settled());
        window.activateWindow();
        window.setFocus();
        QTRY_VERIFY(window.isActiveWindow());
        QTRY_VERIFY(window.findChild<QAction*>(QStringLiteral("editUndo"))->isEnabled());
        QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY(!projects->busy() && session.settled());
        QCOMPARE(session.node(gain)
                     .value(QStringLiteral("block"))
                     .toObject()
                     .value(QStringLiteral("gain_db"))
                     .toDouble(),
                 oldGain);
        QTRY_VERIFY(window.findChild<QAction*>(QStringLiteral("editRedo"))->isEnabled());
        QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(!projects->busy() && session.settled());
        QCOMPARE(session.node(gain)
                     .value(QStringLiteral("block"))
                     .toObject()
                     .value(QStringLiteral("gain_db"))
                     .toDouble(),
                 -18.0);
        auto* search = window.findChild<QLineEdit*>(QStringLiteral("blockSearch"));
        search->setFocus();
        QTest::keyClicks(search, QStringLiteral("ganho"));
        const auto graphBeforeTextUndo = session.snapshot().value(QStringLiteral("graph"));
        QTest::keyClick(search, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(search->text().isEmpty());
        QCOMPARE(session.snapshot().value(QStringLiteral("graph")), graphBeforeTextUndo);
        projects->save();
        QTRY_VERIFY(!projects->busy() && !projects->dirty());
        const auto originalPosition = scene->nodeItem(gain)->pos();
        const auto revision = session.snapshot().value(QStringLiteral("revision"));
        const auto camera = view->camera();
        scene->placeNode(gain, originalPosition + QPointF(120, 60));
        scene->placeNode(gain, originalPosition + QPointF(180, 90));
        projects->undo();
        QTRY_VERIFY(!projects->busy());
        QCOMPARE(scene->nodeItem(gain)->pos(), originalPosition);
        QCOMPARE(session.snapshot().value(QStringLiteral("revision")), revision);
        QCOMPARE(view->camera(), camera);
        QVERIFY(!projects->dirty());
        session.removeNode(gain);
        QTRY_VERIFY(session.settled() && session.node(gain).isEmpty());
        projects->undo();
        QTRY_VERIFY(!projects->busy() && session.settled() && !session.node(gain).isEmpty());
        QCOMPARE(edges(session).size(), 1);
        QCOMPARE(scene->nodeItem(gain)->pos(), originalPosition);
        QVERIFY(projects->canRedo());
        QSignalSpy errors(&session, &nodivu::SessionController::error);
        session.updateField(gain, QStringLiteral("gain_db"), 1e10);
        QTRY_VERIFY(session.settled() && !errors.isEmpty());
        QVERIFY(projects->canRedo());
        session.updateField(gain, QStringLiteral("gain_db"), -24.0);
        QTRY_VERIFY(session.settled());
        QVERIFY(!projects->canRedo());
        projects->undo();
        QTRY_VERIFY(!projects->busy() && session.settled());
        QCOMPARE(session.node(gain)
                     .value(QStringLiteral("block"))
                     .toObject()
                     .value(QStringLiteral("gain_db"))
                     .toDouble(),
                 -18.0);
        projects->open(file);
        QTRY_VERIFY(!projects->busy() && session.settled());
        QVERIFY(!projects->canUndo());
        QVERIFY(!projects->canRedo());
        view->zoomBy(.8);
        QVERIFY(!projects->canUndo());
        capture(window, QStringLiteral("ux-history.png"));
    }
    void deletedPlayerRestoresItsFileReferenceAndIdentity() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        nodivu::MainWindow window(pathsFor(profile));
        window.show();
        auto& session = window.session();
        auto* projects = window.findChild<nodivu::ProjectController*>();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !projects->busy(), 10000);
        QString plugin;
        for (const auto& d : session.catalog().entries())
            if (d.filePlayer && d.fileTransport) {
                plugin = d.pluginId;
                break;
            }
        QVERIFY(!plugin.isEmpty());
        QFile wave(profile.filePath(QStringLiteral("voz.wav")));
        QVERIFY(wave.open(QIODevice::WriteOnly));
        QDataStream stream(&wave);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF", 4);
        stream << quint32(36 + 9600);
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000)
               << quint16(2) << quint16(16);
        stream.writeRawData("data", 4);
        stream << quint32(9600);
        stream.writeRawData(QByteArray(9600, '\0').constData(), 9600);
        wave.close();
        session.addNode(QStringLiteral("plugin"), plugin);
        QTRY_VERIFY(session.settled() && nodes(session).size() == 1);
        const auto id = nodes(session).first().toObject().value(QStringLiteral("id")).toString();
        const auto runtime = [&] {
            return session.snapshot()
                .value(QStringLiteral("plugin_states"))
                .toObject()
                .value(id)
                .toObject();
        };
        session.command(QStringLiteral("plugin.command"),
                        {{QStringLiteral("node_id"), id},
                         {QStringLiteral("action"), QStringLiteral("load")},
                         {QStringLiteral("path"), wave.fileName()}});
        QTRY_COMPARE_WITH_TIMEOUT(runtime().value(QStringLiteral("status")).toInt(), 2, 5000);
        projects->save(profile.filePath(QStringLiteral("player.json")));
        QTRY_VERIFY(!projects->busy());
        session.removeNode(id);
        QTRY_VERIFY(session.settled() && session.node(id).isEmpty());
        projects->undo();
        QTRY_VERIFY(!projects->busy() && session.settled() && !session.node(id).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(runtime().value(QStringLiteral("status")).toInt(), 2, 5000);
        QCOMPARE(runtime().value(QStringLiteral("file")).toString(), wave.fileName());
        QCOMPARE(projects->resourcePath(id), wave.fileName());
    }
    void historyRestoresMissingPluginDataWithoutSendingUnsupportedCommands() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        const auto source =
            QStringLiteral(NODIVU_REPOSITORY_DIR) +
            QStringLiteral("/apps/nodivu-desktop/tests/fixtures/electron-project-v1.json");
        const auto file = profile.filePath(QStringLiteral("missing.json"));
        QVERIFY(QFile::copy(source, file));
        nodivu::MainWindow window(pathsFor(profile));
        window.show();
        auto& session = window.session();
        auto* projects = window.findChild<nodivu::ProjectController*>();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !projects->busy(), 10000);
        projects->open(file);
        QTRY_VERIFY(!projects->busy() && session.settled() && projects->path() == file);
        const auto before = projects->document();
        QString missing;
        for (const auto& node : nodes(session))
            if (session.catalog().describe(node.toObject()).missing) {
                missing = node.toObject().value(QStringLiteral("id")).toString();
                break;
            }
        QVERIFY(!missing.isEmpty());
        QSignalSpy errors(&session, &nodivu::SessionController::error);
        session.removeNode(missing);
        QTRY_VERIFY(session.settled() && session.node(missing).isEmpty());
        projects->undo();
        QTRY_VERIFY(!projects->busy() && session.settled() && !session.node(missing).isEmpty());
        QCOMPARE(projects->document(), before);
        QVERIFY(errors.isEmpty());
    }
    void clearCaptureSelectionAndUndoPreserveTheSavedIntent() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        nodivu::MainWindow window(pathsFor(profile));
        window.show();
        auto& session = window.session();
        auto* projects = window.findChild<nodivu::ProjectController*>();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !projects->busy(), 10000);
        QString plugin;
        for (const auto& d : session.catalog().entries())
            if (d.captureSource) {
                plugin = d.pluginId;
                break;
            }
        QVERIFY(!plugin.isEmpty());
        session.addNode(QStringLiteral("plugin"), plugin);
        QTRY_VERIFY(session.settled() && nodes(session).size() == 1);
        const auto id = nodes(session).first().toObject().value(QStringLiteral("id")).toString();
        // Saved intent for a nonexistent process in the isolated profile. No PID,
        // endpoint or physical capture is selected by this regression.
        const QJsonObject selection{
            {QStringLiteral("mode"), QStringLiteral("application")},
            {QStringLiteral("executable"),
             QDir::toNativeSeparators(profile.filePath(QStringLiteral("absent-ux-test.exe")))}};
        session.command(
            QStringLiteral("capture.configure"),
            {{QStringLiteral("node_id"), id}, {QStringLiteral("selection"), selection}});
        QTRY_VERIFY(session.settled() &&
                    projects->document().value(QStringLiteral("captures")).toArray().size() == 1);
        const auto file = profile.filePath(QStringLiteral("capture-intent.json"));
        projects->save(file);
        QTRY_VERIFY(!projects->busy() && !projects->dirty());
        projects->open(file);
        QTRY_VERIFY(!projects->busy() && session.settled());
        QCOMPARE(projects->savedAt(), QFileInfo(file).lastModified());
        session.command(QStringLiteral("capture.configure"),
                        {{QStringLiteral("node_id"), id},
                         {QStringLiteral("selection"), QJsonValue(QJsonValue::Null)}});
        QTRY_VERIFY(session.settled() &&
                    projects->document().value(QStringLiteral("captures")).toArray().isEmpty());
        projects->undo();
        QTRY_VERIFY(!projects->busy() && session.settled() &&
                    projects->document().value(QStringLiteral("captures")).toArray().size() == 1);
        QCOMPARE(projects->document()
                     .value(QStringLiteral("captures"))
                     .toArray()
                     .first()
                     .toObject()
                     .value(QStringLiteral("selection"))
                     .toObject(),
                 selection);
        projects->redo();
        QTRY_VERIFY(!projects->busy() && session.settled() &&
                    projects->document().value(QStringLiteral("captures")).toArray().isEmpty());
        projects->save();
        QTRY_VERIFY(!projects->busy() && !projects->dirty());
        projects->open(file);
        QTRY_VERIFY(!projects->busy() && session.settled());
        QVERIFY(projects->document().value(QStringLiteral("captures")).toArray().isEmpty());
    }
    void searchAndGuidedStart_data() {
        QTest::addColumn<QString>("start");
        QTest::newRow("microphone") << QStringLiteral("quickStart");
        QTest::newRow("application") << QStringLiteral("startApplication");
        QTest::newRow("file") << QStringLiteral("startFile");
    }
    void searchAndGuidedStart() {
        QFETCH(QString, start);
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        nodivu::MainWindow window(pathsFor(profile));
        window.show();
        auto& session = window.session();
        auto* projects = window.findChild<nodivu::ProjectController*>();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !projects->busy(), 10000);
        auto* library = window.findChild<nodivu::LibraryPanel*>();
        auto* search = library->findChild<QLineEdit*>(QStringLiteral("blockSearch"));
        QVERIFY(search);
        search->setText(QStringLiteral("VOLÚME"));
        auto* gain = library->findChild<nodivu::LibraryBlockButton*>(QStringLiteral("gain"));
        auto* unrelated = library->findChild<nodivu::LibraryBlockButton*>(QStringLiteral("meter"));
        QVERIFY(gain->isVisible());
        QVERIFY(!unrelated->isVisible());
        search->setText(QStringLiteral("sem-resultado-xyz"));
        QVERIFY(library->findChild<QLabel*>(QStringLiteral("blockSearchEmpty"))->isVisible());
        const auto expandedWidth = library->width();
        library->findChild<QToolButton*>(QStringLiteral("libraryToggle"))->click();
        QVERIFY(search->text().isEmpty());
        library->focusSearch();
        QVERIFY(search->isVisible());
        QCOMPARE(library->width(), expandedWidth);
        capture(window, QStringLiteral("ux-welcome.png"));
        auto* button = window.findChild<QPushButton*>(start);
        QVERIFY(button && button->isEnabled());
        button->click();
        QTRY_VERIFY_WITH_TIMEOUT(
            session.settled() && nodes(session).size() == 2 && edges(session).size() == 1, 5000);
        auto* scene = window.findChild<nodivu::GraphScene*>();
        const auto output = idFor(session, QStringLiteral("output"));
        QString source;
        for (const auto& node : nodes(session))
            if (node.toObject().value(QStringLiteral("id")).toString() != output)
                source = node.toObject().value(QStringLiteral("id")).toString();
        QTRY_VERIFY(scene->nodeItem(source)->pos().x() < scene->nodeItem(output)->pos().x());
        for (const auto& node : nodes(session))
            QVERIFY(node.toObject()
                        .value(QStringLiteral("block"))
                        .toObject()
                        .value(QStringLiteral("endpoint_id"))
                        .toString()
                        .isEmpty());
        QVERIFY(session.snapshot().value(QStringLiteral("engine_state")).toString() !=
                QStringLiteral("running"));
        capture(window, QStringLiteral("ux-start-") + start + QStringLiteral(".png"));
    }
    void paletteNavigatesSectionsAndCanvasAddsAtTheRequestedPosition() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        nodivu::MainWindow window(pathsFor(profile));
        window.show();
        auto& session = window.session();
        auto* projects = window.findChild<nodivu::ProjectController*>();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !projects->busy(), 10000);
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("commandPaletteDialog"));
            if (!dialog)
                qFatal("Palette missing");
            auto* search = dialog->findChild<QLineEdit*>();
            search->setText(QStringLiteral("recuperacao"));
            capture(*dialog, QStringLiteral("ux-palette.png"));
            QTest::keyClick(search, Qt::Key_Return);
        });
        window.activateWindow();
        window.setFocus();
        QTest::keyClick(&window, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        auto* panel = window.findChild<nodivu::SystemPanel*>();
        auto* tabs = panel->findChild<QTabBar*>(QStringLiteral("systemSections"));
        QTRY_VERIFY(panel->isVisible() && tabs->currentIndex() == 1);
        capture(window, QStringLiteral("ux-system-project.png"));
        panel->openApplication();
        QCOMPARE(tabs->currentIndex(), 2);
        QVERIFY(panel->findChild<QPushButton*>(QStringLiteral("systemDiagnostics"))->isVisible());
        capture(window, QStringLiteral("ux-system-app.png"));
        panel->openMonitor();
        QCOMPARE(tabs->currentIndex(), 0);
        QVERIFY(panel->findChild<QComboBox*>(QStringLiteral("monitorDevice"))->isVisible());
        capture(window, QStringLiteral("ux-system-audio.png"));
        panel->hide();
        auto* scene = window.findChild<nodivu::GraphScene*>();
        auto* view = window.findChild<nodivu::GraphView*>();
        const auto position = view->mapToScene(QPoint(180, 180));
        QSignalSpy add(scene, &nodivu::GraphScene::addAtRequested);
        QTimer::singleShot(0, &window, [&] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu)
                qFatal("Canvas menu missing");
            for (auto* item : menu->actions())
                if (item->text() == QStringLiteral("Processamento")) {
                    menu->setActiveAction(item);
                    QTest::keyClick(menu, Qt::Key_Right);
                    auto* child = item->menu();
                    for (auto* option : child->actions())
                        if (option->text() == QStringLiteral("Ganho")) {
                            child->setActiveAction(option);
                            QTest::keyClick(child, Qt::Key_Return);
                            return;
                        }
                }
            qFatal("Gain menu missing");
        });
        QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(180, 180),
                                view->viewport()->mapToGlobal(QPoint(180, 180)));
        QApplication::sendEvent(view->viewport(), &event);
        QTRY_VERIFY(session.settled() && nodes(session).size() == 1);
        QCOMPARE(add.count(), 1);
        QCOMPARE(scene->nodeItem(idFor(session, QStringLiteral("gain")))->pos(), position);
    }
    void signalMessagesAndFeedbackKeepFailuresSpecific() {
        nodivu::NodeCatalog catalog;
        QJsonObject node{{QStringLiteral("id"), QStringLiteral("out")},
                         {QStringLiteral("block"),
                          QJsonObject{{QStringLiteral("kind"), QStringLiteral("output")}}}};
        QJsonObject snapshot{
            {QStringLiteral("graph"), QJsonObject{{QStringLiteral("nodes"), QJsonArray{node}},
                                                  {QStringLiteral("edges"), QJsonArray{}}}}};
        QCOMPARE(nodivu::nodeSignalStatus(snapshot, catalog.describe(node), node, {}, true, 0),
                 QStringLiteral("Selecione uma saída"));
        QCOMPARE(nodivu::nodeSignalStatus(snapshot, catalog.describe(node), node, {}, false, 0),
                 QStringLiteral("Motor desconectado"));
        QJsonObject tone{
            {QStringLiteral("id"), QStringLiteral("tone")},
            {QStringLiteral("block"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("tone")},
                                                  {QStringLiteral("enabled"), false}}}};
        QCOMPARE(nodivu::nodeSignalStatus(snapshot, catalog.describe(tone), tone, {}, true, 0),
                 QStringLiteral("Gerador desligado"));
        nodivu::FeedbackBanner banner(nullptr);
        banner.setText(QStringLiteral("JSON vazio ou acima do limite."));
        banner.show();
        QVERIFY(banner.findChild<QLabel*>(QStringLiteral("feedbackSummary"))
                    ->text()
                    .contains(QStringLiteral("ler os dados")));
        QCOMPARE(banner.text(), QStringLiteral("JSON vazio ou acima do limite."));
        banner.findChild<QToolButton*>(QStringLiteral("feedbackDetailsToggle"))->click();
        banner.findChild<QToolButton*>(QStringLiteral("feedbackClose"))->click();
        QVERIFY(!banner.isVisible());
        const auto luminance = [](const QColor& color) {
            const auto c = [](double v) {
                return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
            };
            return .2126 * c(color.redF()) + .7152 * c(color.greenF()) + .0722 * c(color.blueF());
        };
        QVERIFY((luminance(nodivu::Theme::muted()) + .05) /
                    (luminance(nodivu::Theme::panel()) + .05) >=
                4.5);
        QVERIFY((luminance(nodivu::Theme::mint()) + .05) / (luminance(QColor(18, 58, 48)) + .05) >=
                4.5);
        for (auto icon :
             {nodivu::StudioIcon::Microphone, nodivu::StudioIcon::Apps, nodivu::StudioIcon::Close})
            QVERIFY(!nodivu::studioIcon(icon).pixmap(24, 24).isNull());
    }
};
QTEST_MAIN(UxTests)
#include "ux_tests.moc"
