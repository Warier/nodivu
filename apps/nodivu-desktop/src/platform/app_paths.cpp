#include "platform/app_paths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace nodivu {

AppPaths AppPaths::development(const QString& repository) {
    const QDir root(repository);
    return {
        root.absoluteFilePath(QStringLiteral("target/release/nodivu-app-backend.exe")),
        root.absoluteFilePath(QStringLiteral(".local/qt-profile")),
        root.absoluteFilePath(QStringLiteral(".local/qt-profile/plugins")),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(QStringLiteral("Nodivu/Projetos")),
        root.absoluteFilePath(QStringLiteral("apps/nodivu-desktop/resources/store/source.json")),
        root.absoluteFilePath(QStringLiteral(".local/plugins")),
        root.absoluteFilePath(QStringLiteral("apps/nodivu-desktop/resources/updates/source.json")),
        root.absoluteFilePath(QStringLiteral("third-party/vb-cable/VBCABLE_Setup_x64.exe"))};
}

AppPaths AppPaths::installed() {
    // AppDataLocation is roaming on Windows. GenericDataLocation is LocalAppData
    // and would silently split the existing Electron profile.
    const auto profile = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return {QDir(QCoreApplication::applicationDirPath())
                .absoluteFilePath(QStringLiteral("bin/nodivu-app-backend.exe")),
            profile,
            QDir(profile).filePath(QStringLiteral("plugins")),
            QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                .filePath(QStringLiteral("Nodivu/Projetos")),
            QDir(QCoreApplication::applicationDirPath())
                .filePath(QStringLiteral("resources/store/source.json")),
            QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins")),
            QDir(QCoreApplication::applicationDirPath())
                .filePath(QStringLiteral("resources/updates/source.json")),
            QDir(QCoreApplication::applicationDirPath())
                .filePath(QStringLiteral("resources/vb-cable/VBCABLE_Setup_x64.exe"))};
}

QProcessEnvironment AppPaths::backendEnvironment() const {
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("NODIVU_APP_ROOT_PID"),
                       QString::number(QCoreApplication::applicationPid()));
    environment.insert(QStringLiteral("NODIVU_SCAN_PLUGINS"), QStringLiteral("1"));
    environment.insert(QStringLiteral("NODIVU_PLUGIN_REPORT"),
                       QDir(profile).filePath(QStringLiteral("plugin-load-report.json")));
    environment.insert(QStringLiteral("NODIVU_PLUGIN_MANIFEST"),
                       QDir(plugins).filePath(QStringLiteral("plugin.json")));
    return environment;
}

} // namespace nodivu
