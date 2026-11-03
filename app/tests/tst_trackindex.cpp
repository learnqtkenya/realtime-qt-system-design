#include "TrackIndex.h"

#include <QtTest>

#include <cmath>
#include <numbers>
#include <random>

namespace {

qp::Track track(quint32 id, float x, float y, float headingDeg, float length = 4, float width = 2, quint8 cls = 0)
{
    qp::Track t;
    t.id = id;
    t.x = x;
    t.y = y;
    t.heading = headingDeg * std::numbers::pi_v<float> / 180.0f;
    t.length = length;
    t.width = width;
    t.cls = cls;
    return t;
}

} // namespace

class TstTrackIndex : public QObject
{
    Q_OBJECT
private slots:
    void pointInsideAxisAlignedBox()
    {
        TrackIndex idx;
        idx.rebuild({track(7, 10, 5, 0)});
        QCOMPARE(idx.pickPoint(11.9f, 5.9f), 7);
        QCOMPARE(idx.pickPoint(12.1f, 5.0f), -1);
        QCOMPARE(idx.pickPoint(10.0f, 6.1f), -1);
    }

    void pointInsideRotatedBox()
    {
        TrackIndex idx;
        idx.rebuild({track(3, 0, 0, 90)}); // long axis along y
        QCOMPARE(idx.pickPoint(0.0f, 1.9f), 3);
        QCOMPARE(idx.pickPoint(1.9f, 0.0f), -1);
    }

    void overlappingBoxesPickNearestCentre()
    {
        TrackIndex idx;
        idx.rebuild({track(1, 0, 0, 0, 10, 10), track(2, 1, 0, 0, 10, 10)});
        QCOMPARE(idx.pickPoint(0.8f, 0.0f), 2);
        QCOMPARE(idx.pickPoint(0.2f, 0.0f), 1);
    }

    void boxSpanningCellsIsFoundInEach()
    {
        TrackIndex idx(4.0f);
        idx.rebuild({track(9, 4.0f, 4.0f, 45, 9, 2)}); // straddles four cells
        QCOMPARE(idx.pickPoint(4.0f + 3.0f * std::cos(0.785f), 4.0f + 3.0f * std::sin(0.785f)), 9);
        QCOMPARE(idx.pickPoint(4.0f - 3.0f * std::cos(0.785f), 4.0f - 3.0f * std::sin(0.785f)), 9);
    }

    void rayStraightDownHitsTopFace()
    {
        TrackIndex idx;
        idx.rebuild({track(5, 20, -10, 30)});
        float t = 0;
        QCOMPARE(idx.pickRay({20, -10, 50}, {0, 0, -1}, &t), 5);
        QVERIFY(std::abs(t - (50 - (TrackIndex::kGroundZ + TrackIndex::heightFor(0)))) < 1e-4f);
    }

    void rayPicksTheNearerOfTwo()
    {
        TrackIndex idx;
        idx.rebuild({track(1, 10, 0, 0), track(2, 20, 0, 0)});
        QCOMPARE(idx.pickRay({0, 0, -1}, {1, 0, 0}), 1);
        QCOMPARE(idx.pickRay({40, 0, -1}, {-1, 0, 0}), 2);
    }

    void rayAboveEverythingMisses()
    {
        TrackIndex idx;
        idx.rebuild({track(1, 0, 0, 0, 4, 2, 1)}); // a truck
        QCOMPARE(idx.pickRay({-50, 0, 5}, {1, 0, 0}), -1);
        QCOMPARE(idx.pickRay({-50, 0, 1.39f}, {1, 0, 0}), 1); // truck top is at 1.4
    }

    void rayFromOutsideTheGrid()
    {
        TrackIndex idx(4.0f, 64.0f);
        idx.rebuild({track(4, 60, 60, 0)});
        QCOMPARE(idx.pickRay({200, 200, 30}, QVector3D(-140, -140, -31).normalized()), 4);
    }

    void emptyIndex()
    {
        TrackIndex idx;
        idx.rebuild({});
        QCOMPARE(idx.pickPoint(0, 0), -1);
        QCOMPARE(idx.pickRay({0, 0, 10}, {0, 0, -1}), -1);
    }

    // The grid must give exactly the brute-force answer.
    void gridMatchesBruteForce_data()
    {
        QTest::addColumn<int>("objects");
        QTest::addColumn<float>("cell");
        QTest::newRow("50 objects, 4 m cells") << 50 << 4.0f;
        QTest::newRow("800 objects, 4 m cells") << 800 << 4.0f;
        QTest::newRow("800 objects, 1 m cells") << 800 << 1.0f;
        QTest::newRow("2000 objects, 8 m cells") << 2000 << 8.0f;
    }

    void gridMatchesBruteForce()
    {
        QFETCH(int, objects);
        QFETCH(float, cell);
        std::mt19937 rng(1234u + uint32_t(objects));
        std::uniform_real_distribution<float> pos(-55, 55), ang(0, 360), len(0.5f, 9), wid(0.5f, 2.6f);
        std::vector<qp::Track> tracks;
        for (int i = 0; i < objects; ++i)
            tracks.push_back(track(quint32(1000 + i), pos(rng), pos(rng), ang(rng), len(rng), wid(rng),
                                   quint8(rng() % 4)));
        TrackIndex idx(cell);
        idx.rebuild(tracks);

        int pointHits = 0, rayHits = 0;
        for (int i = 0; i < 20000; ++i) {
            const float x = pos(rng) * 1.2f, y = pos(rng) * 1.2f;
            const int a = idx.pickPoint(x, y), b = idx.pickPointBrute(x, y);
            QVERIFY2(a == b, qPrintable(QString("point %1,%2: grid %3 brute %4").arg(x).arg(y).arg(a).arg(b)));
            pointHits += a >= 0;
        }
        std::uniform_real_distribution<float> unit(-1, 1), height(-1, 60);
        for (int i = 0; i < 20000; ++i) {
            const QVector3D o(pos(rng) * 2.5f, pos(rng) * 2.5f, height(rng));
            QVector3D target(pos(rng), pos(rng), TrackIndex::kGroundZ + 3.0f * (unit(rng) + 1) / 2);
            const QVector3D d = (i % 7 == 0) ? QVector3D(unit(rng), unit(rng), unit(rng)).normalized()
                                             : (target - o).normalized();
            float ta = 0, tb = 0;
            const int a = idx.pickRay(o, d, &ta), b = idx.pickRayBrute(o, d, &tb);
            QVERIFY2(a == b, qPrintable(QString("ray %1: grid %2 brute %3").arg(i).arg(a).arg(b)));
            if (a >= 0)
                QVERIFY(std::abs(ta - tb) < 1e-4f);
            rayHits += a >= 0;
        }
        // The test must actually exercise hits, not only misses.
        QVERIFY2(pointHits > 100, qPrintable(QString::number(pointHits)));
        QVERIFY2(rayHits > 100, qPrintable(QString::number(rayHits)));
    }

    void pickCost()
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> pos(-55, 55), ang(0, 360);
        std::vector<qp::Track> tracks;
        for (int i = 0; i < 2000; ++i)
            tracks.push_back(track(quint32(i), pos(rng), pos(rng), ang(rng)));
        TrackIndex idx;
        idx.rebuild(tracks);
        QBENCHMARK {
            idx.pickRay({pos(rng), pos(rng), 40}, QVector3D(pos(rng) * 0.3f, pos(rng) * 0.3f, -40).normalized());
        }
    }
};

QTEST_APPLESS_MAIN(TstTrackIndex)
#include "tst_trackindex.moc"
