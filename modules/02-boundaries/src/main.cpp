#include "EventLogModel.h"
#include "FrameStore.h"
#include "Scene.h"
#include "TrackListModel.h"
#include "qp/AppOptions.h"
#include "qp/SensorSimulator.h"
#include "qp/StageStats.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include <memory>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    qp::AppOptions *opts = qp::AppOptions::instance();
    if (!opts->parse(app.arguments()))
        return 2;
    opts->applyGraphicsApi();

    const qp::Workload workload = opts->workload();
    FrameStore store(workload.sensors);
    TrackListModel tracks(&store);
    EventLogModel events;
    QObject::connect(&store, &FrameStore::eventsArrived, &events,
                     [&events](const qp::EventBatchPtr &b) { events.appendBatch(*b); });

    std::unique_ptr<qp::SensorSimulator> sim;
    quint64 consumed = 0;
    if (!opts->noSim()) {
        sim = std::make_unique<qp::SensorSimulator>(workload);
        QObject::connect(sim.get(), &qp::SensorSimulator::lidarFrame, &store,
                         [&store, &consumed, s = sim.get()](const qp::SensorFrame &f) {
                             auto &stats = qp::StageStats::instance();
                             stats.add(QStringLiteral("backlog_frames"),
                                       double(s->lidarFramesEmitted()) - double(++consumed));
                             stats.add(QStringLiteral("queue_wait_ms"),
                                       double(qp::nowNs() - f.captureNs) / 1e6);
                             store.setLidar(f);
                         });
        QObject::connect(sim.get(), &qp::SensorSimulator::trackSet, &store, &FrameStore::setTracks);
        QObject::connect(sim.get(), &qp::SensorSimulator::eventBatch, &store, &FrameStore::addEvents);
        sim->start();
    }

    Scene scene(&store, &tracks, &events);
    QJSEngine::setObjectOwnership(&scene, QJSEngine::CppOwnership);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.setExternalSingletonInstance("M02Boundaries", "Scene", &scene);
    engine.loadFromModule("M02Boundaries", "Main");
    const int rc = app.exec();
    if (sim)
        sim->stop();
    return rc;
}
