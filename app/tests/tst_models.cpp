#include "EventLogModel.h"
#include "Proxies.h"
#include "TrackListModel.h"

#include <QAbstractItemModelTester>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>

#include <cmath>
#include <limits>
#include <random>

namespace {

qp::TrackSetPtr makeSet(const std::vector<quint32> &ids, float shift)
{
    auto s = std::make_shared<qp::TrackSet>();
    for (quint32 id : ids)
        s->tracks.push_back({id, float(id) + shift, -float(id), 0.1f * float(id), 0.5f * float(id) + shift, 4.5f, 1.9f, quint8(id % 4)});
    return s;
}

} // namespace

class TstModels : public QObject
{
    Q_OBJECT

private slots:
    void diffMatchesTheNewSetAndKeepsRows()
    {
        TrackListModel m(nullptr);
        m.setUpdates(TrackListModel::Updates::Diff);
        QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
        TrackTableProxy proxy(&m);
        QAbstractItemModelTester proxyTester(&proxy, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy resets(&m, &QAbstractItemModel::modelReset);
        std::mt19937 rng(5);
        std::vector<quint32> ids;
        for (quint32 i = 0; i < 50; ++i)
            ids.push_back(1000 + i);
        for (int round = 0; round < 300; ++round) {
            // Remove a few, add a few, move everyone.
            std::shuffle(ids.begin(), ids.end(), rng);
            ids.resize(ids.size() - std::min<size_t>(ids.size(), rng() % 4));
            for (int k = int(rng() % 4); k > 0; --k)
                ids.push_back(2000 + quint32(round * 4 + k));
            const auto set = makeSet(ids, float(round));
            m.apply(set);
            QCOMPARE(m.rowCount(), int(ids.size()));
            std::set<quint32> want(ids.begin(), ids.end()), got;
            for (const qp::Track &t : m.rows()) {
                got.insert(t.id);
                QCOMPARE(t.x, float(t.id) + float(round));
            }
            QCOMPARE(got, want);
            QCOMPARE(proxy.rowCount(), int(ids.size()));
        }
        QCOMPARE(resets.count(), 0);
    }

    void unchangedSetEmitsNothing()
    {
        TrackListModel m(nullptr);
        m.setUpdates(TrackListModel::Updates::Diff);
        m.apply(makeSet({1, 2, 3}, 0));
        QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
        QSignalSpy inserted(&m, &QAbstractItemModel::rowsInserted);
        m.apply(makeSet({1, 2, 3}, 0));
        QCOMPARE(changed.count(), 0);
        QCOMPARE(inserted.count(), 0);
    }

    void proxySortsAndFilters()
    {
        TrackListModel m(nullptr);
        m.setUpdates(TrackListModel::Updates::Diff);
        TrackTableProxy proxy(&m);
        m.apply(makeSet({7, 3, 9, 4, 8}, 0));
        proxy.setSortKey(QStringLiteral("speed"));
        proxy.setDescending(true);
        QCOMPARE(proxy.index(0, 0).data(TrackListModel::TrackIdRole).toUInt(), 9u);
        proxy.setClassFilter(0); // ids divisible by 4
        QCOMPARE(proxy.rowCount(), 2);
        QCOMPARE(proxy.index(0, 0).data(TrackListModel::TrackIdRole).toUInt(), 8u);
        // Live data keeps the order: 3 becomes fastest.
        auto s = makeSet({7, 3, 9, 4, 8}, 0);
        std::const_pointer_cast<qp::TrackSet>(s)->tracks[1].speed = 100;
        std::const_pointer_cast<qp::TrackSet>(s)->tracks[1].cls = 0;
        m.apply(s);
        QCOMPARE(proxy.index(0, 0).data(TrackListModel::TrackIdRole).toUInt(), 3u);
    }

    // Without validation: does one NaN row break the table's order?
    void nanRowAndSortOrder()
    {
        TrackListModel m(nullptr);
        m.setUpdates(TrackListModel::Updates::Diff);
        TrackTableProxy proxy(&m);
        proxy.setSortKey(QStringLiteral("distance"));
        auto s = std::make_shared<qp::TrackSet>();
        std::mt19937 rng(11);
        for (quint32 i = 0; i < 200; ++i)
            s->tracks.push_back({1000 + i, float(rng() % 1000) / 10.f, float(rng() % 1000) / 10.f, 0, 1, 4, 2, 0});
        s->tracks[57].x = std::numeric_limits<float>::quiet_NaN();
        m.apply(s);
        int inversions = 0, nanAt = -1;
        double prev = -1;
        for (int r = 0; r < proxy.rowCount(); ++r) {
            const double d = proxy.index(r, 0).data(TrackListModel::DistanceRole).toDouble();
            if (std::isnan(d)) {
                nanAt = r;
                continue;
            }
            inversions += d < prev;
            prev = d;
        }
        qInfo("one NaN among 200 rows: it sorted to row %d; %d out-of-order neighbours among the rest", nanAt, inversions);
        QCOMPARE(proxy.rowCount(), 200);
    }

    void boundedLogInsertsAndTrims()
    {
        EventLogModel m;
        m.setUpdates(EventLogModel::Updates::Diff, 100);
        QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
        EventFilterProxy proxy(&m);
        QAbstractItemModelTester proxyTester(&proxy, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy resets(&m, &QAbstractItemModel::modelReset);
        quint64 seq = 0;
        for (int b = 0; b < 40; ++b) {
            qp::EventBatch batch;
            for (int i = 0; i < 7; ++i)
                batch.push_back({seq++, 0, quint8(seq % 3), QStringLiteral("src"), QStringLiteral("msg %1").arg(seq)});
            m.appendBatch(batch);
        }
        QCOMPARE(m.rowCount(), 100);
        QCOMPARE(m.index(99).data(EventLogModel::SeqRole).toULongLong(), seq - 1);
        QCOMPARE(m.index(0).data(EventLogModel::SeqRole).toULongLong(), seq - 100);
        QCOMPARE(resets.count(), 0);
        proxy.setSeverities(0b100);
        for (int r = 0; r < proxy.rowCount(); ++r)
            QCOMPARE(proxy.index(r, 0).data(EventLogModel::SeverityRole).toInt(), 2);
        proxy.setSeverities(0b111);
        proxy.setText(QStringLiteral("MSG 275"));
        QCOMPARE(proxy.rowCount(), 1);
    }

    // How a filter change reaches views: count the signals, and time the change.
    void filterChangeSignals()
    {
        EventLogModel m;
        m.setUpdates(EventLogModel::Updates::Diff, 10'000);
        EventFilterProxy proxy(&m);
        const char *msgs[] = {"frame received", "track confirmed", "calibration ok", "clock synchronised",
                              "buffer swapped", "frame arrived late", "low point density", "sensor timeout"};
        qp::EventBatch batch;
        for (int i = 0; i < 10'000; ++i)
            batch.push_back({quint64(i), 0, 0, QStringLiteral("lidar0"), QString::fromLatin1(msgs[i % 8])});
        m.appendBatch(batch);
        QSignalSpy removed(&proxy, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&proxy, &QAbstractItemModel::rowsInserted);
        QElapsedTimer t;
        t.start();
        proxy.setText(QStringLiteral("late"));
        const double onMs = double(t.nsecsElapsed()) / 1e6;
        QCOMPARE(proxy.rowCount(), 1250);
        const qsizetype removals = removed.count();
        t.restart();
        proxy.setText(QString());
        const double offMs = double(t.nsecsElapsed()) / 1e6;
        qInfo("filter on: %lld rowsRemoved signals, %.2f ms; off: %lld rowsInserted signals, %.2f ms",
              qint64(removals), onMs, qint64(inserted.count()), offMs);
        QVERIFY(removals > 1000); // one per gap: the change is fragmented

        EventFilterModel index(&m);
        QAbstractItemModelTester tester(&index, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy resets(&index, &QAbstractItemModel::modelReset);
        QSignalSpy indexRemoved(&index, &QAbstractItemModel::rowsRemoved);
        t.restart();
        index.setText(QStringLiteral("late"));
        qInfo("index model: filter on %.2f ms, %lld reset, %lld removals", double(t.nsecsElapsed()) / 1e6,
              qint64(resets.count()), qint64(indexRemoved.count()));
        QCOMPARE(index.rowCount(), 1250);
        QCOMPARE(resets.count(), 1);
        QCOMPARE(indexRemoved.count(), 0);
    }

    void indexModelFollowsInsertsAndTrims()
    {
        EventLogModel m;
        m.setUpdates(EventLogModel::Updates::Diff, 50);
        EventFilterModel index(&m);
        QAbstractItemModelTester tester(&index, QAbstractItemModelTester::FailureReportingMode::QtTest);
        index.setSeverities(0b100);
        quint64 seq = 0;
        for (int b = 0; b < 30; ++b) {
            qp::EventBatch batch;
            for (int i = 0; i < 7; ++i, ++seq)
                batch.push_back({seq, 0, quint8(seq % 3), QStringLiteral("s"), QStringLiteral("m")});
            m.appendBatch(batch);
            // Exactly the severity-2 events among the newest 50.
            int want = 0;
            for (int r = 0; r < m.rowCount(); ++r)
                want += m.at(r).severity == 2;
            QCOMPARE(index.rowCount(), want);
            for (int r = 0; r < index.rowCount(); ++r)
                QCOMPARE(index.index(r).data(EventLogModel::SeverityRole).toInt(), 2);
        }
        QCOMPARE(index.index(index.rowCount() - 1).data(EventLogModel::SeqRole).toULongLong() % 3, 2ull);
    }
};

QTEST_GUILESS_MAIN(TstModels)
#include "tst_models.moc"
