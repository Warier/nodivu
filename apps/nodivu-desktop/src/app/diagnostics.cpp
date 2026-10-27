#include "backend/backend_client.hpp"
#include "features/store/store_service.hpp"
#include "features/workspace/session_controller.hpp"
#include "platform/app_paths.hpp"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtConcurrentRun>
#include <functional>

namespace {
bool until(const std::function<bool()>& condition, int timeout = 10000) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < timeout) {
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return condition();
}
QString storeProbe(const nodivu::AppPaths& paths) {
    QFile source(paths.storeSource);
    if (!source.open(QIODevice::ReadOnly) || source.size() > 1024 * 1024)
        return QStringLiteral("Configuração da loja ausente.");
    nodivu::StoreService store(paths.profile, paths.plugins, source.readAll());
    auto state = store.initialize();
    if (!state.error.isEmpty())
        return state.error;
    nodivu::TransferProgress progress;
    state = store.refresh(&progress);
    if (!state.error.isEmpty() || !state.online || state.catalog.isEmpty())
        return state.error.isEmpty() ? QStringLiteral("Catálogo público vazio ou offline.")
                                     : state.error;
    for (const auto& value : state.catalog) {
        nodivu::TransferProgress installProgress;
        const auto entry = value.toObject();
        const auto id = entry.value(QStringLiteral("id")).toString();
        const auto readme = store.details(id, &installProgress);
        if (!readme.error.isEmpty() || readme.markdown.isEmpty())
            return readme.error.isEmpty() ? QStringLiteral("README vazio.") : readme.error;
        state =
            store.install(id, entry.value(QStringLiteral("version")).toString(), &installProgress);
        if (!state.error.isEmpty())
            return state.error;
    }
    return state.installed.size() == state.catalog.size()
               ? QString()
               : QStringLiteral("Instalação isolada incompleta.");
}
QString audioProbe(nodivu::SessionController& session, const QString& endpoint) {
    bool found = false;
    for (const auto& value : session.devices()) {
        const auto device = value.toObject();
        found =
            found || (device.value(QStringLiteral("endpoint_id")).toString() == endpoint &&
                      device.value(QStringLiteral("flow")).toString() == QStringLiteral("render") &&
                      device.value(QStringLiteral("state")).toString() == QStringLiteral("active"));
    }
    if (!found || endpoint.isEmpty())
        return QStringLiteral(
            "Forneça o ID opaco de uma saída ativa; nenhuma saída será escolhida automaticamente.");
    const auto nodes = [&] {
        return session.snapshot()
            .value(QStringLiteral("graph"))
            .toObject()
            .value(QStringLiteral("nodes"))
            .toArray();
    };
    for (const auto& kind :
         {QStringLiteral("tone"), QStringLiteral("gain"), QStringLiteral("output")}) {
        const auto previous = nodes().size();
        session.addNode(kind);
        if (!until([&] { return nodes().size() == previous + 1 && session.settled(); }))
            return QStringLiteral("Falha ao adicionar nó de ensaio.");
    }
    const auto tone = nodes()[0].toObject().value(QStringLiteral("id")).toString();
    const auto gain = nodes()[1].toObject().value(QStringLiteral("id")).toString();
    const auto output = nodes()[2].toObject().value(QStringLiteral("id")).toString();
    session.connectNodes(tone, 0, gain, 0);
    if (!until([&] { return session.settled(); }))
        return QStringLiteral("Falha ao conectar fonte.");
    session.connectNodes(gain, 0, output, 0);
    if (!until([&] { return session.settled(); }))
        return QStringLiteral("Falha ao conectar saída.");
    session.updateField(output, QStringLiteral("endpoint_id"), endpoint);
    const auto metrics = [&] {
        return session.snapshot().value(QStringLiteral("metrics")).toObject();
    };
    if (!until([&] {
            return metrics().value(QStringLiteral("rendered_frames")).toInteger() > 10000 &&
                   metrics().value(QStringLiteral("output_peak")).toDouble() > 0.01;
        }))
        return QStringLiteral("WASAPI não confirmou frames e pico de saída.");
    const double peak = metrics().value(QStringLiteral("output_peak")).toDouble();
    session.updateField(gain, QStringLiteral("gain_db"), -24.0);
    if (!until([&] {
            return session.settled() &&
                   metrics().value(QStringLiteral("output_peak")).toDouble() < peak / 10;
        }))
        return QStringLiteral("O ganho não atenuou a saída esperada.");
    session.updateField(gain, QStringLiteral("bypass"), true);
    if (!until([&] {
            return session.settled() &&
                   metrics().value(QStringLiteral("output_peak")).toDouble() > peak * 0.8;
        }))
        return QStringLiteral("Bypass não restaurou a amplitude.");
    bool muted = false;
    session.command(QStringLiteral("audio.mute"), {{QStringLiteral("muted"), true}},
                    [&muted](const nodivu::BackendReply& reply) { muted = reply.ok; });
    if (!until([&] {
            return muted && metrics().value(QStringLiteral("output_peak")).toDouble() == 0;
        }))
        return QStringLiteral("Mute não silenciou o motor.");
    // Keep the existing audio clock, replace the audible source with a silent
    // local WAV, and exercise the same transport commands as the Qt controls.
    QTemporaryDir media;
    if (!media.isValid())
        return QStringLiteral("Não foi possível criar fixture de áudio temporária.");
    const auto file = QDir(media.path()).filePath(QStringLiteral("player fixture.wav"));
    QFile outputFile(file);
    if (!outputFile.open(QIODevice::WriteOnly))
        return QStringLiteral("Não foi possível gravar fixture WAV.");
    QDataStream wav(&outputFile);
    wav.setByteOrder(QDataStream::LittleEndian);
    const QByteArray silence(192000, '\0');
    wav.writeRawData("RIFF", 4);
    wav << quint32(36 + silence.size());
    wav.writeRawData("WAVEfmt ", 8);
    wav << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000) << quint16(2)
        << quint16(16);
    wav.writeRawData("data", 4);
    wav << quint32(silence.size());
    wav.writeRawData(silence.constData(), silence.size());
    if (wav.status() != QDataStream::Ok)
        return QStringLiteral("Fixture WAV incompleta.");
    outputFile.close();
    session.addNode(QStringLiteral("plugin"), QStringLiteral("org.nodivu.mp3-player"));
    if (!until([&] { return nodes().size() == 4 && session.settled(); }))
        return QStringLiteral("Player não foi adicionado.");
    const auto player = nodes()[3].toObject().value(QStringLiteral("id")).toString();
    session.disconnectEdge({{QStringLiteral("from"), gain},
                            {QStringLiteral("from_port"), 0},
                            {QStringLiteral("to"), output},
                            {QStringLiteral("to_port"), 0}});
    if (!until([&] { return session.settled(); }))
        return QStringLiteral("Rota de ensaio não foi liberada.");
    session.connectNodes(player, 0, output, 0);
    if (!until([&] { return session.settled(); }))
        return QStringLiteral("Player não foi conectado à saída explícita.");
    const auto command = [&](const QString& action, QJsonObject extra = {}) {
        extra.insert(QStringLiteral("node_id"), player);
        extra.insert(QStringLiteral("action"), action);
        bool completed = false;
        bool accepted = false;
        session.command(QStringLiteral("plugin.command"), extra,
                        [&](const nodivu::BackendReply& reply) {
                            accepted = reply.ok;
                            completed = true;
                        });
        return until([&] { return completed; }) && accepted;
    };
    const auto runtime = [&] {
        return session.snapshot()
            .value(QStringLiteral("plugin_states"))
            .toObject()
            .value(player)
            .toObject();
    };
    if (!command(QStringLiteral("load"), {{QStringLiteral("path"), file}}) ||
        !until([&] { return runtime().value(QStringLiteral("status")).toInt() == 2; }))
        return QStringLiteral("O WAV não ficou pronto no player.");
    if (!command(QStringLiteral("seek"), {{QStringLiteral("position_ms"), 500}}) ||
        !until([&] { return runtime().value(QStringLiteral("position_ms")).toInt() == 500; }))
        return QStringLiteral("Seek não foi confirmado pelo processamento de áudio.");
    if (!command(QStringLiteral("play")) || !until([&] {
            return runtime().value(QStringLiteral("status")).toInt() == 3 &&
                   runtime().value(QStringLiteral("position_ms")).toInt() > 750;
        }))
        return QStringLiteral("Reprodução não avançou no player.");
    if (!command(QStringLiteral("pause")) ||
        !until([&] { return runtime().value(QStringLiteral("status")).toInt() == 5; }))
        return QStringLiteral("Pausa não foi confirmada pelo player.");
    QTextStream(stdout) << QStringLiteral(
        "WASAPI, ganho, bypass, mute, WAV, seek, play e pause: PASS\n");
    return {};
}
QString installedPluginProbe(nodivu::SessionController& session, const nodivu::AppPaths& paths) {
    const auto installed = nodivu::PackageService(paths.plugins).installed();
    if (installed.isEmpty() || installed.size() > 8)
        return QStringLiteral(
            "O ensaio exige entre um e oito pacotes instalados no perfil temporário.");
    for (const auto& package : installed) {
        QString nodeId;
        const auto connection =
            QObject::connect(&session, &nodivu::SessionController::nodeAdded, &session,
                             [&nodeId](const QString& id) { nodeId = id; });
        session.addNode(QStringLiteral("plugin"), package.id);
        const bool added = until([&] { return !nodeId.isEmpty() && session.settled(); });
        QObject::disconnect(connection);
        if (!added)
            return QStringLiteral("O backend não adicionou o plugin da loja: ") + package.id;
        const auto descriptor = session.catalog().describe(session.node(nodeId));
        if (descriptor.missing || descriptor.controls.isEmpty())
            return QStringLiteral("Descritor declarativo indisponível: ") + package.id;
        for (const auto& control : descriptor.controls) {
            if (control.kind != nodivu::ControlKind::Momentary)
                continue;
            for (const bool pressed : {true, false}) {
                bool completed = false;
                bool accepted = false;
                session.command(QStringLiteral("plugin.gesture"),
                                {{QStringLiteral("node_id"), nodeId},
                                 {QStringLiteral("parameter"), control.key.mid(10).toLongLong()},
                                 {QStringLiteral("pressed"), pressed}},
                                [&](const nodivu::BackendReply& reply) {
                                    accepted = reply.ok;
                                    completed = true;
                                });
                if (!until([&] { return completed; }) || !accepted)
                    return QStringLiteral("Gesto transitório rejeitado: ") + package.id;
            }
        }
        QTextStream(stdout) << package.id << QStringLiteral(" · portas ") << descriptor.inputs
                            << QLatin1Char('/') << descriptor.outputs
                            << QStringLiteral(" · controles ") << descriptor.controls.size()
                            << QLatin1Char('\n');
    }
    return {};
}
} // namespace
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("NodivuDiagnostics"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("repository"), QStringLiteral("Checkout de desenvolvimento."),
                      QStringLiteral("path")});
    parser.addOption(
        {QStringLiteral("store"),
         QStringLiteral("Verificar loja HTTPS real e instalar somente em perfil temporário.")});
    parser.addOption({QStringLiteral("devices"),
                      QStringLiteral("Consultar IDs de dispositivos sem alterar áudio.")});
    parser.addOption(
        {QStringLiteral("audio"),
         QStringLiteral(
             "Emitir tom e verificar WASAPI, ganho, bypass e mute na saída explícita.")});
    parser.addOption({QStringLiteral("output-id"),
                      QStringLiteral("ID opaco da saída para ensaio de áudio."),
                      QStringLiteral("id")});
    parser.process(app);
    QTemporaryDir profile;
    if (!profile.isValid())
        return 2;
    auto paths = parser.isSet(QStringLiteral("repository"))
                     ? nodivu::AppPaths::development(parser.value(QStringLiteral("repository")))
                     : nodivu::AppPaths::installed();
    paths.profile = profile.path();
    paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
    QString error;
    if (parser.isSet(QStringLiteral("store"))) {
        paths.plugins = QDir(profile.path()).filePath(QStringLiteral("plugins"));
        QFutureWatcher<QString> watcher;
        QEventLoop loop;
        QObject::connect(&watcher, &QFutureWatcher<QString>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(QtConcurrent::run([paths] { return storeProbe(paths); }));
        loop.exec();
        error = watcher.result();
    }
    if (error.isEmpty() &&
        (parser.isSet(QStringLiteral("store")) || parser.isSet(QStringLiteral("devices")) ||
         parser.isSet(QStringLiteral("audio")))) {
        nodivu::PackageService packages(paths.plugins);
        const auto prepared = parser.isSet(QStringLiteral("store"))
                                  ? nodivu::PackageResult{}
                                  : packages.prepareBundled(paths.bundledPlugins);
        if (!prepared.ok()) {
            QTextStream(stderr) << prepared.error << QLatin1Char('\n');
            return 1;
        }
        nodivu::BackendClient backend;
        nodivu::SessionController session(backend);
        QString backendError;
        QObject::connect(&session, &nodivu::SessionController::error, &app,
                         [&backendError](const QString& value) { backendError = value; });
        backend.start(paths.backend, paths.backendEnvironment());
        if (!until([&] { return session.online() || !backendError.isEmpty(); }))
            error = QStringLiteral("Inicialização sem resposta.");
        else if (!backendError.isEmpty())
            error = backendError;
        else if (parser.isSet(QStringLiteral("store")))
            error = installedPluginProbe(session, paths);
        else if (parser.isSet(QStringLiteral("devices"))) {
            until([&] { return !session.devices().isEmpty() || !backendError.isEmpty(); }, 5000);
            QTextStream(stdout) << QJsonDocument(session.devices()).toJson();
        } else {
            until([&] { return !session.devices().isEmpty() || !backendError.isEmpty(); }, 5000);
            error = audioProbe(session, parser.value(QStringLiteral("output-id")));
        }
        bool stopped = false;
        QObject::connect(&backend, &nodivu::BackendClient::stopped, &app,
                         [&stopped] { stopped = true; });
        session.beginShutdown();
        backend.shutdown();
        if (!until([&] { return stopped; }, 5000) && error.isEmpty())
            error = QStringLiteral("Motor não encerrou no limite.");
    } else if (error.isEmpty()) {
        parser.showHelp(2);
    }
    QTextStream(error.isEmpty() ? stdout : stderr)
        << (error.isEmpty() ? QStringLiteral("PASS\n")
                            : QStringLiteral("FAIL: ") + error + QLatin1Char('\n'));
    return error.isEmpty() ? 0 : 1;
}
