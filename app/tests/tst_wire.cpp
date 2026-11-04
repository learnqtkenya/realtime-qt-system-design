#include "StreamServer.h"
#include "Wire.h"
#include "qp/SensorSimulator.h"

#include <QtTest>

#include <mutex>

namespace {

qp::SensorFrame frame(int sensor, quint64 seq, qint64 t, int points = 5000)
{
    qp::Workload w;
    w.pointsPerFrame = points;
    return {sensor, seq, t, qp::SensorSimulator::makeCloud(w, sensor, int(seq % 4))};
}

} // namespace

class TstWire : public QObject
{
    Q_OBJECT

private slots:
    void parserTakesAnySplit()
    {
        QByteArray stream = wire::hello(QJsonObject{{"x", 1}});
        const auto f = frame(1, 7, 123);
        stream += wire::lidarRecord(f, 1);
        stream += wire::lidarRecord(f, 0);
        auto set = std::make_shared<qp::TrackSet>();
        set->captureNs = 5;
        set->tracks.push_back({42, 1, 2, 0, 3, 4, 2, 1});
        stream += wire::tracksRecord(set);
        wire::Parser p;
        std::vector<wire::Decoded> out;
        for (char c : stream) // one byte at a time: the hardest split
            QVERIFY(p.feed(&c, 1, [&](const wire::Decoded &d) { out.push_back(d); }));
        QVERIFY(p.hasHello());
        QCOMPARE(p.info().value("x").toInt(), 1);
        QCOMPARE(out.size(), size_t(3));
        QCOMPARE(out[0].lidar.seq, quint64(7));
        QCOMPARE(out[0].lidar.cloud->count(), 5000);
        QCOMPARE(out[1].lidar.cloud->xyz, out[0].lidar.cloud->xyz); // compressed and not: same points
        QCOMPARE(out[2].tracks->tracks[0].id, 42u);
    }

    void parserRejectsStrangers()
    {
        wire::Parser p;
        const QByteArray http("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
        QVERIFY(!p.feed(http.constData(), http.size(), [](const wire::Decoded &) {}));
        wire::Parser q;
        QByteArray huge = wire::hello({});
        huge += wire::record(rec::Type::Lidar, 3, 0, 0, QByteArray(8, 'x'));
        const quint32 lie = 0x7FFFFFFF; // a size no record can have
        std::memcpy(huge.data() + huge.size() - 8 - 4, &lie, 4);
        QVERIFY(!q.feed(huge.constData(), huge.size(), [](const wire::Decoded &) {}));
    }

    void serverToClientLoopback()
    {
        StreamServer server(47400, StreamServer::Policy::Latest, 1, {});
        QVERIFY(server.start(nullptr));
        std::mutex m;
        std::vector<quint64> seqs;
        int events = 0;
        StreamClient client(QStringLiteral("127.0.0.1"), 47400, 0, 0,
                            {[&](const qp::SensorFrame &f) {
                                 std::lock_guard g(m);
                                 seqs.push_back(f.seq);
                             },
                             [](const qp::TrackSetPtr &) {},
                             [&](const qp::EventBatchPtr &b) {
                                 std::lock_guard g(m);
                                 events += int(b->size());
                             },
                             {}});
        client.start();
        QTRY_VERIFY_WITH_TIMEOUT(server.status().size() == 1, 3000);
        for (int i = 0; i < 50; ++i) {
            server.publishLidar(frame(0, quint64(i), i * 1'000'000));
            server.publishEvents(std::make_shared<qp::EventBatch>(qp::EventBatch{{quint64(i), i, 0, "s", "m"}}));
            QTest::qWait(5);
        }
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            std::lock_guard g(m);
            return !seqs.empty() && seqs.back() == 49 && events == 50;
        }(), 3000);
        std::lock_guard g(m);
        for (size_t i = 1; i < seqs.size(); ++i)
            QVERIFY(seqs[i] > seqs[i - 1]); // latest-wins may skip, never reorder
    }
};

QTEST_GUILESS_MAIN(TstWire)
#include "tst_wire.moc"
