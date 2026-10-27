#include "contracts/playback_state.hpp"
#include <QtTest>

class PlaybackTests final : public QObject {
    Q_OBJECT
  private:
    static QJsonObject node(const QString& id, const QString& kind) {
        return {{QStringLiteral("id"), id},
                {QStringLiteral("block"),
                 QJsonObject{{QStringLiteral("kind"), kind},
                             {QStringLiteral("endpoint_id"), QStringLiteral("opaque-output")}}}};
    }
    static QJsonArray devices() {
        return {QJsonObject{{QStringLiteral("endpoint_id"), QStringLiteral("opaque-output")},
                            {QStringLiteral("flow"), QStringLiteral("render")},
                            {QStringLiteral("state"), QStringLiteral("active")}}};
    }
    static QJsonObject snapshot(int status = 2) {
        return {
            {QStringLiteral("engine_state"), QStringLiteral("running")},
            {QStringLiteral("plugin_states"),
             QJsonObject{{QStringLiteral("player"),
                          QJsonObject{{QStringLiteral("status"), status},
                                      {QStringLiteral("file"), QStringLiteral("C:/Music/voz.wav")},
                                      {QStringLiteral("duration_ms"), 600000},
                                      {QStringLiteral("position_ms"), 15400}}}}},
            {QStringLiteral("graph"),
             QJsonObject{
                 {QStringLiteral("nodes"),
                  QJsonArray{node(QStringLiteral("player"), QStringLiteral("plugin")),
                             node(QStringLiteral("out"), QStringLiteral("output"))}},
                 {QStringLiteral("edges"),
                  QJsonArray{QJsonObject{{QStringLiteral("from"), QStringLiteral("player")},
                                         {QStringLiteral("to"), QStringLiteral("out")}}}}}}};
    }
    static nodivu::PlaybackState present(const QJsonObject& state,
                                         const QJsonArray& endpoints = devices()) {
        return nodivu::playbackState(state,
                                     node(QStringLiteral("player"), QStringLiteral("plugin")),
                                     endpoints, true, false);
    }
  private slots:
    void readyFileRequiresActiveConnectedUnmutedOutput() {
        auto state = snapshot();
        QVERIFY(present(state).canPlay);
        QCOMPARE(present(state).name, QStringLiteral("voz.wav"));
        QCOMPARE(nodivu::playbackTime(present(state).durationMs), QStringLiteral("10:00"));
        QVERIFY(!present(state, {}).canPlay);
        state.insert(QStringLiteral("muted"), true);
        QVERIFY(!present(state).canPlay);
        state.insert(QStringLiteral("muted"), false);
        state.insert(QStringLiteral("suspended"), true);
        QVERIFY(!present(state).canPlay);
        state.insert(QStringLiteral("suspended"), false);
        state.insert(QStringLiteral("engine_state"), QStringLiteral("starting"));
        QVERIFY(!present(state).canPlay);
        state.insert(QStringLiteral("engine_state"), QStringLiteral("running"));
        auto graph = state.value(QStringLiteral("graph")).toObject();
        graph.insert(QStringLiteral("edges"), QJsonArray{});
        state.insert(QStringLiteral("graph"), graph);
        QVERIFY(!present(state).canPlay);
        auto player = node(QStringLiteral("player"), QStringLiteral("plugin"));
        auto block = player.value(QStringLiteral("block")).toObject();
        block.insert(QStringLiteral("bypass"), true);
        player.insert(QStringLiteral("block"), block);
        const auto bypass = nodivu::playbackState(snapshot(), player, devices(), true, false);
        QVERIFY(!bypass.canPlay);
        QVERIFY(bypass.reason.contains(QStringLiteral("bypass")));
    }
    void savedFileAndDecodeFailuresRemainVisible() {
        auto state = snapshot(0);
        state.insert(QStringLiteral("plugin_states"), QJsonObject{});
        const auto saved = nodivu::playbackState(
            state, node(QStringLiteral("player"), QStringLiteral("plugin")), devices(), true, false,
            QStringLiteral("C:/música/voz salva.wav"));
        QVERIFY(saved.needsLoad);
        QVERIFY(!saved.canPlay);
        QCOMPARE(saved.name, QStringLiteral("voz salva.wav"));
        const auto failed = present(snapshot(-2147024894)); // HRESULT: file not found.
        QVERIFY(!failed.canPlay);
        QVERIFY(failed.reason.contains(QStringLiteral("não encontrado")));
        QVERIFY(!present(snapshot(1)).canPlay);
        QVERIFY(present(snapshot(3)).playing);
        const auto offline = nodivu::playbackState(
            snapshot(3), node(QStringLiteral("player"), QStringLiteral("plugin")), devices(), false,
            false);
        QVERIFY(offline.playing);
        QVERIFY(!offline.canPlay);
    }
    void missingAudioNeverEnablesPlayEvenWithAReadyRoute() {
        auto state = snapshot(0);
        state.insert(QStringLiteral("plugin_states"), QJsonObject{});
        QVERIFY(!present(state).canPlay);
        QVERIFY(present(state).reason.contains(QStringLiteral("Escolha")));
        // A stale ready status cannot make an absent file playable.
        auto runtime = snapshot().value(QStringLiteral("plugin_states")).toObject();
        auto player = runtime.value(QStringLiteral("player")).toObject();
        player.remove(QStringLiteral("file"));
        runtime.insert(QStringLiteral("player"), player);
        state.insert(QStringLiteral("plugin_states"), runtime);
        QVERIFY(!present(state).canPlay);
        const auto saved =
            nodivu::playbackState(state, node(QStringLiteral("player"), QStringLiteral("plugin")),
                                  devices(), true, false, QStringLiteral("C:/Music/voz.wav"));
        QVERIFY(saved.needsLoad);
        QVERIFY(!saved.canPlay);
    }
    void cyclesTerminateAndConsumersExplainInaudibleRoute() {
        auto state = snapshot();
        auto graph = state.value(QStringLiteral("graph")).toObject();
        auto meter = node(QStringLiteral("meter"), QStringLiteral("meter"));
        auto nodes = graph.value(QStringLiteral("nodes")).toArray();
        nodes.append(meter);
        graph.insert(QStringLiteral("nodes"), nodes);
        graph.insert(QStringLiteral("edges"),
                     QJsonArray{QJsonObject{{QStringLiteral("from"), QStringLiteral("player")},
                                            {QStringLiteral("to"), QStringLiteral("meter")}},
                                QJsonObject{{QStringLiteral("from"), QStringLiteral("meter")},
                                            {QStringLiteral("to"), QStringLiteral("player")}}});
        state.insert(QStringLiteral("graph"), graph);
        const auto consumer = present(state);
        QVERIFY(consumer.canPlay);
        QVERIFY(!consumer.warning.isEmpty());
    }
};
QTEST_GUILESS_MAIN(PlaybackTests)
#include "playback_tests.moc"
