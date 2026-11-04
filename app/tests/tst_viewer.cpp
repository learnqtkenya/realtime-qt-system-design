#include "HealthMonitor.h"
#include "Lab.h"
#include "LidarHub.h"
#include "LidarMailbox.h"
#include "LidarPipeline.h"

#include "qp/SensorSimulator.h"

#include <QtTest>

using Route = LidarHub::Route;

namespace {

qp::SensorFrame frame(quint64 seq)
{
    qp::SensorFrame f;
    f.sensorId = 0;
    f.seq = seq;
    f.captureNs = qp::nowNs();
    return f;
}

} // namespace

class TstViewer : public QObject
{
    Q_OBJECT
private slots:
    // Stream numbers can come from the network (step 10 found one that didn't fit): ignored, not used.
    void healthIgnoresStreamsItDoesNotHave()
    {
        HealthMonitor h;
        h.configure(qp::lidarStream(7), QStringLiteral("lidar 7"), 100, 100);
        for (int stream : {-1, int(qp::kStreamCount), 1000, 65535}) {
            h.arrived(stream, 1, qp::nowNs());
            h.rejected(stream);
            h.noteFault(stream, QStringLiteral("drop50"));
        }
        QCOMPARE(qp::lidarSensor(qp::lidarStream(7)), 7);
        QCOMPARE(qp::lidarStream(3), 3);
        QCOMPARE(qp::lidarStream(4), int(qp::kLidarStream4));
        QCOMPARE(qp::lidarSensor(qp::kTracksStream), -1);
        QCOMPARE(qp::lidarSensor(qp::kGuiStream), -1);
    }

    void initTestCase() { qp::registerSimTypes(); }

    void hubSendsOnlyToTheActiveRoute()
    {
        LidarPipeline pipeline(1, LidarPipeline::Policy::DropOldest, 4, 0);
        LidarMailbox mailbox(1);
        LidarHub hub(&pipeline, &mailbox);
        QSignalSpy canvas(&hub, &LidarHub::canvasFrame);
        QSignalSpy store(&hub, &LidarHub::storeFrame);

        hub.submit(frame(1)); // Route::None
        hub.setRoute(Route::Mailbox);
        hub.submit(frame(2));
        hub.setRoute(Route::Pipeline);
        hub.submit(frame(3));
        hub.setRoute(Route::Canvas);
        hub.submit(frame(4));
        hub.setRoute(Route::Store);
        hub.submit(frame(5));

        QCOMPARE(mailbox.latest(0).frame.seq, quint64(2));
        QCOMPARE(pipeline.queuedSequence(0), std::vector<quint64>{3});
        QCOMPARE(canvas.count(), 1);
        QCOMPARE(canvas.first().at(0).value<qp::SensorFrame>().seq, quint64(4));
        QCOMPARE(store.count(), 1);
        QCOMPARE(store.first().at(0).value<qp::SensorFrame>().seq, quint64(5));
    }

    void rendererSelectsRouteAndClampsLoad()
    {
        LidarPipeline pipeline(1, LidarPipeline::Policy::DropOldest, 1, 0);
        LidarMailbox mailbox(1);
        LidarHub hub(&pipeline, &mailbox);
        qp::Workload w;
        w.pointsPerFrame = 300'000;
        Lab lab(&hub, w, QStringLiteral("canvas"), true);

        QCOMPARE(lab.renderer(), QStringLiteral("canvas"));
        QCOMPARE(hub.route(), Route::Canvas);
        // 0.5M points/s over 4 sensors at 10 Hz; clamped before any simulator exists.
        QCOMPARE(lab.pointsPerSensor(), 12'000);
        QCOMPARE(lab.workload().pointsPerFrame, 12'000);
        lab.setRateHz(20);
        QCOMPARE(lab.maxPoints(), 6'000);
        QCOMPARE(lab.pointsPerSensor(), 6'000);
        lab.setRateHz(10);
        QCOMPARE(lab.pointsPerSensor(), 6'000); // lowering the rate never raises the load

        const std::pair<const char *, Route> expected[] = {{"painted", Route::Store},
                                                           {"worker", Route::Pipeline},
                                                           {"sg", Route::Mailbox},
                                                           {"rhi", Route::Mailbox},
                                                           {"quick3d", Route::Mailbox}};
        for (const auto &[id, route] : expected) {
            lab.setRenderer(QLatin1String(id));
            QCOMPARE(lab.renderer(), QLatin1String(id));
            QCOMPARE(hub.route(), route);
        }
        lab.setRenderer(QStringLiteral("no-such-renderer"));
        QCOMPARE(lab.renderer(), QStringLiteral("quick3d"));
    }

    void unknownInitialRendererFallsBackToRhi()
    {
        LidarPipeline pipeline(1, LidarPipeline::Policy::DropOldest, 1, 0);
        LidarMailbox mailbox(1);
        LidarHub hub(&pipeline, &mailbox);
        Lab lab(&hub, qp::Workload{}, QStringLiteral("bogus"), true);
        QCOMPARE(lab.renderer(), QStringLiteral("rhi"));
        QCOMPARE(hub.route(), Route::Mailbox);
    }

    void rendererListMatchesIds()
    {
        LidarPipeline pipeline(1, LidarPipeline::Policy::DropOldest, 1, 0);
        LidarMailbox mailbox(1);
        LidarHub hub(&pipeline, &mailbox);
        Lab lab(&hub, qp::Workload{}, QStringLiteral("rhi"), true);
        const QVariantList list = lab.renderers();
        QCOMPARE(list.size(), Lab::rendererIds().size());
        for (int i = 0; i < list.size(); ++i)
            QCOMPARE(list[i].toMap().value("id").toString(), Lab::rendererIds()[i]);
    }

    void resetRespectsTheRendererCap()
    {
        LidarPipeline pipeline(1, LidarPipeline::Policy::DropOldest, 1, 0);
        LidarMailbox mailbox(1);
        LidarHub hub(&pipeline, &mailbox);
        Lab lab(&hub, qp::Workload{}, QStringLiteral("canvas"), true);
        lab.resetToNominal();
        QCOMPARE(lab.pointsPerSensor(), 12'000);
        lab.setRenderer(QStringLiteral("rhi"));
        lab.resetToNominal();
        QCOMPARE(lab.pointsPerSensor(), 150'000);
        QCOMPARE(lab.rateHz(), 10.0);
    }

    void loadChangesReachTheSimulator()
    {
        LidarPipeline pipeline(2, LidarPipeline::Policy::DropOldest, 1, 0);
        LidarMailbox mailbox(2);
        LidarHub hub(&pipeline, &mailbox);
        qp::Workload w;
        w.sensors = 2;
        w.pointsPerFrame = 1000;
        w.lidarRateHz = 20;
        Lab lab(&hub, w, QStringLiteral("rhi"), true);
        qp::SensorSimulator sim(lab.workload());
        connect(&sim, &qp::SensorSimulator::lidarFrame, &hub,
                [&hub](const qp::SensorFrame &f) { hub.submit(f); }, Qt::DirectConnection);
        lab.setSimulator(&sim);
        sim.start();
        QTRY_VERIFY_WITH_TIMEOUT(mailbox.latest(0).serial > 0, 2000);

        for (int n : {2000, 3000, 4000}) // several edits inside the debounce window
            lab.setPointsPerSensor(n);
        QTRY_COMPARE_WITH_TIMEOUT(mailbox.latest(0).frame.cloud->count(), 4000, 3000);
        sim.stop();
    }
};

QTEST_MAIN(TstViewer)
#include "tst_viewer.moc"
