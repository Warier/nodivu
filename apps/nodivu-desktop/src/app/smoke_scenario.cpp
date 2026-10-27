#include "app/smoke_scenario.hpp"
#include "features/graph/graph_scene.hpp"
#include "features/graph/graph_view.hpp"
#include "features/inspector/inspector_panel.hpp"
#include "features/projects/project_controller.hpp"
#include "features/workspace/main_window.hpp"
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>
#include <memory>

namespace nodivu {
void startNativeSmoke(MainWindow& window, const QString& project, const QString& screenshot,
                      int benchmarkMilliseconds) {
    struct State {
        int step = 0;
        QElapsedTimer elapsed;
        QElapsedTimer settled;
    };
    auto state = std::make_shared<State>();
    state->elapsed.start();
    auto* timer = new QTimer(&window);
    timer->setInterval(100);
    QObject::connect(&window.session(), &SessionController::error, timer, [](const QString& error) {
        qCritical().noquote() << error;
        QCoreApplication::exit(1);
    });
    QObject::connect(
        timer, &QTimer::timeout, &window,
        [&window, timer, state, project, screenshot, benchmarkMilliseconds] {
            if (state->elapsed.elapsed() > 15000 + benchmarkMilliseconds) {
                timer->stop();
                QCoreApplication::exit(2);
                return;
            }
            auto& session = window.session();
            if (!session.settled())
                return;
            const auto graph = session.snapshot().value(QStringLiteral("graph")).toObject();
            const auto nodes = graph.value(QStringLiteral("nodes")).toArray();
            const auto edges = graph.value(QStringLiteral("edges")).toArray();
            const auto id = [&nodes](int index) {
                return nodes[index].toObject().value(QStringLiteral("id")).toString();
            };
            switch (state->step) {
            case 0:
                session.addNode(QStringLiteral("tone"));
                ++state->step;
                break;
            case 1:
                if (nodes.size() == 1) {
                    session.addNode(QStringLiteral("gain"));
                    ++state->step;
                }
                break;
            case 2:
                if (nodes.size() == 2) {
                    session.addNode(QStringLiteral("output"));
                    ++state->step;
                }
                break;
            case 3:
                if (nodes.size() == 3) {
                    session.connectNodes(id(0), 0, id(1), 0);
                    ++state->step;
                }
                break;
            case 4:
                if (edges.size() == 1) {
                    session.connectNodes(id(1), 0, id(2), 0);
                    ++state->step;
                }
                break;
            case 5:
                if (edges.size() == 2) {
                    session.updateField(id(1), QStringLiteral("gain_db"), -12.0);
                    ++state->step;
                }
                break;
            case 6:
                if (session.node(id(1))
                        .value(QStringLiteral("block"))
                        .toObject()
                        .value(QStringLiteral("gain_db"))
                        .toDouble() == -12.0) {
                    window.findChild<InspectorPanel*>()->inspect(id(1));
                    window.findChild<GraphView*>()->fitNodes();
                    ++state->step;
                }
                break;
            case 7:
                if (!screenshot.isEmpty() && !window.grab().save(screenshot)) {
                    QCoreApplication::exit(3);
                    return;
                }
                window.findChild<ProjectController*>()->save(project);
                ++state->step;
                break;
            case 8:
                if (auto* projects = window.findChild<ProjectController*>();
                    !projects->busy() && !projects->dirty() && !projects->path().isEmpty()) {
                    if (benchmarkMilliseconds > 0) {
                        if (!state->settled.isValid())
                            state->settled.start();
                        if (state->settled.elapsed() < benchmarkMilliseconds)
                            return;
                    }
                    timer->stop();
                    window.session().beginShutdown();
                    window.close();
                }
                break;
            default:
                break;
            }
        });
    timer->start();
}
} // namespace nodivu
