#include "qp/SensorSimulator.h"

#include <QtTest>

#include <cmath>

class TstSimulator : public QObject
{
    Q_OBJECT
private slots:
    void cloudHasRequestedPointCount()
    {
        qp::Workload w;
        w.pointsPerFrame = 12'345;
        const auto c = qp::SensorSimulator::makeCloud(w, 0, 0);
        QCOMPARE(c->count(), 12'345);
        QCOMPARE(c->xyz.size(), size_t(12'345) * 3);
    }

    void cloudIsDeterministic()
    {
        qp::Workload w;
        w.pointsPerFrame = 5000;
        const auto a = qp::SensorSimulator::makeCloud(w, 2, 1);
        const auto b = qp::SensorSimulator::makeCloud(w, 2, 1);
        QVERIFY(a->xyz == b->xyz);
    }

    void differentSeedsDiffer()
    {
        qp::Workload w;
        w.pointsPerFrame = 5000;
        const auto a = qp::SensorSimulator::makeCloud(w, 0, 0);
        w.seed = 2;
        const auto b = qp::SensorSimulator::makeCloud(w, 0, 0);
        QVERIFY(a->xyz != b->xyz);
    }

    void sensorsAndVariantsDiffer()
    {
        qp::Workload w;
        w.pointsPerFrame = 5000;
        QVERIFY(qp::SensorSimulator::makeCloud(w, 0, 0)->xyz
                != qp::SensorSimulator::makeCloud(w, 1, 0)->xyz);
        QVERIFY(qp::SensorSimulator::makeCloud(w, 0, 0)->xyz
                != qp::SensorSimulator::makeCloud(w, 0, 1)->xyz);
    }

    void pointsStayInsideTheWorld()
    {
        qp::Workload w;
        w.pointsPerFrame = 20'000;
        const auto c = qp::SensorSimulator::makeCloud(w, 3, 2);
        for (int i = 0; i < c->count(); ++i) {
            const float x = c->xyz[size_t(3 * i)];
            const float y = c->xyz[size_t(3 * i + 1)];
            const float z = c->xyz[size_t(3 * i + 2)];
            QVERIFY(std::isfinite(x) && std::isfinite(y) && std::isfinite(z));
            QVERIFY(std::abs(x) < 70.f && std::abs(y) < 70.f);
            QVERIFY(z > -2.5f && z < 3.5f);
        }
    }

    void zeroPointsIsAllowed()
    {
        qp::Workload w;
        w.pointsPerFrame = 0;
        QCOMPARE(qp::SensorSimulator::makeCloud(w, 0, 0)->count(), 0);
    }

    void emitsAtTheRequestedRates()
    {
        qp::Workload w;
        w.sensors = 2;
        w.pointsPerFrame = 1000;
        w.lidarRateHz = 20;
        w.objects = 10;
        w.trackRateHz = 10;
        w.eventsPerSecond = 100;

        qp::SensorSimulator sim(w);
        QSignalSpy lidar(&sim, &qp::SensorSimulator::lidarFrame);
        QSignalSpy tracks(&sim, &qp::SensorSimulator::trackSet);
        QSignalSpy events(&sim, &qp::SensorSimulator::eventBatch);
        sim.start();
        QTest::qWait(2000);
        sim.stop();

        // 2 sensors * 20 Hz * 2 s = 80, allow start-up and scheduling slack.
        QVERIFY2(lidar.count() >= 60 && lidar.count() <= 84, qPrintable(QString::number(lidar.count())));
        QVERIFY2(tracks.count() >= 14 && tracks.count() <= 22, qPrintable(QString::number(tracks.count())));

        quint64 eventTotal = 0;
        for (const auto &args : events)
            eventTotal += args.at(0).value<qp::EventBatchPtr>()->size();
        QVERIFY2(eventTotal >= 150 && eventTotal <= 210, qPrintable(QString::number(eventTotal)));
    }

    void trackSetsContainEveryObject()
    {
        qp::Workload w;
        w.objects = 37;
        w.pointsPerFrame = 100;
        qp::SensorSimulator sim(w);
        QSignalSpy tracks(&sim, &qp::SensorSimulator::trackSet);
        sim.start();
        QVERIFY(tracks.wait(2000));
        sim.stop();
        const auto set = tracks.first().at(0).value<qp::TrackSetPtr>();
        QCOMPARE(set->tracks.size(), size_t(37));
    }

    void workloadCanChangeWhileRunning()
    {
        qp::Workload w;
        w.sensors = 2;
        w.pointsPerFrame = 1000;
        w.lidarRateHz = 10;
        w.objects = 5;
        qp::SensorSimulator sim(w);
        QSignalSpy lidar(&sim, &qp::SensorSimulator::lidarFrame);
        QSignalSpy tracks(&sim, &qp::SensorSimulator::trackSet);
        sim.start();
        QTRY_VERIFY_WITH_TIMEOUT(lidar.count() >= 4, 2000);

        w.pointsPerFrame = 3000;
        w.lidarRateHz = 40;
        w.objects = 12;
        sim.setWorkload(w);
        QTest::qWait(300); // let the rebuild finish
        lidar.clear();
        tracks.clear();
        QTest::qWait(1000);
        sim.stop();

        // 2 sensors * 40 Hz * 1 s = 80
        QVERIFY2(lidar.count() >= 64 && lidar.count() <= 84, qPrintable(QString::number(lidar.count())));
        for (const auto &args : lidar)
            QCOMPARE(args.at(0).value<qp::SensorFrame>().cloud->count(), 3000);
        QVERIFY(!tracks.isEmpty());
        QCOMPARE(tracks.last().at(0).value<qp::TrackSetPtr>()->tracks.size(), size_t(12));
    }

    void repeatedWorkloadChangesAreSafe()
    {
        qp::Workload w;
        w.sensors = 4;
        w.pointsPerFrame = 500;
        w.lidarRateHz = 50;
        qp::SensorSimulator sim(w);
        QSignalSpy lidar(&sim, &qp::SensorSimulator::lidarFrame);
        sim.start();
        for (int i = 0; i < 40; ++i) { // faster than the stagger timers fire
            w.pointsPerFrame = 500 + 100 * (i % 5);
            w.lidarRateHz = (i % 2) ? 50 : 25;
            sim.setWorkload(w);
            QTest::qWait(5);
        }
        lidar.clear();
        QTest::qWait(500);
        sim.stop();
        QVERIFY(lidar.count() > 20);
        for (const auto &args : lidar)
            QCOMPARE(args.at(0).value<qp::SensorFrame>().cloud->count(), w.pointsPerFrame);
    }

    void captureTimestampsAreMonotonic()
    {
        qp::Workload w;
        w.sensors = 1;
        w.pointsPerFrame = 100;
        w.lidarRateHz = 50;
        qp::SensorSimulator sim(w);
        QSignalSpy lidar(&sim, &qp::SensorSimulator::lidarFrame);
        sim.start();
        QTest::qWait(500);
        sim.stop();
        QVERIFY(lidar.count() > 5);
        int64_t prev = 0;
        quint64 prevSeq = 0;
        for (int i = 0; i < lidar.count(); ++i) {
            const auto f = lidar.at(i).at(0).value<qp::SensorFrame>();
            QVERIFY(f.captureNs > prev);
            if (i > 0)
                QCOMPARE(f.seq, prevSeq + 1);
            prev = f.captureNs;
            prevSeq = f.seq;
        }
    }
};

QTEST_MAIN(TstSimulator)
#include "tst_simulator.moc"
