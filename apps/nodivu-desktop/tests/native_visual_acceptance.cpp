// Explicit desktop acceptance gate. Uses a real Windows window and real Rust.
#include "features/graph/graph_scene.hpp"
#include "features/graph/graph_view.hpp"
#include "features/inspector/inspector_panel.hpp"
#include "features/projects/project_controller.hpp"
#include "features/store/store_controller.hpp"
#include "features/store/store_window.hpp"
#include "features/workspace/main_window.hpp"
#include "ui/theme/theme.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QProcess>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTreeWidget>
#include <QUuid>
#include <algorithm>
#include <functional>
#include <stdexcept>
// clang-format off
#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>
#include <tlhelp32.h>
// clang-format on

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
QString sha256(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly) && file.size() <= 64LL * 1024 * 1024,
            "bounded fingerprint input unavailable");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&file), "fingerprint read failed");
    return QString::fromLatin1(hash.result().toHex());
}
HANDLE backendProcess() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    require(snapshot != INVALID_HANDLE_VALUE, "owned process enumeration failed");
    const auto close = qScopeGuard([&] { CloseHandle(snapshot); });
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL present = Process32FirstW(snapshot, &entry); present;
         present = Process32NextW(snapshot, &entry))
        if (entry.th32ParentProcessID == GetCurrentProcessId() &&
            _wcsicmp(entry.szExeFile, L"nodivu-app-backend.exe") == 0) {
            HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                                         entry.th32ProcessID);
            require(process, "owned backend resource counters unavailable");
            return process;
        }
    throw std::runtime_error("owned backend not found");
}
double cpuSeconds(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    require(GetProcessTimes(process, &created, &exited, &kernel, &user), "CPU counters failed");
    const auto ticks = [](FILETIME value) {
        return (static_cast<uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 10000000;
}
qint64 privateBytes(HANDLE process) {
    PROCESS_MEMORY_COUNTERS_EX memory{};
    require(GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                 sizeof(memory)),
            "private committed memory counters failed");
    return static_cast<qint64>(memory.PrivateUsage);
}
double qpcMilliseconds() {
    LARGE_INTEGER counter{}, frequency{};
    require(QueryPerformanceCounter(&counter) && QueryPerformanceFrequency(&frequency),
            "QPC clock unavailable");
    return static_cast<double>(counter.QuadPart) * 1000 / static_cast<double>(frequency.QuadPart);
}
bool wait(const std::function<bool()>& condition, int milliseconds = 10000) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < milliseconds) {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return condition();
}
QJsonObject fixture(bool large) {
    QJsonArray nodes, edges, positions;
    QList<QString> ids;
    const auto add = [&](const QJsonObject& block) {
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const int index = ids.size();
        ids.append(id);
        nodes.append(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("block"), block}});
        positions.append(QJsonObject{{QStringLiteral("node_id"), id},
                                     {QStringLiteral("x"), 50 + (index % 6) * 275},
                                     {QStringLiteral("y"), 50 + (index / 6) * 260}});
        return id;
    };
    const auto wire = [&](const QString& from, const QString& to, int port = 0) {
        edges.append(QJsonObject{{QStringLiteral("from"), from},
                                 {QStringLiteral("to"), to},
                                 {QStringLiteral("from_port"), 0},
                                 {QStringLiteral("to_port"), port}});
    };
    auto previous =
        add({{QStringLiteral("kind"), QStringLiteral("tone")}, {QStringLiteral("enabled"), true}});
    const auto first = add({{QStringLiteral("kind"), QStringLiteral("gain")},
                            {QStringLiteral("gain_db"), -60.0},
                            {QStringLiteral("bypass"), false}});
    wire(previous, first);
    previous = first;
    if (large) {
        for (int index = 1; index < 8; ++index) {
            const auto gain = add({{QStringLiteral("kind"), QStringLiteral("gain")},
                                   {QStringLiteral("gain_db"), 0.0},
                                   {QStringLiteral("bypass"), false}});
            wire(previous, gain);
            previous = gain;
        }
        QList<QString> mixers;
        for (int index = 0; index < 14; ++index) {
            const auto mixer = add({{QStringLiteral("kind"), QStringLiteral("mixer")}});
            wire(previous, mixer);
            previous = mixer;
            mixers.append(mixer);
        }
        for (int index = 0; index < 8; ++index) {
            const auto source =
                add({{QStringLiteral("kind"), QStringLiteral("plugin")},
                     {QStringLiteral("source"), true},
                     {QStringLiteral("consumer"), false},
                     {QStringLiteral("plugin_id"), QStringLiteral("org.nodivu.windows-audio")},
                     {QStringLiteral("parameters"), QJsonObject{}},
                     {QStringLiteral("bypass"), false}});
            wire(source, mixers[index], 1);
        }
        // The current backend exposes four ports per Mixer. Fill only free ports.
        for (int index = 0; index < mixers.size(); ++index)
            for (int port = index < 8 ? 2 : 1; port < 4 && edges.size() < 63; ++port)
                wire(first, mixers[index], port);
    }
    const auto output = add({{QStringLiteral("kind"), QStringLiteral("output")},
                             {QStringLiteral("endpoint_id"), QJsonValue(QJsonValue::Null)}});
    wire(previous, output);
    return {{QStringLiteral("format"), QStringLiteral("nodivu-project")},
            {QStringLiteral("version"), 1},
            {QStringLiteral("graph"), QJsonObject{{QStringLiteral("schema_version"), 2},
                                                  {QStringLiteral("nodes"), nodes},
                                                  {QStringLiteral("edges"), edges}}},
            {QStringLiteral("positions"), positions},
            {QStringLiteral("camera"), QJsonObject{{QStringLiteral("x"), 0},
                                                   {QStringLiteral("y"), 0},
                                                   {QStringLiteral("scale"), 0.6}}},
            {QStringLiteral("resources"), QJsonArray{}},
            {QStringLiteral("captures"), QJsonArray{}}};
}
QJsonObject externalAccessibility(QWidget& window, bool invokeRouting) {
    QProcess actor;
    QStringList args{QString::number(window.winId()),
                     QString::number(QCoreApplication::applicationPid())};
    if (invokeRouting)
        args.append(QStringLiteral("Roteamento"));
    actor.start(QCoreApplication::applicationDirPath() +
                    QStringLiteral("/windows_accessibility_actor.exe"),
                args);
    require(actor.waitForStarted(3000), "accessibility actor did not start");
    if (!wait([&] { return actor.state() == QProcess::NotRunning; }, 15000)) {
        actor.kill();
        actor.waitForFinished(3000);
        throw std::runtime_error("accessibility actor timed out");
    }
    const auto evidence = QJsonDocument::fromJson(actor.readAllStandardOutput()).object();
    if (actor.exitCode() != 0)
        QTextStream(stderr) << QJsonDocument(evidence).toJson() << actor.readAllStandardError();
    require(actor.exitCode() == 0, "native Windows accessibility failed");
    return evidence;
}
bool named(const QJsonArray& controls, const QString& name) {
    return std::any_of(controls.begin(), controls.end(), [&](const QJsonValue& value) {
        return value.toObject().value(QStringLiteral("name")).toString() == name;
    });
}
double percentile(QVector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, static_cast<qsizetype>(fraction * values.size()))];
}
void run(const QCommandLineParser& parser, QJsonObject& evidence) {
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary profile unavailable");
    const bool large = parser.isSet(QStringLiteral("large"));
    const auto document = fixture(large);
    const auto file = temporary.filePath(QStringLiteral("visual.json"));
    require(nodivu::ProjectRepository::write(file, document).ok(), "fixture write failed");
    if (parser.isSet(QStringLiteral("fixture")))
        require(nodivu::ProjectRepository::write(parser.value(QStringLiteral("fixture")), document)
                    .ok(),
                "reference fixture write failed");
    auto paths = nodivu::AppPaths::development(parser.value(QStringLiteral("repository")));
    evidence.insert(QStringLiteral("backend_sha256"), sha256(paths.backend));
    paths.profile = temporary.path();
    paths.plugins = temporary.filePath(QStringLiteral("plugins"));
    paths.storeSource.clear();
    nodivu::MainWindow window(paths);
    window.resize(1440, 844);
    window.show();
    auto& session = window.session();
    if (parser.isSet(QStringLiteral("opengl"))) {
        auto* graphView = window.findChild<nodivu::GraphView*>();
        auto* gpu = new QOpenGLWidget(graphView);
        graphView->setViewport(gpu);
        graphView->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
        require(wait([&] { return gpu->isValid(); }), "OpenGL test viewport unavailable");
        evidence.insert(QStringLiteral("experimental_renderer"), QStringLiteral("OpenGL"));
        evidence.insert(QStringLiteral("opengl_swap_interval"),
                        gpu->context()->format().swapInterval());
        gpu->makeCurrent();
        const auto* renderer = gpu->context()->functions()->glGetString(GL_RENDERER);
        evidence.insert(QStringLiteral("opengl_renderer"),
                        renderer ? QString::fromLatin1(reinterpret_cast<const char*>(renderer))
                                 : QString());
        gpu->doneCurrent();
    }
    auto* projects = window.findChild<nodivu::ProjectController*>();
    require(projects, "project controller unavailable");
    QObject::connect(
        &session, &nodivu::SessionController::error, &window,
        [&](const QString& message) { evidence.insert(QStringLiteral("session_error"), message); });
    QObject::connect(
        projects, &nodivu::ProjectController::error, &window,
        [&](const QString& message) { evidence.insert(QStringLiteral("project_error"), message); });
    require(wait([&] { return session.settled() && !projects->busy(); }), "backend startup failed");
    projects->open(file);
    require(
        wait([&] { return !projects->busy() && projects->path() == file && session.settled(); }),
        "real backend refused the presentation graph");
    const auto graph = session.snapshot().value(QStringLiteral("graph")).toObject();
    evidence.insert(QStringLiteral("nodes"), graph.value(QStringLiteral("nodes")).toArray().size());
    evidence.insert(QStringLiteral("edges"), graph.value(QStringLiteral("edges")).toArray().size());
    require(!large || (evidence.value(QStringLiteral("nodes")).toInt() == 32 &&
                       evidence.value(QStringLiteral("edges")).toInt() == 64),
            "maximum graph shape mismatch");
    auto* inspector = window.findChild<nodivu::InspectorPanel*>();
    auto* view = window.findChild<nodivu::GraphView*>();
    auto* scene = window.findChild<nodivu::GraphScene*>();
    require(inspector && view && scene, "workspace widgets missing");
    const auto gain = graph.value(QStringLiteral("nodes"))
                          .toArray()[1]
                          .toObject()
                          .value(QStringLiteral("id"))
                          .toString();
    inspector->inspect(gain);
    view->fitNodes();
    const auto accessibility = externalAccessibility(window, true);
    evidence.insert(QStringLiteral("accessibility"), accessibility);
    const auto before = accessibility.value(QStringLiteral("before")).toArray();
    const auto after = accessibility.value(QStringLiteral("after")).toArray();
    require(named(before, QStringLiteral("Salvar…")) &&
                named(before, QStringLiteral("Aumentar zoom")) &&
                named(before, QStringLiteral("Ganho")) &&
                named(after, QStringLiteral("Blocos e conexões")),
            "Windows accessibility lost named controls or routing invocation");
    auto* routing = window.findChild<QDockWidget*>(QStringLiteral("routingDock"));
    require(routing && routing->isVisible(), "UI Automation did not open routing");
    require(inspector->parentWidget()->rect().contains(inspector->geometry()),
            "inspector escaped workspace after dock resize");
    routing->hide();
    QElapsedTimer layoutDelay;
    layoutDelay.start();
    require(wait([&] { return layoutDelay.elapsed() >= 150; }), "layout did not settle");
    view->fitNodes();
    const auto screenshot = parser.value(QStringLiteral("evidence")) + QStringLiteral(".png");
    require(window.grab().save(screenshot), "window capture failed");
    auto* controller = window.findChild<nodivu::StoreController*>();
    require(controller, "store controller missing");
    require(wait([&] { return !controller->busy(); }), "store initialization failed");
    {
        nodivu::StoreWindow store(*controller, &window);
        store.show();
        const auto accessibleStore = externalAccessibility(store, false);
        evidence.insert(QStringLiteral("store_accessibility"), accessibleStore);
        const auto controls = accessibleStore.value(QStringLiteral("before")).toArray();
        require(named(controls, QStringLiteral("Buscar plugins")) &&
                    named(controls, QStringLiteral("Plugins disponíveis e instalados")),
                "store search/list inaccessible");
        require(store.grab().save(parser.value(QStringLiteral("evidence")) +
                                  QStringLiteral(".store.png")),
                "store capture failed");
    }
    window.raise();
    const auto isolation = parser.value(QStringLiteral("paint-isolation"));
    if (!isolation.isEmpty()) {
        require(isolation == QStringLiteral("nodes") || isolation == QStringLiteral("wires"),
                "unknown diagnostic paint isolation");
        for (auto* item : scene->items()) {
            if (isolation == QStringLiteral("nodes") && item->data(3).isValid())
                item->hide();
            if (isolation == QStringLiteral("wires") &&
                item->flags().testFlag(QGraphicsItem::ItemIsMovable))
                item->hide();
        }
        evidence.insert(QStringLiteral("diagnostic_paint_isolation"), isolation);
    }
    const QString endpoint = parser.value(QStringLiteral("output-id"));
    if (!endpoint.isEmpty()) {
        const auto output = graph.value(QStringLiteral("nodes"))
                                .toArray()
                                .last()
                                .toObject()
                                .value(QStringLiteral("id"))
                                .toString();
        session.updateField(output, QStringLiteral("endpoint_id"), endpoint);
        require(wait([&] {
                    return session.settled() &&
                           session.snapshot().value(QStringLiteral("engine_state")).toString() ==
                               QStringLiteral("running");
                }),
                "explicit audio output failed");
    }
    QElapsedTimer clock;
    HANDLE backend = backendProcess();
    const auto closeBackend = qScopeGuard([&] { CloseHandle(backend); });
    const double frontendCpu = cpuSeconds(GetCurrentProcess());
    const double backendCpu = cpuSeconds(backend);
    evidence.insert(QStringLiteral("frontend_private_start_bytes"),
                    privateBytes(GetCurrentProcess()));
    evidence.insert(QStringLiteral("backend_private_start_bytes"), privateBytes(backend));
    evidence.insert(QStringLiteral("interaction_start_qpc_ms"), qpcMilliseconds());
    evidence.insert(QStringLiteral("logical_width"), window.width());
    evidence.insert(QStringLiteral("logical_height"), window.height());
    evidence.insert(QStringLiteral("device_pixel_ratio"), window.devicePixelRatioF());
    const bool externalTrace = parser.isSet(QStringLiteral("external-trace"));
    const auto nativeWindow = reinterpret_cast<HWND>(window.winId());
    const bool nativeVisible = IsWindowVisible(nativeWindow) && !IsIconic(nativeWindow);
    evidence.insert(QStringLiteral("native_window_visible"), nativeVisible);
    require(nativeVisible, "presentation requires a visible, non-minimized native window");
    const bool animateMeters = parser.isSet(QStringLiteral("animate-meters"));
    require(!animateMeters || !endpoint.isEmpty(), "animated meters require explicit audio output");
    evidence.insert(QStringLiteral("animated_meters"), animateMeters);
    clock.start();
    QVector<double> intervals, paints, mutations, eventWork, repaints, synchronizations;
    qint64 previous = clock.nsecsElapsed();
    const auto initialPosition = scene->positions();
    for (int frame = 0; frame < 240; ++frame) {
        const auto started = clock.nsecsElapsed();
        if (animateMeters)
            session.updateField(gain, QStringLiteral("gain_db"), -60.0 + (frame % 60) / 10.0);
        if (frame < 80)
            scene->placeNode(gain, QPointF(325 + (frame % 40) * 2, 50 + (frame % 13)));
        else if (frame < 160)
            view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->value() +
                                                  (frame % 2 ? 3 : -3));
        else
            view->zoomBy(frame % 2 ? 1.005 : 1 / 1.005);
        const auto mutated = clock.nsecsElapsed();
        QApplication::processEvents(QEventLoop::AllEvents, 2);
        const auto processed = clock.nsecsElapsed();
        // processEvents delivers the requested scene paint. A second forced
        // repaint would measure two full frames for one user interaction.
        const auto repainted = clock.nsecsElapsed();
        QGuiApplication::sync();
        const auto painted = clock.nsecsElapsed();
        if (externalTrace) {
            const auto remaining = started + 16666667 - clock.nsecsElapsed();
            if (remaining > 0) {
                QEventLoop loop;
                QTimer::singleShot(static_cast<int>((remaining + 999999) / 1000000),
                                   Qt::PreciseTimer, &loop, &QEventLoop::quit);
                loop.exec();
            }
        } else {
            require(SUCCEEDED(DwmFlush()), "DWM composition synchronization unavailable");
        }
        const auto presented = clock.nsecsElapsed();
        if (frame >= 10) {
            paints.append(static_cast<double>(painted - started) / 1000000);
            mutations.append(static_cast<double>(mutated - started) / 1000000);
            eventWork.append(static_cast<double>(processed - mutated) / 1000000);
            repaints.append(static_cast<double>(repainted - processed) / 1000000);
            synchronizations.append(static_cast<double>(painted - repainted) / 1000000);
            intervals.append(static_cast<double>(presented - previous) / 1000000);
        }
        previous = presented;
    }
    require(scene->positions() != initialPosition, "drag scenario did not move a node");
    QJsonArray rawIntervals, rawPaints;
    for (const auto value : intervals)
        rawIntervals.append(value);
    for (const auto value : paints)
        rawPaints.append(value);
    evidence.insert(QStringLiteral("composition_intervals_ms"), rawIntervals);
    evidence.insert(QStringLiteral("paint_ms"), rawPaints);
    evidence.insert(QStringLiteral("composition_p95_ms"), percentile(intervals, 0.95));
    evidence.insert(QStringLiteral("composition_p99_ms"), percentile(intervals, 0.99));
    evidence.insert(QStringLiteral("paint_p95_ms"), percentile(paints, 0.95));
    evidence.insert(QStringLiteral("mutation_p95_ms"), percentile(mutations, 0.95));
    evidence.insert(QStringLiteral("event_work_p95_ms"), percentile(eventWork, 0.95));
    evidence.insert(QStringLiteral("repaint_p95_ms"), percentile(repaints, 0.95));
    evidence.insert(QStringLiteral("sync_p95_ms"), percentile(synchronizations, 0.95));
    evidence.insert(QStringLiteral("interaction_end_qpc_ms"), qpcMilliseconds());
    evidence.insert(QStringLiteral("frontend_cpu_seconds"),
                    cpuSeconds(GetCurrentProcess()) - frontendCpu);
    evidence.insert(QStringLiteral("backend_cpu_seconds"), cpuSeconds(backend) - backendCpu);
    evidence.insert(QStringLiteral("interaction_seconds"), clock.elapsed() / 1000.0);
    evidence.insert(QStringLiteral("frontend_private_end_bytes"),
                    privateBytes(GetCurrentProcess()));
    evidence.insert(QStringLiteral("backend_private_end_bytes"), privateBytes(backend));
    evidence.insert(QStringLiteral("stalls_over_50_ms"),
                    static_cast<int>(std::count_if(intervals.begin(), intervals.end(),
                                                   [](double ms) { return ms > 50; })));
    evidence.insert(
        QStringLiteral("presentation_scope"),
        externalTrace
            ? QStringLiteral("16.7 ms interaction request cadence, no DwmFlush; analyze external "
                             "per-process ETW trace for actual display cadence")
            : QStringLiteral(
                  "Scene mutation + event delivery + QGuiApplication::sync + DwmFlush; desktop "
                  "composition cadence, not a per-window PresentMon trace"));
    evidence.insert(QStringLiteral("final_metrics"),
                    session.snapshot().value(QStringLiteral("metrics")));
    require(wait([&] { return session.settled(); }), "final control edits did not settle");
    projects->save(file);
    require(wait([&] { return !projects->busy() && !projects->dirty(); }),
            "save after interaction failed");
    window.close();
    require(wait([&] { return !window.isVisible(); }), "shutdown after interaction failed");
}
} // namespace
int main(int argc, char** argv) {
    QApplication application(argc, argv);
    nodivu::Theme::apply(application);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption(
        {QStringLiteral("external-trace"),
         QStringLiteral("Cadência de interação para coleta externa ETW, sem DwmFlush")});
    parser.addOption({QStringLiteral("animate-meters"),
                      QStringLiteral("Modular ganho baixo para atualizar medidores reais")});
    parser.addOption(
        {QStringLiteral("opengl"), QStringLiteral("Viewport experimental somente neste ensaio")});
    parser.addOption(
        {QStringLiteral("repository"), QStringLiteral("Checkout"), QStringLiteral("path")});
    parser.addOption(
        {QStringLiteral("evidence"), QStringLiteral("Novo arquivo JSON"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("fixture"), QStringLiteral("Exportar grafo para comparação"),
                      QStringLiteral("path")});
    parser.addOption(
        {QStringLiteral("large"), QStringLiteral("32 nós e 64 fios com oito plugins")});
    parser.addOption({QStringLiteral("output-id"),
                      QStringLiteral("Saída explícita opcional; tom baixo"), QStringLiteral("id")});
    parser.addOption({QStringLiteral("paint-isolation"),
                      QStringLiteral("Somente diagnóstico, omite nós ou fios da pintura"),
                      QStringLiteral("nodes|wires")});
    parser.process(application);
    if (!parser.isSet(QStringLiteral("repository")) || !parser.isSet(QStringLiteral("evidence")))
        parser.showHelp(2);
    QJsonObject evidence{{QStringLiteral("schema_version"), 1},
                         {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
                         {QStringLiteral("windows_kernel"), QSysInfo::kernelVersion()},
                         {QStringLiteral("status"), QStringLiteral("failed")}};
    try {
        evidence.insert(QStringLiteral("runner_sha256"),
                        sha256(QCoreApplication::applicationFilePath()));
        run(parser, evidence);
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
    QTextStream(stdout) << evidence.value(QStringLiteral("status")).toString() << Qt::endl;
    return evidence.value(QStringLiteral("status")).toString() == QStringLiteral("passed") ? 0 : 1;
}
