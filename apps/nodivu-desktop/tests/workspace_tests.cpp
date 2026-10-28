#include "contracts/audio_state.hpp"
#include "contracts/monitor_state.hpp"
#include "contracts/virtual_cable.hpp"
#include "features/graph/empty_graph_widget.hpp"
#include "features/graph/graph_scene.hpp"
#include "features/graph/graph_view.hpp"
#include "features/graph/node_content.hpp"
#include "features/graph/routing_panel.hpp"
#include "features/inspector/inspector_panel.hpp"
#include "features/library/library_block_button.hpp"
#include "features/library/library_panel.hpp"
#include "features/projects/project_controller.hpp"
#include "features/updates/update_panel.hpp"
#include "features/workspace/audio_status_panel.hpp"
#include "features/workspace/main_window.hpp"
#include "features/workspace/system_panel.hpp"
#include "ui/controls/action_availability.hpp"
#include "ui/controls/worker_controls.hpp"
#include "ui/theme/theme.hpp"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDataStream>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGraphicsItem>
#include <QGraphicsProxyWidget>
#include <QHelpEvent>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSlider>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QWheelEvent>
#include <QtTest>

class WorkspaceTests final : public QObject {
    Q_OBJECT
  private slots:
    void menusNavigateAndOpenExactRecentPathWithSharedSaveShortcut() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        nodivu::Theme::apply(*qobject_cast<QApplication*>(QCoreApplication::instance()));
        nodivu::MainWindow window(paths);
        window.show();
        auto& session = window.session();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 10000);
        auto* projects = window.findChild<nodivu::ProjectController*>();
        auto* menus = window.findChild<QMenuBar*>(QStringLiteral("studioMenuBar"));
        auto* file = window.findChild<QMenu*>(QStringLiteral("fileMenu"));
        auto* recents = window.findChild<QMenu*>(QStringLiteral("projectRecents"));
        auto* devices = window.findChild<QMenu*>(QStringLiteral("devicesMenu"));
        QVERIFY(projects && menus && file && recents && devices);
        QVERIFY(menus->y() >= window.findChild<QWidget*>(QStringLiteral("studioHeader"))->height());
        QVERIFY(!recents->menuAction()->isEnabled());
        window.resize(960, 650);
        QCoreApplication::processEvents();
        for (auto* item : menus->actions())
            QVERIFY(menus->rect().contains(menus->actionGeometry(item)));
        const auto capture = [&](const QString& name, QWidget* widget) {
            const auto directory = qEnvironmentVariable("NODIVU_WORKSPACE_SCREENSHOTS");
            return directory.isEmpty() || widget->grab().save(QDir(directory).filePath(name));
        };
        QVERIFY(capture(QStringLiteral("menus-960.png"), &window));
        window.resize(1440, 900);
        session.addNode(QStringLiteral("gain"));
        QTRY_VERIFY(session.settled());
        const auto nodes = [&] {
            return session.snapshot()
                .value(QStringLiteral("graph"))
                .toObject()
                .value(QStringLiteral("nodes"))
                .toArray();
        };
        QTRY_COMPARE(nodes().size(), 1);
        const auto gain = nodes().first().toObject().value(QStringLiteral("id")).toString();
        QVERIFY(QDir().mkpath(profile.filePath(QStringLiteral("A"))));
        QVERIFY(QDir().mkpath(profile.filePath(QStringLiteral("B"))));
        const auto first = profile.filePath(QStringLiteral("A/Projeto & voz.nodivu.json"));
        const auto second = profile.filePath(QStringLiteral("B/Projeto & voz.nodivu.json"));
        projects->save(first);
        QTRY_VERIFY(!projects->busy() && !projects->dirty());
        session.updateField(gain, QStringLiteral("gain_db"), -18.0);
        QTRY_VERIFY(session.settled() && projects->dirty());
        window.activateWindow();
        window.setFocus();
        // Ctrl+S writes the existing file without opening another picker.
        QSignalSpy saves(&window, &nodivu::MainWindow::saveRequested);
        QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);
        QCOMPARE(saves.count(), 1);
        QTRY_VERIFY(!projects->busy() && !projects->dirty());
        QVERIFY(!window.findChild<QFileDialog*>());
        session.addNode(QStringLiteral("meter"));
        QTRY_VERIFY(session.settled() && nodes().size() == 2);
        projects->save(second);
        QTRY_VERIFY(!projects->busy() && !projects->dirty());
        QVERIFY(recents->menuAction()->isEnabled());
        QCOMPARE(recents->actions().size(), 2);
        QVERIFY(recents->actions().first()->text() != recents->actions().last()->text());
        QCOMPARE(recents->actions().last()->data().toString(), first);
        QVERIFY(capture(QStringLiteral("menus-studio.png"), &window));
        // Exercise actual popup navigation: right opens a submenu, Enter opens its exact path.
        QTest::mouseClick(menus, Qt::LeftButton, Qt::NoModifier,
                          menus->actionGeometry(file->menuAction()).center());
        QTRY_VERIFY(file->isVisible());
        file->setActiveAction(recents->menuAction());
        QTest::keyClick(file, Qt::Key_Right);
        QTRY_VERIFY(recents->isVisible());
        recents->setActiveAction(recents->actions().last());
        QVERIFY(capture(QStringLiteral("menus-file.png"), file));
        QVERIFY(capture(QStringLiteral("menus-recents.png"), recents));
        QTest::keyClick(recents, Qt::Key_Return);
        QTRY_VERIFY(!projects->busy() && session.settled() && projects->path() == first);
        QCOMPARE(nodes().size(), 1);
        QCOMPARE(session.node(gain)
                     .value(QStringLiteral("block"))
                     .toObject()
                     .value(QStringLiteral("gain_db"))
                     .toDouble(),
                 -18.0);
        const auto before = projects->document();
        auto* listen = window.findChild<QAction*>(QStringLiteral("listenShortcut"));
        listen->trigger();
        QVERIFY(window.findChild<nodivu::SystemPanel*>()->isVisible());
        QCOMPARE(projects->document(), before);
    }
    void spacePanMovesCameraWithoutMovingNodesOrConsumingEditorInput() {
        QGraphicsScene scene;
        scene.setSceneRect(-2000, -2000, 6000, 6000);
        auto* node = scene.addRect(QRectF(0, 0, 220, 140));
        node->setFlag(QGraphicsItem::ItemIsMovable);
        node->setPos(200, 200);
        nodivu::GraphView view(&scene);
        view.resize(800, 600);
        view.show();
        view.restoreCamera(
            {{QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}, {QStringLiteral("scale"), 1}});
        const auto originalPosition = node->pos();
        const auto camera = view.camera();
        const auto start = view.mapFromScene(node->sceneBoundingRect().center());
        QTest::keyPress(&view, Qt::Key_Space);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(view.viewport(), start + QPoint(70, 40));
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                            start + QPoint(70, 40));
        QTest::keyRelease(&view, Qt::Key_Space);
        QVERIFY(view.camera() != camera);
        QCOMPARE(node->pos(), originalPosition);
        QTest::keyPress(&view, Qt::Key_Space);
        QEvent deactivate(QEvent::WindowDeactivate);
        QApplication::sendEvent(&view, &deactivate);
        QVERIFY(view.viewport()->cursor().shape() != Qt::OpenHandCursor);
        auto* editor = new QLineEdit;
        auto* proxy = scene.addWidget(editor);
        proxy->setPos(50, 50);
        editor->setFocus();
        QTest::keyClicks(editor, QStringLiteral("a b"));
        QCOMPARE(editor->text(), QStringLiteral("a b"));
        const auto beforeInput = view.camera();
        QTest::keyClick(editor, Qt::Key_Space);
        QCOMPARE(view.camera(), beforeInput);
    }
    void virtualCablePresentationKeepsFeedbackSpecificWithoutSelectingDevices() {
        // Presentation fixtures only. No endpoint is opened or selected in the backend.
        QJsonArray devices{
            QJsonObject{
                {QStringLiteral("endpoint_id"), QStringLiteral("render-opaque")},
                {QStringLiteral("flow"), QStringLiteral("render")},
                {QStringLiteral("state"), QStringLiteral("active")},
                {QStringLiteral("name"), QStringLiteral("CABLE Input (VB-Audio Virtual Cable)")}},
            QJsonObject{
                {QStringLiteral("endpoint_id"), QStringLiteral("capture-opaque")},
                {QStringLiteral("flow"), QStringLiteral("capture")},
                {QStringLiteral("state"), QStringLiteral("active")},
                {QStringLiteral("name"), QStringLiteral("CABLE Output (VB-Audio Virtual Cable)")}}};
        QJsonArray nodes{QJsonObject{
            {QStringLiteral("block"),
             QJsonObject{{QStringLiteral("kind"), QStringLiteral("output")},
                         {QStringLiteral("endpoint_id"), QStringLiteral("render-opaque")}}}}};
        QJsonObject snapshot{
            {QStringLiteral("graph"), QJsonObject{{QStringLiteral("nodes"), nodes}}},
            {QStringLiteral("engine_state"), QStringLiteral("running")},
            {QStringLiteral("signal"), 1}};
        const auto original = snapshot;
        QCOMPARE(nodivu::outputPresentation(QStringLiteral("render-opaque"), devices).title,
                 QStringLiteral("Microfone virtual"));
        QVERIFY(nodivu::cableRouteMessage(snapshot, devices)
                    .contains(QStringLiteral("Envio ao cabo ativo")));
        snapshot.insert(QStringLiteral("signal"), 0);
        QVERIFY(nodivu::cableRouteMessage(snapshot, devices).contains(QStringLiteral("silêncio")));
        snapshot.insert(QStringLiteral("muted"), true);
        QVERIFY(
            nodivu::cableRouteMessage(snapshot, devices).contains(QStringLiteral("silenciado")));
        snapshot.insert(QStringLiteral("suspended"), true);
        QVERIFY(
            nodivu::cableRouteMessage(snapshot, devices).contains(QStringLiteral("Preparando")));
        nodes.append(QJsonObject{
            {QStringLiteral("block"),
             QJsonObject{{QStringLiteral("kind"), QStringLiteral("capture")},
                         {QStringLiteral("endpoint_id"), QStringLiteral("capture-opaque")}}}});
        snapshot.insert(QStringLiteral("graph"), QJsonObject{{QStringLiteral("nodes"), nodes}});
        QVERIFY(nodivu::cableRouteMessage(snapshot, devices)
                    .contains(QStringLiteral("pode criar retorno")));
        auto destination = devices[0].toObject();
        destination.insert(QStringLiteral("name"), QStringLiteral("Cabo renomeado"));
        devices[0] = destination;
        QCOMPARE(nodivu::outputPresentation(QStringLiteral("render-opaque"), devices).title,
                 QStringLiteral("Saída de áudio"));
        QVERIFY(nodivu::cableRouteMessage(original, devices)
                    .contains(QStringLiteral("identificação manual")));
        destination.insert(QStringLiteral("state"), QStringLiteral("unplugged"));
        devices[0] = destination;
        QVERIFY(nodivu::cableRouteMessage(original, devices)
                    .contains(QStringLiteral("Nenhum outro dispositivo")));
        QCOMPARE(original.value(QStringLiteral("graph"))
                     .toObject()
                     .value(QStringLiteral("nodes"))
                     .toArray()
                     .first()
                     .toObject()
                     .value(QStringLiteral("block"))
                     .toObject()
                     .value(QStringLiteral("endpoint_id"))
                     .toString(),
                 QStringLiteral("render-opaque"));
        nodivu::BackendClient backend;
        nodivu::SessionController session(backend);
        nodivu::NodeItem output(QStringLiteral("output"),
                                session.catalog().describe(nodes.first().toObject()), session);
        output.setState(nodes.first().toObject().value(QStringLiteral("block")).toObject(), 0);
        destination.insert(QStringLiteral("name"),
                           QStringLiteral("CABLE Input (VB-Audio Virtual Cable)"));
        devices[0] = destination;
        output.synchronizeControls(devices, false, {});
        QCOMPARE(output.property("accessibleName").toString(), QStringLiteral("Microfone virtual"));
        output.setState({{QStringLiteral("kind"), QStringLiteral("output")}}, 0);
        output.synchronizeControls(devices, false, {});
        QCOMPARE(output.property("accessibleName").toString(), QStringLiteral("Saída de áudio"));
    }
    void checkpointReportsLastSuccessfulCopyAndSaveClearsIt() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        nodivu::MainWindow window(paths);
        window.show();
        auto* projects = window.findChild<nodivu::ProjectController*>();
        auto& session = window.session();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !projects->busy(), 10000);
        QVERIFY(!projects->recoverySavedAt().isValid());
        session.addNode(QStringLiteral("gain"));
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(projects->recoverySavedAt().isValid() && !projects->busy(), 10000);
        QVERIFY(projects->recoveryStatus().contains(QStringLiteral("Cópia automática:")));
        auto* panel = window.findChild<nodivu::SystemPanel*>();
        panel->show();
        auto* status = panel->findChild<QLabel*>(QStringLiteral("systemRecoveryStatus"));
        QVERIFY(status &&
                status->text().contains(projects->recoverySavedAt().toLocalTime().toString(
                    QStringLiteral("dd/MM/yyyy HH:mm:ss"))));
        projects->save(QDir(profile.path()).filePath(QStringLiteral("saved.nodivu.json")));
        QTRY_VERIFY_WITH_TIMEOUT(!projects->busy() && !projects->dirty(), 5000);
        QVERIFY(!projects->recoverySavedAt().isValid());
        QVERIFY(status->text().contains(QStringLiteral("a cada 5 segundos")));
    }
    void addedNodesStayVisibleWithPanZoomOverlaysAndExplicitDrop() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        nodivu::MainWindow window(paths);
        window.show();
        auto& session = window.session();
        auto* scene = window.findChild<nodivu::GraphScene*>();
        auto* view = window.findChild<nodivu::GraphView*>();
        auto* panel = window.findChild<nodivu::SystemPanel*>();
        QVERIFY(scene && view && panel);
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 10000);
        QString id;
        connect(&session, &nodivu::SessionController::nodeAdded, &window,
                [&id](const QString& added) { id = added; });
        const auto visible = [&](const QString& node) {
            auto* item = scene->nodeItem(node);
            return item && item->isSelected() &&
                   view->availableViewportRect()
                       .adjusted(-1, -1, 1, 1)
                       .contains(view->mapFromScene(item->sceneBoundingRect()).boundingRect());
        };
        view->restoreCamera({{QStringLiteral("x"), -6000},
                             {QStringLiteral("y"), -4200},
                             {QStringLiteral("scale"), .75}});
        auto* gain = window.findChild<nodivu::LibraryBlockButton*>(QStringLiteral("gain"));
        QVERIFY(gain);
        QTest::mouseClick(gain, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!id.isEmpty() && session.settled() && visible(id), 5000);
        const auto firstId = id;
        const auto firstPosition = scene->nodeItem(firstId)->pos();
        QCOMPARE(view->transform().m11(), .75);
        const auto beforePan = view->camera();
        const QPoint point = view->viewport()->rect().center();
        QWheelEvent wheel(point, view->viewport()->mapToGlobal(point), {}, QPoint(0, 120),
                          Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view->viewport(), &wheel);
        QCOMPARE(view->camera().value(QStringLiteral("scale")),
                 beforePan.value(QStringLiteral("scale")));
        QVERIFY(view->camera().value(QStringLiteral("x")) != beforePan.value(QStringLiteral("x")));
        view->restoreCamera({{QStringLiteral("x"), 3500},
                             {QStringLiteral("y"), 2400},
                             {QStringLiteral("scale"), 1.0}});
        session.addNode(QStringLiteral("mixer"));
        QTRY_VERIFY_WITH_TIMEOUT(id != firstId && session.settled() && visible(id), 5000);
        QCOMPARE(scene->nodeItem(firstId)->pos(), firstPosition);
        const auto mixerId = id;
        const auto mixerPosition = scene->nodeItem(id)->pos();
        panel->show();
        session.addNode(QStringLiteral("plugin"), QStringLiteral("org.nodivu.peak-limiter"));
        QTRY_VERIFY_WITH_TIMEOUT(id != mixerId && session.settled() && visible(id), 5000);
        const auto limiterId = id;
        auto* inspector = window.findChild<nodivu::InspectorPanel*>();
        QVERIFY(inspector && inspector->isVisible());
        const QRect systemBounds(view->viewport()->mapFromGlobal(panel->mapToGlobal(QPoint())),
                                 panel->size());
        QVERIFY(!systemBounds.intersects(
            view->mapFromScene(scene->nodeItem(id)->sceneBoundingRect()).boundingRect()));
        panel->hide();
        view->zoomBy(2 / view->transform().m11());
        session.addNode(QStringLiteral("plugin"), QStringLiteral("org.nodivu.mp3-player"));
        QTRY_VERIFY_WITH_TIMEOUT(id != limiterId && session.settled() && visible(id), 5000);
        QVERIFY(view->transform().m11() <= 2.0);
        QCOMPARE(scene->nodeItem(firstId)->pos(), firstPosition);
        QCOMPARE(scene->nodeItem(mixerId)->pos(), mixerPosition);
        const auto playerId = id;
        const auto dropPoint = view->availableViewportRect().topLeft() + QPoint(35, 35);
        const auto expectedPosition = view->mapToScene(dropPoint);
        QMimeData mime;
        mime.setData(QStringLiteral("application/x-nodivu-block"),
                     QByteArray("{\"kind\":\"tone\"}"));
        QDragEnterEvent enter(dropPoint, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(dropPoint, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &drop);
        QTRY_VERIFY_WITH_TIMEOUT(id != playerId && session.settled() && visible(id), 5000);
        QCOMPARE(scene->nodeItem(id)->pos(), expectedPosition);
        const auto evidence = qEnvironmentVariable("NODIVU_WORKSPACE_SCREENSHOTS");
        if (!evidence.isEmpty())
            QVERIFY(window.grab().save(
                QDir(evidence).filePath(QStringLiteral("new-node-visible.png"))));
        QVERIFY(session.snapshot().value(QStringLiteral("engine_state")).toString() !=
                QStringLiteral("running"));
    }
    void canvasHintsCycleWithoutMovingTheCanvasAndPauseWhileReading() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        nodivu::MainWindow window(paths);
        window.resize(1100, 680);
        window.show();
        window.activateWindow();
        QTRY_VERIFY_WITH_TIMEOUT(window.session().settled() && window.isActiveWindow(), 10000);
        auto* hint = window.findChild<QLabel*>(QStringLiteral("connectionHint"));
        auto* timer = window.findChild<QTimer*>(QStringLiteral("canvasHintTimer"));
        auto* view = window.findChild<nodivu::GraphView*>();
        QVERIFY(hint && timer && view);
        QCOMPARE(timer->interval(), 8000);
        QTest::mouseMove(&window, QPoint(20, 20));
        QTRY_VERIFY(timer->isActive());
        timer->stop();
        const auto original = hint->text();
        const auto canvasBounds = view->geometry();
        QSet<QString> hints;
        for (int index = 0; index < 4; ++index) {
            hints.insert(hint->text());
            const auto evidence = qEnvironmentVariable("NODIVU_WORKSPACE_SCREENSHOTS");
            if (!evidence.isEmpty())
                QVERIFY(window.grab().save(
                    QDir(evidence).filePath(QStringLiteral("canvas-tip-%1.png").arg(index))));
            QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
            QCoreApplication::processEvents();
            QCOMPARE(view->geometry(), canvasBounds);
        }
        QCOMPARE(hints.size(), 4);
        QCOMPARE(hint->text(), original);
        QVERIFY(hint->toolTip().contains(QStringLiteral("Ganho")));
        QVERIFY(hint->accessibleDescription().contains(QStringLiteral("Shift+roda")));

        timer->setInterval(60);
        timer->start();
        QTest::mouseMove(hint, hint->rect().center());
        QTRY_VERIFY(!timer->isActive());
        const auto paused = hint->text();
        QTest::qWait(180);
        QCOMPARE(hint->text(), paused);
        QTest::mouseMove(&window, QPoint(20, 20));
        QTRY_VERIFY(timer->isActive());
        QTRY_VERIFY(hint->text() != paused);
        window.hide();
        QVERIFY(!timer->isActive());
        const auto hidden = hint->text();
        QTest::qWait(180);
        QCOMPARE(hint->text(), hidden);
        window.show();
        QTRY_VERIFY(timer->isActive());
    }
    void audioBannerSelectsOutputWithoutSelectingADeviceAndFooterTracksRevision() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        nodivu::MainWindow window(paths);
        window.show();
        auto& session = window.session();
        auto* panel = window.findChild<nodivu::AudioStatusPanel*>();
        auto* action = window.findChild<QPushButton*>(QStringLiteral("audioAction"));
        auto* title = window.findChild<QLabel*>(QStringLiteral("audioTitle"));
        auto* revision = window.findChild<QLabel*>(QStringLiteral("projectRevision"));
        QVERIFY(panel && action && title && revision);
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !session.devicesRefreshing(), 10000);
        QCOMPARE(title->text(), QStringLiteral("Comece pelo seu áudio"));
        QVERIFY(!action->isVisible());
        session.addNode(QStringLiteral("gain"));
        QTRY_VERIFY(session.settled());
        QCOMPARE(title->text(), QStringLiteral("Escolha uma saída"));
        QCOMPARE(action->text(), QStringLiteral("Selecionar saída"));
        QTest::mouseClick(action, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(session.snapshot()
                                      .value(QStringLiteral("graph"))
                                      .toObject()
                                      .value(QStringLiteral("nodes"))
                                      .toArray()
                                      .size(),
                                  2, 5000);
        QTRY_VERIFY(session.settled() && action->isEnabled());
        QJsonObject node;
        for (const auto& value : session.snapshot()
                                     .value(QStringLiteral("graph"))
                                     .toObject()
                                     .value(QStringLiteral("nodes"))
                                     .toArray())
            if (value.toObject()
                    .value(QStringLiteral("block"))
                    .toObject()
                    .value(QStringLiteral("kind"))
                    .toString() == QStringLiteral("output"))
                node = value.toObject();
        QCOMPARE(
            node.value(QStringLiteral("block")).toObject().value(QStringLiteral("kind")).toString(),
            QStringLiteral("output"));
        QVERIFY(node.value(QStringLiteral("block"))
                    .toObject()
                    .value(QStringLiteral("endpoint_id"))
                    .toString()
                    .isEmpty());
        QCOMPARE(revision->text(),
                 QStringLiteral("Revisão %1")
                     .arg(session.snapshot().value(QStringLiteral("revision")).toInteger()));
        QTest::mouseClick(action, Qt::LeftButton);
        QTest::qWait(120);
        QCOMPARE(session.snapshot()
                     .value(QStringLiteral("graph"))
                     .toObject()
                     .value(QStringLiteral("nodes"))
                     .toArray()
                     .size(),
                 2);
        auto* footer = window.findChild<QFrame*>(QStringLiteral("projectFooter"));
        QVERIFY(footer && footer->isVisible());
        QVERIFY(footer->height() >= 26);
        const auto evidence = qEnvironmentVariable("NODIVU_WORKSPACE_SCREENSHOTS");
        if (!evidence.isEmpty())
            QVERIFY(window.grab().save(
                QDir(evidence).filePath(QStringLiteral("audio-output-footer.png"))));
    }
    void audioPresentationKeepsUnavailableOutputsErrorsAndReadyStatesExplicit() {
        // Presentation fixture only, without opening an endpoint or claiming hardware
        // audio.
        QJsonObject snapshot;
        QCOMPARE(nodivu::audioState(snapshot, {}, false).action, nodivu::AudioAction::Reconnect);
        QCOMPARE(nodivu::audioState(snapshot, {}, true).action, nodivu::AudioAction::None);
        snapshot.insert(
            QStringLiteral("graph"),
            QJsonObject{{QStringLiteral("nodes"),
                         QJsonArray{QJsonObject{
                             {QStringLiteral("block"),
                              QJsonObject{{QStringLiteral("kind"), QStringLiteral("output")}}}}}}});
        QCOMPARE(nodivu::audioState(snapshot, {}, true).action, nodivu::AudioAction::Output);
        snapshot.insert(
            QStringLiteral("graph"),
            QJsonObject{
                {QStringLiteral("nodes"),
                 QJsonArray{QJsonObject{
                     {QStringLiteral("block"),
                      QJsonObject{{QStringLiteral("kind"), QStringLiteral("output")},
                                  {QStringLiteral("endpoint_id"), QStringLiteral("opaque")}}}}}}});
        const QJsonArray devices{
            QJsonObject{{QStringLiteral("endpoint_id"), QStringLiteral("opaque")},
                        {QStringLiteral("flow"), QStringLiteral("render")},
                        {QStringLiteral("state"), QStringLiteral("active")},
                        {QStringLiteral("name"), QStringLiteral("Fones")}}};
        QCOMPARE(nodivu::audioState(snapshot, {}, true).title,
                 QStringLiteral("Saída indisponível"));
        QCOMPARE(nodivu::audioState(snapshot, devices, true, QStringLiteral("erro de descoberta"))
                     .action,
                 nodivu::AudioAction::Refresh);
        snapshot.insert(QStringLiteral("engine_state"), QStringLiteral("starting"));
        QCOMPARE(nodivu::audioState(snapshot, devices, true).action, nodivu::AudioAction::None);
        snapshot.insert(QStringLiteral("engine_state"), QStringLiteral("error"));
        snapshot.insert(
            QStringLiteral("last_error"),
            QJsonObject{{QStringLiteral("message"), QStringLiteral("Falha <b>real</b>")}});
        auto state = nodivu::audioState(snapshot, devices, true);
        QCOMPARE(state.action, nodivu::AudioAction::Retry);
        nodivu::AudioStatusPanel panel;
        panel.resize(700, 90);
        panel.show();
        panel.synchronize(state);
        auto* detail = panel.findChild<QLabel*>(QStringLiteral("audioDetail"));
        QVERIFY(detail && detail->isVisible());
        QCOMPARE(detail->textFormat(), Qt::PlainText);
        QCOMPARE(detail->text(), QStringLiteral("Falha <b>real</b>"));
        QSignalSpy action(&panel, &nodivu::AudioStatusPanel::actionRequested);
        panel.synchronize(state, QStringLiteral("Aguarde a operação atual."));
        panel.findChild<QPushButton*>(QStringLiteral("audioAction"))->click();
        QCOMPARE(action.count(), 0);
        snapshot.remove(QStringLiteral("last_error"));
        snapshot.insert(QStringLiteral("engine_state"), QStringLiteral("running"));
        snapshot.insert(QStringLiteral("signal"), 0);
        state = nodivu::audioState(snapshot, devices, true);
        QVERIFY(state.ready);
        panel.synchronize(state);
        QVERIFY(!detail->isVisible());
        QCOMPARE(panel.property("ready").toBool(), true);
        snapshot.insert(QStringLiteral("muted"), true);
        QCOMPARE(nodivu::audioState(snapshot, devices, true).action, nodivu::AudioAction::Unmute);
        snapshot.remove(QStringLiteral("muted"));
        snapshot.insert(
            QStringLiteral("capture_error"),
            QJsonObject{{QStringLiteral("message"), QStringLiteral("Microfone ocupado")}});
        state = nodivu::audioState(snapshot, devices, true);
        QVERIFY(state.ready && state.action == nodivu::AudioAction::Retry);
        const auto evidence = qEnvironmentVariable("NODIVU_WORKSPACE_SCREENSHOTS");
        if (!evidence.isEmpty())
            QVERIFY(panel.grab().save(
                QDir(evidence).filePath(QStringLiteral("audio-ready-fixture.png"))));
    }
    void headerAndSystemActionsMatchElectronNavigation() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        nodivu::MainWindow window(paths);
        window.show();
        auto& session = window.session();
        auto* panel = window.findChild<nodivu::SystemPanel*>();
        auto* scene = window.findChild<nodivu::GraphScene*>();
        auto* inspector = window.findChild<nodivu::InspectorPanel*>();
        auto* projects = window.findChild<nodivu::ProjectController*>();
        QVERIFY(panel && scene && inspector && projects);
        const auto button = [&window](const char* name) {
            return window.findChild<QPushButton*>(QLatin1String(name));
        };
        const auto action = [&window](const char* name) {
            return window.findChild<QAction*>(QLatin1String(name));
        };
        auto* settings = action("settingsToggle");
        auto* listen = action("listenShortcut");
        auto* inspect = action("inspectSelected");
        auto* routing = action("routingToggle");
        auto* mute = button("muteAll");
        QVERIFY(settings && listen && inspect && routing && mute);
        QVERIFY(!action("projectOpen")->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 10000);
        QVERIFY(action("projectOpen")->isEnabled() && action("projectSave")->isEnabled());
        session.command(QStringLiteral("session.snapshot"), {});
        QVERIFY(!action("projectOpen")->isEnabled() && !action("projectSave")->isEnabled());
        QVERIFY(action("projectSave")->toolTip().contains(QStringLiteral("Aguarde")));
        QTRY_VERIFY(session.settled() && action("projectSave")->isEnabled());
        QVERIFY(!inspect->isEnabled() && !routing->isEnabled() && !mute->isEnabled());
        QVERIFY(mute->toolTip().contains(QStringLiteral("áudio ativo")));
        QSignalSpy muted(mute, &QPushButton::toggled);
        mute->click();
        QCOMPARE(muted.count(), 0);
        QVERIFY(mute->accessibleDescription().contains(QStringLiteral("Indisponível")));
        QHelpEvent help(QEvent::ToolTip, QPoint(2, 2), mute->mapToGlobal(QPoint(2, 2)));
        QApplication::sendEvent(mute, &help);
        QTRY_VERIFY_WITH_TIMEOUT(QToolTip::isVisible(), 3000);
        QToolTip::hideText();
        QVERIFY(window.findChild<QFrame*>(QStringLiteral("projectAreaDot"))->isVisible());
        const auto before = projects->document();
        settings->trigger();
        QVERIFY(panel->isVisible());
        QCOMPARE(window.findChildren<nodivu::SystemPanel*>().size(), 1);
        auto* device = panel->findChild<QComboBox*>(QStringLiteral("monitorDevice"));
        auto* monitor = button("monitorToggle");
        QVERIFY(device && monitor);
        QVERIFY(!monitor->isEnabled() && !device->isEnabled());
        QVERIFY(monitor->toolTip().contains(QStringLiteral("Saída de áudio")));
        auto* scroll = panel->findChild<QScrollArea*>(QStringLiteral("systemScroll"));
        QVERIFY(scroll);
        const auto evidence = qEnvironmentVariable("NODIVU_WORKSPACE_SCREENSHOTS");
        const auto capture = [&](const QString& name) {
            if (!evidence.isEmpty())
                return window.grab().save(QDir(evidence).filePath(name));
            return true;
        };
        QTest::qWait(50);
        QVERIFY(capture(QStringLiteral("system-top.png")));
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        listen->trigger();
        QTest::qWait(50);
        auto* monitorSection = panel->findChild<QWidget*>(QStringLiteral("monitorSection"));
        const auto monitorBounds =
            QRect(monitorSection->mapTo(scroll->viewport(), QPoint()), monitorSection->size());
        QVERIFY(scroll->viewport()->rect().intersects(monitorBounds));
        QCOMPARE(projects->document(), before);
        QVERIFY(capture(QStringLiteral("system-listen.png")));
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QTest::qWait(30);
        panel->openApplication();
        QVERIFY(capture(QStringLiteral("system-bottom.png")));
        auto* update = button("updateAction");
        QVERIFY(update && !update->isEnabled());
        QVERIFY(update->toolTip().contains(QStringLiteral("canal")));
        settings->trigger();
        QVERIFY(!panel->isVisible());
        QString id;
        connect(&session, &nodivu::SessionController::nodeAdded, &window,
                [&id](const QString& added) { id = added; });
        session.addNode(QStringLiteral("gain"));
        QTRY_VERIFY_WITH_TIMEOUT(!id.isEmpty() && session.settled(), 5000);
        const auto gain = id;
        QTRY_VERIFY(inspect->isEnabled() && routing->isEnabled());
        inspect->trigger();
        QVERIFY(inspector->isVisible());
        QCOMPARE(inspector->selectedId(), gain);
        scene->clearSelection();
        QVERIFY(!inspect->isEnabled());
        routing->trigger();
        auto* dock = window.findChild<QDockWidget*>(QStringLiteral("routingDock"));
        QVERIFY(dock && dock->isVisible());
        auto* connectButton = button("routingConnect");
        QVERIFY(connectButton && !connectButton->isEnabled());
        session.addNode(QStringLiteral("tone"));
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && id != gain, 5000);
        const auto tone = id;
        QTRY_VERIFY(connectButton->isEnabled());
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("connectBlocksDialog"));
            if (!dialog)
                qFatal("Expected connect dialog");
            for (auto* combo : dialog->findChildren<QComboBox*>()) {
                if (combo->accessibleName() == QStringLiteral("Bloco de origem"))
                    combo->setCurrentIndex(combo->findData(tone));
                if (combo->accessibleName() == QStringLiteral("Bloco de destino"))
                    combo->setCurrentIndex(combo->findData(gain));
            }
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!buttons || !buttons->button(QDialogButtonBox::Ok)->isEnabled())
                qFatal("Expected free input");
            buttons->button(QDialogButtonBox::Ok)->click();
        });
        QTest::mouseClick(connectButton, Qt::LeftButton);
        const auto edges = [&] {
            return session.snapshot()
                .value(QStringLiteral("graph"))
                .toObject()
                .value(QStringLiteral("edges"))
                .toArray();
        };
        QTRY_VERIFY(session.settled() && edges().size() == 1);
        window.findChild<nodivu::GraphView*>()->fitNodes();
        QTest::qWait(50);
        QVERIFY(capture(QStringLiteral("routing-panel.png")));
        dock->hide();
        settings->trigger();
        QTRY_VERIFY(panel->isVisible());
        QPushButton* wire = nullptr;
        for (auto* candidate : panel->findChildren<QPushButton*>())
            if (candidate->property("connection").isValid())
                wire = candidate;
        QVERIFY(wire && wire->isEnabled());
        scroll->ensureWidgetVisible(wire);
        QTest::mouseClick(wire, Qt::LeftButton);
        QTRY_VERIFY(session.settled() && edges().isEmpty());
        panel->hide();
        QVERIFY(!session.snapshot().value(QStringLiteral("muted")).toBool());
        QVERIFY(session.snapshot().value(QStringLiteral("engine_state")).toString() !=
                QStringLiteral("running"));
    }
    void systemRecoveryUsesExistingRestoreAndDiscardFlows_data() {
        QTest::addColumn<bool>("restore");
        QTest::newRow("restore") << true;
        QTest::newRow("discard") << false;
    }
    void systemRecoveryUsesExistingRestoreAndDiscardFlows() {
        QFETCH(bool, restore);
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        const auto directory = QDir(profile.path()).filePath(QStringLiteral("recovery"));
        QVERIFY(QDir().mkpath(directory));
        const auto file = QDir(directory).filePath(QStringLiteral("recovery.nodivu.json"));
        QVERIFY(QFile::copy(
            QStringLiteral(NODIVU_REPOSITORY_DIR) +
                QStringLiteral("/apps/nodivu-desktop/tests/fixtures/electron-project-v1.json"),
            file));
        nodivu::MainWindow window(paths);
        window.show();
        auto* projects = window.findChild<nodivu::ProjectController*>();
        auto* panel = window.findChild<nodivu::SystemPanel*>();
        QVERIFY(projects && panel);
        QTRY_VERIFY_WITH_TIMEOUT(window.session().settled() && !projects->busy(), 10000);
        QVERIFY(projects->recoveryPending());
        QVERIFY(projects->recoverySavedAt().isValid());
        QVERIFY(
            projects->recoveryStatus().contains(projects->recoverySavedAt().toLocalTime().toString(
                QStringLiteral("dd/MM/yyyy HH:mm:ss"))));
        panel->openProject();
        auto* button =
            panel->findChild<QPushButton*>(restore ? QStringLiteral("systemRecoveryRestore")
                                                   : QStringLiteral("systemRecoveryDiscard"));
        QVERIFY(button && button->isVisible() && button->isEnabled());
        if (!restore) {
            QTimer::singleShot(0, &window, [] {
                auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (!prompt)
                    qFatal("Expected recovery confirmation");
                prompt->button(QMessageBox::Cancel)->click();
            });
            button->click();
            QVERIFY(projects->recoveryPending() && QFileInfo::exists(file));
            QTimer::singleShot(0, &window, [] {
                auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (!prompt)
                    qFatal("Expected recovery confirmation");
                prompt->button(QMessageBox::Discard)->click();
            });
        }
        button->click();
        QTRY_VERIFY_WITH_TIMEOUT(
            !projects->recoveryPending() && !projects->busy() && window.session().settled(), 5000);
        QVERIFY(!button->isVisible());
        if (restore) {
            QCOMPARE(window.session()
                         .snapshot()
                         .value(QStringLiteral("graph"))
                         .toObject()
                         .value(QStringLiteral("nodes"))
                         .toArray()
                         .size(),
                     4);
            QVERIFY(projects->dirty());
        } else
            QVERIFY(!QFileInfo::exists(file));
    }
    void monitorRequirementsPreserveOpaqueIdsAndNeverSelectAnotherDevice() {
        QPushButton hint;
        nodivu::setActionAvailability(&hint, QStringLiteral("Plugin <b>externo</b>"),
                                      QStringLiteral("Aguarde <img src=x>"));
        QVERIFY(hint.toolTip().contains(QStringLiteral("&lt;img")));
        QVERIFY(hint.accessibleDescription().contains(QStringLiteral("<img src=x>")));
        // Explicit inventory fixture; never opens a physical output.
        const auto endpoint = [](const QString& id, const QString& name) {
            return QJsonObject{{QStringLiteral("endpoint_id"), id},
                               {QStringLiteral("name"), name},
                               {QStringLiteral("state"), QStringLiteral("active")},
                               {QStringLiteral("flow"), QStringLiteral("render")}};
        };
        const QJsonArray devices{endpoint(QStringLiteral("primary"), QStringLiteral("Fones")),
                                 endpoint(QStringLiteral("secondary"), QStringLiteral("Fones"))};
        QJsonObject snapshot{
            {QStringLiteral("graph"),
             QJsonObject{{QStringLiteral("nodes"),
                          QJsonArray{QJsonObject{
                              {QStringLiteral("block"),
                               QJsonObject{{QStringLiteral("kind"), QStringLiteral("output")},
                                           {QStringLiteral("endpoint_id"),
                                            QStringLiteral("primary")}}}}}}}}};
        auto state = nodivu::monitorState(snapshot, devices, true, false, {});
        QCOMPARE(state.devices.size(), 1);
        QVERIFY(!state.toggleReason.isEmpty() && state.selectionReason.isEmpty());
        state = nodivu::monitorState(snapshot, devices, true, false, QStringLiteral("secondary"));
        QVERIFY(state.toggleReason.isEmpty());
        state = nodivu::monitorState(snapshot, devices, true, false, QStringLiteral("primary"));
        QVERIFY(!state.toggleReason.isEmpty());
        state = nodivu::monitorState(snapshot, {devices.first()}, true, false,
                                     QStringLiteral("secondary"));
        QVERIFY(state.toggleReason.contains(QStringLiteral("outra saída")));
        snapshot.insert(
            QStringLiteral("monitor"),
            QJsonObject{{QStringLiteral("endpoint_id"), QStringLiteral("gone")},
                        {QStringLiteral("runtime"),
                         QJsonObject{{QStringLiteral("state"), QStringLiteral("error")},
                                     {QStringLiteral("error"),
                                      QJsonObject{{QStringLiteral("message"),
                                                   QStringLiteral("Saída <indisponível>")}}}}}});
        state = nodivu::monitorState(snapshot, devices, true, false, {});
        QVERIFY(state.active && state.failed && state.toggleReason.isEmpty());
        QCOMPARE(state.selectedId, QStringLiteral("gone"));
        QVERIFY(!state.selectionReason.isEmpty() &&
                state.message.contains(QStringLiteral("<indisponível>")));
        state = nodivu::monitorState(snapshot, devices, true, true, {});
        QVERIFY(!state.toggleReason.isEmpty());
        state = nodivu::monitorState(snapshot, devices, false, false, {});
        QVERIFY(!state.toggleReason.isEmpty());
    }
    void updatePanelExplainsUnavailableChannelAndPreservesErrors() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        nodivu::UpdatePanel unavailable(profile.path(), {}, nullptr);
        auto* action = unavailable.findChild<QPushButton*>(QStringLiteral("updateAction"));
        QVERIFY(action && !action->isEnabled());
        QVERIFY(action->accessibleDescription().contains(QStringLiteral("canal")));
        const auto source = QDir(profile.path()).filePath(QStringLiteral("invalid-source.json"));
        QFile input(source);
        QVERIFY(input.open(QIODevice::WriteOnly));
        input.write("{}");
        input.close();
        nodivu::UpdatePanel invalid(profile.path(), source, nullptr);
        invalid.show();
        action = invalid.findChild<QPushButton*>(QStringLiteral("updateAction"));
        QVERIFY(action && action->isEnabled());
        QSignalSpy install(&invalid, &nodivu::UpdatePanel::installRequested);
        action->click();
        QTRY_COMPARE(action->text(), QStringLiteral("Tentar novamente"));
        QVERIFY(action->isEnabled());
        QCOMPARE(install.count(), 0);
        QVERIFY(!invalid.findChild<QProgressBar*>(QStringLiteral("updateProgress"))->isVisible());
        QVERIFY(!invalid.findChild<QLabel*>(QStringLiteral("updateMessage"))->text().isEmpty());
    }
    void everyRegisteredInspectorSharesControlsAndPreservesAudioState() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        nodivu::MainWindow window(paths);
        window.show();
        auto& session = window.session();
        auto* scene = window.findChild<nodivu::GraphScene*>();
        auto* view = window.findChild<nodivu::GraphView*>();
        auto* inspector = window.findChild<nodivu::InspectorPanel*>();
        QVERIFY(scene && view && inspector);
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 10000);
        QString id;
        connect(&session, &nodivu::SessionController::nodeAdded, &window,
                [&id](const QString& added) { id = added; });
        const auto evidence = qEnvironmentVariable("NODIVU_WORKSPACE_SCREENSHOTS");
        const auto descriptors = session.catalog().entries();
        for (const auto& descriptor : descriptors) {
            QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 10000);
            id.clear();
            session.addNode(descriptor.kind, descriptor.pluginId);
            QTRY_VERIFY_WITH_TIMEOUT(!id.isEmpty() && session.settled(), 10000);
            nodivu::NodeContent* card = nullptr;
            for (auto* item : scene->items())
                if (auto* proxy = dynamic_cast<QGraphicsProxyWidget*>(item))
                    if (auto* content = qobject_cast<nodivu::NodeContent*>(proxy->widget()))
                        if (content->property("node_id").toString() == id)
                            card = content;
            QVERIFY(card);
            inspector->inspect(id);
            const auto reveal = [inspector](QWidget* widget) {
                for (auto* tabs : inspector->findChildren<QTabWidget*>())
                    for (int index = 0; index < tabs->count(); ++index)
                        if (tabs->widget(index)->isAncestorOf(widget))
                            tabs->setCurrentIndex(index);
            };
            bool editedSlider = false;
            for (const auto& control : descriptor.controls) {
                QWidget* widget = nullptr;
                for (auto* candidate : inspector->findChildren<QWidget*>())
                    if (candidate->property("field").toString() == control.key)
                        widget = candidate;
                QVERIFY2(widget, qPrintable(descriptor.title + QStringLiteral(": ") + control.key));
                reveal(widget);
                if (control.readonly)
                    QVERIFY(!widget->isEnabled());
                if (control.kind == nodivu::ControlKind::Toggle) {
                    auto* check = qobject_cast<QCheckBox*>(widget);
                    QVERIFY(check);
                    for (auto* label : inspector->findChildren<QLabel*>())
                        QVERIFY(label->text() != check->text());
                    if (control.key == QStringLiteral("bypass")) {
                        const bool before = check->isChecked();
                        check->setFocus();
                        QTest::keyClick(check, Qt::Key_Space);
                        QTRY_VERIFY(session.settled() && check->isChecked() != before);
                        auto* quick = card->findChild<QCheckBox*>(QStringLiteral("bypass"));
                        QVERIFY(quick);
                        QTRY_COMPARE(quick->isChecked(), check->isChecked());
                    }
                }
                if (control.kind == nodivu::ControlKind::Slider && !control.readonly &&
                    !control.advanced && !editedSlider) {
                    auto* slider = qobject_cast<QSlider*>(widget);
                    QVERIFY(slider && slider->isVisible());
                    const auto before =
                        nodivu::fieldValue(
                            session.node(id).value(QStringLiteral("block")).toObject(), control.key)
                            .toDouble();
                    slider->setFocus();
                    QTest::keyClick(slider, slider->value() == slider->maximum() ? Qt::Key_Left
                                                                                 : Qt::Key_Right);
                    QTRY_VERIFY(
                        session.settled() &&
                        nodivu::fieldValue(
                            session.node(id).value(QStringLiteral("block")).toObject(), control.key)
                                .toDouble() != before);
                    if (control.quick) {
                        auto* quickValue =
                            card->findChild<QLabel*>(control.key + QStringLiteral(":value"));
                        auto* panelValue =
                            inspector->findChild<QLabel*>(control.key + QStringLiteral(":value"));
                        QVERIFY(quickValue && panelValue);
                        QTRY_COMPARE(quickValue->text(), panelValue->text());
                    }
                    QVERIFY(!qobject_cast<QDoubleSpinBox*>(widget));
                    editedSlider = true;
                }
            }
            // Capture discovery is bounded; never choose an application/endpoint.
            if (auto* target = inspector->findChild<QComboBox*>(QStringLiteral("captureTarget")))
                QTRY_VERIFY_WITH_TIMEOUT(session.settled() && target->isEnabled(), 10000);
            if (auto* tabs = inspector->findChild<QTabWidget*>())
                tabs->setCurrentIndex(0);
            inspector->dockToCorner();
            view->zoomBy(1.0 / view->transform().m11());
            view->centerOn(card->graphicsProxyWidget()->sceneBoundingRect().center() +
                           QPointF(160, 0));
            QTest::qWait(80);
            auto* scroll = inspector->findChild<QScrollArea*>();
            QVERIFY(scroll);
            QTRY_COMPARE(scroll->verticalScrollBar()->maximum(), 0);
            QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
            QVERIFY(inspector->findChild<QLabel*>(QStringLiteral("blockNote")));
            if (!evidence.isEmpty()) {
                const auto key = descriptor.pluginId.isEmpty()
                                     ? descriptor.kind
                                     : descriptor.pluginId.section(QLatin1Char('.'), -1);
                QVERIFY(window.grab().save(
                    QDir(evidence).filePath(QStringLiteral("review-%1.png").arg(key))));
                if (auto* tabs = inspector->findChild<QTabWidget*>(); tabs && tabs->count() > 1) {
                    const int firstHeight = inspector->height();
                    tabs->setCurrentIndex(1);
                    QTest::qWait(50);
                    QVERIFY(inspector->height() <= firstHeight);
                    QVERIFY(window.grab().save(
                        QDir(evidence).filePath(QStringLiteral("review-%1-route.png").arg(key))));
                }
            }
        }
        QVERIFY(session.snapshot().value(QStringLiteral("engine_state")).toString() !=
                QStringLiteral("running"));
    }
    void workerPresentationKeepsFailureVisibleAndRestartExplicit() {
        // Presentation fixture only: no Python process or audio engine is started.
        nodivu::WorkerControls card(true), panel(false);
        const QJsonObject failed{
            {QStringLiteral("status"), 3},
            {QStringLiteral("missing_blocks"), 7},
            {QStringLiteral("error"), QStringLiteral("Falha <worker> sem resposta")}};
        card.synchronize(failed, true);
        panel.synchronize(failed, true);
        auto* status = card.findChild<QLabel*>(QStringLiteral("workerStatus"));
        auto* other = panel.findChild<QLabel*>(QStringLiteral("workerStatus"));
        QVERIFY(status && other);
        QCOMPARE(status->text(), other->text());
        QVERIFY(status->text().contains(QStringLiteral("<worker>")));
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QSignalSpy restart(&card, &nodivu::WorkerControls::restartRequested);
        auto* button = card.findChild<QPushButton*>(QStringLiteral("workerRestart"));
        QVERIFY(button);
        button->click();
        QCOMPARE(restart.count(), 1);
        card.synchronize(failed, false);
        button->click();
        QCOMPARE(restart.count(), 1);
    }
    void canceledPendingChangesPreserveDocumentAndSession() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        nodivu::MainWindow window(paths);
        window.show();
        auto& session = window.session();
        auto* projects = window.findChild<nodivu::ProjectController*>();
        QVERIFY(projects);
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 10000);
        session.addNode(QStringLiteral("tone"));
        QTRY_VERIFY(session.settled() && projects->dirty());
        const auto original = QJsonDocument(projects->document()).toJson(QJsonDocument::Compact);
        const auto respond = [&](nodivu::PendingChangesAction action, bool proceed, bool escape) {
            QTimer::singleShot(0, &window, [action, proceed, escape] {
                auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (!dialog)
                    qFatal("Expected pending changes dialog.");
                const auto text = nodivu::pendingChangesText(action);
                const auto message = dialog->text();
                const auto defaultText = dialog->defaultButton()->text();
                const auto escapeText = dialog->escapeButton()->text();
                if (proceed)
                    dialog->findChild<QPushButton*>(QStringLiteral("pendingChangesProceed"))
                        ->click();
                else
                    QTest::keyClick(dialog, escape ? Qt::Key_Escape : Qt::Key_Return);
                QCOMPARE(message, text.message);
                QCOMPARE(defaultText, text.cancel);
                QCOMPARE(escapeText, text.cancel);
            });
        };
        respond(nodivu::PendingChangesAction::Close, false, false);
        QVERIFY(!window.close());
        QVERIFY(window.isVisible() && session.online() && projects->dirty());
        QCOMPARE(QJsonDocument(projects->document()).toJson(QJsonDocument::Compact), original);
        respond(nodivu::PendingChangesAction::Open, false, true);
        projects->open(QDir(profile.path()).filePath(QStringLiteral("other.json")));
        QVERIFY(projects->path().isEmpty() && !projects->busy());
        QCOMPARE(QJsonDocument(projects->document()).toJson(QJsonDocument::Compact), original);
        respond(nodivu::PendingChangesAction::Restore, false, true);
        projects->restoreRecovery();
        QVERIFY(!projects->busy());
        QCOMPARE(QJsonDocument(projects->document()).toJson(QJsonDocument::Compact), original);
        respond(nodivu::PendingChangesAction::Close, true, false);
        QVERIFY(projects->confirmDiscard());
        QVERIFY(projects->dirty()); // Confirmation alone never mutates a document.
    }
    void applicationCaptureSharesControlsWithoutUnnecessaryScrolling() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        nodivu::MainWindow window(paths);
        window.show();
        auto& session = window.session();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 10000);
        QString id;
        connect(&session, &nodivu::SessionController::nodeAdded, &window,
                [&id](const QString& added) { id = added; });
        session.addNode(QStringLiteral("plugin"), QStringLiteral("org.nodivu.windows-audio"));
        QTRY_VERIFY_WITH_TIMEOUT(!id.isEmpty() && session.settled(), 10000);
        auto* scene = window.findChild<nodivu::GraphScene*>();
        auto* view = window.findChild<nodivu::GraphView*>();
        auto* inspector = window.findChild<nodivu::InspectorPanel*>();
        QVERIFY(scene && view && inspector);
        QVERIFY(!inspector->isVisible());
        nodivu::NodeContent* card = nullptr;
        for (auto* item : scene->items())
            if (auto* proxy = dynamic_cast<QGraphicsProxyWidget*>(item))
                if (auto* content = qobject_cast<nodivu::NodeContent*>(proxy->widget()))
                    if (content->property("node_id").toString() == id)
                        card = content;
        QVERIFY(card);
        auto* cardTarget = card->findChild<QComboBox*>(QStringLiteral("captureTarget"));
        QVERIFY(cardTarget);
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && cardTarget->isEnabled(), 10000);
        inspector->inspect(id);
        auto* panelTarget = inspector->findChild<QComboBox*>(QStringLiteral("captureTarget"));
        QVERIFY(panelTarget);
        QTRY_VERIFY_WITH_TIMEOUT(
            session.settled() && panelTarget->isEnabled() && cardTarget->isEnabled(), 10000);
        QCOMPARE(cardTarget->currentData().toString(), QString());
        QCOMPARE(panelTarget->currentData().toString(), QString());
        QVERIFY(panelTarget->findData(QStringLiteral("system")) >= 0);
        auto* cardMessage = card->findChild<QTextEdit*>(QStringLiteral("captureStatus"));
        auto* panelMessage = inspector->findChild<QTextEdit*>(QStringLiteral("captureStatus"));
        QVERIFY(cardMessage && panelMessage);
        QCOMPARE(cardMessage->toPlainText(),
                 QStringLiteral("Selecione um aplicativo ou o som do PC."));
        QCOMPARE(cardMessage->toPlainText(), panelMessage->toPlainText());
        QCheckBox* panelMute = nullptr;
        for (auto* check : inspector->findChildren<QCheckBox*>())
            if (check->property("field").toString() == QStringLiteral("bypass"))
                panelMute = check;
        auto* cardMute = card->findChild<QCheckBox*>(QStringLiteral("bypass"));
        QVERIFY(panelMute && cardMute);
        for (auto* label : inspector->findChildren<QLabel*>())
            QVERIFY(label->text() != QStringLiteral("Silenciar fonte"));
        panelMute->setFocus();
        QTest::keyClick(panelMute, Qt::Key_Space);
        QTRY_VERIFY(session.settled() && panelMute->isChecked() && cardMute->isChecked());
        cardMute->setFocus();
        QTest::keyClick(cardMute, Qt::Key_Space);
        QTRY_VERIFY(session.settled() && !panelMute->isChecked() && !cardMute->isChecked());
        auto* refresh = inspector->findChild<QPushButton*>(QStringLiteral("captureRefresh"));
        QVERIFY(refresh && refresh->isEnabled());
        refresh->click();
        QTRY_VERIFY_WITH_TIMEOUT(
            session.settled() && panelTarget->isEnabled() && refresh->isEnabled(), 10000);
        QCOMPARE(panelTarget->currentData().toString(), QString());
        QCOMPARE(cardMessage->toPlainText(), panelMessage->toPlainText());
        auto* scroll = inspector->findChild<QScrollArea*>();
        QVERIFY(scroll);
        QTRY_COMPARE(scroll->verticalScrollBar()->maximum(), 0);
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QVERIFY(inspector->isVisible());
        auto* timing = inspector->findChild<QLabel*>(QStringLiteral("captureTiming"));
        QVERIFY(timing && timing->isHidden());
        QVERIFY(!card->findChild<QLabel*>(QStringLiteral("captureTiming")));
        // Listing and editing controls must not choose a source or start audio.
        const auto runtime = session.snapshot()
                                 .value(QStringLiteral("plugin_states"))
                                 .toObject()
                                 .value(id)
                                 .toObject();
        QVERIFY(runtime.value(QStringLiteral("capture_selection")).toObject().isEmpty());
        QVERIFY(session.snapshot().value(QStringLiteral("engine_state")).toString() !=
                QStringLiteral("running"));
        inspector->dockToCorner();
        view->fitNodes();
        QTest::qWait(150);
        const auto evidence = qEnvironmentVariable("NODIVU_WORKSPACE_SCREENSHOTS");
        if (!evidence.isEmpty())
            QVERIFY(window.grab().save(
                QDir(evidence).filePath(QStringLiteral("application-capture.png"))));
    }
    void builtinCardsEditInlineAndKeepInspectorExplicit() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        nodivu::MainWindow window(paths);
        window.show();
        auto& session = window.session();
        auto* scene = window.findChild<nodivu::GraphScene*>();
        auto* view = window.findChild<nodivu::GraphView*>();
        auto* inspector = window.findChild<nodivu::InspectorPanel*>();
        QVERIFY(scene && view && inspector);
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 10000);
        {
            // Explicit presentation fixture: a package with many ports must not
            // leave its last connector outside the card's declared bounds.
            nodivu::NodeDescriptor multiport;
            multiport.kind = QStringLiteral("plugin");
            multiport.inputs = 8;
            multiport.outputs = 1;
            nodivu::NodeItem item(QStringLiteral("multiport-fixture"), multiport, session);
            for (int i = 0; i < multiport.inputs; ++i)
                QVERIFY(item.boundingRect().contains(
                    QRectF(item.portPosition(false, i) - QPointF(11, 11), QSizeF(22, 22))));
        }
        QHash<QString, QString> ids;
        QHash<QString, nodivu::NodeContent*> contents;
        for (const auto& kind :
             {QStringLiteral("capture"), QStringLiteral("gain"), QStringLiteral("tone"),
              QStringLiteral("mixer"), QStringLiteral("meter"), QStringLiteral("output")}) {
            session.addNode(kind);
            QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 5000);
            for (auto* item : scene->items())
                if (auto* proxy = dynamic_cast<QGraphicsProxyWidget*>(item))
                    if (auto* content = qobject_cast<nodivu::NodeContent*>(proxy->widget())) {
                        const auto id = content->property("node_id").toString();
                        if (session.node(id)
                                .value(QStringLiteral("block"))
                                .toObject()
                                .value(QStringLiteral("kind"))
                                .toString() == kind) {
                            ids.insert(kind, id);
                            contents.insert(kind, content);
                        }
                    }
            QVERIFY(contents.contains(kind));
            QVERIFY(!inspector->isVisible());
            QVERIFY(!session.catalog().describe(session.node(ids.value(kind))).needsInspector());
        }
        auto* capture = contents.value(QStringLiteral("capture"))
                            ->findChild<QComboBox*>(QStringLiteral("endpoint_id"));
        QVERIFY(capture);
        QVERIFY(capture->currentData().toString().isEmpty());
        for (int i = 1; i < capture->count(); ++i) {
            bool valid = false;
            for (const auto& device : session.devices())
                valid |= device.toObject().value(QStringLiteral("flow")).toString() ==
                             QStringLiteral("capture") &&
                         device.toObject().value(QStringLiteral("endpoint_id")).toString() ==
                             capture->itemData(i).toString();
            QVERIFY(valid);
        }
        auto* gain =
            contents.value(QStringLiteral("gain"))->findChild<QSlider*>(QStringLiteral("gain_db"));
        auto* bypass =
            contents.value(QStringLiteral("gain"))->findChild<QCheckBox*>(QStringLiteral("bypass"));
        auto* tone = contents.value(QStringLiteral("tone"))
                         ->findChild<QCheckBox*>(QStringLiteral("enabled"));
        QVERIFY(gain && bypass && tone);
        const auto positions = scene->positions();
        const auto before = session.node(ids.value(QStringLiteral("gain")))
                                .value(QStringLiteral("block"))
                                .toObject()
                                .value(QStringLiteral("gain_db"))
                                .toDouble();
        gain->setFocus();
        QTest::keyClick(gain, Qt::Key_Right);
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() &&
                                     session.node(ids.value(QStringLiteral("gain")))
                                             .value(QStringLiteral("block"))
                                             .toObject()
                                             .value(QStringLiteral("gain_db"))
                                             .toDouble() > before,
                                 5000);
        QTest::mouseClick(bypass, Qt::LeftButton, Qt::NoModifier, QPoint(7, bypass->height() / 2));
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() &&
                                     session.node(ids.value(QStringLiteral("gain")))
                                         .value(QStringLiteral("block"))
                                         .toObject()
                                         .value(QStringLiteral("bypass"))
                                         .toBool(),
                                 5000);
        const bool toneBefore = tone->isChecked();
        QTest::mouseClick(tone, Qt::LeftButton, Qt::NoModifier, QPoint(7, tone->height() / 2));
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() &&
                                     session.node(ids.value(QStringLiteral("tone")))
                                             .value(QStringLiteral("block"))
                                             .toObject()
                                             .value(QStringLiteral("enabled"))
                                             .toBool() != toneBefore,
                                 5000);
        QCOMPARE(scene->positions(), positions);
        QVERIFY(!inspector->isVisible());
        QVERIFY(session.node(ids.value(QStringLiteral("output")))
                    .value(QStringLiteral("block"))
                    .toObject()
                    .value(QStringLiteral("endpoint_id"))
                    .isNull());
        inspector->inspect(ids.value(QStringLiteral("gain")));
        QVERIFY(inspector->isVisible());
        scene->selectNode(ids.value(QStringLiteral("capture")));
        QVERIFY(!inspector->isVisible());
        QTRY_VERIFY(capture->hasFocus());
        view->fitNodes();
        const auto evidence = qEnvironmentVariable("NODIVU_WORKSPACE_SCREENSHOTS");
        if (!evidence.isEmpty() && QDir().mkpath(evidence))
            QVERIFY(
                window.grab().save(QDir(evidence).filePath(QStringLiteral("builtin-cards.png"))));
        // Real local packages, without an audio endpoint or playback. Discovery
        // for app capture must settle on the card before the next insertion.
        for (const auto& descriptor : session.catalog().entries()) {
            if (descriptor.pluginId.isEmpty())
                continue;
            QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 10000);
            session.addNode(QStringLiteral("plugin"), descriptor.pluginId);
            QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 10000);
            QComboBox* captureTarget = nullptr;
            bool found = false;
            for (auto* item : scene->items())
                if (auto* proxy = dynamic_cast<QGraphicsProxyWidget*>(item))
                    if (auto* content = qobject_cast<nodivu::NodeContent*>(proxy->widget())) {
                        const auto id = content->property("node_id").toString();
                        if (session.node(id)
                                .value(QStringLiteral("block"))
                                .toObject()
                                .value(QStringLiteral("plugin_id"))
                                .toString() == descriptor.pluginId) {
                            found = true;
                            captureTarget =
                                content->findChild<QComboBox*>(QStringLiteral("captureTarget"));
                        }
                    }
            QVERIFY(found);
            if (descriptor.captureSource) {
                QVERIFY(captureTarget);
                QTRY_VERIFY_WITH_TIMEOUT(session.settled() && captureTarget->isEnabled(), 10000);
                QVERIFY(captureTarget->findData(QStringLiteral("system")) >= 0);
                QVERIFY(captureTarget->currentData().toString().isEmpty());
            }
            QCOMPARE(inspector->isVisible(), descriptor.needsInspector());
        }
        inspector->hide();
        scene->organize();
        QList<QRectF> bounds;
        for (auto* item : scene->items())
            if (dynamic_cast<nodivu::NodeItem*>(item)) {
                for (const auto& previous : bounds)
                    QVERIFY(!previous.intersects(item->sceneBoundingRect()));
                bounds.append(item->sceneBoundingRect());
            }
        view->fitNodes();
        if (!evidence.isEmpty())
            QVERIFY(
                window.grab().save(QDir(evidence).filePath(QStringLiteral("plugin-cards.png"))));
    }
    void pluginQuickControlsPreserveTypesReadOnlyAndAdvancedSettings() {
        // Explicit presentation fixture; no fake engine and no audio acquisition.
        nodivu::NodeDescriptor descriptor;
        descriptor.kind = QStringLiteral("plugin");
        descriptor.pluginId = QStringLiteral("test.declarative");
        descriptor.title = QStringLiteral("Controles declarativos");
        const auto control = [](const QString& key, nodivu::ControlKind kind) {
            nodivu::ControlDescriptor result;
            result.key = key;
            result.label = key;
            result.kind = kind;
            result.quick = true;
            result.maximum = 10;
            return result;
        };
        auto select = control(QStringLiteral("parameter:1"), nodivu::ControlKind::Select);
        select.options = {QJsonObject{{QStringLiteral("label"), QStringLiteral("A")},
                                      {QStringLiteral("value"), 3}},
                          QJsonObject{{QStringLiteral("label"), QStringLiteral("B")},
                                      {QStringLiteral("value"), 7}}};
        auto readonly = control(QStringLiteral("parameter:2"), nodivu::ControlKind::Number);
        readonly.readonly = true;
        auto advanced = control(QStringLiteral("parameter:3"), nodivu::ControlKind::Slider);
        advanced.advanced = true;
        auto momentary = control(QStringLiteral("parameter:4"), nodivu::ControlKind::Momentary);
        descriptor.controls = {select, readonly, advanced, momentary};
        QVERIFY(descriptor.needsInspector());
        nodivu::NodeContent content(QStringLiteral("fixture"), descriptor);
        content.show();
        content.synchronize({{QStringLiteral("parameters"),
                              QJsonObject{{QStringLiteral("1"), 7}, {QStringLiteral("2"), 4}}}},
                            {}, true, 0);
        auto* combo = content.findChild<QComboBox*>(select.key);
        auto* number = content.findChild<QDoubleSpinBox*>(readonly.key);
        auto* press = content.findChild<QPushButton*>(momentary.key);
        QVERIFY(combo && number && press);
        QCOMPARE(combo->currentData().toDouble(), 7.0);
        QVERIFY(!number->isEnabled());
        QVERIFY(!content.findChild<QSlider*>(advanced.key));
        QSignalSpy edits(&content, &nodivu::NodeContent::fieldEdited);
        QTest::keyClick(combo, Qt::Key_Up);
        QCOMPARE(edits.count(), 1);
        QCOMPARE(edits.first().at(1).value<QJsonValue>().toDouble(), 3.0);
        QSignalSpy gestures(&content, &nodivu::NodeContent::gestureRequested);
        QTest::mousePress(press, Qt::LeftButton);
        QTest::mouseRelease(press, Qt::LeftButton);
        QCOMPARE(gestures.count(), 2);
        QCOMPARE(gestures.first().at(0).toUInt(), 4u);
        QCOMPARE(gestures.first().at(1).toBool(), true);
        QCOMPARE(gestures.last().at(1).toBool(), false);
        descriptor.controls.removeAt(2);
        QVERIFY(!descriptor.needsInspector());
    }
    void virtualOutputShortcutCreatesReusesAndFocusesOutputWithoutChangingRoute() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        nodivu::MainWindow window(paths);
        window.show();
        auto& session = window.session();
        auto* shortcut = window.findChild<QPushButton*>(QStringLiteral("virtualOutputShortcut"));
        auto* inspector = window.findChild<nodivu::InspectorPanel*>();
        auto* scene = window.findChild<nodivu::GraphScene*>();
        auto* view = window.findChild<nodivu::GraphView*>();
        QVERIFY(shortcut && inspector && scene && view);
        QVERIFY(!shortcut->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && shortcut->isEnabled(), 10000);
        const auto graph = [&] {
            return session.snapshot().value(QStringLiteral("graph")).toObject();
        };
        const auto idFor = [&](const QString& kind) {
            for (const auto& value : graph().value(QStringLiteral("nodes")).toArray()) {
                const auto node = value.toObject();
                if (node.value(QStringLiteral("block"))
                        .toObject()
                        .value(QStringLiteral("kind"))
                        .toString() == kind)
                    return node.value(QStringLiteral("id")).toString();
            }
            return QString();
        };
        const auto destination = [&]() -> QComboBox* {
            for (auto* item : scene->items())
                if (auto* proxy = dynamic_cast<QGraphicsProxyWidget*>(item))
                    if (auto* content = qobject_cast<nodivu::NodeContent*>(proxy->widget()))
                        if (auto* combo =
                                content->findChild<QComboBox*>(QStringLiteral("outputDevice")))
                            return combo;
            return nullptr;
        };
        QSignalSpy additions(&session, &nodivu::SessionController::nodeAdded);
        QTest::mouseClick(shortcut, Qt::LeftButton);
        QTest::mouseClick(shortcut, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && shortcut->isEnabled() &&
                                     !idFor(QStringLiteral("output")).isEmpty(),
                                 5000);
        const auto output = idFor(QStringLiteral("output"));
        QCOMPARE(graph().value(QStringLiteral("nodes")).toArray().size(), 1);
        QCOMPARE(additions.count(), 1);
        QVERIFY(!inspector->isVisible());
        QVERIFY(destination());
        QTRY_VERIFY(destination()->hasFocus());
        QVERIFY(destination()->currentData().toString().isEmpty());
        QVERIFY(session.node(output)
                    .value(QStringLiteral("block"))
                    .toObject()
                    .value(QStringLiteral("endpoint_id"))
                    .toString()
                    .isEmpty());
        QVERIFY(graph().value(QStringLiteral("edges")).toArray().isEmpty());
        const auto evidence = qEnvironmentVariable("NODIVU_WORKSPACE_SCREENSHOTS");
        if (!evidence.isEmpty() && QDir().mkpath(evidence))
            QVERIFY(window.grab().save(QDir(evidence).filePath(QStringLiteral("output-node.png"))));
        const auto originalGraph = graph();
        inspector->inspect(output); // Explicit inspection remains available.
        QVERIFY(inspector->isVisible());
        QTest::mouseClick(shortcut, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(shortcut->isEnabled() && !inspector->isVisible(), 5000);
        QCOMPARE(graph(), originalGraph);
        QCOMPARE(additions.count(), 1);
        QTRY_VERIFY(destination()->hasFocus());
        const auto positions = scene->positions();
        // Physical-device acceptance is explicit, so ordinary CI does not need
        // a sound card. The control/ID/keyboard contract is also exercised below
        // with an explicitly synthetic inventory and no audio engine.
        if (qEnvironmentVariableIntValue("NODIVU_OUTPUT_DEVICE_ACCEPTANCE") == 1) {
            QVERIFY(destination()->count() > 1);
            const auto endpoint = destination()->itemData(1).toString();
            QTest::keyClick(view->viewport(), Qt::Key_Down);
            QTRY_VERIFY_WITH_TIMEOUT(session.settled() &&
                                         session.node(output)
                                                 .value(QStringLiteral("block"))
                                                 .toObject()
                                                 .value(QStringLiteral("endpoint_id"))
                                                 .toString() == endpoint,
                                     5000);
            QCOMPARE(scene->positions(), positions);
            QVERIFY(!inspector->isVisible());
            QTRY_VERIFY(destination()->hasFocus());
            QTest::keyClick(view->viewport(), Qt::Key_Up);
            QTRY_VERIFY_WITH_TIMEOUT(session.settled() && session.node(output)
                                                              .value(QStringLiteral("block"))
                                                              .toObject()
                                                              .value(QStringLiteral("endpoint_id"))
                                                              .isNull(),
                                     5000);
            qInfo("Seleção/liberação do endpoint render real: PASS (sem rota/fonte de "
                  "áudio).");
        } else {
            qInfo("Ensaio físico não solicitado: use NODIVU_OUTPUT_DEVICE_ACCEPTANCE=1.");
        }
        // Mouse interaction reaches the embedded combo even at a transformed zoom.
        QWidget* contentRoot = destination();
        while (contentRoot && !contentRoot->graphicsProxyWidget())
            contentRoot = contentRoot->parentWidget();
        QVERIFY(contentRoot);
        auto* proxy = contentRoot->graphicsProxyWidget();
        QVERIFY(proxy);
        const auto comboPoint = view->mapFromScene(
            proxy->mapToScene(destination()->mapTo(contentRoot, destination()->rect().center())));
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, comboPoint);
        QTRY_VERIFY(destination()->view()->isVisible());
        QTest::keyClick(view->viewport(), Qt::Key_Escape);
        QTRY_VERIFY(!destination()->view()->isVisible());
        QVERIFY(!inspector->isVisible());
        // Selecting the empty option must use JSON null, even without hardware.
        QSignalSpy confirmations(&session, &nodivu::SessionController::documentEdited);
        QSignalSpy activations(destination(), &QComboBox::activated);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, comboPoint);
        QTRY_VERIFY(destination()->view()->isVisible());
        QTest::keyClick(view->viewport(), Qt::Key_Return);
        QTRY_COMPARE(activations.count(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && confirmations.count() == 1, 5000);
        QVERIFY(session.node(output)
                    .value(QStringLiteral("block"))
                    .toObject()
                    .value(QStringLiteral("endpoint_id"))
                    .isNull());
        session.addNode(QStringLiteral("tone"));
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !idFor(QStringLiteral("tone")).isEmpty(),
                                 5000);
        session.addNode(QStringLiteral("gain"));
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !idFor(QStringLiteral("gain")).isEmpty(),
                                 5000);
        const auto gain = idFor(QStringLiteral("gain"));
        session.connectNodes(idFor(QStringLiteral("tone")), 0, gain, 0);
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() &&
                                     graph().value(QStringLiteral("edges")).toArray().size() == 1,
                                 5000);
        session.connectNodes(gain, 0, output, 0);
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() &&
                                     graph().value(QStringLiteral("edges")).toArray().size() == 2,
                                 5000);
        const auto edges = graph().value(QStringLiteral("edges"));
        session.updateField(gain, QStringLiteral("gain_db"), -9.0);
        QTest::mouseClick(shortcut, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && shortcut->isEnabled(), 5000);
        QCOMPARE(idFor(QStringLiteral("output")), output);
        QCOMPARE(graph().value(QStringLiteral("nodes")).toArray().size(), 3);
        QCOMPARE(graph().value(QStringLiteral("edges")), edges);
        QCOMPARE(session.node(gain)
                     .value(QStringLiteral("block"))
                     .toObject()
                     .value(QStringLiteral("gain_db"))
                     .toDouble(),
                 -9.0);
        QVERIFY(!inspector->isVisible());
        QTRY_VERIFY(destination()->hasFocus());
        QCOMPARE(additions.count(), 3);
    }
    void outputControlPreservesOpaqueEndpointAndReportsLevel() {
        nodivu::NodeCatalog catalog;
        const auto descriptor =
            catalog.describe({{QStringLiteral("block"),
                               QJsonObject{{QStringLiteral("kind"), QStringLiteral("output")}}}});
        nodivu::NodeContent content(QStringLiteral("output-test"), descriptor);
        content.show();
        auto* combo = content.findChild<QComboBox*>(QStringLiteral("outputDevice"));
        auto* level = content.findChild<QLabel*>(QStringLiteral("nodeLevel"));
        auto* meter = content.findChild<QProgressBar*>();
        QVERIFY(combo && level && meter);
        const auto device = [](const QString& id, const QString& flow, const QString& state) {
            return QJsonObject{{QStringLiteral("endpoint_id"), id},
                               {QStringLiteral("name"), QStringLiteral("Mesmo nome")},
                               {QStringLiteral("flow"), flow},
                               {QStringLiteral("state"), state}};
        };
        QJsonArray devices{
            device(QStringLiteral("opaque-A"), QStringLiteral("render"), QStringLiteral("active")),
            device(QStringLiteral("opaque-B"), QStringLiteral("render"), QStringLiteral("active")),
            device(QStringLiteral("input"), QStringLiteral("capture"), QStringLiteral("active")),
            device(QStringLiteral("inactive"), QStringLiteral("render"),
                   QStringLiteral("disabled"))};
        QSignalSpy edits(&content, &nodivu::NodeContent::fieldEdited);
        content.synchronize({{QStringLiteral("endpoint_id"), QStringLiteral("opaque-B")}}, devices,
                            true, 0.0);
        QCOMPARE(combo->count(), 3);
        QCOMPARE(combo->currentData().toString(), QStringLiteral("opaque-B"));
        QCOMPARE(level->text(), QStringLiteral("Silêncio"));
        QCOMPARE(meter->value(), 0);
        auto* accessible = QAccessible::queryAccessibleInterface(combo);
        QVERIFY(accessible);
        QCOMPARE(accessible->role(), QAccessible::ComboBox);
        QCOMPARE(accessible->text(QAccessible::Name),
                 QStringLiteral("Destino · fones ou CABLE Input"));
        QTest::keyClick(combo, Qt::Key_Up);
        QCOMPARE(edits.count(), 1);
        QCOMPARE(edits.last().at(1).value<QJsonValue>().toString(), QStringLiteral("opaque-A"));
        QTest::keyClick(combo, Qt::Key_Up);
        QCOMPARE(edits.count(), 2);
        QVERIFY(edits.last().at(1).value<QJsonValue>().toString().isEmpty());
        edits.clear();
        // A renamed/missing device must not silently switch to one with the same name.
        auto renamed = devices[1].toObject();
        renamed.insert(QStringLiteral("name"), QStringLiteral("Novo nome"));
        devices[1] = renamed;
        content.synchronize({{QStringLiteral("endpoint_id"), QStringLiteral("opaque-B")}}, devices,
                            true, 0.1);
        QCOMPARE(combo->currentText(), QStringLiteral("Novo nome"));
        QCOMPARE(level->text(), QStringLiteral("-20.0 dBFS"));
        QCOMPARE(meter->value(), 667);
        devices.removeAt(1);
        content.synchronize({{QStringLiteral("endpoint_id"), QStringLiteral("opaque-B")}}, devices,
                            false, 0);
        QCOMPARE(combo->currentData().toString(), QStringLiteral("opaque-B"));
        QCOMPARE(combo->currentText(), QStringLiteral("Selecionado indisponível"));
        QVERIFY(!combo->isEnabled());
        QCOMPARE(edits.count(), 0);
    }
    void sidebarKeepsBlockActionsWhenScrolledOrCollapsed() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        nodivu::MainWindow window(paths);
        window.show();
        auto& session = window.session();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 10000);
        auto* library = window.findChild<nodivu::LibraryPanel*>();
        QVERIFY(library);
        auto* scroll = library->findChild<QScrollArea*>(QStringLiteral("libraryScroll"));
        auto* menus = window.findChild<QMenuBar*>(QStringLiteral("studioMenuBar"));
        auto* toggle = library->findChild<QToolButton*>(QStringLiteral("libraryToggle"));
        auto* processing =
            library->findChild<QToolButton*>(QStringLiteral("category:Processamento"));
        auto* capture = library->findChild<nodivu::LibraryBlockButton*>(QStringLiteral("capture"));
        auto* tone = library->findChild<nodivu::LibraryBlockButton*>(QStringLiteral("tone"));
        QVERIFY(scroll && menus && toggle && processing && capture && tone);
        const auto menuPosition = menus->mapTo(&window, QPoint());
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QCoreApplication::processEvents();
        QCOMPARE(menus->mapTo(&window, QPoint()), menuPosition);
        auto* manage = window.findChild<QAction*>(QStringLiteral("managePlugins"));
        QVERIFY(manage && manage->isEnabled());
        scroll->verticalScrollBar()->setValue(0);
        processing->setChecked(false);
        const auto expandedWidth = library->width();
        QTest::mouseClick(toggle, Qt::LeftButton);
        QTRY_COMPARE(library->width(), 64);
        QTRY_VERIFY(scroll->widget()->width() <= scroll->viewport()->width());
        QTRY_VERIFY(capture->width() <= scroll->viewport()->width());
        QTRY_VERIFY(scroll->viewport()->rect().contains(
            QRect(toggle->mapTo(scroll->viewport(), QPoint()), toggle->size())));
        QVERIFY(capture->isVisible() && tone->isVisible());
        QVERIFY(!processing->isVisible());
        QVERIFY(menus->isVisible() && manage->isEnabled());
        const auto evidence = qEnvironmentVariable("NODIVU_WORKSPACE_SCREENSHOTS");
        if (!evidence.isEmpty() && QDir().mkpath(evidence))
            QVERIFY(window.grab().save(
                QDir(evidence).filePath(QStringLiteral("sidebar-collapsed.png"))));
        tone->setFocus();
        QTest::keyClick(tone, Qt::Key_Return);
        const auto nodes = [&] {
            return session.snapshot()
                .value(QStringLiteral("graph"))
                .toObject()
                .value(QStringLiteral("nodes"))
                .toArray();
        };
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && nodes().size() == 1, 5000);
        QTest::mouseClick(capture, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && nodes().size() == 2, 5000);
        QTest::mouseClick(toggle, Qt::LeftButton);
        QTRY_COMPARE(library->width(), expandedWidth);
        QVERIFY(!processing->isChecked());
        QVERIFY(menus->isVisible() && manage->isEnabled());
    }
    void zoomReadoutAndEmptyProjectQuickStart() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        nodivu::MainWindow window(paths);
        window.show();
        auto& session = window.session();
        auto* view = window.findChild<nodivu::GraphView*>();
        auto* welcome = window.findChild<nodivu::EmptyGraphWidget*>();
        auto* start = window.findChild<QPushButton*>(QStringLiteral("quickStart"));
        auto* reset = window.findChild<QPushButton*>(QStringLiteral("zoomReset"));
        QVERIFY(view && welcome && start && reset);
        QVERIFY(welcome->isVisible());
        QVERIFY(!start->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && start->isEnabled(), 10000);
        QCOMPARE(welcome->geometry().center(), view->viewport()->rect().center());
        QCOMPARE(session.snapshot()
                     .value(QStringLiteral("graph"))
                     .toObject()
                     .value(QStringLiteral("nodes"))
                     .toArray()
                     .size(),
                 0);
        const auto capture = [&](const QString& name) {
            const auto directory = qEnvironmentVariable("NODIVU_WORKSPACE_SCREENSHOTS");
            if (!directory.isEmpty() && QDir().mkpath(directory))
                window.grab().save(QDir(directory).filePath(name));
        };
        capture(QStringLiteral("empty-project.png"));
        const auto button = [&](const QString& name) -> QPushButton* {
            for (auto* candidate : window.findChildren<QPushButton*>())
                if (candidate->accessibleName() == name || candidate->text() == name)
                    return candidate;
            return nullptr;
        };
        auto* plus = button(QStringLiteral("Aumentar zoom"));
        auto* minus = button(QStringLiteral("Diminuir zoom"));
        auto* fit = button(QStringLiteral("Enquadrar"));
        QVERIFY(plus && minus && fit);
        const auto graph = [&] {
            return session.snapshot().value(QStringLiteral("graph")).toObject();
        };
        const auto wheelZoom = [&] {
            const auto position = view->viewport()->rect().center();
            QWheelEvent wheel(position, view->viewport()->mapToGlobal(position), QPoint(),
                              QPoint(0, 120), Qt::NoButton, Qt::ControlModifier, Qt::ScrollUpdate,
                              false);
            QApplication::sendEvent(view->viewport(), &wheel);
        };
        QVERIFY(!plus->isEnabled() && !minus->isEnabled() && !reset->isEnabled() &&
                !fit->isEnabled());
        const auto initialCamera = view->camera();
        QSignalSpy emptyEdits(view, &nodivu::GraphView::cameraEdited);
        QTest::mouseClick(plus, Qt::LeftButton);
        QTest::mouseClick(minus, Qt::LeftButton);
        QTest::mouseClick(reset, Qt::LeftButton);
        wheelZoom();
        view->zoomBy(1.2);
        QCOMPARE(view->camera(), initialCamera);
        QCOMPARE(emptyEdits.count(), 0);
        // An empty saved project may still have a camera. Restoring it must not
        // reactivate controls or turn the fixed welcome panel into graph content.
        view->restoreCamera({{QStringLiteral("scale"), 0.72}});
        QCOMPARE(reset->text(), QStringLiteral("72%"));
        QVERIFY(!plus->isEnabled() && !minus->isEnabled() && !reset->isEnabled());
        QCOMPARE(emptyEdits.count(), 0);
        view->restoreCamera({});
        window.resize(1100, 720);
        QCoreApplication::processEvents();
        QVERIFY(view->viewport()->rect().contains(welcome->geometry()));
        QCOMPARE(welcome->geometry().center(), view->viewport()->rect().center());
        window.resize(1440, 900);
        QTest::mouseClick(start, Qt::LeftButton);
        QTest::mouseClick(start,
                          Qt::LeftButton); // A rapid second click cannot duplicate nodes.
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() &&
                                     graph().value(QStringLiteral("nodes")).toArray().size() == 2,
                                 5000);
        QVERIFY(!welcome->isVisible());
        QVERIFY(plus->isEnabled() && minus->isEnabled() && !reset->isEnabled() && fit->isEnabled());
        QCOMPARE(reset->text(), QStringLiteral("100%"));
        QTest::mouseClick(plus, Qt::LeftButton);
        QCOMPARE(reset->text(), QStringLiteral("120%"));
        QTest::mouseClick(minus, Qt::LeftButton);
        QCOMPARE(reset->text(), QStringLiteral("100%"));
        wheelZoom();
        QVERIFY(reset->text() != QStringLiteral("100%"));
        QTest::mouseClick(reset, Qt::LeftButton);
        QCOMPARE(reset->text(), QStringLiteral("100%"));
        QSignalSpy edits(view, &nodivu::GraphView::cameraEdited);
        view->restoreCamera({{QStringLiteral("scale"), 0.72}});
        QCOMPARE(reset->text(), QStringLiteral("72%"));
        QCOMPARE(edits.count(), 0); // Opening a camera must not mark a project edited.
        view->zoomBy(1000);
        QCOMPARE(reset->text(), QStringLiteral("200%"));
        QVERIFY(!plus->isEnabled());
        view->zoomBy(0.0001);
        QCOMPARE(reset->text(), QStringLiteral("10%"));
        QVERIFY(!minus->isEnabled());
        QTest::mouseClick(reset, Qt::LeftButton);
        QVERIFY(plus->isEnabled() && minus->isEnabled());
        QStringList kinds;
        QStringList ids;
        for (const auto& value : graph().value(QStringLiteral("nodes")).toArray()) {
            const auto node = value.toObject();
            const auto block = node.value(QStringLiteral("block")).toObject();
            kinds.append(block.value(QStringLiteral("kind")).toString());
            ids.append(node.value(QStringLiteral("id")).toString());
            QVERIFY(block.value(QStringLiteral("endpoint_id")).toString().isEmpty());
        }
        QVERIFY(kinds.contains(QStringLiteral("capture")) &&
                kinds.contains(QStringLiteral("output")));
        QTRY_COMPARE(graph().value(QStringLiteral("edges")).toArray().size(), 1);
        view->fitNodes();
        QCOMPARE(reset->text(),
                 QString::number(qRound(view->transform().m11() * 100)) + QLatin1Char('%'));
        capture(QStringLiteral("quick-start-fit.png"));
        session.addInitialNodes(); // Also reject the action when the graph is already
                                   // populated.
        QVERIFY(session.settled());
        for (const auto& id : ids) {
            session.removeNode(id);
            QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 5000);
        }
        QTRY_VERIFY(welcome->isVisible() && start->isEnabled());
        QVERIFY(!plus->isEnabled() && !minus->isEnabled() && !reset->isEnabled() &&
                !fit->isEnabled());
        const auto clearedCamera = view->camera();
        edits.clear();
        wheelZoom();
        view->zoomBy(1.2);
        QCOMPARE(view->camera(), clearedCamera);
        QCOMPARE(edits.count(), 0);
        session.addNode(QStringLiteral("tone"));
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() &&
                                     graph().value(QStringLiteral("nodes")).toArray().size() == 1,
                                 5000);
        QVERIFY(plus->isEnabled() && minus->isEnabled() && fit->isEnabled());
        QCOMPARE(view->camera(), clearedCamera);
        QCOMPARE(reset->isEnabled(), qAbs(view->transform().m11() - 1) > 0.000001);
        QTest::mouseClick(plus, Qt::LeftButton);
        QVERIFY(view->camera().value(QStringLiteral("scale")) !=
                clearedCamera.value(QStringLiteral("scale")));
    }
    void opensElectronProjectAndPreservesMissingPluginsResourcesAndCaptureIntent() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        const auto source =
            QStringLiteral(NODIVU_REPOSITORY_DIR) +
            QStringLiteral("/apps/nodivu-desktop/tests/fixtures/electron-project-v1.json");
        const auto input = QDir(profile.path()).filePath(QStringLiteral("Electron original.json"));
        QVERIFY(QFile::copy(source, input));
        const auto originalBytes = nodivu::ProjectRepository::read(input);
        QVERIFY(originalBytes.ok());
        const auto original = QJsonDocument::fromJson(originalBytes.contents).object();
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        nodivu::MainWindow window(paths);
        window.show();
        auto& session = window.session();
        auto* projects = window.findChild<nodivu::ProjectController*>();
        QVERIFY(projects);
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !projects->busy(), 10000);
        projects->open(input);
        QTRY_VERIFY_WITH_TIMEOUT(
            !projects->busy() && projects->path() == input && session.settled(), 5000);
        QCOMPARE(session.snapshot().value(QStringLiteral("missing_plugins")).toArray().size(), 2);
        auto* inspector = window.findChild<nodivu::InspectorPanel*>();
        QVERIFY(inspector);
        bool inspectedMissing = false;
        for (const auto& value : session.snapshot()
                                     .value(QStringLiteral("graph"))
                                     .toObject()
                                     .value(QStringLiteral("nodes"))
                                     .toArray()) {
            const auto node = value.toObject();
            if (!session.catalog().describe(node).missing)
                continue;
            inspectedMissing = true;
            inspector->inspect(node.value(QStringLiteral("id")).toString());
            auto* note = inspector->findChild<QLabel*>(QStringLiteral("blockNote"));
            QVERIFY(note && note->isVisible());
            QVERIFY(note->text().contains(QStringLiteral("este caminho fica silencioso")));
            QCOMPARE(note->textFormat(), Qt::PlainText);
            QVERIFY(inspector->findChildren<QSlider*>().isEmpty());
            const auto evidence = qEnvironmentVariable("NODIVU_WORKSPACE_SCREENSHOTS");
            if (!evidence.isEmpty()) {
                inspector->dockToCorner();
                QTest::qWait(50);
                QVERIFY(window.grab().save(
                    QDir(evidence).filePath(QStringLiteral("review-missing-plugin.png"))));
            }
            break;
        }
        QVERIFY(inspectedMissing);
        const auto output = QDir(profile.path()).filePath(QStringLiteral("Qt roundtrip.json"));
        projects->save(output);
        QTRY_VERIFY_WITH_TIMEOUT(
            !projects->busy() && projects->path() == output && !projects->dirty(), 5000);
        const auto savedBytes = nodivu::ProjectRepository::read(output);
        QVERIFY(savedBytes.ok());
        const auto saved = QJsonDocument::fromJson(savedBytes.contents).object();
        QCOMPARE(saved.value(QStringLiteral("graph")), original.value(QStringLiteral("graph")));
        QCOMPARE(saved.value(QStringLiteral("captures")),
                 original.value(QStringLiteral("captures")));
        const auto resources = saved.value(QStringLiteral("resources")).toArray();
        QCOMPARE(resources.size(), 1);
        QCOMPARE(QDir::fromNativeSeparators(
                     resources.first().toObject().value(QStringLiteral("path")).toString()),
                 QStringLiteral("Áudio/voz.wav"));
        for (const auto& expected : original.value(QStringLiteral("positions")).toArray()) {
            bool preserved = false;
            for (const auto& actual : saved.value(QStringLiteral("positions")).toArray())
                preserved |= actual == expected;
            QVERIFY(preserved);
        }
        const auto camera = saved.value(QStringLiteral("camera")).toObject();
        QCOMPARE(
            camera.value(QStringLiteral("scale")),
            original.value(QStringLiteral("camera")).toObject().value(QStringLiteral("scale")));
        const auto cameraDescription = QJsonDocument(camera).toJson(QJsonDocument::Compact);
        QVERIFY2(qAbs(camera.value(QStringLiteral("x")).toDouble() + 83.0) <= 1.0,
                 cameraDescription.constData());
        QVERIFY2(qAbs(camera.value(QStringLiteral("y")).toDouble() - 120.0) <= 1.0,
                 cameraDescription.constData());
        QCOMPARE(nodivu::ProjectRepository::read(input).contents, originalBytes.contents);
        projects->open(output);
        QTRY_VERIFY_WITH_TIMEOUT(!projects->busy() && session.settled(), 5000);
        QCOMPARE(projects->document().value(QStringLiteral("graph")),
                 saved.value(QStringLiteral("graph")));
        QCOMPARE(session.snapshot().value(QStringLiteral("engine_state")).toString(),
                 QStringLiteral("idle"));
    }
    void initTestCase() {
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeuib.ttf"));
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/seguisym.ttf"));
        nodivu::Theme::apply(*qApp);
    }
    void filePlayerLoadsWithoutStartingAudio() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        const auto audio = QDir(profile.path()).filePath(QStringLiteral("voz de teste.wav"));
        QFile fixture(audio);
        QVERIFY(fixture.open(QIODevice::WriteOnly));
        QDataStream wav(&fixture);
        wav.setByteOrder(QDataStream::LittleEndian);
        const QByteArray silence(192000, '\0'); // Two seconds, 48 kHz, mono PCM16.
        wav.writeRawData("RIFF", 4);
        wav << quint32(36 + silence.size());
        wav.writeRawData("WAVEfmt ", 8);
        wav << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000)
            << quint16(2) << quint16(16);
        wav.writeRawData("data", 4);
        wav << quint32(silence.size());
        QCOMPARE(wav.writeRawData(silence.constData(), silence.size()), silence.size());
        fixture.close();
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        nodivu::MainWindow window(paths);
        auto& session = window.session();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 10000);
        QString player;
        connect(&session, &nodivu::SessionController::nodeAdded, &window,
                [&player](const QString& id) { player = id; });
        session.addNode(QStringLiteral("plugin"), QStringLiteral("org.nodivu.mp3-player"));
        QTRY_VERIFY_WITH_TIMEOUT(!player.isEmpty() && session.settled(), 5000);
        auto* inspector = window.findChild<nodivu::InspectorPanel*>();
        QVERIFY(inspector);
        inspector->inspect(player);
        auto* play = inspector->findChild<QPushButton*>(QStringLiteral("playerPlay"));
        QVERIFY(play && !play->isEnabled());
        nodivu::NodeContent* card = nullptr;
        auto* scene = window.findChild<nodivu::GraphScene*>();
        QVERIFY(scene);
        for (auto* item : scene->items())
            if (auto* proxy = dynamic_cast<QGraphicsProxyWidget*>(item))
                if (auto* content = qobject_cast<nodivu::NodeContent*>(proxy->widget()))
                    if (content->property("node_id").toString() == player)
                        card = content;
        QVERIFY(card);
        auto* cardPlay = card->findChild<QPushButton*>(QStringLiteral("play"));
        QVERIFY(cardPlay && !cardPlay->isEnabled());
        auto* cardReason = card->findChild<QTextEdit*>(QStringLiteral("playerReason"));
        auto* panelReason = inspector->findChild<QTextEdit*>(QStringLiteral("playerReason"));
        QVERIFY(cardReason && panelReason);
        QVERIFY(cardReason->toPlainText().contains(QStringLiteral("Escolha")));
        QCOMPARE(cardReason->toPlainText(), panelReason->toPlainText());
        auto* panelStatus = inspector->findChild<QLabel*>(QStringLiteral("playerStatus"));
        QVERIFY(panelStatus);
        QCOMPARE(panelStatus->text(),
                 QStringLiteral("Nenhum áudio escolhido · Arquivo ainda não carregado"));
        QSignalSpy cardActions(card, &nodivu::NodeContent::actionRequested);
        QTest::mouseClick(cardPlay, Qt::LeftButton);
        QTest::mouseClick(play, Qt::LeftButton);
        QCOMPARE(cardActions.count(), 0);
        QVERIFY(!session.busy());
        window.show();
        inspector->dockToCorner();
        auto* view = window.findChild<nodivu::GraphView*>();
        QVERIFY(view);
        view->fitNodes();
        QTest::qWait(100);
        auto* panelScroll = inspector->findChild<QScrollArea*>();
        QVERIFY(panelScroll);
        QTRY_COMPARE(panelScroll->verticalScrollBar()->maximum(), 0);
        QCOMPARE(panelScroll->horizontalScrollBar()->maximum(), 0);
        QCheckBox* panelMute = nullptr;
        for (auto* check : inspector->findChildren<QCheckBox*>())
            if (check->property("field").toString() == QStringLiteral("bypass"))
                panelMute = check;
        auto* cardMute = card->findChild<QCheckBox*>(QStringLiteral("bypass"));
        QVERIFY(panelMute && cardMute);
        for (auto* label : inspector->findChildren<QLabel*>())
            QVERIFY(label->text() != QStringLiteral("Silenciar fonte"));
        panelMute->setFocus();
        QTest::keyClick(panelMute, Qt::Key_Space);
        QTRY_VERIFY(session.settled() && cardMute->isChecked() && panelMute->isChecked());
        cardMute->setFocus();
        QTest::keyClick(cardMute, Qt::Key_Space);
        QTRY_VERIFY(session.settled() && !cardMute->isChecked() && !panelMute->isChecked());
        QVERIFY(inspector->isVisible());
        const auto screenshots = qEnvironmentVariable("NODIVU_WORKSPACE_SCREENSHOTS");
        if (!screenshots.isEmpty())
            QVERIFY(
                window.grab().save(QDir(screenshots).filePath(QStringLiteral("player-empty.png"))));
        session.command(QStringLiteral("plugin.command"),
                        {{QStringLiteral("node_id"), player},
                         {QStringLiteral("action"), QStringLiteral("load")},
                         {QStringLiteral("path"), audio}});
        const auto runtime = [&] {
            return session.snapshot()
                .value(QStringLiteral("plugin_states"))
                .toObject()
                .value(player)
                .toObject();
        };
        QTRY_COMPARE_WITH_TIMEOUT(runtime().value(QStringLiteral("status")).toInt(), 2, 5000);
        QTest::qWait(150);
        QVERIFY(inspector->isVisible()); // Loading must preserve an explicitly opened panel.
        QTRY_COMPARE(panelScroll->verticalScrollBar()->maximum(), 0);
        QCOMPARE(runtime().value(QStringLiteral("duration_ms")).toInt(), 2000);
        QVERIFY(!play->isEnabled()); // A decoded file is insufficient without an audio
                                     // destination.
        QVERIFY(!cardPlay->isEnabled());
        QVERIFY(play->toolTip().contains(QStringLiteral("Saída")));
        QCOMPARE(cardReason->toPlainText(), panelReason->toPlainText());
        QVERIFY(cardReason->height() <= 60);
        QTRY_COMPARE(cardReason->verticalScrollBar()->maximum(), 0);
        QVERIFY(cardReason->viewport()->height() >= cardReason->document()->size().height());
        auto* time = card->findChild<QLabel*>(QStringLiteral("playerTime"));
        QVERIFY(time);
        QCOMPARE(time->text(), QStringLiteral("0:00 / 0:02"));
        QCOMPARE(time->alignment(), Qt::AlignCenter);
        if (!screenshots.isEmpty())
            QVERIFY(window.grab().save(
                QDir(screenshots).filePath(QStringLiteral("player-loaded.png"))));
        QSlider* seek = nullptr;
        for (auto* slider : inspector->findChildren<QSlider*>())
            if (slider->accessibleName() == QStringLiteral("Posição da reprodução"))
                seek = slider;
        QVERIFY(seek && !seek->isEnabled()); // Position is acknowledged only by the audio callback.
        QCOMPARE(runtime().value(QStringLiteral("position_ms")).toInt(), 0);
        QCOMPARE(runtime().value(QStringLiteral("status")).toInt(), 2);
        auto* projects = window.findChild<nodivu::ProjectController*>();
        QVERIFY(projects);
        const auto savedProject =
            QDir(profile.path()).filePath(QStringLiteral("retry-player.nodivu.json"));
        projects->save(savedProject);
        QTRY_VERIFY_WITH_TIMEOUT(!projects->busy() && !projects->dirty(), 5000);
        QVERIFY(QFile::rename(audio, audio + QStringLiteral(".held")));
        projects->open(savedProject);
        QTRY_VERIFY_WITH_TIMEOUT(!projects->busy() && session.settled(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(runtime().value(QStringLiteral("status")).toInt() != 1 &&
                                     runtime().value(QStringLiteral("status")).toInt() != 2,
                                 5000);
        QVERIFY(QFile::rename(audio + QStringLiteral(".held"), audio));
        auto* retry = window.findChild<QAction*>(QStringLiteral("retryDevices"));
        QVERIFY(retry);
        QSignalSpy discovery(&session, &nodivu::SessionController::devicesRefreshed);
        // A pending edit must settle before retry, and the missing saved file must be
        // retried.
        session.updateField(player, QStringLiteral("bypass"), true);
        retry->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!projects->busy() && session.settled(), 5000);
        QVERIFY(!discovery.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(runtime().value(QStringLiteral("status")).toInt(), 2, 5000);
        QCOMPARE(runtime().value(QStringLiteral("duration_ms")).toInt(), 2000);
        QVERIFY(session.node(player)
                    .value(QStringLiteral("block"))
                    .toObject()
                    .value(QStringLiteral("bypass"))
                    .toBool());
        const auto loaded = runtime();
        retry->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!projects->busy() && session.settled(), 5000);
        QCOMPARE(runtime().value(QStringLiteral("file")), loaded.value(QStringLiteral("file")));
        QCOMPARE(runtime().value(QStringLiteral("position_ms")),
                 loaded.value(QStringLiteral("position_ms")));
        const auto engine = session.snapshot().value(QStringLiteral("engine_state")).toString();
        QVERIFY(engine != QStringLiteral("running"));
        // Presentation-only fixture: test ready/blocked transitions and seek
        // previews without selecting a physical device or sending audio commands.
        nodivu::NodeContent preview(QStringLiteral("player-presentation"),
                                    session.catalog().describe(session.node(player)));
        nodivu::PlaybackState ready;
        ready.name = QStringLiteral("voz.wav");
        ready.label = QStringLiteral("Arquivo pronto");
        ready.canPlay = true;
        ready.durationMs = 2000;
        preview.setPlayback(ready, true);
        auto* previewPlay = preview.findChild<QPushButton*>(QStringLiteral("play"));
        auto* previewSeek = preview.findChild<QSlider*>(QStringLiteral("playerSeek"));
        auto* previewTime = preview.findChild<QLabel*>(QStringLiteral("playerTime"));
        QVERIFY(previewPlay && previewSeek && previewTime);
        QVERIFY(previewPlay->isEnabled());
        QSignalSpy previewActions(&preview, &nodivu::NodeContent::actionRequested);
        previewPlay->click();
        QCOMPARE(previewActions.count(), 1);
        QCOMPARE(previewActions.first().first().toString(), QStringLiteral("play"));
        previewSeek->setSliderDown(true);
        previewSeek->setValue(1000);
        preview.setPlayback(ready, true);
        QCOMPARE(previewTime->text(), QStringLiteral("0:01 / 0:02"));
        previewSeek->setSliderDown(false);
        ready.canPlay = false;
        ready.reason = QStringLiteral("Não foi possível carregar este áudio. ").repeated(30);
        preview.setPlayback(ready, true);
        QVERIFY(!previewPlay->isEnabled());
        auto* previewReason = preview.findChild<QTextEdit*>(QStringLiteral("playerReason"));
        QVERIFY(previewReason);
        QCOMPARE(previewReason->toPlainText(), ready.reason);
        QVERIFY(previewReason->height() <= 60);
    }
    void unexpectedBackendExitPreservesOriginalAndRecovery() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        nodivu::MainWindow window(paths);
        auto& session = window.session();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 10000);
        auto* projects = window.findChild<nodivu::ProjectController*>();
        QVERIFY(projects);
        const auto original = QDir(profile.path()).filePath(QStringLiteral("original.nodivu.json"));
        projects->save(original);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(original) && !projects->busy(), 5000);
        const auto saved = nodivu::ProjectRepository::read(original);
        QVERIFY(saved.ok());
        session.addNode(QStringLiteral("tone"));
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && projects->dirty(), 5000);
        // Deliberately ask the owned child to exit outside the GUI shutdown path.
        // Its unexpected EOF must protect the last valid snapshot.
        session.command(QStringLiteral("system.shutdown"), {});
        QTRY_VERIFY_WITH_TIMEOUT(!session.online(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(projects->recoveryPending() && !projects->busy(), 5000);
        const auto recovery =
            QDir(profile.path()).filePath(QStringLiteral("recovery/recovery.nodivu.json"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(recovery), 5000);
        const auto recovered = nodivu::ProjectRepository::read(recovery);
        QVERIFY(recovered.ok());
        QCOMPARE(QJsonDocument::fromJson(recovered.contents)
                     .object()
                     .value(QStringLiteral("graph"))
                     .toObject()
                     .value(QStringLiteral("nodes"))
                     .toArray()
                     .size(),
                 1);
        QCOMPARE(nodivu::ProjectRepository::read(original).contents, saved.contents);
        QVERIFY(projects->finishClose());
        QVERIFY(QFileInfo::exists(recovery));
    }
    void realBackendAndGraph() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = profile.path();
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        // The offscreen platform has no Windows font discovery. Use the same
        // system font as the real application for meaningful visual evidence.
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeuib.ttf"));
        QVERIFY2(QFileInfo::exists(paths.backend),
                 "Build the existing Rust backend before integration tests.");
        nodivu::Theme::apply(*qobject_cast<QApplication*>(QCoreApplication::instance()));
        nodivu::MainWindow window(paths);
        window.show();
        auto& session = window.session();
        QSignalSpy errors(&session, &nodivu::SessionController::error);
        QTRY_VERIFY_WITH_TIMEOUT(session.online(), 10000);
        auto* projectTitle = window.findChild<QLabel*>(QStringLiteral("projectTitle"));
        QVERIFY(projectTitle);
        QCOMPARE(projectTitle->text(), QStringLiteral("Projeto sem título"));
        auto* recents = window.findChild<QMenu*>(QStringLiteral("projectRecents"));
        QVERIFY(recents);
        QVERIFY(!recents->menuAction()->isEnabled());
        QVERIFY2(errors.isEmpty(), "Backend initialization must not silently fail.");
        QVERIFY(session.catalog().entries().size() >= 10);
        const auto nodes = [&] {
            return session.snapshot()
                .value(QStringLiteral("graph"))
                .toObject()
                .value(QStringLiteral("nodes"))
                .toArray();
        };
        session.addNode(QStringLiteral("tone"));
        QTRY_COMPARE_WITH_TIMEOUT(nodes().size(), 1, 5000);
        QTRY_VERIFY(projectTitle->text().endsWith(QStringLiteral(" •")));
        session.addNode(QStringLiteral("gain"));
        QTRY_COMPARE_WITH_TIMEOUT(nodes().size(), 2, 5000);
        session.addNode(QStringLiteral("output"));
        QTRY_COMPARE_WITH_TIMEOUT(nodes().size(), 3, 5000);
        const auto idFor = [&](const QString& kind) {
            for (const auto& value : nodes())
                if (value.toObject()
                        .value(QStringLiteral("block"))
                        .toObject()
                        .value(QStringLiteral("kind"))
                        .toString() == kind)
                    return value.toObject().value(QStringLiteral("id")).toString();
            return QString();
        };
        const auto tone = idFor(QStringLiteral("tone"));
        const auto gain = idFor(QStringLiteral("gain"));
        const auto output = idFor(QStringLiteral("output"));
        session.connectNodes(tone, 0, gain, 0);
        const auto edges = [&] {
            return session.snapshot()
                .value(QStringLiteral("graph"))
                .toObject()
                .value(QStringLiteral("edges"))
                .toArray();
        };
        QTRY_COMPARE_WITH_TIMEOUT(edges().size(), 1, 5000);
        session.connectNodes(gain, 0, output, 0);
        QTRY_COMPARE_WITH_TIMEOUT(edges().size(), 2, 5000);
        session.updateField(gain, QStringLiteral("gain_db"), -12.0);
        QTRY_COMPARE_WITH_TIMEOUT(session.node(gain)
                                      .value(QStringLiteral("block"))
                                      .toObject()
                                      .value(QStringLiteral("gain_db"))
                                      .toDouble(),
                                  -12.0, 5000);
        auto* inspector = window.findChild<nodivu::InspectorPanel*>();
        auto* scene = window.findChild<nodivu::GraphScene*>();
        auto* view = window.findChild<nodivu::GraphView*>();
        QVERIFY(inspector && scene && view);
        inspector->inspect(gain);
        view->fitNodes();
        QTRY_VERIFY(inspector->isVisible());
        QCOMPARE(scene->positions().size(), 3);
        const QRect inspectorBounds(
            view->viewport()->mapFromGlobal(inspector->mapToGlobal(QPoint())), inspector->size());
        for (auto* item : scene->items()) {
            if (item->parentItem() || !item->flags().testFlag(QGraphicsItem::ItemIsMovable))
                continue;
            const auto visibleBounds = view->mapFromScene(item->sceneBoundingRect()).boundingRect();
            QVERIFY(view->viewport()->rect().contains(visibleBounds));
            QVERIFY(!inspectorBounds.intersects(visibleBounds));
        }
        QTest::qWait(100);
        const auto screenshot =
            QDir(QStringLiteral(NODIVU_REPOSITORY_DIR))
                .filePath(QStringLiteral(".local/qt-migration/qt-workspace.png"));
        QVERIFY(window.grab().save(screenshot));
        QVERIFY2(errors.isEmpty(), "Real graph operations must succeed.");
        auto* routingDock = window.findChild<QDockWidget*>(QStringLiteral("routingDock"));
        QVERIFY(routingDock);
        routingDock->show();
        QTRY_VERIFY(inspector->parentWidget()->rect().contains(inspector->geometry()));
        auto* routing = routingDock->findChild<nodivu::RoutingPanel*>();
        QVERIFY(routing);
        auto* tree = routing->findChild<QTreeWidget*>();
        QVERIFY(tree);
        QTRY_COMPARE(tree->topLevelItemCount(), 3);
        auto* accessible = QAccessible::queryAccessibleInterface(tree);
        QVERIFY(accessible);
        QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Blocos e conexões"));
        QSignalSpy routingInspection(routing, &nodivu::RoutingPanel::inspectRequested);
        tree->setCurrentItem(tree->topLevelItem(1));
        tree->setFocus();
        QTest::keyClick(tree, Qt::Key_Return);
        QCOMPARE(routingInspection.size(), 1);
        QCOMPARE(inspector->selectedId(), gain);
        routingDock->hide();
        QGraphicsPathItem* connection = nullptr;
        for (auto* item : scene->items()) {
            const auto edge = item->data(3).toJsonObject();
            if (edge.value(QStringLiteral("from")).toString() == tone &&
                edge.value(QStringLiteral("to")).toString() == gain)
                connection = dynamic_cast<QGraphicsPathItem*>(item);
        }
        QVERIFY(connection);
        const auto oldPath = connection->path();
        scene->placeNode(gain, QPointF(410, 170));
        QVERIFY(connection->path() != oldPath);
        const auto wirePoint = connection->path().pointAtPercent(0.5);
        QVERIFY(scene->items(wirePoint, Qt::IntersectsItemShape).contains(connection));
        scene->clearSelection();
        connection->setSelected(true);
        QTest::keyClick(view->viewport(), Qt::Key_Delete);
        QTRY_COMPARE_WITH_TIMEOUT(edges().size(), 1, 5000);
        session.connectNodes(tone, 0, gain, 0);
        QTRY_COMPARE_WITH_TIMEOUT(edges().size(), 2, 5000);
        session.removeNode(gain);
        QTRY_COMPARE_WITH_TIMEOUT(nodes().size(), 2, 5000);
        QCOMPARE(edges().size(), 0);
        auto* projects = window.findChild<nodivu::ProjectController*>();
        QVERIFY(projects);
        QTRY_VERIFY(session.settled());
        const auto savedFile =
            QDir(profile.path()).filePath(QStringLiteral("Projeto de teste.nodivu.json"));
        projects->save(savedFile);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(savedFile), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!projects->busy() && !projects->dirty(), 5000);
        QCOMPARE(projectTitle->text(), QFileInfo(savedFile).fileName());
        QVERIFY(recents->menuAction()->isEnabled());
        QCOMPARE(recents->actions().first()->data().toString(), savedFile);
        projects->open(savedFile);
        QTRY_VERIFY_WITH_TIMEOUT(!projects->busy() && session.settled(), 5000);
        QCOMPARE(nodes().size(), 2);
        QVERIFY(!projects->dirty());
        window.close();
        QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 5000);
    }
};
QTEST_MAIN(WorkspaceTests)
#include "workspace_tests.moc"
