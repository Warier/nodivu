#include "platform/install_transaction.hpp"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QTextStream>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("NodivuInstallTool"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("target"), QStringLiteral("Instalação de destino."),
                      QStringLiteral("path")});
    parser.addOption({QStringLiteral("payload"),
                      QStringLiteral("Payload extraído pelo instalador."), QStringLiteral("path")});
    parser.addOption(
        {QStringLiteral("recover"), QStringLiteral("Restaurar transação interrompida.")});
    parser.process(app);
    if (!parser.isSet(QStringLiteral("target")) ||
        !QDir::isAbsolutePath(parser.value(QStringLiteral("target"))))
        return 2;
    nodivu::InstallTransaction transaction(parser.value(QStringLiteral("target")));
    QString error;
    if (parser.isSet(QStringLiteral("recover"))) {
        error = transaction.recover();
    } else {
        if (!parser.isSet(QStringLiteral("payload")))
            return 2;
        const auto result = transaction.apply(
            parser.value(QStringLiteral("payload")), [](const QString& installed) {
                QProcess probe;
                probe.setWorkingDirectory(installed);
                probe.start(QDir(installed).filePath(QStringLiteral("nodivu.exe")),
                            {QStringLiteral("--installation-check")});
                if (!probe.waitForStarted(5000))
                    return QStringLiteral("A nova interface não iniciou: ") + probe.errorString();
                if (!probe.waitForFinished(25000)) {
                    probe.kill(); // Only the validation child created above, never a user session.
                    probe.waitForFinished(5000);
                    return QStringLiteral("A validação da nova instalação excedeu o tempo limite.");
                }
                return probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0
                           ? QString()
                           : QStringLiteral(
                                 "A nova interface/backend não passou na validação (%1).")
                                 .arg(probe.exitCode());
            });
        error = result.error;
        if (error.isEmpty() && !result.backup.isEmpty())
            QTextStream(stdout) << QStringLiteral("Versão anterior preservada em: ")
                                << result.backup << QLatin1Char('\n');
    }
    if (!error.isEmpty())
        QTextStream(stderr) << error << QLatin1Char('\n');
    return error.isEmpty() ? 0 : 1;
}
