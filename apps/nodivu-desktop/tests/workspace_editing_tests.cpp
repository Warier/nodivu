#include "features/graph/empty_graph_widget.hpp"
#include "features/graph/graph_scene.hpp"
#include "features/graph/graph_view.hpp"
#include "features/graph/mini_map.hpp"
#include "features/inspector/inspector_panel.hpp"
#include "features/library/library_block_button.hpp"
#include "features/library/library_panel.hpp"
#include "features/projects/project_controller.hpp"
#include "features/workspace/main_window.hpp"
#include "features/workspace/workspace_tools.hpp"
#include "ui/theme/theme.hpp"
#include <QAction>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGraphicsProxyWidget>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSplitter>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

namespace {
nodivu::AppPaths pathsFor(const QTemporaryDir& profile) {
    auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
    paths.profile = profile.path();
    paths.plugins = profile.filePath(QStringLiteral("plugins"));
    paths.storeSource.clear();
    return paths;
}
QJsonArray nodes(const nodivu::SessionController& session) {
    return session.snapshot()
        .value(QStringLiteral("graph"))
        .toObject()
        .value(QStringLiteral("nodes"))
        .toArray();
}
QString idFor(const nodivu::SessionController& session, const QString& kind) {
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
    QTest::qWait(50);
    const auto directory = qEnvironmentVariable("NODIVU_WORKSPACE_SCREENSHOTS");
    if (!directory.isEmpty() && QDir().mkpath(directory))
        widget.grab().save(QDir(directory).filePath(name));
}
} // namespace
class WorkspaceEditingTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeuib.ttf"));
        nodivu::Theme::apply(*qApp);
    }
    void namesSurviveHistorySaveAsAndRecoveryWithoutChangingTheAudioFormat() {
        QTemporaryDir profile;
        nodivu::MainWindow window(pathsFor(profile));
        window.show();
        auto& session = window.session();
        auto* projects = window.findChild<nodivu::ProjectController*>();
        auto* scene = window.findChild<nodivu::GraphScene*>();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !projects->busy(), 10000);
        session.addNode(QStringLiteral("gain"));
        QTRY_COMPARE(nodes(session).size(), 1);
        QTRY_VERIFY(session.settled());
        const auto id = idFor(session, QStringLiteral("gain"));
        // A real click/shortcut follows the deferred first placement and Qt layout.
        QTest::qWait(50);
        const auto file = profile.filePath(QStringLiteral("Original.nodivu.json"));
        projects->save(file);
        QTRY_VERIFY(!projects->busy() && !projects->dirty());
        const auto graph = session.snapshot().value(QStringLiteral("graph"));
        scene->renameNode(id, QStringLiteral("Volume da música"));
        QVERIFY(projects->dirty());
        projects->undo();
        QTRY_VERIFY(!projects->busy());
        QCOMPARE(scene->nodeTitle(id), QStringLiteral("Ganho"));
        projects->redo();
        QTRY_VERIFY(!projects->busy());
        QCOMPARE(scene->nodeTitle(id), QStringLiteral("Volume da música"));
        QCOMPARE(session.snapshot().value(QStringLiteral("graph")), graph);
        const auto copy = profile.filePath(QStringLiteral("Cópia.nodivu.json"));
        projects->save(copy);
        QTRY_VERIFY(!projects->busy() && !projects->dirty());
        const auto saved = nodivu::ProjectRepository::readProject(copy);
        QVERIFY(saved.ok());
        QVERIFY(saved.warning.isEmpty());
        QCOMPARE(saved.presentation.value(QStringLiteral("names")).toObject().value(id).toString(),
                 QStringLiteral("Volume da música"));
        QVERIFY(!QJsonDocument::fromJson(saved.contents).object().contains(QStringLiteral("_ui")));
        projects->open(file);
        QTRY_VERIFY(!projects->busy() && session.settled());
        QCOMPARE(scene->nodeTitle(id), QStringLiteral("Ganho"));
        projects->open(copy);
        QTRY_VERIFY(!projects->busy() && session.settled());
        QCOMPARE(scene->nodeTitle(id), QStringLiteral("Volume da música"));
        scene->renameNode(id, QStringLiteral("Volume da voz"));
        const auto recovery = profile.filePath(QStringLiteral("recovery/recovery.nodivu.json"));
        QVERIFY(nodivu::ProjectRepository::writeProject(recovery, projects->document(),
                                                        {scene->presentation()})
                    .ok());
        projects->save(copy);
        QTRY_VERIFY(!projects->busy() && !projects->dirty());
        QVERIFY(nodivu::ProjectRepository::writeProject(recovery, projects->document(),
                                                        {scene->presentation()})
                    .ok());
        projects->restoreRecovery();
        QTRY_VERIFY(!projects->busy() && session.settled());
        QCOMPARE(scene->nodeTitle(id), QStringLiteral("Volume da voz"));
        QVERIFY(projects->dirty());
    }
    void groupEditingIsAtomicAndTemplatesDoNotCopyLiveSelections() {
        QTemporaryDir profile;
        nodivu::MainWindow window(pathsFor(profile));
        window.show();
        auto& session = window.session();
        auto* projects = window.findChild<nodivu::ProjectController*>();
        auto* scene = window.findChild<nodivu::GraphScene*>();
        auto* view = window.findChild<nodivu::GraphView*>();
        auto* tools = window.findChild<nodivu::WorkspaceTools*>();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !projects->busy(), 10000);
        session.addNode(QStringLiteral("mixer"));
        QTRY_VERIFY(session.settled() && nodes(session).size() == 1);
        session.addNode(QStringLiteral("gain"));
        QTRY_VERIFY(session.settled() && nodes(session).size() == 2);
        const auto tone = idFor(session, QStringLiteral("mixer")),
                   gain = idFor(session, QStringLiteral("gain"));
        session.connectNodes(tone, 0, gain, 0);
        QTRY_VERIFY(session.settled());
        scene->placeNode(tone, QPointF(60, 70));
        scene->placeNode(gain, QPointF(400, 190));
        view->fitNodes();
        const auto rectangle = scene->nodeItem(tone)
                                   ->sceneBoundingRect()
                                   .united(scene->nodeItem(gain)->sceneBoundingRect())
                                   .adjusted(-15, -15, 15, 15);
        scene->clearSelection();
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::ShiftModifier,
                          view->mapFromScene(rectangle.topLeft()));
        QTest::mouseMove(view->viewport(), view->mapFromScene(rectangle.bottomRight()));
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::ShiftModifier,
                            view->mapFromScene(rectangle.bottomRight()));
        QCOMPARE(scene->selectedNodeIds().size(), 2);
        scene->renameNode(gain, QStringLiteral("Volume da trilha"));
        projects->save(profile.filePath(QStringLiteral("Group.nodivu.json")));
        QTRY_VERIFY(!projects->busy());
        scene->selectNodes({tone, gain});
        scene->alignSelection(true);
        QCOMPARE(scene->nodeItem(tone)->pos().y(), scene->nodeItem(gain)->pos().y());
        projects->undo();
        QTRY_VERIFY(!projects->busy());
        QCOMPARE(scene->nodeItem(tone)->pos(), QPointF(60, 70));
        QCOMPARE(scene->nodeItem(gain)->pos(), QPointF(400, 190));
        scene->selectNodes({tone, gain});
        view->setFocus();
        scene->clearFocus();
        QTest::keyClick(view->viewport(), Qt::Key_Right);
        QCOMPARE(scene->nodeItem(tone)->pos(), QPointF(70, 70));
        QCOMPARE(scene->nodeItem(gain)->pos(), QPointF(410, 190));
        const auto model = profile.filePath(QStringLiteral("Voz.nodivu-chain.json"));
        tools->saveTemplate(model);
        QVERIFY(QFileInfo::exists(model));
        const auto fragment =
            QJsonDocument::fromJson(nodivu::ProjectRepository::read(model).contents).object();
        QCOMPARE(fragment.value(QStringLiteral("nodes")).toArray().size(), 2);
        tools->duplicateSelection();
        QTRY_VERIFY(session.settled() && nodes(session).size() == 4);
        QCOMPARE(scene->selectedNodeIds().size(), 2);
        QVERIFY(!scene->selectedNodeIds().contains(tone));
        QVERIFY(!scene->selectedNodeIds().contains(gain));
        bool renamed = false;
        for (const auto& id : scene->selectedNodeIds())
            renamed |= scene->nodeTitle(id) == QStringLiteral("Volume da trilha");
        QVERIFY(renamed);
        QCOMPARE(session.snapshot()
                     .value(QStringLiteral("graph"))
                     .toObject()
                     .value(QStringLiteral("edges"))
                     .toArray()
                     .size(),
                 2);
        scene->removeSelection();
        QTRY_VERIFY(session.settled() && nodes(session).size() == 2);
        projects->undo();
        QTRY_VERIFY(!projects->busy() && session.settled() && nodes(session).size() == 4);
        tools->insertTemplate(model);
        QTRY_VERIFY(session.settled() && nodes(session).size() == 6);
        const auto before = session.snapshot().value(QStringLiteral("graph"));
        auto invalid = fragment;
        invalid.insert(QStringLiteral("positions"), QJsonArray{});
        QSignalSpy errors(&session, &nodivu::SessionController::error);
        scene->insertFragment(invalid, QPointF());
        QVERIFY(!errors.isEmpty());
        QCOMPARE(session.snapshot().value(QStringLiteral("graph")), before);
        invalid = fragment;
        auto invalidEdges = fragment.value(QStringLiteral("edges")).toArray();
        invalidEdges.append(QJsonObject{{QStringLiteral("from"), gain},
                                        {QStringLiteral("from_port"), 0},
                                        {QStringLiteral("to"), tone},
                                        {QStringLiteral("to_port"), 0}});
        invalid.insert(QStringLiteral("edges"), invalidEdges);
        errors.clear();
        scene->insertFragment(invalid, QPointF());
        QTRY_VERIFY(session.settled() && !errors.isEmpty());
        QCOMPARE(session.snapshot().value(QStringLiteral("graph")), before);
        scene->organize();
        window.findChild<QToolButton*>(QStringLiteral("feedbackClose"))->click();
        window.findChild<nodivu::LibraryPanel*>()->setFavorites({QStringLiteral("gain")});
        window.findChild<QAction*>(QStringLiteral("miniMapToggle"))->setChecked(true);
        view->fitNodes();
        capture(window, QStringLiteral("workspace-group-editing.png"));
    }
    void organizingSelectionUsesSpacingAndWorksWithCtrlClick() {
        QTemporaryDir profile;
        nodivu::MainWindow window(pathsFor(profile));
        window.show();
        auto& session = window.session();
        auto* scene = window.findChild<nodivu::GraphScene*>();
        auto* view = window.findChild<nodivu::GraphView*>();
        auto* projects = window.findChild<nodivu::ProjectController*>();
        auto* row = window.findChild<QAction*>(QStringLiteral("alignHorizontal"));
        auto* column = window.findChild<QAction*>(QStringLiteral("alignVertical"));
        QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !projects->busy(), 10000);
        for (const auto& kind :
             {QStringLiteral("gain"), QStringLiteral("mixer"), QStringLiteral("meter")}) {
            const auto count = nodes(session).size();
            session.addNode(kind);
            QTRY_VERIFY(session.settled() && nodes(session).size() == count + 1);
        }
        QTest::qWait(50);
        const auto gain = idFor(session, QStringLiteral("gain"));
        const auto mixer = idFor(session, QStringLiteral("mixer"));
        const auto meter = idFor(session, QStringLiteral("meter"));
        scene->placeNode(gain, QPointF(60, 70));
        scene->placeNode(mixer, QPointF(400, 400));
        scene->placeNode(meter, QPointF(350, 70));
        view->fitNodes();
        const auto header = [&](const QString& id) {
            return view->mapFromScene(scene->nodeItem(id)->mapToScene(QPointF(90, 14)));
        };
        scene->clearSelection();
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, header(gain));
        QCOMPARE(scene->selectedNodeIds().size(), 1);
        QVERIFY(!row->isEnabled() && !column->isEnabled());
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ControlModifier, header(mixer));
        QCOMPARE(scene->selectedNodeIds().size(), 2);
        QVERIFY(row->isEnabled() && column->isEnabled());
        const auto original = scene->positions();
        const auto stationary = scene->nodeItem(meter)->pos();
        const auto graph = session.snapshot().value(QStringLiteral("graph"));
        projects->save(profile.filePath(QStringLiteral("Arrange.nodivu.json")));
        QTRY_VERIFY(!projects->busy());
        const auto noOverlap = [&] {
            return !scene->nodeItem(gain)->sceneBoundingRect().intersects(
                       scene->nodeItem(mixer)->sceneBoundingRect()) &&
                   !scene->nodeItem(gain)->sceneBoundingRect().intersects(
                       scene->nodeItem(meter)->sceneBoundingRect()) &&
                   !scene->nodeItem(mixer)->sceneBoundingRect().intersects(
                       scene->nodeItem(meter)->sceneBoundingRect());
        };
        row->trigger();
        QCOMPARE(scene->nodeItem(gain)->pos().y(), scene->nodeItem(mixer)->pos().y());
        QVERIFY(noOverlap());
        QCOMPARE(scene->nodeItem(meter)->pos(), stationary);
        projects->undo();
        QTRY_VERIFY(!projects->busy());
        QCOMPARE(scene->positions(), original);
        column->trigger();
        QCOMPARE(scene->nodeItem(gain)->pos().x(), scene->nodeItem(mixer)->pos().x());
        QVERIFY(noOverlap());
        QCOMPARE(scene->nodeItem(meter)->pos(), stationary);
        const auto arranged = scene->positions();
        projects->undo();
        QTRY_VERIFY(!projects->busy());
        QCOMPARE(scene->positions(), original);
        projects->redo();
        QTRY_VERIFY(!projects->busy());
        QCOMPARE(scene->positions(), arranged);
        QCOMPARE(session.snapshot().value(QStringLiteral("graph")), graph);
        capture(window, QStringLiteral("organize-column.png"));
    }
    void templatesClearLiveSourceSelectionsAndRespectSingletons() {
        QTemporaryDir profile;
        nodivu::MainWindow window(pathsFor(profile));
        window.show();
        auto& session = window.session();
        auto* scene = window.findChild<nodivu::GraphScene*>();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 10000);
        session.addNode(QStringLiteral("tone"));
        QTRY_VERIFY(session.settled() && nodes(session).size() == 1);
        const auto id = idFor(session, QStringLiteral("tone"));
        session.updateField(id, QStringLiteral("enabled"), true);
        QTRY_VERIFY(session.settled());
        scene->selectNode(id);
        const auto fragment = scene->selectedFragment();
        QVERIFY(!fragment.value(QStringLiteral("nodes"))
                     .toArray()
                     .first()
                     .toObject()
                     .value(QStringLiteral("block"))
                     .toObject()
                     .value(QStringLiteral("enabled"))
                     .toBool());
        const auto before = session.snapshot().value(QStringLiteral("graph"));
        QSignalSpy errors(&session, &nodivu::SessionController::error);
        scene->insertFragment(fragment, QPointF());
        QVERIFY(!errors.isEmpty());
        QCOMPARE(session.snapshot().value(QStringLiteral("graph")), before);
        scene->removeSelection();
        QTRY_VERIFY(session.settled() && nodes(session).isEmpty());
        scene->insertFragment(fragment, QPointF(30, 40));
        QTRY_VERIFY(session.settled() && nodes(session).size() == 1);
        QVERIFY(!nodes(session)
                     .first()
                     .toObject()
                     .value(QStringLiteral("block"))
                     .toObject()
                     .value(QStringLiteral("enabled"))
                     .toBool());
        scene->removeSelection();
        QTRY_VERIFY(session.settled() && nodes(session).isEmpty());
        auto capture = fragment;
        auto source = fragment.value(QStringLiteral("nodes")).toArray().first().toObject();
        source.insert(QStringLiteral("block"),
                      QJsonObject{{QStringLiteral("kind"), QStringLiteral("capture")},
                                  {QStringLiteral("endpoint_id"),
                                   QStringLiteral("explicit-test-only-unavailable-endpoint")}});
        capture.insert(QStringLiteral("nodes"), QJsonArray{source});
        scene->insertFragment(capture, QPointF());
        QTRY_VERIFY(session.settled() && nodes(session).size() == 1);
        QVERIFY(nodes(session)
                    .first()
                    .toObject()
                    .value(QStringLiteral("block"))
                    .toObject()
                    .value(QStringLiteral("endpoint_id"))
                    .isNull());
    }
    void invalidNameCompanionsAndFailedMetadataSavesRemainRecoverable() {
        QTemporaryDir profile;
        const auto file = profile.filePath(QStringLiteral("Data.json"));
        const QJsonObject data{{QStringLiteral("format"), QStringLiteral("test-only")}};
        const QJsonObject names{
            {QStringLiteral("names"),
             QJsonObject{{QStringLiteral("node"), QStringLiteral("Minha voz")}}}};
        QVERIFY(nodivu::ProjectRepository::writeProject(file, data, {names}).ok());
        auto changed = data;
        changed.insert(QStringLiteral("changed"), true);
        QVERIFY(nodivu::ProjectRepository::write(file, changed).ok());
        const auto read = nodivu::ProjectRepository::readProject(file);
        QVERIFY(read.ok());
        QVERIFY(!read.warning.isEmpty());
        QVERIFY(read.presentation.isEmpty());
        const auto other = profile.filePath(QStringLiteral("Other.json"));
        QVERIFY(QDir().mkpath(other + QStringLiteral(".ui.json")));
        const auto result = nodivu::ProjectRepository::writeProject(other, data, {names});
        QVERIFY(!result.ok());
        QVERIFY(result.error.contains(QStringLiteral("projeto de áudio foi gravado")));
        QCOMPARE(QJsonDocument::fromJson(nodivu::ProjectRepository::read(other).contents).object(),
                 data);
    }
    void guidedConnectionsRejectOccupiedInputsAndCycles() {
        QTemporaryDir profile;
        nodivu::MainWindow window(pathsFor(profile));
        window.show();
        auto& session = window.session();
        auto* scene = window.findChild<nodivu::GraphScene*>();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 10000);
        session.addNode(QStringLiteral("gain"));
        QTRY_VERIFY(session.settled() && nodes(session).size() == 1);
        const auto gain = idFor(session, QStringLiteral("gain"));
        session.addNode(QStringLiteral("mixer"));
        QTRY_VERIFY(session.settled() && nodes(session).size() == 2);
        const auto mixer = idFor(session, QStringLiteral("mixer"));
        QVERIFY(scene->canConnect(gain, mixer, 0));
        QVERIFY(!scene->canConnect(gain, gain, 0));
        session.connectNodes(gain, 0, mixer, 0);
        QTRY_VERIFY(session.settled());
        QVERIFY(!scene->canConnect(gain, mixer, 0));
        QVERIFY(scene->canConnect(gain, mixer, 1));
        QVERIFY(!scene->canConnect(mixer, gain, 0));
        auto* view = window.findChild<nodivu::GraphView*>();
        QCoreApplication::processEvents();
        const auto source =
            scene->nodeItem(gain)->mapToScene(scene->nodeItem(gain)->portPosition(true, 0));
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                          view->mapFromScene(source));
        bool occupiedDimmed = false, freeHighlighted = false;
        for (auto* port : scene->nodeItem(mixer)->childItems()) {
            if (port->data(1).toString() != QStringLiteral("sink"))
                continue;
            if (port->data(2).toInt() == 0)
                occupiedDimmed |= port->opacity() < .5;
            if (port->data(2).toInt() == 1)
                freeHighlighted |= port->opacity() == 1 &&
                                   port->toolTip().contains(QStringLiteral("conectar aqui"));
        }
        QVERIFY(occupiedDimmed && freeHighlighted);
        QTest::keyClick(view->viewport(), Qt::Key_Escape);
        for (auto* port : scene->nodeItem(mixer)->childItems())
            if (port->data(1).toString() == QStringLiteral("sink"))
                QCOMPARE(port->opacity(), 1.0);
        scene->renameNode(gain, QStringLiteral("Voz principal"));
        bool tooltip = false;
        for (auto* item : scene->items())
            if (item->data(3).isValid())
                tooltip |= item->toolTip().contains(QStringLiteral("Voz principal")) &&
                           item->toolTip().contains(QStringLiteral("Mixer"));
        QVERIFY(tooltip);
    }
    void renameFromTheKeyboardReachesNativeInspectorAndRoutingLabels() {
        QTemporaryDir profile;
        nodivu::MainWindow window(pathsFor(profile));
        window.show();
        auto& session = window.session();
        auto* scene = window.findChild<nodivu::GraphScene*>();
        auto* view = window.findChild<nodivu::GraphView*>();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 10000);
        session.addNode(QStringLiteral("gain"));
        QTRY_VERIFY(session.settled() && nodes(session).size() == 1);
        const auto id = idFor(session, QStringLiteral("gain"));
        scene->selectNode(id);
        view->setFocus();
        scene->clearFocus();
        const auto name = QStringLiteral("<Mix> & Voz");
        QTimer::singleShot(0, &window, [&window, name] {
            auto* dialog = window.findChild<QInputDialog*>(QStringLiteral("renameNodeDialog"));
            QVERIFY(dialog);
            dialog->setTextValue(name);
            dialog->accept();
        });
        QTest::keyClick(view->viewport(), Qt::Key_F2);
        QCOMPARE(scene->nodeTitle(id), name);
        QVERIFY(scene->nodeItem(id)->toolTip().contains(QStringLiteral("&lt;Mix&gt; &amp; Voz")));
        window.findChild<QAction*>(QStringLiteral("routingToggle"))->trigger();
        auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("routingTree"));
        QTRY_VERIFY(tree && tree->topLevelItemCount() == 1);
        QVERIFY(tree->topLevelItem(0)->text(0).contains(name));
        auto* inspector = window.findChild<nodivu::InspectorPanel*>();
        inspector->inspect(id);
        bool visibleName = false;
        for (auto* label : inspector->findChildren<QLabel*>())
            visibleName |= label->text() == name && label->textFormat() == Qt::PlainText;
        QVERIFY(visibleName);
    }
    void compactModeMiniMapAndSearchKeepTheSelectionUsable() {
        QTemporaryDir profile;
        nodivu::MainWindow window(pathsFor(profile));
        window.show();
        auto& session = window.session();
        auto* scene = window.findChild<nodivu::GraphScene*>();
        auto* view = window.findChild<nodivu::GraphView*>();
        QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 10000);
        session.addNode(QStringLiteral("gain"));
        QTRY_VERIFY(session.settled() && nodes(session).size() == 1);
        const auto id = idFor(session, QStringLiteral("gain"));
        scene->renameNode(id, QStringLiteral("Áudio da música"));
        scene->selectNode(id);
        const auto expandedHeight = scene->nodeItem(id)->boundingRect().height();
        window.findChild<QAction*>(QStringLiteral("compactNodes"))->setChecked(true);
        scene->clearSelection();
        QTRY_VERIFY2(scene->nodeItem(id)->boundingRect().height() < expandedHeight,
                     qPrintable(QStringLiteral("expanded=%1 current=%2 selected=%3")
                                    .arg(expandedHeight)
                                    .arg(scene->nodeItem(id)->boundingRect().height())
                                    .arg(scene->nodeItem(id)->isSelected())));
        capture(window, QStringLiteral("compact-node-summary.png"));
        scene->selectNode(id);
        QCOMPARE(scene->nodeItem(id)->boundingRect().height(), expandedHeight);
        auto* minimap = window.findChild<nodivu::MiniMap*>();
        window.findChild<QAction*>(QStringLiteral("miniMapToggle"))->setChecked(true);
        QVERIFY(minimap->isVisible());
        const auto before = view->camera();
        minimap->setFocus();
        QTest::keyClick(minimap, Qt::Key_Right);
        QVERIFY(view->camera() != before);
        QCOMPARE(minimap->mapToGlobal(QPoint()),
                 view->viewport()->mapToGlobal(
                     QPoint(12, view->viewport()->height() - minimap->height() - 12)));
        QTimer::singleShot(0, &window, [&window] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("findProjectNodeDialog"));
            QVERIFY(dialog);
            dialog->findChild<QLineEdit*>(QStringLiteral("projectNodeSearch"))
                ->setText(QStringLiteral("audio musica"));
            QCOMPARE(dialog->findChild<QListWidget*>(QStringLiteral("projectNodeResults"))->count(),
                     1);
            dialog->accept();
        });
        window.findChild<QAction*>(QStringLiteral("findProjectNode"))->trigger();
        QCOMPARE(scene->selectedNodeIds(), QStringList{id});
        const auto center = view->mapFromScene(scene->nodeItem(id)->sceneBoundingRect().center());
        QVERIFY((center - view->availableViewportRect().center()).manhattanLength() <= 3);
        capture(window, QStringLiteral("workspace-tools.png"));
    }
    void favoritesAndWorkspacePreferencesSurviveReopening() {
        QTemporaryDir profile;
        {
            nodivu::MainWindow window(pathsFor(profile));
            window.show();
            QTRY_VERIFY_WITH_TIMEOUT(window.session().settled(), 10000);
            auto* library = window.findChild<nodivu::LibraryPanel*>();
            auto* gain = library->findChild<nodivu::LibraryBlockButton*>(QStringLiteral("gain"));
            QVERIFY(!gain->actions().isEmpty());
            gain->actions().first()->trigger();
            QTRY_VERIFY(library->favorites().contains(QStringLiteral("gain")));
            auto* favoriteCard =
                library->findChild<nodivu::LibraryBlockButton*>(QStringLiteral("favorite:gain"));
            QVERIFY(favoriteCard);
            QTRY_COMPARE(favoriteCard->visibleRegion().boundingRect().size(), favoriteCard->size());
            library->findChild<QToolButton*>(QStringLiteral("libraryToggle"))->click();
            window.findChild<QAction*>(QStringLiteral("compactNodes"))->setChecked(true);
            window.findChild<QAction*>(QStringLiteral("miniMapToggle"))->setChecked(true);
            window.findChild<QAction*>(QStringLiteral("highContrast"))->setChecked(true);
            QVERIFY(nodivu::Theme::highContrast());
            window.findChild<QAction*>(QStringLiteral("interfaceScale125"))->trigger();
            window.findChild<nodivu::WorkspaceTools*>()->savePreferences();
            QSettings saved(profile.filePath(QStringLiteral("workspace.ini")),
                            QSettings::IniFormat);
            QCOMPARE(saved.value(QStringLiteral("scale")).toInt(), 125);
        }
        const auto restore = qScopeGuard([] { nodivu::Theme::apply(*qApp); });
        nodivu::MainWindow reopened(pathsFor(profile));
        reopened.show();
        QTRY_VERIFY_WITH_TIMEOUT(reopened.session().settled(), 10000);
        auto* library = reopened.findChild<nodivu::LibraryPanel*>();
        QVERIFY(library->favorites().contains(QStringLiteral("gain")));
        QCOMPARE(library->width(), 64);
        QVERIFY(reopened.property("workspaceRestored").toBool());
        QVERIFY(reopened.findChild<QAction*>(QStringLiteral("compactNodes"))->isChecked());
        QVERIFY(reopened.findChild<QAction*>(QStringLiteral("miniMapToggle"))->isChecked());
        QVERIFY(reopened.findChild<QAction*>(QStringLiteral("highContrast"))->isChecked());
        capture(reopened, QStringLiteral("workspace-high-contrast.png"));
    }
    void restoringSidebarKeepsItAtTheLeftAndTheCanvasVisible_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<bool>("collapsed");
        QTest::newRow("narrow") << 184 << false;
        QTest::newRow("default") << 248 << false;
        QTest::newRow("wide") << 320 << false;
        QTest::newRow("collapsed") << 184 << true;
    }
    void restoringSidebarKeepsItAtTheLeftAndTheCanvasVisible() {
        QFETCH(int, width);
        QFETCH(bool, collapsed);
        QTemporaryDir profile;
        int savedWidth = width;
        {
            nodivu::MainWindow original(pathsFor(profile));
            original.showMaximized();
            QTRY_VERIFY_WITH_TIMEOUT(original.session().settled(), 10000);
            auto* splitter = original.findChild<QSplitter*>();
            auto* library = original.findChild<nodivu::LibraryPanel*>();
            splitter->setSizes({width, splitter->width() - splitter->handleWidth() - width});
            const int needed =
                width + splitter->handleWidth() + splitter->widget(1)->minimumSizeHint().width();
            if (needed <= splitter->contentsRect().width())
                QTRY_COMPARE(library->width(), width);
            else
                QTRY_VERIFY(library->width() >= library->minimumWidth() &&
                            library->width() <= width);
            // The offscreen platform's 800px monitor may constrain wide panes.
            // Persist and restore the width the splitter can actually display.
            savedWidth = library->width();
            if (collapsed) {
                library->findChild<QToolButton*>(QStringLiteral("libraryToggle"))->click();
                QTRY_COMPARE(library->width(), 64);
            }
            original.findChild<nodivu::WorkspaceTools*>()->savePreferences();
        }
        nodivu::MainWindow reopened(pathsFor(profile));
        reopened.show();
        QTRY_VERIFY_WITH_TIMEOUT(reopened.session().settled(), 10000);
        auto* splitter = reopened.findChild<QSplitter*>();
        auto* library = reopened.findChild<nodivu::LibraryPanel*>();
        auto* view = reopened.findChild<nodivu::GraphView*>();
        capture(reopened, QStringLiteral("startup-sidebar-%1-%2.png").arg(width).arg(collapsed));
        QCOMPARE(splitter->count(), 2);
        const auto offset = library->mapTo(splitter, QPoint()).x();
        QVERIFY2(offset == 0, qPrintable(QStringLiteral("Sidebar x=%1, width=%2, canvas=%3")
                                             .arg(offset)
                                             .arg(library->width())
                                             .arg(splitter->widget(1)->width())));
        QVERIFY(splitter->widget(1)->isVisible() && splitter->widget(1)->width() > 0);
        QVERIFY(view->isVisible() && view->viewport()->width() > 0);
        QCOMPARE(library->width(), collapsed ? 64 : savedWidth);
        QVERIFY(reopened.property("workspaceRestored").toBool());
    }
    void sidebarReflowsCardsAndKeepsControlsVisibleAtEveryWidth() {
        QTemporaryDir profile;
        nodivu::MainWindow window(pathsFor(profile));
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(window.session().settled(), 10000);
        auto* library = window.findChild<nodivu::LibraryPanel*>();
        auto* splitter = window.findChild<QSplitter*>();
        auto* scroll = library->findChild<QScrollArea*>(QStringLiteral("libraryScroll"));
        auto* search = library->findChild<QLineEdit*>(QStringLiteral("blockSearch"));
        auto* output = library->findChild<QPushButton*>(QStringLiteral("virtualOutputShortcut"));
        library->setFavorites({QStringLiteral("gain")});
        auto* source = library->findChild<nodivu::LibraryBlockButton*>(QStringLiteral("capture"));
        const auto tooltip = source->toolTip();
        int narrowHeight = 0;
        for (const int width : {184, 208, 248, 320, 184, 320}) {
            splitter->setSizes({width, splitter->width() - splitter->handleWidth() - width});
            QTRY_COMPARE(library->width(), width);
            QTRY_VERIFY(scroll->widget()->width() <= scroll->viewport()->width());
            for (auto* control : {static_cast<QWidget*>(search), static_cast<QWidget*>(output)}) {
                const auto x = control->mapTo(scroll->viewport(), QPoint()).x();
                QVERIFY(x >= 0 && x + control->width() <= scroll->viewport()->width());
            }
            for (auto* card : library->findChildren<nodivu::LibraryBlockButton*>()) {
                if (!card->isVisible())
                    continue;
                scroll->ensureWidgetVisible(card);
                QTRY_COMPARE(card->visibleRegion().boundingRect().size(), card->size());
            }
            QCOMPARE(source->toolTip(), tooltip);
            if (width == 184)
                narrowHeight = source->height();
            if (width == 320)
                QVERIFY(narrowHeight > source->height());
            scroll->verticalScrollBar()->setValue(0);
            capture(window, QStringLiteral("sidebar-%1.png").arg(width));
        }
        library->findChild<QToolButton*>(QStringLiteral("libraryToggle"))->click();
        QTRY_COMPARE(library->width(), 64);
        QTRY_VERIFY(scroll->widget()->width() <= scroll->viewport()->width());
        library->focusSearch();
        QTRY_COMPARE(library->width(), 320);
        search->setText(QStringLiteral("microfone"));
        QTRY_VERIFY(source->isVisible());
        QTest::mouseClick(source, Qt::LeftButton);
        QTRY_VERIFY(window.session().settled() && nodes(window.session()).size() == 1);
    }
    void onboardingScrollsToKeepActionsReachableOnShortViewports() {
        QGraphicsScene scene;
        nodivu::GraphView view(&scene);
        view.resize(600, 240);
        nodivu::EmptyGraphWidget welcome(view.viewport());
        view.show();
        welcome.show();
        view.activateWindow();
        QTRY_VERIFY(view.isActiveWindow());
        auto* scroll = welcome.findChild<QScrollArea*>(QStringLiteral("welcomeScroll"));
        auto* file = welcome.findChild<QPushButton*>(QStringLiteral("startFile"));
        QVERIFY(scroll && file);
        QTRY_VERIFY(view.viewport()->rect().contains(welcome.geometry()));
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
        // This standalone geometry test exercises focus/scrolling, not a media source.
        file->setEnabled(true);
        file->setFocus(Qt::TabFocusReason);
        QTRY_VERIFY(scroll->viewport()->rect().contains(
            QRect(file->mapTo(scroll->viewport(), QPoint()), file->size())));
    }
};
QTEST_MAIN(WorkspaceEditingTests)
#include "workspace_editing_tests.moc"
