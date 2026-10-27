// Explicit hardware gate; deliberately absent from CTest's automatic suite.
#include "features/inspector/inspector_panel.hpp"
#include "features/projects/project_controller.hpp"
#include "features/workspace/main_window.hpp"
#include "ui/theme/theme.hpp"
#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTextStream>
#include <functional>
#include <memory>
#include <stdexcept>
// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on

namespace {
void require(bool accepted, const char* message) {
    if (!accepted)
        throw std::runtime_error(message);
}
bool wait(const std::function<bool()>& ready, int timeout = 10000) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < timeout) {
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return ready();
}
void settle(int milliseconds) {
    QElapsedTimer elapsed;
    elapsed.start();
    wait([&] { return elapsed.elapsed() >= milliseconds; }, milliseconds + 500);
}
qint64 ownedBackend() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    require(snapshot != INVALID_HANDLE_VALUE, "process snapshot failed");
    const auto close = qScopeGuard([snapshot] { CloseHandle(snapshot); });
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL found = Process32FirstW(snapshot, &entry); found;
         found = Process32NextW(snapshot, &entry))
        if (entry.th32ParentProcessID == GetCurrentProcessId() &&
            _wcsicmp(entry.szExeFile, L"nodivu-app-backend.exe") == 0)
            return entry.th32ProcessID;
    return 0;
}
QJsonObject line(QProcess& process, int timeout = 7000) {
    require(wait([&] { return process.canReadLine() || process.state() == QProcess::NotRunning; },
                 timeout),
            "audio actor timed out");
    const auto bytes = process.readLine();
    if (bytes.isEmpty())
        QTextStream(stderr) << process.readAllStandardError();
    const auto result = QJsonDocument::fromJson(bytes).object();
    require(!result.isEmpty(), "audio actor did not return JSON");
    return result;
}
QJsonObject invoke(nodivu::SessionController& session, const QString& command,
                   const QJsonObject& params, bool expectedSuccess = true) {
    struct Pending {
        bool done = false;
        nodivu::BackendReply reply;
    };
    auto pending = std::make_shared<Pending>();
    session.command(command, params, [pending](const nodivu::BackendReply& reply) {
        pending->reply = reply;
        pending->done = true;
    });
    require(wait([pending] { return pending->done; }), "frontend command timed out");
    if (!pending->reply.ok)
        QTextStream(stderr) << pending->reply.message << Qt::endl;
    require(pending->reply.ok == expectedSuccess, "unexpected frontend command result");
    if (!expectedSuccess)
        return {{QStringLiteral("rejected"), true},
                {QStringLiteral("message"), pending->reply.message}};
    return pending->reply.result;
}
QJsonObject sample(QProcess& receiver) {
    require(receiver.write("sample\n") == 7, "receiver write failed");
    const auto record = line(receiver);
    require(record.value(QStringLiteral("frames")).toInteger() >= 24000,
            "insufficient captured frames");
    require(record.value(QStringLiteral("nonfinite")).toInteger() == 0, "nonfinite received PCM");
    return record;
}
void run(const QString& root, const QString& endpoint, QJsonObject& evidence) {
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary profile failed");
    auto paths = nodivu::AppPaths::development(root);
    paths.profile = temporary.path();
    paths.plugins = QDir(temporary.path()).filePath(QStringLiteral("plugins"));
    paths.storeSource.clear();
    nodivu::MainWindow window(paths);
    window.show();
    auto& session = window.session();
    const auto preserveSnapshot =
        qScopeGuard([&] { evidence.insert(QStringLiteral("final_snapshot"), session.snapshot()); });
    require(wait([&] { return session.settled() && !session.devices().isEmpty(); }),
            "backend/device discovery failed");
    bool found = false;
    for (const auto& item : session.devices()) {
        const auto device = item.toObject();
        found |= device.value(QStringLiteral("endpoint_id")).toString() == endpoint &&
                 device.value(QStringLiteral("flow")).toString() == QStringLiteral("render") &&
                 device.value(QStringLiteral("state")).toString() == QStringLiteral("active");
    }
    require(found, "supply an explicit active render endpoint ID");
    const auto actor = QDir(QCoreApplication::applicationDirPath())
                           .filePath(QStringLiteral("windows_audio_actor.exe"));
    const auto emitterA =
        QDir(temporary.path()).filePath(QStringLiteral("Nodivu acceptance A.exe"));
    const auto emitterB =
        QDir(temporary.path()).filePath(QStringLiteral("Nodivu acceptance B.exe"));
    require(QFile::copy(actor, emitterA) && QFile::copy(actor, emitterB),
            "emitter fixture copy failed");
    QProcess first, second, receiver;
    const auto stop = qScopeGuard([&] {
        for (auto* child : {&receiver, &first, &second})
            if (child->state() != QProcess::NotRunning) {
                child->kill();
                child->waitForFinished(3000);
            }
    });
    first.start(emitterA, {QStringLiteral("--emit"), endpoint, QStringLiteral("997")});
    require(first.waitForStarted(3000) && line(first).value(QStringLiteral("ready")).toBool(),
            "emitter A failed");
    second.start(emitterB, {QStringLiteral("--emit"), endpoint, QStringLiteral("1733")});
    require(second.waitForStarted(3000) && line(second).value(QStringLiteral("ready")).toBool(),
            "emitter B failed");
    const auto add = [&](const QString& kind, const QString& plugin = QString()) {
        QString added;
        const auto connection =
            QObject::connect(&session, &nodivu::SessionController::nodeAdded, &window,
                             [&added](const QString& id) { added = id; });
        session.addNode(kind, plugin);
        const bool accepted = wait([&] { return !added.isEmpty() && session.settled(); });
        QObject::disconnect(connection);
        require(accepted, "adding an audio node failed");
        return added;
    };
    const auto a = add(QStringLiteral("plugin"), QStringLiteral("org.nodivu.windows-audio"));
    const auto b = add(QStringLiteral("plugin"), QStringLiteral("org.nodivu.windows-audio"));
    const auto mixer = add(QStringLiteral("mixer"));
    const auto gain = add(QStringLiteral("gain"));
    const auto output = add(QStringLiteral("output"));
    const auto wire = [&](const QString& from, const QString& to, int port = 0) {
        session.connectNodes(from, 0, to, port);
        require(wait([&] { return session.settled(); }), "routing failed");
    };
    wire(a, mixer, 0);
    wire(b, mixer, 1);
    wire(mixer, gain);
    wire(gain, output);
    session.updateField(output, QStringLiteral("endpoint_id"), endpoint);
    require(wait([&] { return session.settled(); }), "selecting output failed");
    evidence.insert(QStringLiteral("same_output_monitor"),
                    invoke(session, QStringLiteral("audio.monitor"),
                           {{QStringLiteral("endpoint_id"), endpoint}}, false));
    auto* inspector = window.findChild<nodivu::InspectorPanel*>();
    require(inspector, "inspector missing");
    const auto selectCapture = [&](const QString& node, qint64 pid) {
        inspector->inspect(node);
        QJsonObject target;
        invoke(session, QStringLiteral("capture.targets"), {{QStringLiteral("refresh"), true}});
        require(wait([&] {
                    const auto reply = invoke(session, QStringLiteral("capture.targets"), {});
                    if (reply.value(QStringLiteral("refreshing")).toBool())
                        return false;
                    for (const auto& value : reply.value(QStringLiteral("targets")).toArray())
                        if (value.toObject().value(QStringLiteral("process_id")).toInteger() == pid)
                            target = value.toObject();
                    return !target.isEmpty();
                }),
                "emitter not discovered by capture API");
        QComboBox* selection = nullptr;
        for (auto* combo : inspector->findChildren<QComboBox*>())
            if (combo->accessibleName() == QStringLiteral("Aplicativo ou som do PC"))
                selection = combo;
        require(selection, "native capture selector missing");
        require(wait([&] {
                    return selection->findData(target.value(QStringLiteral("token")).toString()) >=
                           0;
                }),
                "native selector did not contain emitter");
        const int index = selection->findData(target.value(QStringLiteral("token")).toString());
        selection->setCurrentIndex(index);
        require(QMetaObject::invokeMethod(selection, "activated", Qt::DirectConnection,
                                          Q_ARG(int, index)),
                "native capture selection failed");
        settle(900);
    };
    selectCapture(a, first.processId());
    selectCapture(b, second.processId());
    require(wait([&] {
                return session.snapshot()
                           .value(QStringLiteral("metrics"))
                           .toObject()
                           .value(QStringLiteral("output_peak"))
                           .toDouble() > 0.01;
            }),
            "captured mix did not reach output");
    const auto backend = ownedBackend();
    require(backend > 0, "owned backend PID not found");
    receiver.start(actor, {QStringLiteral("--receiver"), QString::number(backend)});
    require(receiver.waitForStarted(3000) && line(receiver).value(QStringLiteral("ready")).toBool(),
            "independent receiver failed");
    const auto base = sample(receiver);
    evidence.insert(QStringLiteral("mixed_sources"), base);
    evidence.insert(QStringLiteral("mixed_snapshot"), session.snapshot());
    const double originalA = base.value(QStringLiteral("a997")).toDouble();
    const double originalB = base.value(QStringLiteral("b1733")).toDouble();
    require(originalA > 0.005 && originalB > 0.005,
            "both isolated sources must reach independent receiver");
    session.updateField(gain, QStringLiteral("gain_db"), -12.0);
    require(wait([&] { return session.settled(); }), "gain edit failed");
    const auto attenuated = sample(receiver);
    evidence.insert(QStringLiteral("gain_minus_12_db"), attenuated);
    for (const auto& band : {QStringLiteral("a997"), QStringLiteral("b1733")}) {
        const double ratio = attenuated.value(band).toDouble() / base.value(band).toDouble();
        require(ratio > 0.20 && ratio < 0.30, "measured gain outside -12 dB tolerance");
    }
    session.updateField(gain, QStringLiteral("bypass"), true);
    require(wait([&] { return session.settled(); }), "bypass edit failed");
    const auto bypass = sample(receiver);
    evidence.insert(QStringLiteral("bypass"), bypass);
    require(bypass.value(QStringLiteral("a997")).toDouble() > originalA * 0.8 &&
                bypass.value(QStringLiteral("b1733")).toDouble() > originalB * 0.8,
            "bypass did not restore both sources");
    invoke(session, QStringLiteral("audio.mute"), {{QStringLiteral("muted"), true}});
    const auto muted = sample(receiver);
    evidence.insert(QStringLiteral("mute"), muted);
    require(muted.value(QStringLiteral("rms")).toDouble() < 0.00001,
            "mute did not silence independent receiver");
    invoke(session, QStringLiteral("audio.mute"), {{QStringLiteral("muted"), false}});
    first.kill();
    first.waitForFinished(3000);
    settle(1600);
    const auto exited = sample(receiver);
    evidence.insert(QStringLiteral("source_exit"), exited);
    require(exited.value(QStringLiteral("a997")).toDouble() < originalA * 0.03 &&
                exited.value(QStringLiteral("b1733")).toDouble() > originalB * 0.7,
            "source exit must preserve unrelated source");
    evidence.insert(QStringLiteral("source_exit_snapshot"), session.snapshot());
    receiver.kill();
    receiver.waitForFinished(3000);
    second.kill();
    second.waitForFinished(3000);
    first.start(emitterA, {QStringLiteral("--pulse"), endpoint});
    require(first.waitForStarted(3000) && line(first).value(QStringLiteral("ready")).toBool(),
            "latency pulse emitter failed");
    selectCapture(a, first.processId());
    receiver.start(actor, {QStringLiteral("--latency"), QString::number(first.processId()),
                           QString::number(backend)});
    require(receiver.waitForStarted(3000) && line(receiver).value(QStringLiteral("ready")).toBool(),
            "latency receiver failed");
    const auto timing = line(receiver, 20000);
    require(timing.value(QStringLiteral("delays_ms")).toArray().size() >= 8,
            "insufficient independent latency pulses");
    evidence.insert(QStringLiteral("software_path_latency"), timing);
    evidence.insert(QStringLiteral("final_snapshot"), session.snapshot());
    auto* projects = window.findChild<nodivu::ProjectController*>();
    require(projects, "project controller missing");
    projects->save(QDir(temporary.path()).filePath(QStringLiteral("acceptance.json")));
    require(wait([&] { return !projects->busy() && !projects->dirty(); }),
            "saving acceptance project failed");
    window.close();
    require(wait([&] { return !window.isVisible(); }), "frontend shutdown failed");
}
} // namespace
int main(int argc, char** argv) {
    QApplication application(argc, argv);
    nodivu::Theme::apply(application);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption(
        {QStringLiteral("repository"), QStringLiteral("Checkout"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("output-id"),
                      QStringLiteral("ID opaco explícito; emite tons baixos"),
                      QStringLiteral("id")});
    parser.addOption({QStringLiteral("evidence"), QStringLiteral("Arquivo JSON de evidências"),
                      QStringLiteral("path")});
    parser.process(application);
    if (!parser.isSet(QStringLiteral("repository")) || !parser.isSet(QStringLiteral("output-id")) ||
        !parser.isSet(QStringLiteral("evidence")))
        parser.showHelp(2);
    QJsonObject evidence{{QStringLiteral("schema_version"), 1},
                         {QStringLiteral("status"), QStringLiteral("failed")}};
    try {
        run(parser.value(QStringLiteral("repository")), parser.value(QStringLiteral("output-id")),
            evidence);
        evidence.insert(QStringLiteral("status"), QStringLiteral("passed"));
    } catch (const std::exception& error) {
        evidence.insert(QStringLiteral("error"), QString::fromUtf8(error.what()));
    }
    QFile output(parser.value(QStringLiteral("evidence")));
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return 2;
    const auto bytes = QJsonDocument(evidence).toJson();
    if (output.write(bytes) != bytes.size())
        return 2;
    QTextStream(stdout) << bytes;
    return evidence.value(QStringLiteral("status")).toString() == QStringLiteral("passed") ? 0 : 1;
}
