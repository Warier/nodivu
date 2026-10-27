#include "backend/backend_client.hpp"
#include "contracts/protocol_codec.hpp"
#include <QJsonDocument>
#include <QtTest>
#include <chrono>
#include <iostream>
#include <thread>

namespace {
int fixture(const QString& mode) {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (mode == QStringLiteral("hang")) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            return 0;
        }
        if (mode == QStringLiteral("crash"))
            return 7;
        const auto request = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        const auto reply = QJsonDocument(QJsonObject{{QStringLiteral("protocol_version"), 1},
                                                     {QStringLiteral("id"),
                                                      mode == QStringLiteral("wrong-id")
                                                          ? QJsonValue(QStringLiteral("wrong"))
                                                          : request.value(QStringLiteral("id"))},
                                                     {QStringLiteral("ok"), true},
                                                     {QStringLiteral("result"), QJsonObject{}}})
                               .toJson(QJsonDocument::Compact) +
                           '\n';
        const auto split = reply.size() / 2;
        std::cout.write(reply.constData(), split).flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::cout.write(reply.constData() + split, reply.size() - split).flush();
        if (request.value(QStringLiteral("command")).toString() ==
            QStringLiteral("system.shutdown"))
            return 0;
    }
    return 0;
}
} // namespace

class ProtocolTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        qRegisterMetaType<nodivu::BackendReply>();
    }
    void fragmentedFramesAndQueueBackpressure() {
        nodivu::JsonlTransport transport;
        QSignalSpy ready(&transport, &nodivu::JsonlTransport::ready);
        QSignalSpy completed(&transport, &nodivu::JsonlTransport::completed);
        QSignalSpy stopped(&transport, &nodivu::JsonlTransport::stopped);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("NODIVU_PROTOCOL_FIXTURE"), QStringLiteral("normal"));
        transport.start(QCoreApplication::applicationFilePath(), {}, environment);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 3000);
        for (int index = 0; index < 20; ++index)
            transport.enqueue(QString::number(index), QStringLiteral("test.command"), {});
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 20, 5000);
        int failures = 0;
        for (const auto& record : completed)
            failures += !qvariant_cast<nodivu::BackendReply>(record[0]).ok ? 1 : 0;
        QCOMPARE(failures, 3); // one in flight and sixteen queued commands
        transport.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 5000);
    }
    void failuresAlwaysCompletePendingRequests_data() {
        QTest::addColumn<QString>("mode");
        for (const auto& mode :
             {QStringLiteral("wrong-id"), QStringLiteral("crash"), QStringLiteral("hang")})
            QTest::newRow(qPrintable(mode)) << mode;
    }
    void failuresAlwaysCompletePendingRequests() {
        QFETCH(QString, mode);
        nodivu::JsonlTransport transport;
        QSignalSpy completed(&transport, &nodivu::JsonlTransport::completed);
        QSignalSpy faults(&transport, &nodivu::JsonlTransport::fault);
        QSignalSpy stopped(&transport, &nodivu::JsonlTransport::stopped);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("NODIVU_PROTOCOL_FIXTURE"), mode);
        transport.start(QCoreApplication::applicationFilePath(), {}, environment);
        transport.enqueue(QStringLiteral("1"), QStringLiteral("test.command"), {});
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 8000);
        QCOMPARE(completed.size(), 1);
        QCOMPARE(faults.size(), 1);
        QVERIFY(!qvariant_cast<nodivu::BackendReply>(completed[0][0]).ok);
    }
    void clientCanRestartAfterFailedLaunch() {
        nodivu::BackendClient client;
        QSignalSpy stopped(&client, &nodivu::BackendClient::stopped);
        QSignalSpy ready(&client, &nodivu::BackendClient::ready);
        auto environment = QProcessEnvironment::systemEnvironment();
        client.start(QStringLiteral("C:/Nodivu-nonexistent-fixture/no-backend.exe"), environment);
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 3000);
        environment.insert(QStringLiteral("NODIVU_PROTOCOL_FIXTURE"), QStringLiteral("normal"));
        client.start(QCoreApplication::applicationFilePath(), environment);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 3000);
        bool completed = false;
        client.request(QStringLiteral("test.command"), {}, this,
                       [&completed](const nodivu::BackendReply& reply) { completed = reply.ok; });
        QTRY_VERIFY_WITH_TIMEOUT(completed, 3000);
        client.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 2, 5000);
    }
    void preservesOpaqueIds() {
        const auto frame = nodivu::encodeRequest(
            QStringLiteral("42"), QStringLiteral("node.add"),
            {{QStringLiteral("plugin_id"), QStringLiteral("org.example.voice")}});
        QVERIFY(frame.endsWith('\n'));
        const auto object = QJsonDocument::fromJson(frame).object();
        QCOMPARE(object.value(QStringLiteral("id")).toString(), QStringLiteral("42"));
        QCOMPARE(object.value(QStringLiteral("params"))
                     .toObject()
                     .value(QStringLiteral("plugin_id"))
                     .toString(),
                 QStringLiteral("org.example.voice"));
    }
    void rejectsMismatchedEnvelope() {
        const QByteArray frame = R"({"protocol_version":1,"id":"other","ok":true,"result":{}})";
        const auto reply = nodivu::decodeReply(frame, QStringLiteral("42"));
        QVERIFY(!reply.ok);
        QCOMPARE(reply.code, QStringLiteral("transport_error"));
    }
    void preservesBackendCause() {
        const QByteArray frame =
            R"({"protocol_version":1,"id":"42","ok":false,"error":{"code":"revision_conflict","message":"Revision changed"}})";
        const auto reply = nodivu::decodeReply(frame, QStringLiteral("42"));
        QVERIFY(!reply.ok);
        QCOMPARE(reply.code, QStringLiteral("revision_conflict"));
        QCOMPARE(reply.message, QStringLiteral("Revision changed"));
    }
    void limitsRequests() {
        QVERIFY(nodivu::encodeRequest(
                    QStringLiteral("1"), QStringLiteral("graph.apply"),
                    {{QStringLiteral("data"), QString(nodivu::MaxFrameBytes, QLatin1Char('a'))}})
                    .isEmpty());
    }
};
int main(int argc, char** argv) {
    const auto mode = qEnvironmentVariable("NODIVU_PROTOCOL_FIXTURE");
    if (!mode.isEmpty())
        return fixture(mode);
    QCoreApplication app(argc, argv);
    ProtocolTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "protocol_tests.moc"
