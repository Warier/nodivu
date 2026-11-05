// Maintainer diagnostic: authenticates the real public channel, optionally downloads;
// never launches the installer and never touches an installed user's profile.
#include "features/updates/update_service.hpp"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtConcurrentRun>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption(
        {QStringLiteral("source"), QStringLiteral("Bootstrap público Qt"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("current"), QStringLiteral("Versão instalada simulada"),
                      QStringLiteral("version"), QStringLiteral("0.2.1")});
    parser.addOption(
        {QStringLiteral("download"), QStringLiteral("Baixar e verificar sem instalar")});
    parser.process(app);
    QFile source(parser.value(QStringLiteral("source")));
    QTemporaryDir temporary;
    if (!source.open(QIODevice::ReadOnly) || !temporary.isValid() || source.size() > 1024 * 1024)
        return 2;
    const auto bootstrap = source.readAll();
    const auto current = parser.value(QStringLiteral("current"));
    const bool download = parser.isSet(QStringLiteral("download"));
    QFutureWatcher<nodivu::UpdateOffer> watcher;
    QObject::connect(&watcher, &QFutureWatcher<nodivu::UpdateOffer>::finished, &app, [&] {
        const auto offer = watcher.result();
        QTextStream(stdout) << QJsonDocument(
                                   QJsonObject{{QStringLiteral("available"), offer.available},
                                               {QStringLiteral("version"), offer.version},
                                               {QStringLiteral("bytes"), offer.bytes},
                                               {QStringLiteral("sha256"), offer.sha256},
                                               {QStringLiteral("download_verified"),
                                                !offer.installer.isEmpty()},
                                               {QStringLiteral("error"), offer.error}})
                                   .toJson();
        app.exit(offer.error.isEmpty() ? 0 : 1);
    });
    watcher.setFuture(QtConcurrent::run([&, bootstrap, current, download] {
        nodivu::UpdateService updates(temporary.path(), bootstrap, current,
                                      QStringLiteral("stable"));
        auto offer = updates.check(nullptr);
        if (offer.error.isEmpty() && offer.available && download)
            offer = updates.download(nullptr);
        return offer;
    }));
    return app.exec();
}
