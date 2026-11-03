#include "EventLogModel.h"
#include "FrameStore.h"
#include "LidarPipeline.h"
#include "Scene.h"
#include "TrackListModel.h"
#include "qp/AppOptions.h"
#include "qp/SensorSimulator.h"

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
    LidarPipeline pipeline(workload.sensors, LidarPipeline::policyFromString(opts->policy()),
                           opts->queueDepth(), opts->workers());
    FrameStore store;
    TrackListModel tracks(&store);
    EventLogModel events;
    QObject::connect(&store, &FrameStore::eventsArrived, &events,
                     [&events](const qp::EventBatchPtr &b) { events.appendBatch(*b); });

    std::unique_ptr<qp::SensorSimulator> sim;
    if (!opts->noSim()) {
        sim = std::make_unique<qp::SensorSimulator>(workload);
        // Direct: lidar never touches the GUI thread.
        QObject::connect(
            sim.get(), &qp::SensorSimulator::lidarFrame, &pipeline,
            [&pipeline](const qp::SensorFrame &f) { pipeline.submit(f); }, Qt::DirectConnection);
        QObject::connect(sim.get(), &qp::SensorSimulator::trackSet, &store, &FrameStore::setTracks);
        QObject::connect(sim.get(), &qp::SensorSimulator::eventBatch, &store, &FrameStore::addEvents);
        sim->start();
    }

    Scene scene(&pipeline, &tracks, &events);
    QJSEngine::setObjectOwnership(&scene, QJSEngine::CppOwnership);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.setExternalSingletonInstance("M03Pipeline", "Scene", &scene);
    engine.loadFromModule("M03Pipeline", "Main");
    const int rc = app.exec();

    // A blocked producer must be released before the simulator thread can stop.
    pipeline.shutdown();
    if (sim)
        sim->stop();
    return rc;
}
