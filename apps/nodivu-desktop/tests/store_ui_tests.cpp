#include "features/store/readme_view.hpp"
#include "features/store/store_controller.hpp"
#include "features/store/store_window.hpp"
#include "platform/single_instance.hpp"
#include "ui/theme/theme.hpp"
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QTreeWidget>
#include <QtTest>

namespace {
// Inert filesystem fixture, never loaded by the GUI or Rust backend.
QString packageFixture(const QString& directory, const QString& version) {
    QDir().mkpath(directory);
    const QJsonObject manifest{{QStringLiteral("schema_version"), 1},
                               {QStringLiteral("plugin_id"), QStringLiteral("org.example.parity")},
                               {QStringLiteral("title"), QStringLiteral("Parity test plugin")},
                               {QStringLiteral("library"), QStringLiteral("inert.clap")}};
    const QJsonObject envelope{{QStringLiteral("schema_version"), 1},
                               {QStringLiteral("plugin_id"), QStringLiteral("org.example.parity")},
                               {QStringLiteral("version"), version},
                               {QStringLiteral("author"), QStringLiteral("Test only")}};
    const QList<QPair<QString, QByteArray>> files{
        {QStringLiteral("plugin.json"), QJsonDocument(manifest).toJson()},
        {QStringLiteral("nodivu-package.json"), QJsonDocument(envelope).toJson()},
        {QStringLiteral("inert.clap"), QByteArray("inert; not executable")}};
    for (const auto& file : files) {
        QFile output(QDir(directory).filePath(file.first));
        if (!output.open(QIODevice::WriteOnly) || output.write(file.second) != file.second.size())
            qFatal("Could not write isolated package fixture");
    }
    return directory;
}
} // namespace

class StoreUiTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeuib.ttf"));
        nodivu::Theme::apply(*qApp);
    }
    void firstSelectionUpdatesManagementActionsAndPendingChanges() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto paths = nodivu::AppPaths::development(directory.path());
        paths.storeSource.clear();
        nodivu::PackageService packages(paths.plugins);
        const auto first = packageFixture(QDir(directory.path()).filePath(QStringLiteral("first")),
                                          QStringLiteral("1.0.0"));
        const auto next = packageFixture(QDir(directory.path()).filePath(QStringLiteral("next")),
                                         QStringLiteral("1.1.0"));
        QVERIFY(packages.importFolder(first).ok());
        const auto key = packages.installed().first().key;
        const auto manifest =
            QDir(packages.installed().first().directory).filePath(QStringLiteral("plugin.json"));
        QFile report(QDir(paths.profile).filePath(QStringLiteral("plugin-load-report.json")));
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(
            QJsonDocument(QJsonArray{QJsonObject{{QStringLiteral("manifest"), manifest},
                                                 {QStringLiteral("message"),
                                                  QStringLiteral("DLL fixture could not load")}}})
                .toJson());
        report.close();
        nodivu::StoreController controller(paths);
        nodivu::StoreWindow window(controller);
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        auto* list = window.findChild<QTreeWidget*>(QStringLiteral("pluginsList"));
        auto* enable = window.findChild<QPushButton*>(QStringLiteral("pluginEnable"));
        auto* remove = window.findChild<QPushButton*>(QStringLiteral("pluginRemove"));
        auto* rollback = window.findChild<QPushButton*>(QStringLiteral("pluginRollback"));
        auto* cancel = window.findChild<QPushButton*>(QStringLiteral("pluginCancelUpdate"));
        auto* details = window.findChild<QPushButton*>(QStringLiteral("pluginAction"));
        QVERIFY(list && enable && remove && rollback && cancel && details);
        QCOMPARE(list->topLevelItemCount(), 1);
        QVERIFY(!enable->isEnabled() && !remove->isEnabled() && !details->isEnabled());
        list->setCurrentItem(list->topLevelItem(0));
        QVERIFY(enable->isEnabled() && remove->isEnabled() && details->isEnabled());
        QVERIFY(!rollback->isEnabled() && !cancel->isEnabled());
        QVERIFY(
            list->topLevelItem(0)->text(2).contains(QStringLiteral("DLL fixture could not load")));
        enable->click();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(!controller.state().installed.first().enabled);
        QVERIFY(controller.state().packageStatus.value(key).pending);
        QCOMPARE(enable->text(), QStringLiteral("Ativar"));
        controller.importFolder(next);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QCOMPARE(controller.state().packageStatus.value(key).pendingVersion,
                 QStringLiteral("1.1.0"));
        QVERIFY(list->topLevelItem(0)->text(2).contains(QStringLiteral("1.1.0 agendada")));
        QVERIFY(cancel->isEnabled() && !remove->isEnabled());
        const auto screenshots = qEnvironmentVariable("NODIVU_STORE_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            window.show();
            QCoreApplication::processEvents();
            QVERIFY(window.grab().save(
                QDir(screenshots).filePath(QStringLiteral("plugins-pending.png"))));
        }
        cancel->click();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(controller.state().packageStatus.value(key).pendingVersion.isEmpty());
        QVERIFY(!cancel->isEnabled() && remove->isEnabled());
        QCOMPARE(controller.state().installed.first().version, QStringLiteral("1.0.0"));
        list->clearSelection();
        QVERIFY(!enable->isEnabled() && !remove->isEnabled() && !details->isEnabled());
    }
    void protectedPackagesAndBrokenReportsRemainExplicit() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto paths = nodivu::AppPaths::development(directory.path());
        paths.storeSource.clear();
        packageFixture(QDir(paths.plugins).filePath(QStringLiteral("mp3")),
                       QStringLiteral("1.0.0"));
        nodivu::StoreController controller(paths);
        nodivu::StoreWindow window(controller);
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        auto* list = window.findChild<QTreeWidget*>(QStringLiteral("pluginsList"));
        QCOMPARE(list->topLevelItemCount(), 1);
        list->setCurrentItem(list->topLevelItem(0));
        QVERIFY(window.findChild<QPushButton*>(QStringLiteral("pluginEnable"))->isEnabled());
        QVERIFY(!window.findChild<QPushButton*>(QStringLiteral("pluginRemove"))->isEnabled());
        QFile report(QDir(paths.profile).filePath(QStringLiteral("plugin-load-report.json")));
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QByteArray(256 * 1024 + 1, 'x'));
        report.close();
        controller.refreshInstalled();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(!controller.state().warnings.isEmpty());
        QVERIFY(controller.state().packageStatus.value(QStringLiteral("mp3")).loadError.isEmpty());
    }
    void catalogActionsExplainInstallationCompatibilityAndOfflineStates() {
        // Presentation-only state fixture; no repository, download or activation is simulated.
        QJsonObject entry{{QStringLiteral("id"), QStringLiteral("org.example.parity")},
                          {QStringLiteral("version"), QStringLiteral("1.1.0")},
                          {QStringLiteral("platform"), QStringLiteral("win32-x64")},
                          {QStringLiteral("host_api"), 2},
                          {QStringLiteral("description"), QStringLiteral("Description visible")},
                          {QStringLiteral("resource_tier"), QStringLiteral("light")}};
        nodivu::StoreState state;
        state.online = true;
        auto presentation = nodivu::catalogPresentation(entry, state);
        QCOMPARE(presentation.action, QStringLiteral("Instalar"));
        QVERIFY(presentation.reason.isEmpty());
        QVERIFY(presentation.detail.contains(QStringLiteral("Description visible")));
        QVERIFY(presentation.detail.contains(QStringLiteral("Leve")));
        nodivu::PackageRecord package;
        package.id = QStringLiteral("org.example.parity");
        package.key = QStringLiteral("parity");
        package.version = QStringLiteral("1.0.0");
        state.installed.append(package);
        presentation = nodivu::catalogPresentation(entry, state);
        QVERIFY(presentation.updateAvailable);
        QCOMPARE(presentation.action, QStringLiteral("Atualizar para 1.1.0"));
        state.packageStatus[package.key].pendingVersion = QStringLiteral("1.1.0");
        presentation = nodivu::catalogPresentation(entry, state);
        QCOMPARE(presentation.action, QStringLiteral("Agendado"));
        QVERIFY(!presentation.reason.isEmpty());
        state.packageStatus.clear();
        state.installed[0].version = QStringLiteral("1.1.0");
        presentation = nodivu::catalogPresentation(entry, state);
        QCOMPARE(presentation.action, QStringLiteral("Instalado"));
        QVERIFY(!presentation.updateAvailable && !presentation.reason.isEmpty());
        state.installed.clear();
        state.online = false;
        QVERIFY(!nodivu::catalogPresentation(entry, state).reason.isEmpty());
        state.online = true;
        entry.insert(QStringLiteral("platform"), QStringLiteral("linux-x64"));
        QVERIFY(
            nodivu::catalogPresentation(entry, state).reason.contains(QStringLiteral("linux-x64")));
        entry.insert(QStringLiteral("platform"), QStringLiteral("win32-x64"));
        entry.insert(QStringLiteral("host_api"), 3);
        QVERIFY(nodivu::catalogPresentation(entry, state)
                    .reason.contains(QStringLiteral("contrato 3")));
    }
    void readmeNeverLoadsRemoteFileSvgOrTraversalResources() {
        QTemporaryDir directory;
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(QDir(directory.path()).filePath(QStringLiteral("logo.png"))));
        nodivu::ReadmeView view;
        nodivu::ReadmeContent content;
        content.directory = directory.path();
        content.markdown = QStringLiteral(
            "# Plugin\n\nLocal **documentation**\n\n![logo](logo.png)\n<script>alert(1)</script>");
        view.showContent(content);
        QVERIFY(view.document()->toPlainText().contains(QStringLiteral("documentation")));
        QVERIFY(!view.document()
                     ->resource(QTextDocument::ImageResource, QUrl(QStringLiteral("logo.png")))
                     .value<QImage>()
                     .isNull());
        for (const auto& name :
             {QStringLiteral("https://example.com/logo.png"),
              QStringLiteral("file:///C:/Windows/logo.png"), QStringLiteral("../logo.png"),
              QStringLiteral("logo.svg"), QStringLiteral("data:image/png;base64,AAAA")})
            QVERIFY(view.document()
                        ->resource(QTextDocument::ImageResource, QUrl(name))
                        .value<QImage>()
                        .isNull());
    }
    void localStoreAndSecondInstanceStayIsolated() {
        QTemporaryDir directory;
        auto paths = nodivu::AppPaths::development(directory.path());
        nodivu::StoreController controller(paths);
        nodivu::StoreWindow window(controller);
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(!controller.state().online);
        QVERIFY(!controller.state().error.isEmpty());
        nodivu::SingleInstance first(directory.path()), second(directory.path());
        QVERIFY(first.acquire());
        QVERIFY(!second.acquire());
        QVERIFY(second.error().isEmpty());
        window.close();
    }
};
QTEST_MAIN(StoreUiTests)
#include "store_ui_tests.moc"
