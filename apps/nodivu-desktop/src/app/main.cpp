#include "app/smoke_scenario.hpp"
#include "features/workspace/main_window.hpp"
#include "platform/app_paths.hpp"
#include "platform/diagnostic_log.hpp"
#include "platform/single_instance.hpp"
#include "ui/theme/theme.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QProcess>
#include <QTemporaryDir>
#ifdef Q_OS_WIN
#include "platform/windows/native_desktop.hpp"
#endif

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    nodivu::windows::initializeDesktopIdentity();
#endif
    QElapsedTimer startup;
    startup.start();
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Nodivu"));
    QCoreApplication::setApplicationVersion(QStringLiteral(NODIVU_APP_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Nodivu — Estúdio de áudio Qt/C++"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("installation-check"),
                      QStringLiteral("Validar instalação sem áudio e sem abrir janela.")});
    parser.addOption({QStringLiteral("portable"),
                      QStringLiteral("Usar perfil separado na pasta deste executável.")});
    parser.addOption({QStringLiteral("benchmark-ms"),
                      QStringLiteral("Permanência do ensaio nativo, de 0 a 3600000 ms."),
                      QStringLiteral("milliseconds"), QStringLiteral("0")});
    parser.addOption({QStringLiteral("smoke-test"),
                      QStringLiteral("Ensaio nativo sem áudio em perfil temporário.")});
    parser.addOption({QStringLiteral("screenshot"), QStringLiteral("PNG do ensaio nativo."),
                      QStringLiteral("path")});
    parser.addOption({QStringLiteral("repository"), QStringLiteral("Checkout de desenvolvimento."),
                      QStringLiteral("path")});
    parser.process(application);
    const bool installationCheck = parser.isSet(QStringLiteral("installation-check"));
    const bool smoke = installationCheck || parser.isSet(QStringLiteral("smoke-test"));
    if (parser.isSet(QStringLiteral("portable")) && parser.isSet(QStringLiteral("repository")))
        parser.showHelp(2);
    bool benchmarkValid = false;
    const int benchmarkMilliseconds =
        parser.value(QStringLiteral("benchmark-ms")).toInt(&benchmarkValid);
    if (!benchmarkValid || benchmarkMilliseconds < 0 || benchmarkMilliseconds > 3600000 ||
        (benchmarkMilliseconds > 0 && !smoke))
        parser.showHelp(2);
    nodivu::Theme::apply(application);
    auto paths = parser.isSet(QStringLiteral("repository"))
                     ? nodivu::AppPaths::development(parser.value(QStringLiteral("repository")))
                     : nodivu::AppPaths::installed();
    if (parser.isSet(QStringLiteral("portable"))) {
        paths.profile = QDir(QCoreApplication::applicationDirPath())
                            .filePath(QStringLiteral("portable-profile"));
        paths.plugins = QDir(paths.profile).filePath(QStringLiteral("plugins"));
    }
    QTemporaryDir smokeProfile;
    if (smoke) {
        if (!smokeProfile.isValid())
            return 2;
        paths.profile = smokeProfile.path();
        paths.plugins = QDir(paths.profile).filePath(QStringLiteral("plugins"));
    } else if (parser.isSet(QStringLiteral("screenshot"))) {
        parser.showHelp(2);
    }
#ifdef Q_OS_WIN
    if (!smoke && !parser.isSet(QStringLiteral("portable")) &&
        !parser.isSet(QStringLiteral("repository"))) {
        const auto conflict = nodivu::windows::legacyProfileConflict();
        if (!conflict.isEmpty()) {
            QMessageBox::warning(nullptr, QStringLiteral("Nodivu"), conflict);
            return 1;
        }
    }
#endif
    nodivu::SingleInstance instance(paths.profile);
    if (!instance.acquire()) {
        if (!instance.error().isEmpty())
            QMessageBox::warning(nullptr, QStringLiteral("Nodivu"), instance.error());
        return instance.error().isEmpty() ? 0 : 1;
    }
    nodivu::DiagnosticLog log(paths.profile);
    log.record(QStringLiteral("application"),
               QStringLiteral("Inicialização Qt/C++ ") + QCoreApplication::applicationVersion());
    nodivu::MainWindow window(paths);
    QObject::connect(
        &window.session(), &nodivu::SessionController::changed, &window, [&window, &log, startup] {
            if (window.session().online() && !window.property("readyLogged").toBool()) {
                window.setProperty("readyLogged", true);
                log.record(QStringLiteral("application.ready"),
                           QString::number(startup.elapsed()) + QStringLiteral(" ms"));
            }
        });
    QObject::connect(
        &window.session(), &nodivu::SessionController::error, &window,
        [&log](const QString& error) { log.record(QStringLiteral("backend"), error); });
    QObject::connect(&instance, &nodivu::SingleInstance::activated, &window, [&window] {
        window.setWindowState(window.windowState() & ~Qt::WindowMinimized);
        window.show();
        window.raise();
        window.activateWindow();
    });
    if (installationCheck)
        window.setAttribute(Qt::WA_DontShowOnScreen);
    if (smoke)
        window.show();
    else
        window.showMaximized();
#ifdef Q_OS_WIN
    nodivu::windows::applyWindowTheme(window.winId());
#endif
    if (smoke)
        nodivu::startNativeSmoke(window,
                                 QDir(paths.profile).filePath(QStringLiteral("smoke.nodivu.json")),
                                 parser.value(QStringLiteral("screenshot")), benchmarkMilliseconds);
    const int result = application.exec();
    if (!window.installer().isEmpty()) {
        const auto helper = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("nodivu-update-helper.exe"));
        if (!QProcess::startDetached(helper, {QString::number(QCoreApplication::applicationPid()),
                                              window.installer(), window.installerHash()})) {
            log.record(QStringLiteral("update"),
                       QStringLiteral("Não foi possível iniciar o auxiliar de atualização."));
            QMessageBox::warning(
                nullptr, QStringLiteral("Atualização"),
                QStringLiteral(
                    "Não foi possível abrir o instalador. A versão atual permanece instalada."));
            return 1;
        }
    }
    return result;
}
