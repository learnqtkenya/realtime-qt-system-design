#include "LidarPipeline.h"

#include "qp/SensorSimulator.h"

#include <QtTest>

#include <atomic>
#include <thread>

using Policy = LidarPipeline::Policy;
using Seq = std::vector<quint64>;

namespace {

qp::SensorFrame frame(int sensor, quint64 seq)
{
    qp::SensorFrame f;
    f.sensorId = sensor;
    f.seq = seq;
    f.captureNs = qp::nowNs();
    return f;
}

} // namespace

class TstPipeline : public QObject
{
    Q_OBJECT
private slots:
    void dropOldestKeepsTheNewest()
    {
        LidarPipeline p(1, Policy::DropOldest, 3, 0);
        for (quint64 s = 1; s <= 5; ++s)
            p.submit(frame(0, s));
        QCOMPARE(p.queuedSequence(0), (Seq{3, 4, 5}));
    }

    void dropNewestKeepsTheOldest()
    {
        LidarPipeline p(1, Policy::DropNewest, 3, 0);
        for (quint64 s = 1; s <= 5; ++s)
            p.submit(frame(0, s));
        QCOMPARE(p.queuedSequence(0), (Seq{1, 2, 3}));
    }

    void depthOneIsLatestValue()
    {
        LidarPipeline p(1, Policy::DropOldest, 1, 0);
        for (quint64 s = 1; s <= 5; ++s)
            p.submit(frame(0, s));
        QCOMPARE(p.queuedSequence(0), (Seq{5}));
    }

    void lanesAreIndependent()
    {
        LidarPipeline p(2, Policy::DropOldest, 1, 0);
        p.submit(frame(0, 1));
        p.submit(frame(1, 7));
        p.submit(frame(0, 2));
        QCOMPARE(p.queuedSequence(0), (Seq{2}));
        QCOMPARE(p.queuedSequence(1), (Seq{7}));
    }

    void invalidSensorIsIgnored()
    {
        LidarPipeline p(1, Policy::DropOldest, 2, 0);
        p.submit(frame(5, 1));
        p.submit(frame(-1, 1));
        QCOMPARE(p.queuedSequence(0), Seq{});
    }

    void blockWaitsUntilShutdown()
    {
        LidarPipeline p(1, Policy::Block, 1, 0);
        p.submit(frame(0, 1));
        std::atomic<bool> returned{false};
        std::thread producer([&] {
            p.submit(frame(0, 2));
            returned = true;
        });
        QTest::qWait(100);
        QVERIFY(!returned);
        p.shutdown();
        producer.join();
        QVERIFY(returned);
        QCOMPARE(p.queuedSequence(0), (Seq{1}));
    }

    void workersProduceOrderedLayers()
    {
        qp::Workload w;
        w.pointsPerFrame = 500;
        LidarPipeline p(2, Policy::Block, 4, 2);
        p.setViewport({200, 100, 2.0});
        std::atomic<int> ready{0};
        connect(&p, &LidarPipeline::layerReady, this, [&] { ++ready; }, Qt::DirectConnection);

        for (quint64 s = 0; s < 6; ++s) {
            qp::SensorFrame f = frame(int(s % 2), s);
            f.cloud = qp::SensorSimulator::makeCloud(w, f.sensorId, 0);
            p.submit(f);
        }
        QTRY_COMPARE_WITH_TIMEOUT(ready.load(), 6, 5000);

        for (int sensor = 0; sensor < 2; ++sensor) {
            const LidarPipeline::Layer layer = p.layer(sensor);
            QCOMPARE(layer.image.size(), QSize(200, 100));
            QVERIFY(layer.serial > 0);
        }
        // A lane is never processed by two workers at once, so its last frame wins.
        QVERIFY(p.layer(0).captureNs < p.layer(1).captureNs); // seq 4 was captured before seq 5
    }

    void layersContainPixels()
    {
        qp::Workload w;
        w.pointsPerFrame = 2000;
        LidarPipeline p(1, Policy::DropOldest, 1, 1);
        p.setViewport({300, 300, 3.0});
        std::atomic<int> ready{0};
        connect(&p, &LidarPipeline::layerReady, this, [&] { ++ready; }, Qt::DirectConnection);
        qp::SensorFrame f = frame(0, 1);
        f.cloud = qp::SensorSimulator::makeCloud(w, 0, 0);
        p.submit(f);
        QTRY_COMPARE_WITH_TIMEOUT(ready.load(), 1, 5000);
        const QImage img = p.layer(0).image;
        int lit = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                lit += qAlpha(img.pixel(x, y)) > 0;
        QVERIFY2(lit > 500, qPrintable(QString::number(lit)));
    }
};

QTEST_MAIN(TstPipeline)
#include "tst_pipeline.moc"
