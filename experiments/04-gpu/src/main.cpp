#include "EventLogModel.h"
#include "FrameStore.h"
#include "LidarMailbox.h"
#include "Scene.h"
#include "TrackListModel.h"
#include "qp/AppOptions.h"
#include "qp/SensorSimulator.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTextStream>

#include <memory>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    qp::AppOptions *opts = qp::AppOptions::instance();
    if (!opts->parse(app.arguments()))
        return 2;
    if (!QStringList{"", "sg", "rhi", "rhi-ring", "quick3d"}.contains(opts->variant())) {
        QTextStream(stderr) << "invalid --variant: " << opts->variant() << " (sg|rhi|rhi-ring|quick3d)\n";
        return 2;
    }
    opts->applyGraphicsApi();

    const qp::Workload workload = opts->workload();
    LidarMailbox mailbox(workload.sensors);
    FrameStore store;
    TrackListModel tracks(&store);
    EventLogModel events;
    QObject::connect(&store, &FrameStore::eventsArrived, &events,
                     [&events](const qp::EventBatchPtr &b) { events.appendBatch(*b); });

    std::unique_ptr<qp::SensorSimulator> sim;
    if (!opts->noSim()) {
        sim = std::make_unique<qp::SensorSimulator>(workload);
        QObject::connect(
            sim.get(), &qp::SensorSimulator::lidarFrame, &mailbox,
            [&mailbox](const qp::SensorFrame &f) { mailbox.submit(f); }, Qt::DirectConnection);
        QObject::connect(sim.get(), &qp::SensorSimulator::trackSet, &store, &FrameStore::setTracks);
        QObject::connect(sim.get(), &qp::SensorSimulator::eventBatch, &store, &FrameStore::addEvents);
        sim->start();
    }

    Scene scene(&mailbox, &tracks, &events);
    QJSEngine::setObjectOwnership(&scene, QJSEngine::CppOwnership);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.setExternalSingletonInstance("M04Gpu", "Scene", &scene);
    engine.loadFromModule("M04Gpu", "Main");
    const int rc = app.exec();
    if (sim)
        sim->stop();
    return rc;
}
