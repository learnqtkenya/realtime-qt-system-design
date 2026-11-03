#include "CanvasFeed.h"
#include "EventLogModel.h"
#include "FrameStore.h"
#include "Lab.h"
#include "LidarHub.h"
#include "LidarMailbox.h"
#include "LidarPipeline.h"
#include "Scene.h"
#include "TrackListModel.h"
#include "qp/AppOptions.h"
#include "qp/SensorSimulator.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTextStream>
#include <QTimer>

#include <memory>

int main(int argc, char *argv[])
{
    // GPU timestamps feed the lab panel's GPU chart; --set gpuTiming=0 turns them off.
    bool gpuTiming = true;
    for (int i = 1; i + 1 < argc; ++i)
        if (qstrcmp(argv[i], "--set") == 0 && qstrcmp(argv[i + 1], "gpuTiming=0") == 0)
            gpuTiming = false;
    if (gpuTiming && !qEnvironmentVariableIsSet("QSG_RHI_PROFILE"))
        qputenv("QSG_RHI_PROFILE", "1");

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("QtQMLPro Perception Viewer"));
    qp::AppOptions *opts = qp::AppOptions::instance();
    if (!opts->parse(app.arguments()))
        return 2;
    if (!opts->variant().isEmpty() && !Lab::rendererIds().contains(opts->variant())) {
        QTextStream(stderr) << "invalid --variant: " << opts->variant() << " ("
                            << Lab::rendererIds().join(u'|') << ")\n";
        return 2;
    }
    opts->applyGraphicsApi();
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    const qp::Workload requested = opts->workload();
    FrameStore store(requested.sensors);
    LidarPipeline pipeline(requested.sensors, LidarPipeline::Policy::DropOldest, 1, 4);
    LidarMailbox mailbox(requested.sensors);
    CanvasFeed canvasFeed;
    TrackListModel tracks(&store);
    EventLogModel events;
    QObject::connect(&store, &FrameStore::eventsArrived, &events,
                     [&events](const qp::EventBatchPtr &b) { events.appendBatch(*b); });

    LidarHub hub(&pipeline, &mailbox);
    QObject::connect(&hub, &LidarHub::canvasFrame, &canvasFeed, &CanvasFeed::onFrame, Qt::QueuedConnection);
    QObject::connect(&hub, &LidarHub::storeFrame, &store, &FrameStore::setLidar, Qt::QueuedConnection);

    Lab lab(&hub, requested, opts->variant().isEmpty() ? QStringLiteral("rhi") : opts->variant(),
            opts->extra(QStringLiteral("charts"), true).toBool());

    std::unique_ptr<qp::SensorSimulator> sim;
    if (!opts->noSim()) {
        sim = std::make_unique<qp::SensorSimulator>(lab.workload());
        QObject::connect(
            sim.get(), &qp::SensorSimulator::lidarFrame, &hub,
            [&hub](const qp::SensorFrame &f) { hub.submit(f); }, Qt::DirectConnection);
        QObject::connect(sim.get(), &qp::SensorSimulator::trackSet, &store, &FrameStore::setTracks);
        QObject::connect(sim.get(), &qp::SensorSimulator::eventBatch, &store, &FrameStore::addEvents);
        lab.setSimulator(sim.get());
        sim->start();
    }
    if (const double cycle = opts->extra(QStringLiteral("cycle"), 0).toDouble(); cycle > 0)
        lab.startCycle(cycle, opts->extra(QStringLiteral("cycleSet")).toString().split(u',', Qt::SkipEmptyParts));
    if (const double loadCycle = opts->extra(QStringLiteral("loadCycle"), 0).toDouble(); loadCycle > 0)
        lab.startLoadCycle(loadCycle);
    // --set switchTo=<renderer>@<seconds>: one scripted switch, for the recovery test.
    if (const QStringList to = opts->extra(QStringLiteral("switchTo")).toString().split(u'@'); to.size() == 2)
        QTimer::singleShot(int(to[1].toDouble() * 1000), &lab, [&lab, id = to[0]] { lab.setRenderer(id); });

    Scene scene({&store, &pipeline, &mailbox, &canvasFeed, &tracks, &events});
    QJSEngine::setObjectOwnership(&scene, QJSEngine::CppOwnership);
    QJSEngine::setObjectOwnership(&lab, QJSEngine::CppOwnership);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.setExternalSingletonInstance("Viewer", "Scene", &scene);
    engine.setExternalSingletonInstance("Viewer", "Lab", &lab);
    engine.loadFromModule("Viewer", "Main");
    const int rc = app.exec();

    pipeline.shutdown();
    if (sim)
        sim->stop();
    return rc;
}
