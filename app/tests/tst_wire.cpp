#include "StreamServer.h"
#include "Wire.h"
#include "qp/SensorSimulator.h"

#include <QWebSocket>
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

    // Step 10: notes are events with an anchor appended to the item. They round-trip, and a reader
    // that only knows version 1 fields still gets the text.
    void anchoredEventsRoundTrip()
    {
        qp::EventRecord note{7, 123, 0, "note", "#1003: crossing"};
        note.trackId = 1003;
        note.x = 12.5f;
        note.y = -3.25f;
        note.anchored = true;
        const qp::EventBatch batch{{1, 100, 1, "lidar2", "late"}, note};
        const QByteArray b = rec::encodeEvents(batch);
        const qp::EventBatchPtr back = rec::decodeEvents(rec::kEventsVersion, b.constData(), size_t(b.size()));
        QVERIFY(back);
        QCOMPARE(back->size(), size_t(2));
        QVERIFY(!(*back)[0].anchored);
        QVERIFY((*back)[1].anchored);
        QCOMPARE((*back)[1].trackId, 1003);
        QCOMPARE((*back)[1].x, 12.5f);
        QCOMPARE((*back)[1].text, QStringLiteral("#1003: crossing"));
        const qp::EventBatchPtr asV1 = rec::decodeEvents(1, b.constData(), size_t(b.size()));
        QVERIFY(asV1);
        QCOMPARE((*asV1)[1].text, QStringLiteral("#1003: crossing"));
    }

    // A slow client loses plain events beyond its queue, never a note.
    void slowClientKeepsNotes()
    {
        // Small socket buffers, and the socket full before any event: the queue must overflow on every run.
        StreamServer server(47401, StreamServer::Policy::Latest, 0, {}, 64 * 1024, 16 * 1024);
        QVERIFY(server.start(nullptr));
        std::mutex m;
        int events = 0, notes = 0;
        StreamClient client(QStringLiteral("127.0.0.1"), 47401, 0.3, 64 * 1024,
                            {[](const qp::SensorFrame &) {}, [](const qp::TrackSetPtr &) {},
                             [&](const qp::EventBatchPtr &b) {
                                 std::lock_guard g(m);
                                 for (const auto &e : *b)
                                     (e.anchored ? notes : events) += 1;
                             },
                             {}});
        client.start();
        QTRY_VERIFY_WITH_TIMEOUT(server.status().size() == 1, 3000);
        for (int i = 0; i < 4; ++i)
            server.publishLidar(frame(i, quint64(i), i, 150'000));
        for (int i = 0; i < 600; ++i) {
            if (i % 20 == 0)
                server.publishLidar(frame(0, quint64(i), i * 1'000'000, 150'000)); // fills the socket
            qp::EventRecord e{quint64(i), i, 0, "s", QString(200, u'x')};
            if (i == 0 || i == 300) {
                e.source = QStringLiteral("note");
                e.anchored = true;
                e.trackId = i;
            }
            server.publishEvents(std::make_shared<qp::EventBatch>(qp::EventBatch{e}));
        }
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            std::lock_guard g(m);
            return notes == 2;
        }(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(server.status().value(0).contains(u"events dropped")
                                     && !server.status().value(0).contains(u" 0 events dropped"),
                                 3000);
        std::lock_guard g(m);
        QVERIFY2(events < 598, qPrintable(QString::number(events))); // plain events were dropped
    }

    // Step 11: the same records over WebSockets, one record per message.
    void webSocketLoopback()
    {
        StreamServer server(47402, StreamServer::Policy::Latest, 1, {});
        server.setWebSocket(47403, {});
        QVERIFY(server.start(nullptr));
        std::mutex m;
        std::vector<quint64> seqs;
        int events = 0, notes = 0, sessions = 0;
        StreamClient client(QStringLiteral("ws://127.0.0.1:47403"), 0, 0, 0,
                            {[&](const qp::SensorFrame &f) {
                                 std::lock_guard g(m);
                                 seqs.push_back(f.seq);
                             },
                             [](const qp::TrackSetPtr &) {},
                             [&](const qp::EventBatchPtr &b) {
                                 std::lock_guard g(m);
                                 for (const auto &e : *b)
                                     (e.anchored ? notes : events) += 1;
                             },
                             [&] {
                                 std::lock_guard g(m);
                                 ++sessions;
                             }});
        client.start();
        QTRY_VERIFY_WITH_TIMEOUT(server.status().size() == 1, 3000);
        QVERIFY(server.status().value(0).startsWith(u"ws "));
        for (int i = 0; i < 50; ++i) {
            server.publishLidar(frame(0, quint64(i), i * 1'000'000));
            qp::EventRecord e{quint64(i), i, 0, "s", "m"};
            e.anchored = i == 25;
            server.publishEvents(std::make_shared<qp::EventBatch>(qp::EventBatch{e}));
            QTest::qWait(5);
        }
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            std::lock_guard g(m);
            return !seqs.empty() && seqs.back() == 49 && events == 49 && notes == 1;
        }(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(client.status().startsWith(u"connected to ws://"), 3000);
        std::lock_guard g(m);
        QCOMPARE(sessions, 1);
        for (size_t i = 1; i < seqs.size(); ++i)
            QVERIFY(seqs[i] > seqs[i - 1]);
    }

    // Any web page can open a WebSocket to localhost; only local and listed origins get the stream.
    void webSocketOrigins()
    {
        StreamServer server(47404, StreamServer::Policy::Latest, 0, {});
        server.setWebSocket(47405, {QStringLiteral("https://viewer.example")});
        QVERIFY(server.start(nullptr));
        const auto hello = [](const QString &origin) {
            QWebSocket ws(origin);
            QByteArray first;
            QObject::connect(&ws, &QWebSocket::binaryMessageReceived, &ws, [&](const QByteArray &b) {
                if (first.isEmpty())
                    first = b;
            });
            ws.open(QUrl(QStringLiteral("ws://127.0.0.1:47405")));
            QElapsedTimer t;
            t.start();
            while (first.isEmpty() && t.elapsed() < 1500)
                QTest::qWait(20);
            return first;
        };
        QVERIFY(hello(QStringLiteral("http://localhost:8080")).startsWith("QPSTREAM"));
        QVERIFY(hello(QStringLiteral("http://127.0.0.1:8000")).startsWith("QPSTREAM"));
        QVERIFY(hello(QStringLiteral("https://viewer.example")).startsWith("QPSTREAM"));
        QVERIFY(hello(QStringLiteral("https://evil.example")).isEmpty());
        QVERIFY(hello(QStringLiteral("http://localhost.evil.example")).isEmpty());
    }

    // Step 11: the server's clock from probes. The lowest round trip wins; a slow probe doesn't move it.
    void clockSyncPrefersShortRoundTrips()
    {
        wire::ClockSync c;
        QVERIFY(!c.known());
        c.seed(5'000'000'000, 1'000'000'000); // the hello: server 4 s ahead
        QCOMPARE(c.offsetNs(), 4'000'000'000);
        QCOMPARE(c.rttNs(), -1);
        // True offset 4.000 s; probe 1 waits 10 ms each way, probe 2 is quick, probe 3 waits on the way back.
        c.add(2'000'000'000, 6'010'000'000, 2'020'000'000);
        QCOMPARE(c.offsetNs(), 4'000'000'000);
        c.add(3'000'000'000, 7'000'500'000, 3'001'000'000);
        QCOMPARE(c.rttNs(), 1'000'000);
        QCOMPARE(c.offsetNs(), 4'000'000'000);
        c.add(4'000'000'000, 8'000'000'000, 4'080'000'000); // offset estimate off by 40 ms
        QCOMPARE(c.offsetNs(), 4'000'000'000);
        QCOMPARE(c.toLocal(9'000'000'000), 5'000'000'000);
        c.seed(0, 0); // a later hello never overrides probes
        QCOMPARE(c.offsetNs(), 4'000'000'000);
        for (int i = 0; i < 16; ++i) // the quick probe ages out of the window
            c.add(10'000'000'000 + i, 14'002'000'000 + i, 10'004'000'000 + i);
        QCOMPARE(c.rttNs(), 4'000'000);
        QCOMPARE(c.offsetNs(), 4'000'000'000);
        c.add(20'000'000'000, 1, 19'000'000'000); // a reply before its probe: ignored
        QCOMPARE(c.rttNs(), 4'000'000);
    }

    // On one machine the two clocks are the same: probes must find (almost) no offset.
    void clockSyncOverTheWire()
    {
        StreamServer server(47406, StreamServer::Policy::Latest, 0, {});
        server.setWebSocket(47407, {});
        QVERIFY(server.start(nullptr));
        for (const QString &target : {QStringLiteral("127.0.0.1"), QStringLiteral("ws://127.0.0.1:47407")}) {
            StreamClient client(target, 47406, 0, 0,
                                {[](const qp::SensorFrame &) {}, [](const qp::TrackSetPtr &) {},
                                 [](const qp::EventBatchPtr &) {}, {}});
            client.start();
            QTRY_VERIFY_WITH_TIMEOUT(client.status().contains(u" ± "), 4000);
            const QString st = client.status();
            const double offset = st.section(QStringLiteral("clock "), 1).section(u' ', 0, 0).toDouble();
            QVERIFY2(std::abs(offset) < 2.0, qPrintable(st));
        }
    }

    // A browser reads every message as it arrives, so its socket never backs up. Without credit the
    // server sends everything; with credit it stops a window ahead of what the client has processed.
    void creditHoldsBackForEagerReaders()
    {
        constexpr qint64 window = 256 << 10;
        StreamServer server(47408, StreamServer::Policy::Latest, 0, {}, window);
        server.setWebSocket(47409, {});
        server.setCreditWindow(window);
        QVERIFY(server.start(nullptr));
        const qint64 record = wire::lidarRecord(frame(0, 0, 0, 20'000), 0).size();
        for (const bool credit : {false, true}) {
            QWebSocket ws;
            qint64 received = 0;
            bool hello = false;
            QObject::connect(&ws, &QWebSocket::binaryMessageReceived, &ws, [&](const QByteArray &b) {
                received += b.size();
                if (!hello && credit)
                    ws.sendBinaryMessage(wire::hello(QJsonObject{{"credit", 1}}));
                hello = true;
            });
            ws.open(QUrl(QStringLiteral("ws://127.0.0.1:47409")));
            QTRY_VERIFY_WITH_TIMEOUT(hello, 3000);
            QTest::qWait(100); // the client's hello reaches the server
            for (int i = 0; i < 60; ++i) { // ~12 MB/s: a rate the link carries, as in a browser that's slow to process
                server.publishLidar(frame(i % 4, quint64(i), i, 20'000));
                QTest::qWait(10);
            }
            QTest::qWait(300);
            if (!credit) {
                QVERIFY2(received >= 60 * record, qPrintable(QString::number(received)));
                continue;
            }
            QVERIFY2(received <= window + record + 4096, qPrintable(QString::number(received)));
            QTRY_VERIFY_WITH_TIMEOUT(server.status().join(u' ').contains(u"(credit)"), 3000);
            const qint64 before = received;
            ws.sendBinaryMessage(wire::creditRecord(received)); // all of it processed
            QTRY_VERIFY_WITH_TIMEOUT(received > before, 3000);  // the newest held records follow
            QVERIFY(received < 60 * record);                    // older ones were replaced, not queued
        }
    }

    // A browser hands over whole messages; the parser must not care where they end.
    void parserTakesMessages()
    {
        const auto f = frame(2, 3, 9);
        const QList<QByteArray> messages{wire::hello({}), wire::lidarRecord(f, 1), wire::lidarRecord(f, 0)};
        wire::Parser p;
        std::vector<wire::Decoded> out;
        for (const QByteArray &msg : messages)
            QVERIFY(p.feed(msg.constData(), msg.size(), [&](const wire::Decoded &d) { out.push_back(d); }));
        QCOMPARE(out.size(), size_t(2));
        QCOMPARE(out[0].lidar.seq, quint64(3));
        QCOMPARE(out[0].lidar.cloud->count(), f.cloud->count());
        QCOMPARE(out[1].lidar.cloud->xyz, out[0].lidar.cloud->xyz);
    }
};

QTEST_GUILESS_MAIN(TstWire)
#include "tst_wire.moc"
