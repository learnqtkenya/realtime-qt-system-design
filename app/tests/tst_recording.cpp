#include "Codec.h"
#include "FileSource.h"
#include "History.h"
#include "Recording.h"
#include "qp/SensorSimulator.h"

#include <QTemporaryDir>
#include <QtTest>

#include <random>

namespace {

qp::SensorFrame frame(int sensor, quint64 seq, qint64 t, int points = 2000)
{
    qp::Workload w;
    w.pointsPerFrame = points;
    return {sensor, seq, t, qp::SensorSimulator::makeCloud(w, sensor, int(seq % 4))};
}

qp::TrackSetPtr tracks(quint64 seq, qint64 t, int n = 5)
{
    auto s = std::make_shared<qp::TrackSet>();
    s->seq = seq;
    s->captureNs = t;
    for (int i = 0; i < n; ++i)
        s->tracks.push_back({quint32(1000 + i), float(i), float(-i), 0.5f, 3.f, 4.5f, 1.9f, quint8(i & 3)});
    return s;
}

qp::EventBatchPtr events(quint64 seq, qint64 t)
{
    auto b = std::make_shared<qp::EventBatch>();
    b->push_back({seq, t, 1, QStringLiteral("lidar0"), QStringLiteral("frame arrived late ✓")});
    b->push_back({seq + 1, t, 2, QStringLiteral("fusion"), QStringLiteral("sensor timeout")});
    return b;
}

float maxError(const qp::PointCloud &a, const qp::PointCloud &b)
{
    float e = 0;
    for (size_t i = 0; i < a.xyz.size(); ++i)
        e = std::max(e, std::abs(a.xyz[i] - b.xyz[i]));
    return e;
}

constexpr qint64 ms = 1'000'000;

// 2 s of a small world: 2 sensors at 10 Hz (the second 30 ms late, so arrival order is not time
// order), tracks at 10 Hz, events at 20 Hz, one camera at 30 Hz.
struct Script
{
    std::vector<qp::SensorFrame> lidar;
    int trackSets = 0, eventBatches = 0, marks = 0;

    void write(rec::Writer &w)
    {
        for (int k = 0; k < 60; ++k) {
            const qint64 t = 1000 * ms + k * 33 * ms;
            if (k % 3 == 0) {
                const int i = k / 3;
                lidar.push_back(frame(0, quint64(i), t));
                w.addLidar(lidar.back());
                if (i > 0) {
                    lidar.push_back(frame(1, quint64(i - 1), t - 100 * ms + 30 * ms));
                    w.addLidar(lidar.back());
                }
                w.addTracks(tracks(quint64(i), t));
                ++trackSets;
            }
            if (k % 2 == 0) {
                w.addEvents(events(quint64(k), t));
                ++eventBatches;
            }
            w.addCameraMark({0, t, k * 33'333});
            ++marks;
        }
    }
};

} // namespace

class TstRecording : public QObject
{
    Q_OBJECT

private slots:
    void lidarRoundTrip()
    {
        const qp::SensorFrame f = frame(2, 7, 123);
        for (auto enc : {rec::LidarEncoding::Float32, rec::LidarEncoding::Int16, rec::LidarEncoding::Int16Planar}) {
            const QByteArray b = rec::encodeLidar(f, enc);
            const auto back = rec::decodeLidar(quint16(enc), 2, 123, b.constData(), size_t(b.size()));
            QVERIFY(back);
            QCOMPARE(back->seq, quint64(7));
            QCOMPARE(back->cloud->count(), f.cloud->count());
            const float err = maxError(*f.cloud, *back->cloud);
            if (enc == rec::LidarEncoding::Float32)
                QCOMPARE(err, 0.0f);
            else
                QVERIFY2(err <= 0.005f + 2e-5f, qPrintable(QString::number(err)));
        }
    }

    void tracksEventsAndMarksRoundTrip()
    {
        const auto t = tracks(3, 99, 7);
        const QByteArray tb = rec::encodeTracks(*t);
        const auto t2 = rec::decodeTracks(1, 99, tb.constData(), size_t(tb.size()));
        QVERIFY(t2);
        QCOMPARE(t2->tracks.size(), size_t(7));
        QCOMPARE(t2->tracks[6].id, 1006u);
        QCOMPARE(t2->tracks[6].y, -6.0f);
        QCOMPARE(t2->tracks[3].cls, quint8(3));

        const auto e = events(10, 55);
        const QByteArray eb = rec::encodeEvents(*e);
        const auto e2 = rec::decodeEvents(1, eb.constData(), size_t(eb.size()));
        QVERIFY(e2);
        QCOMPARE(e2->size(), size_t(2));
        QCOMPARE((*e2)[0].text, QStringLiteral("frame arrived late ✓"));
        QCOMPARE((*e2)[1].seq, quint64(11));
        QCOMPARE((*e2)[1].captureNs, qint64(55));

        const QByteArray mb = rec::encodeCameraMark({3, 77, 123456});
        const auto m = rec::decodeCameraMark(1, 3, 77, mb.constData(), size_t(mb.size()));
        QVERIFY(m);
        QCOMPARE(m->mediaUs, qint64(123456));
    }

    // A newer writer appends a field to every track; this reader still reads the fields it knows.
    void tracksWithAppendedFieldsStillRead()
    {
        QByteArray b;
        rec::ByteWriter w(b);
        w.put<quint64>(5);
        w.put<quint32>(2);
        w.put<quint16>(29 + 8);
        for (int i = 0; i < 2; ++i) {
            w.put<quint32>(quint32(40 + i));
            for (float v : {1.f, 2.f, 3.f, 4.f, 5.f, 6.f})
                w.put<float>(v + float(i));
            w.put<quint8>(2);
            w.put<double>(42.0); // the new field
        }
        const auto t = rec::decodeTracks(2, 0, b.constData(), size_t(b.size()));
        QVERIFY(t);
        QCOMPARE(t->tracks.size(), size_t(2));
        QCOMPARE(t->tracks[1].id, 41u);
        QCOMPARE(t->tracks[1].width, 7.0f);
        QCOMPARE(t->tracks[1].cls, quint8(2));
    }

    void decodersRejectGarbage()
    {
        std::mt19937 rng(7);
        const QByteArray valid[] = {rec::encodeLidar(frame(0, 1, 1, 50), rec::LidarEncoding::Int16Planar),
                                    rec::encodeTracks(*tracks(1, 1)), rec::encodeEvents(*events(1, 1))};
        int accepted = 0;
        for (int i = 0; i < 20'000; ++i) {
            QByteArray b = valid[i % 3];
            if (i % 2)
                b.truncate(qsizetype(rng() % quint32(b.size())));
            else
                for (int k = 0; k < 4; ++k)
                    b[qsizetype(rng() % quint32(b.size()))] = char(rng());
            switch (i % 3) {
            case 0: accepted += rec::decodeLidar(3, 0, 0, b.constData(), size_t(b.size())).has_value(); break;
            case 1: accepted += rec::decodeTracks(1, 0, b.constData(), size_t(b.size())) != nullptr; break;
            case 2: accepted += rec::decodeEvents(1, b.constData(), size_t(b.size())) != nullptr; break;
            }
        }
        // No crash is the test; flipped bytes in point data are still a valid frame.
        QVERIFY(accepted < 20'000);
    }

    void fileRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("a.qprec");
        rec::WriterOptions opt;
        opt.chunkBytes = 64 << 10;
        opt.info["test"] = "roundtrip";
        rec::Writer w(path, opt);
        QVERIFY(w.start(nullptr));
        Script script;
        script.write(w);
        w.stop();
        const rec::WriterStats st = w.stats();
        QVERIFY(st.chunks > 3);
        QVERIFY(st.outOfOrder > 0); // sensor 1 arrives 70 ms behind
        QCOMPARE(st.droppedRecords, qint64(0));

        FileSource src;
        QString error;
        QVERIFY2(src.open(path, &error), qPrintable(error));
        QVERIFY(!src.reader().info().recovered);
        QCOMPARE(src.reader().info().info.value("test").toString(), QStringLiteral("roundtrip"));
        QCOMPARE(src.reader().info().unknownRecords, qint64(0));
        QVERIFY(src.reader().info().indexBytes < src.reader().info().fileBytes / 50);

        const auto &streams = src.reader().streams();
        QCOMPARE(streams.at({rec::Type::Tracks, 0}).size(), size_t(script.trackSets));
        QCOMPARE(streams.at({rec::Type::Events, 0}).size(), size_t(script.eventBatches));
        QCOMPARE(streams.at({rec::Type::CameraMark, 0}).size(), size_t(script.marks));
        for (const auto &[key, refs] : streams)
            for (size_t i = 1; i < refs.size(); ++i)
                QVERIFY(refs[i - 1].t <= refs[i].t);

        // Every lidar frame comes back at its own time, within 1 cm quantisation.
        for (const qp::SensorFrame &f : script.lidar) {
            const auto s = src.latestIn({rec::Type::Lidar, f.sensorId}, f.captureNs - 1, f.captureNs);
            QVERIFY(s);
            QCOMPARE(s->lidar.seq, f.seq);
            QVERIFY(maxError(*f.cloud, *s->lidar.cloud) <= 0.005f + 2e-5f);
        }
        const auto last = src.latestIn({rec::Type::Tracks, 0}, std::numeric_limits<qint64>::min(),
                                       std::numeric_limits<qint64>::max());
        QVERIFY(last);
        QCOMPARE(last->tracks->seq, quint64(script.trackSets - 1));
        std::vector<Sample> ev;
        src.allIn({rec::Type::Events, 0}, src.startNs() - 1, src.endNs(), ev);
        QCOMPARE(ev.size(), size_t(script.eventBatches));
        QCOMPARE(src.nextAfter({rec::Type::Lidar, 0}, 1000 * ms), std::optional<qint64>(1099 * ms));
    }

    // The event backlog after a seek must not decompress lidar.
    void smallStreamsLiveInTheirOwnChunks()
    {
        QTemporaryDir dir;
        qint64 reads[2] = {0, 0};
        for (const bool separate : {false, true}) {
            const QString path = dir.filePath(separate ? "s.qprec" : "m.qprec");
            rec::WriterOptions opt;
            opt.chunkBytes = 64 << 10;
            opt.separateSmall = separate;
            {
                rec::Writer w(path, opt);
                QVERIFY(w.start(nullptr));
                Script().write(w);
                w.stop();
            }
            FileSource src;
            QVERIFY(src.open(path, nullptr));
            std::vector<Sample> ev;
            src.allIn({rec::Type::Events, 0}, src.startNs() - 1, src.endNs(), ev);
            QCOMPARE(ev.size(), size_t(30));
            reads[separate] = src.reader().chunkReads();
        }
        QVERIFY2(reads[1] * 5 < reads[0], qPrintable(QStringLiteral("%1 vs %2").arg(reads[1]).arg(reads[0])));
    }

    void float32FileIsExact()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("f.qprec");
        rec::WriterOptions opt;
        opt.lidar = rec::LidarEncoding::Float32;
        opt.zstdLevel = 0;
        rec::Writer w(path, opt);
        QVERIFY(w.start(nullptr));
        const qp::SensorFrame f = frame(1, 3, 5 * ms, 20'000);
        w.addLidar(f);
        w.stop();
        FileSource src;
        QVERIFY(src.open(path, nullptr));
        const auto s = src.latestIn({rec::Type::Lidar, 1}, 0, 10 * ms);
        QVERIFY(s);
        QVERIFY(s->lidar.cloud->xyz == f.cloud->xyz);
    }

    void cutFilesRecoverEveryCompleteChunk()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("c.qprec");
        rec::WriterOptions opt;
        opt.chunkBytes = 32 << 10;
        {
            rec::Writer w(path, opt);
            QVERIFY(w.start(nullptr));
            Script().write(w);
            w.stop();
        }
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray whole = f.readAll();
        f.close();
        FileSource full;
        QVERIFY(full.open(path, nullptr));
        size_t total = 0;
        for (const auto &[k, refs] : full.reader().streams())
            total += refs.size();

        std::mt19937 rng(3);
        size_t previous = 0;
        for (int cut = 0; cut < 25; ++cut) {
            const qsizetype at = 100 + qsizetype(rng() % quint32(whole.size() - 100));
            const QString cutPath = dir.filePath(QStringLiteral("cut%1.qprec").arg(cut));
            QFile out(cutPath);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(whole.left(at));
            out.close();
            FileSource src;
            QString error;
            QVERIFY2(src.open(cutPath, &error), qPrintable(error));
            QVERIFY(src.reader().info().recovered);
            size_t n = 0;
            for (const auto &[k, refs] : src.reader().streams()) {
                n += refs.size();
                for (const rec::RecordRef &r : refs) // every indexed record is readable
                    QVERIFY(src.reader().read(r).has_value());
            }
            QVERIFY(n <= total);
            QVERIFY(src.reader().info().badChunks == 0);
            previous = std::max(previous, n);
        }
        QVERIFY(previous > 0);
    }

    void corruptChunkIsSkipped()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("x.qprec");
        rec::WriterOptions opt;
        opt.chunkBytes = 32 << 10;
        opt.zstdLevel = 0;
        {
            rec::Writer w(path, opt);
            QVERIFY(w.start(nullptr));
            Script().write(w);
            w.stop();
        }
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QByteArray all = f.readAll();
        const qsizetype second = all.indexOf("QPCK", all.indexOf("QPCK") + 4);
        QVERIFY(second > 0);
        all[second + 44 + 100] = char(all[second + 44 + 100] ^ 0x5A); // inside its payload
        const qsizetype trailer = all.size() - 20;
        all.truncate(trailer); // and lose the index, as if the writer died
        f.seek(0);
        f.write(all);
        f.resize(all.size());
        f.close();

        FileSource src;
        QVERIFY(src.open(path, nullptr));
        QVERIFY(src.reader().info().recovered);
        QCOMPARE(src.reader().info().badChunks, qint64(1));
        QVERIFY(src.reader().info().recoveredChunks >= 3);
    }

    void damagedChunkHeaderIsSkippedByResync()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("h.qprec");
        rec::WriterOptions opt;
        opt.chunkBytes = 32 << 10;
        {
            rec::Writer w(path, opt);
            QVERIFY(w.start(nullptr));
            Script().write(w);
            w.stop();
        }
        FileSource intact;
        QVERIFY(intact.open(path, nullptr));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QByteArray all = f.readAll();
        const qsizetype second = all.indexOf("QPCK", all.indexOf("QPCK") + 4);
        all[second + 6] = char(all[second + 6] ^ 0x01); // its stored size: now a lie
        all.truncate(all.size() - 20);
        f.seek(0);
        f.write(all);
        f.resize(all.size());
        f.close();
        FileSource src;
        QVERIFY(src.open(path, nullptr));
        QCOMPARE(src.reader().info().badChunks, qint64(1));
        qint64 chunksAfter = 0;
        for (qsizetype at = all.indexOf("QPCK", second + 4); at >= 0; at = all.indexOf("QPCK", at + 4))
            ++chunksAfter;
        QCOMPARE(src.reader().info().recoveredChunks, 1 + chunksAfter);
    }

    void unknownRecordsAreSkipped()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("u.qprec");
        rec::Writer w(path, {});
        QVERIFY(w.start(nullptr));
        w.addTracks(tracks(1, 10 * ms));
        w.addRaw(rec::Type(99), 1, 0, 11 * ms, QByteArray("from the future"));
        w.addRaw(rec::Type::Lidar, 9, 0, 12 * ms, QByteArray(64, 'x')); // a lidar layout we don't know
        w.addTracks(tracks(2, 13 * ms));
        w.stop();
        FileSource src;
        QVERIFY(src.open(path, nullptr));
        QCOMPARE(src.reader().info().unknownRecords, qint64(2));
        QCOMPARE(src.reader().streams().at({rec::Type::Tracks, 0}).size(), size_t(2));
        QVERIFY(!src.reader().streams().contains({rec::Type::Lidar, 0}));
    }

    void newerMajorVersionIsRefused()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("v.qprec");
        {
            rec::Writer w(path, {});
            QVERIFY(w.start(nullptr));
            w.addTracks(tracks(1, 10));
            w.stop();
        }
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        f.seek(8);
        const quint16 major = 2;
        f.write(reinterpret_cast<const char *>(&major), 2);
        f.close();
        FileSource src;
        QString error;
        QVERIFY(!src.open(path, &error));
        QVERIFY2(error.contains("newer"), qPrintable(error));
    }

    void fullQueueDropsAndLeavesAGap()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("g.qprec");
        rec::WriterOptions opt;
        opt.queueBytes = 4 << 20; // three frames of 100k points
        opt.zstdLevel = 9;        // slow, so the queue fills
        std::vector<qp::SensorFrame> frames;
        for (int i = 0; i < 60; ++i)
            frames.push_back(frame(0, quint64(i), (i + 1) * 100 * ms, 100'000));
        rec::Writer w(path, opt);
        QVERIFY(w.start(nullptr));
        for (const qp::SensorFrame &f : frames)
            w.addLidar(f);
        w.addTracks(tracks(1, 7000 * ms));
        w.stop();
        const rec::WriterStats st = w.stats();
        QVERIFY(st.droppedRecords > 0);
        FileSource src;
        QVERIFY(src.open(path, nullptr));
        QVERIFY(src.reader().streams().contains({rec::Type::Gap, 0}));
        QCOMPARE(qint64(src.reader().streams().at({rec::Type::Lidar, 0}).size()), 60 - st.droppedRecords);
    }

    void historyKeepsTheNewestWithinBudget()
    {
        const qint64 frameBytes = 2000 * 12;
        History h(10 * frameBytes + 5000, History::Shape::Shared);
        for (int i = 0; i < 30; ++i)
            h.addLidar(frame(0, quint64(i), i * 100 * ms));
        QVERIFY(h.bytes() <= h.budget());
        QVERIFY(h.evicted() >= 19);
        QCOMPARE(h.endNs(), 2900 * ms);
        QVERIFY(h.startNs() >= 1900 * ms && h.startNs() <= 2100 * ms);
        const auto s = h.latestIn({rec::Type::Lidar, 0}, 0, 2550 * ms);
        QVERIFY(s);
        QCOMPARE(s->lidar.seq, quint64(25));
        QVERIFY(!h.latestIn({rec::Type::Lidar, 0}, 2500 * ms, 2550 * ms)); // nothing in (2500, 2550]
        QCOMPARE(h.nextAfter({rec::Type::Lidar, 0}, 2500 * ms), std::optional<qint64>(2600 * ms));
    }

    void historyOrdersLateArrivals()
    {
        History h(1 << 30, History::Shape::Shared);
        for (qint64 t : {100, 300, 200, 50, 400})
            h.addTracks(tracks(quint64(t), t * ms));
        std::vector<Sample> out;
        h.allIn({rec::Type::Tracks, 0}, 0, 1000 * ms, out);
        QCOMPARE(out.size(), size_t(5));
        for (size_t i = 1; i < out.size(); ++i)
            QVERIFY(out[i - 1].t < out[i].t);
    }

    void historyInt16HalvesTheBytes()
    {
        History shared(1 << 30, History::Shape::Shared);
        History packed(1 << 30, History::Shape::Int16);
        const qp::SensorFrame f = frame(0, 1, 10 * ms, 50'000);
        shared.addLidar(f);
        packed.addLidar(f);
        QVERIFY(packed.bytes() < shared.bytes() * 0.51);
        const auto s = packed.latestIn({rec::Type::Lidar, 0}, 0, 10 * ms);
        QVERIFY(s);
        QVERIFY2(maxError(*f.cloud, *s->lidar.cloud) <= 0.005f + 2e-5f, qPrintable(QString::number(maxError(*f.cloud, *s->lidar.cloud)) + " " + QString::number(s->lidar.cloud->count())));
    }
};

QTEST_GUILESS_MAIN(TstRecording)
#include "tst_recording.moc"
