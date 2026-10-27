#include "features/graph/graph_view.hpp"
#include "features/inspector/inspector_panel.hpp"
#include "features/projects/project_controller.hpp"
#include "features/store/store_window.hpp"
#include "features/workspace/main_window.hpp"
#include "ui/theme/theme.hpp"
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFontDatabase>
#include <QPointer>
#include <QTemporaryDir>
#include <QtTest>

class LifecycleTests final : public QObject {
    Q_OBJECT
  private slots:
    void oneHundredOpenEditSaveCloseCyclesWithRealBackend() {
        QApplication::setQuitOnLastWindowClosed(false);
        auto* application = qobject_cast<QApplication*>(QCoreApplication::instance());
        QVERIFY(application);
        nodivu::Theme::apply(*application);
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeuib.ttf"));
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto project = QDir(temporary.path()).filePath(QStringLiteral("cycle.json"));
        QVERIFY(QFile::copy(
            QStringLiteral(NODIVU_REPOSITORY_DIR) +
                QStringLiteral("/apps/nodivu-desktop/tests/fixtures/electron-project-v1.json"),
            project));
        auto paths = nodivu::AppPaths::development(QStringLiteral(NODIVU_REPOSITORY_DIR));
        paths.profile = temporary.path();
        paths.plugins = QDir(temporary.path()).filePath(QStringLiteral("plugins"));
        paths.storeSource.clear();
        for (int cycle = 0; cycle < 100; ++cycle) {
            nodivu::MainWindow window(paths);
            window.show();
            auto& session = window.session();
            auto* projects = window.findChild<nodivu::ProjectController*>();
            QVERIFY(projects);
            QTRY_VERIFY_WITH_TIMEOUT(session.settled() && !projects->busy(), 10000);
            projects->open(project);
            QTRY_VERIFY_WITH_TIMEOUT(
                !projects->busy() && projects->path() == project && session.settled(), 5000);
            QCOMPARE(session.snapshot()
                         .value(QStringLiteral("graph"))
                         .toObject()
                         .value(QStringLiteral("nodes"))
                         .toArray()
                         .size(),
                     4);
            const auto gain = QStringLiteral("00000000-0000-4000-8000-000000000002");
            const double value = cycle % 2 == 0 ? -12.0 : -6.0;
            session.updateField(gain, QStringLiteral("gain_db"), value);
            QTRY_COMPARE_WITH_TIMEOUT(session.node(gain)
                                          .value(QStringLiteral("block"))
                                          .toObject()
                                          .value(QStringLiteral("gain_db"))
                                          .toDouble(),
                                      value, 5000);
            window.findChild<nodivu::InspectorPanel*>()->inspect(gain);
            auto* storeController = window.findChild<nodivu::StoreController*>();
            QVERIFY(storeController);
            QTRY_VERIFY_WITH_TIMEOUT(!storeController->busy(), 5000);
            QPointer<nodivu::StoreWindow> store =
                new nodivu::StoreWindow(*storeController, &window);
            store->show();
            QTRY_VERIFY(store->isVisible());
            store->close();
            QTRY_VERIFY(store.isNull());
            window.findChild<nodivu::GraphView*>()->fitNodes();
            if (cycle % 10 == 0)
                window.findChild<QDockWidget*>(QStringLiteral("routingDock"))->show();
            QTRY_VERIFY_WITH_TIMEOUT(session.settled(), 5000);
            projects->save(project);
            QTRY_VERIFY_WITH_TIMEOUT(!projects->busy() && !projects->dirty(), 5000);
            window.close();
            QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 5000);
            QVERIFY(!session.online());
            QCOMPARE(session.snapshot().value(QStringLiteral("engine_state")).toString(),
                     QStringLiteral("idle"));
        }
    }
};
QTEST_MAIN(LifecycleTests)
#include "lifecycle_tests.moc"
