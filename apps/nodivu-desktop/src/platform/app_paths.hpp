#pragma once

#include <QProcessEnvironment>
#include <QString>

namespace nodivu {

struct AppPaths {
    QString backend;
    QString profile;
    QString plugins;
    QString projects;
    QString storeSource;
    QString bundledPlugins;
    QString updatesSource;
    QString cableInstaller;

    static AppPaths development(const QString& repository);
    static AppPaths installed();
    QProcessEnvironment backendEnvironment() const;
};

} // namespace nodivu
