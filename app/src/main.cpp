#include "Cameras.h"
#include "Diagnostics.h"
#include "CanvasFeed.h"
#include "EventLogModel.h"
#include "FrameStore.h"
#include "HealthMonitor.h"
#include "InputRobot.h"
#include "Lab.h"
#include "LayerOverlay.h"
#include "LayoutStore.h"
#include "LidarHub.h"
#include "LidarMailbox.h"
#include "LidarPipeline.h"
#include "Plugins.h"
#include "Scene.h"
#include "Selection.h"
#include "StreamServer.h"
#include "Timeline.h"
#include "TrackListModel.h"
#include "qp/AppOptions.h"
#include "qp/SensorSimulator.h"
#include "qp/StageStats.h"
#include "qp_version.h"

#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <QSocketNotifier>

#include <csignal>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int g_signalPipe[2] = {-1, -1};

// SIGTERM and SIGINT end the event loop, so a viewer stopped by the system saves its layout and
// writes its logs like one closed by the user. The handler only writes a byte (async-signal-safe).
void onSignal(int)
{
    const char c = 1;
    [[maybe_unused]] const auto n = ::write(g_signalPipe[0], &c, 1);
}

} // namespace

int main(int argc, char *argv[])
{
    const double startedMainMs = Diagnostics::msSinceProcessStart();
    // GPU timestamps feed the lab panel's GPU chart; --set gpuTiming=0 turns them off.
    bool gpuTiming = true;
    for (int i = 1; i + 1 < argc; ++i)
        if (qstrcmp(argv[i], "--set") == 0 && qstrcmp(argv[i + 1], "gpuTiming=0") == 0)
            gpuTiming = false;
    if (gpuTiming && !qEnvironmentVariableIsSet("QSG_RHI_PROFILE"))
        qputenv("QSG_RHI_PROFILE", "1");
    // --set hwdecode=1|0: VA-API video decoding on or off. Qt reads this once, so it's set here.
    QString decoder = QStringLiteral("Qt default");
    for (int i = 1; i + 1 < argc; ++i) {
        if (qstrcmp(argv[i], "--set") != 0)
            continue;
        if (qstrcmp(argv[i + 1], "hwdecode=1") == 0) {
            qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "vaapi");
            decoder = QStringLiteral("VA-API hardware");
        } else if (qstrcmp(argv[i + 1], "hwdecode=0") == 0) {
            qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", ",");
            decoder = QStringLiteral("software");
        }
    }

    // FFmpeg describes every input file at info level; warnings in every category stay on.
    // QT_LOGGING_RULES still overrides this.
    QLoggingCategory::setFilterRules(QStringLiteral("qt.multimedia.ffmpeg.info=false\n"
                                                    "qt.multimedia.ffmpeg.mediadataholder.info=false\n"
                                                    "qt.multimedia.playbackengine.codec.info=false"));

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("QtQMLPro Perception Viewer"));
    QGuiApplication::setOrganizationName(QStringLiteral("QtQMLPro"));
    QGuiApplication::setApplicationVersion(QStringLiteral(QP_GIT_VERSION));
    qp::AppOptions *opts = qp::AppOptions::instance();
    if (!opts->parse(app.arguments()))
        return 2;
    auto given = [opts](const char *key) { return opts->extra(QString::fromLatin1(key)).isValid(); };

    // Step 9: an installed viewer reads from share/qtqmlpro and writes where the user may write; a
    // build-tree viewer keeps everything next to itself.
    const QString appDir = QCoreApplication::applicationDirPath();
    const bool installed = QDir(appDir + QStringLiteral("/../share/qtqmlpro")).exists();
    const QString shareDir = installed ? QDir::cleanPath(appDir + QStringLiteral("/../share/qtqmlpro"))
                                       : QDir::cleanPath(appDir + QStringLiteral("/.."));
    const QString writableDir = installed ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                                          : QDir::cleanPath(appDir + QStringLiteral("/.."));
    // Outside benchmarks the stage statistics are a window of the newest values per stage
    // (--set stageCap=<n>, 0: keep everything); a benchmark keeps all, over its bounded run.
    if (!opts->bench())
        qp::StageStats::instance().setCapacity(size_t(opts->extra(QStringLiteral("stageCap"), 10'000).toLongLong()));
    Diagnostics diagnostics;
    if (opts->extra(QStringLiteral("log"), true).toBool())
        diagnostics.installLog(opts->extra(QStringLiteral("logDir"), writableDir + QStringLiteral("/logs")).toString());
    diagnostics.setBundleDir(writableDir + QStringLiteral("/bundles"));
    diagnostics.setStartup(QStringLiteral("mainMs"), startedMainMs);
    diagnostics.note(QStringLiteral("viewer"), QStringLiteral("started %1 (%2), Qt %3")
                                                   .arg(QStringLiteral(QP_GIT_VERSION),
                                                        installed ? QStringLiteral("installed") : QStringLiteral("build tree"),
                                                        QString::fromLatin1(qVersion())));
    if (!opts->variant().isEmpty() && !Lab::rendererIds().contains(opts->variant())) {
        QTextStream(stderr) << "invalid --variant: " << opts->variant() << " ("
                            << Lab::rendererIds().join(u'|') << ")\n";
        return 2;
    }
    opts->applyGraphicsApi();
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    qp::Workload requested = opts->workload();
    // From step 4 on, every frame is its own allocation, as from a real sensor.
    requested.freshClouds = opts->extra(QStringLiteral("freshClouds"), true).toBool();
    requested.lidarDelayMs = opts->extra(QStringLiteral("lidarDelayMs"), 0).toDouble();
    FrameStore store(requested.sensors);
    LidarPipeline pipeline(requested.sensors, LidarPipeline::Policy::DropOldest, 1, 4);
    LidarMailbox mailbox(requested.sensors);
    CanvasFeed canvasFeed;
    TrackListModel tracks(&store);
    EventLogModel events;
    // --set updates=reset|diff: how model changes reach the views (step 5).
    const bool diffUpdates = opts->extra(QStringLiteral("updates"), QStringLiteral("diff")).toString() == QLatin1String("diff");
    tracks.setUpdates(diffUpdates ? TrackListModel::Updates::Diff : TrackListModel::Updates::Reset);
    events.setUpdates(diffUpdates ? EventLogModel::Updates::Diff : EventLogModel::Updates::Reset,
                      opts->extra(QStringLiteral("eventCap"), 10'000).toInt());
    TrackTableProxy objectTable(&tracks);
    EventFilterProxy eventLogProxy(&events);
    EventFilterModel eventLogIndex(&events);
    const bool proxyFilter = opts->extra(QStringLiteral("eventFilter"), QStringLiteral("index")).toString() == QLatin1String("proxy");
    QAbstractItemModel *eventLog = proxyFilter ? static_cast<QAbstractItemModel *>(&eventLogProxy) : &eventLogIndex;
    QObject::connect(&store, &FrameStore::eventsArrived, &events,
                     [&events](const qp::EventBatchPtr &b) { events.appendBatch(*b); });

    LidarHub hub(&pipeline, &mailbox);
    QObject::connect(&hub, &LidarHub::canvasFrame, &canvasFeed, &CanvasFeed::onFrame, Qt::QueuedConnection);
    QObject::connect(&hub, &LidarHub::storeFrame, &store, &FrameStore::setLidar, Qt::QueuedConnection);

    Lab lab(&hub, requested, opts->variant().isEmpty() ? QStringLiteral("rhi") : opts->variant(),
            opts->extra(QStringLiteral("charts"), true).toBool());
    lab.setViewMode(opts->extra(QStringLiteral("view"), QStringLiteral("top")).toString());
    lab.setLodEnabled(opts->extra(QStringLiteral("lod"), true).toBool());
    Selection selection(&store);
    Cameras cameras(opts->extra(QStringLiteral("mediaDir"), shareDir + QStringLiteral("/media")).toString(),
                    &hub, decoder);
    cameras.setPath(opts->extra(QStringLiteral("cameraPath"), QStringLiteral("forward")).toString());
    cameras.setSync(opts->extra(QStringLiteral("cameraSync"), QStringLiteral("latest")).toString());
    // Queued from the simulator thread: aligned cameras re-pick when the lidar clock moves.
    QObject::connect(&mailbox, &LidarMailbox::changed, &cameras, &Cameras::lidarAdvanced, Qt::QueuedConnection);

    Timeline::Options timelineOptions;
    timelineOptions.historyBytes = qint64(opts->extra(QStringLiteral("historyMb"), 1000).toDouble() * 1e6);
    timelineOptions.shape = opts->extra(QStringLiteral("historyShape")).toString() == QLatin1String("int16")
                                ? History::Shape::Int16
                                : History::Shape::Shared;
    timelineOptions.recordDir = writableDir + QStringLiteral("/recordings");
    const QString recordEncoding = opts->extra(QStringLiteral("recordEncoding"), QStringLiteral("int16planar")).toString();
    timelineOptions.writer.lidar = recordEncoding == QLatin1String("float32") ? rec::LidarEncoding::Float32
                                   : recordEncoding == QLatin1String("int16") ? rec::LidarEncoding::Int16
                                                                              : rec::LidarEncoding::Int16Planar;
    timelineOptions.writer.zstdLevel = opts->extra(QStringLiteral("recordZstd"), 1).toInt();
    timelineOptions.writer.separateSmall = opts->extra(QStringLiteral("recordSeparate"), true).toBool();
    timelineOptions.coldReads = opts->extra(QStringLiteral("coldReads"), false).toBool();
    Presenter::Sinks sinks;
    sinks.lidar = [&hub](const qp::SensorFrame &f) { hub.submit(f); };
    sinks.tracks = [&store](const qp::TrackSetPtr &t) {
        QMetaObject::invokeMethod(&store, [&store, t] { store.setTracks(t); }, Qt::QueuedConnection);
    };
    sinks.events = [&store, &events](const qp::EventBatchPtr &b, bool reset) {
        if (reset)
            QMetaObject::invokeMethod(&events, [&events, b] { events.replaceAll(*b); }, Qt::QueuedConnection);
        else
            QMetaObject::invokeMethod(&store, [&store, b] { store.addEvents(b); }, Qt::QueuedConnection);
    };
    sinks.camera = [&cameras](const rec::CameraMark &m, qint64 t, double rate, bool playing, qint64 wall) {
        QMetaObject::invokeMethod(
            &cameras, [&cameras, m, t, rate, playing, wall] { cameras.follow(m, t, rate, playing, wall); },
            Qt::QueuedConnection);
    };
    Timeline timeline(sinks, timelineOptions);
    timeline.setDelayMs(opts->extra(QStringLiteral("delayMs"), 0).toInt());
    cameras.setMarkSink([&timeline](const rec::CameraMark &m) { timeline.ingestCameraMark(m); });
    QObject::connect(&timeline, &Timeline::stateChanged, &cameras, [&timeline, &cameras] {
        cameras.setLive(timeline.mode() == QLatin1String("live") && !timeline.replayingFile(),
                        qint64(timeline.delayMs()) * 1'000'000);
    });
    cameras.setLive(true, qint64(timeline.delayMs()) * 1'000'000);
    {
        QJsonObject info;
        info["app"] = QStringLiteral("viewer");
        info["workload"] = QJsonObject::fromVariantMap(opts->workloadMap());
        QJsonArray cams;
        for (CameraFeed *f : cameras.feeds())
            cams.append(QStringLiteral("cam%1.mp4").arg(f->index()));
        info["cameras"] = cams;
        timeline.setRecordingInfo(info);
    }

    // Step 6: who finds out when a stream fails. Its own thread; the GUI thread is one of the streams.
    HealthMonitor health;
    {
        const qp::Workload &w = lab.workload();
        for (int i = 0; i < w.sensors && i < 4; ++i)
            health.configure(qp::kLidarStream0 + i, QStringLiteral("lidar %1").arg(i), 1000.0 / w.lidarRateHz, 100);
        health.configure(qp::kTracksStream, QStringLiteral("tracks"), 1000.0 / w.trackRateHz, 100);
        if (w.eventsPerSecond > 0)
            health.configure(qp::kEventsStream, QStringLiteral("events"), 50, 100);
        for (CameraFeed *f : cameras.feeds())
            health.configure(qp::kCameraStream0 + f->index(), QStringLiteral("camera %1").arg(f->index()), 1000.0 / 30, 100);
        health.configure(qp::kGuiStream, QStringLiteral("GUI thread"), 20, 1e9);
    }
    QObject::connect(&lab, &Lab::workloadChanged, &health, [&lab, &health] {
        for (int i = 0; i < lab.sensors() && i < 4; ++i)
            health.setPeriod(qp::kLidarStream0 + i, 1000.0 / lab.rateHz());
    });
    const bool hideStale = opts->extra(QStringLiteral("stalePolicy"), QStringLiteral("hide")).toString() == QLatin1String("hide");
    std::atomic<quint64> healthSeq{0};
    health.setStatusSink([&timeline, &hub, &healthSeq, &diagnostics, hideStale](int stream, HealthMonitor::Status,
                                                                                 HealthMonitor::Status to, const QString &why) {
        using S = HealthMonitor::Status;
        qp::EventRecord e;
        e.seq = ++healthSeq;
        e.captureNs = qp::nowNs();
        e.severity = to == S::Stale || to == S::Corrupt ? 2 : (to == S::Ok ? 0 : 1);
        e.source = QStringLiteral("health");
        static const char *names[] = {"lidar 0", "lidar 1", "lidar 2", "lidar 3", "tracks", "events",
                                      "camera 0", "camera 1", "camera 2", "camera 3", "GUI thread"};
        e.text = QStringLiteral("%1 %2%3").arg(QLatin1String(names[stream]), HealthMonitor::statusName(to),
                                               why.isEmpty() ? QString() : QStringLiteral(": ") + why);
        timeline.ingestEvents(std::make_shared<qp::EventBatch>(qp::EventBatch{e}));
        diagnostics.note(QStringLiteral("health"), e.text);
        // Stale lidar: stop showing its last frame as if it were current.
        if (hideStale && to == S::Stale && stream >= qp::kLidarStream0 && stream < qp::kLidarStream0 + 4)
            hub.submit(qp::SensorFrame{stream - qp::kLidarStream0, 0, qp::nowNs(), std::make_shared<qp::PointCloud>()});
    });
    timeline.setHealth(&health, opts->extra(QStringLiteral("validate"), true).toBool(),
                       opts->extra(QStringLiteral("keepOrder"), true).toBool());
    lab.setHealth(&health);
    health.start();

    // Step 7: --set serve=<port> shares the live streams; --set connect=<host>:<port> (with --no-sim)
    // shows another viewer's.
    std::unique_ptr<StreamServer> server;
    std::unique_ptr<StreamClient> client;
    const int servePort = opts->extra(QStringLiteral("serve"), 0).toInt();
    const QString connectTo = opts->extra(QStringLiteral("connect")).toString();
    NetworkStatus network(servePort > 0 ? QStringLiteral("server") : (connectTo.isEmpty() ? QStringLiteral("off")
                                                                                          : QStringLiteral("client")));
    if (servePort > 0) {
        QJsonObject info;
        info["app"] = QStringLiteral("viewer");
        info["workload"] = QJsonObject::fromVariantMap(opts->workloadMap());
        server = std::make_unique<StreamServer>(
            quint16(servePort),
            opts->extra(QStringLiteral("sendPolicy"), QStringLiteral("latest")).toString() == QLatin1String("all")
                ? StreamServer::Policy::All
                : StreamServer::Policy::Latest,
            opts->extra(QStringLiteral("wireZstd"), 1).toInt(), info,
            qint64(opts->extra(QStringLiteral("highWaterKb"), 1024).toInt()) * 1024,
            opts->extra(QStringLiteral("sendBufferKb"), 0).toInt() * 1024);
        QString error;
        if (!server->start(&error)) {
            QTextStream(stderr) << "--set serve=" << servePort << ": " << error << "\n";
            return 4;
        }
        timeline.setPublisher(server.get());
    }
    if (!connectTo.isEmpty()) {
        const QStringList hp = connectTo.split(u':');
        client = std::make_unique<StreamClient>(
            hp.value(0), quint16(hp.value(1).toInt()), opts->extra(QStringLiteral("readMbps"), 0).toDouble(),
            opts->extra(QStringLiteral("receiveBufferKb"), 0).toInt() * 1024,
            StreamClient::Sinks{[&timeline](const qp::SensorFrame &f) { timeline.ingestLidar(f); },
                                [&timeline](const qp::TrackSetPtr &t) { timeline.ingestTracks(t); },
                                [&timeline](const qp::EventBatchPtr &b) { timeline.ingestEvents(b); },
                                [&health] { health.resetSequences(); }});
        client->start();
    }
    QTimer networkTick;
    QObject::connect(&networkTick, &QTimer::timeout, &network, [&] {
        network.setLines(server ? server->status() : (client ? QStringList{client->status()} : QStringList{}));
    });
    networkTick.start(1000);

    std::unique_ptr<qp::SensorSimulator> sim;
    if (!opts->noSim()) {
        sim = std::make_unique<qp::SensorSimulator>(lab.workload());
        QObject::connect(
            sim.get(), &qp::SensorSimulator::lidarFrame, &hub,
            [&timeline](const qp::SensorFrame &f) { timeline.ingestLidar(f); }, Qt::DirectConnection);
        QObject::connect(
            sim.get(), &qp::SensorSimulator::trackSet, &store,
            [&timeline](const qp::TrackSetPtr &t) { timeline.ingestTracks(t); }, Qt::DirectConnection);
        QObject::connect(
            sim.get(), &qp::SensorSimulator::eventBatch, &store,
            [&timeline](const qp::EventBatchPtr &b) { timeline.ingestEvents(b); }, Qt::DirectConnection);
        lab.setSimulator(sim.get());
        sim->start();
    }
    // --set record=1 [--set recordStopAt=<s>] [--set recordPath=<file>]: record from the start.
    if (opts->extra(QStringLiteral("record"), false).toBool()) {
        timeline.startRecording(opts->extra(QStringLiteral("recordPath")).toString());
        if (const double stopAt = opts->extra(QStringLiteral("recordStopAt"), 0).toDouble(); stopAt > 0)
            QTimer::singleShot(int(stopAt * 1000), &timeline, [&timeline] { timeline.stopRecording(); });
    }
    // --set replay=<file> [--set replayRate=<x>]: open a recording and play it, looping.
    if (const QString replay = opts->extra(QStringLiteral("replay")).toString(); !replay.isEmpty()) {
        timeline.setRate(opts->extra(QStringLiteral("replayRate"), 1).toDouble());
        const bool autoplay = opts->extra(QStringLiteral("replayPlay"), true).toBool();
        QObject::connect(&timeline, &Timeline::fileOpened, &timeline, [&timeline, autoplay](bool ok) {
            if (ok && autoplay)
                timeline.play();
        });
        QObject::connect(&timeline, &Timeline::stateChanged, &timeline, [&timeline, autoplay] {
            if (autoplay && timeline.replayingFile() && timeline.mode() == QLatin1String("paused")
                && timeline.position() >= timeline.length() - 0.05) {
                timeline.seek(0);
                timeline.play();
            }
        });
        timeline.openRecording(replay);
    }
    // --set seekCycle=<s>: pause, then seek to a random point every <s> seconds (seek-to-photon).
    std::unique_ptr<QTimer> seekCycle;
    if (const double every = opts->extra(QStringLiteral("seekCycle"), 0).toDouble(); every > 0) {
        seekCycle = std::make_unique<QTimer>();
        QObject::connect(seekCycle.get(), &QTimer::timeout, &timeline, [&timeline] {
            if (timeline.length() > 1)
                timeline.seek(double(std::rand() % 10'000) / 10'000 * timeline.length());
        });
        QTimer::singleShot(4000, seekCycle.get(), [&seekCycle, every] { seekCycle->start(int(every * 1000)); });
    }
    if (const double cycle = opts->extra(QStringLiteral("cycle"), 0).toDouble(); cycle > 0)
        lab.startCycle(cycle, opts->extra(QStringLiteral("cycleSet")).toString().split(u',', Qt::SkipEmptyParts));
    if (const double loadCycle = opts->extra(QStringLiteral("loadCycle"), 0).toDouble(); loadCycle > 0)
        lab.startLoadCycle(loadCycle);
    // --set switchTo=<renderer>@<seconds>: one scripted switch, for the recovery test.
    if (const QStringList to = opts->extra(QStringLiteral("switchTo")).toString().split(u'@'); to.size() == 2)
        QTimer::singleShot(int(to[1].toDouble() * 1000), &lab, [&lab, id = to[0]] { lab.setRenderer(id); });

    // Step 8: overlay layers from plugins. --set pluginDir=<dir>[,<dir>...] --set layers=<id>,<id>
    // --set scriptThread=worker|gui --set sandbox=1
    PluginHost::Options pluginOptions;
    {
        pluginOptions.dirs = opts->extra(QStringLiteral("pluginDir"), shareDir + QStringLiteral("/plugins"))
                                 .toString()
                                 .split(u',', Qt::SkipEmptyParts);
        if (opts->extra(QStringLiteral("labPlugins"), false).toBool())
            pluginOptions.dirs << shareDir + QStringLiteral("/plugins-lab");
        pluginOptions.enabled = opts->extra(QStringLiteral("layers")).toString().split(u',', Qt::SkipEmptyParts);
        pluginOptions.scriptOnGui = opts->extra(QStringLiteral("scriptThread"), QStringLiteral("worker")).toString()
                                    == QLatin1String("gui");
        pluginOptions.budgetMs = opts->extra(QStringLiteral("layerBudgetMs"), 4).toDouble();
        pluginOptions.hardMs = opts->extra(QStringLiteral("layerHardMs"), 100).toDouble();
        pluginOptions.silenceMs = opts->extra(QStringLiteral("layerSilenceMs"), 2000).toInt();
        pluginOptions.sandbox = opts->extra(QStringLiteral("sandbox"), false).toBool();
        pluginOptions.logPath = opts->extra(QStringLiteral("layerLog")).toString();
        pluginOptions.answerSlack = opts->extra(QStringLiteral("answerSlack"), 50).toInt();
        pluginOptions.postEvery = opts->extra(QStringLiteral("layerPost"), QStringLiteral("latest")).toString() == QLatin1String("every");
    }
    std::atomic<quint64> pluginSeq{0};
    PluginHost plugins(&store, &selection, &tracks, pluginOptions,
                       [&timeline, &pluginSeq, &diagnostics](const QString &source, const QString &text, int severity) {
                           qp::EventRecord e;
                           e.seq = ++pluginSeq;
                           e.captureNs = qp::nowNs();
                           e.severity = quint8(severity);
                           e.source = source;
                           e.text = text;
                           timeline.ingestEvents(std::make_shared<qp::EventBatch>(qp::EventBatch{e}));
                           if (severity > 0)
                               diagnostics.note(source, text);
                       });

    Scene scene({&store, &pipeline, &mailbox, &canvasFeed, &tracks, &events, &objectTable, eventLog});
    QJSEngine::setObjectOwnership(&scene, QJSEngine::CppOwnership);
    QJSEngine::setObjectOwnership(&lab, QJSEngine::CppOwnership);
    QJSEngine::setObjectOwnership(&selection, QJSEngine::CppOwnership);
    QJSEngine::setObjectOwnership(&cameras, QJSEngine::CppOwnership);
    QJSEngine::setObjectOwnership(&timeline, QJSEngine::CppOwnership);
    QJSEngine::setObjectOwnership(&health, QJSEngine::CppOwnership);
    QJSEngine::setObjectOwnership(&network, QJSEngine::CppOwnership);
    QJSEngine::setObjectOwnership(&plugins, QJSEngine::CppOwnership);
    QJSEngine::setObjectOwnership(&diagnostics, QJSEngine::CppOwnership);

    // --set layout=0 or a benchmark: nothing is restored or saved. --set layoutFile=<file>.
    const bool keepLayout = !opts->bench() && opts->extra(QStringLiteral("layout"), true).toBool();
    LayoutStore layout(keepLayout ? opts->extra(QStringLiteral("layoutFile"),
                                                QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                                                    + QStringLiteral("/layout.json"))
                                        .toString()
                                  : QString());
    QJSEngine::setObjectOwnership(&layout, QJSEngine::CppOwnership);
    layout.load();
    layout.setDataPanel(opts->extra(QStringLiteral("dataPanel"), true).toBool());
    layout.setLabPanel(opts->extra(QStringLiteral("labPanel"), true).toBool());

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.setExternalSingletonInstance("Viewer", "Scene", &scene);
    engine.setExternalSingletonInstance("Viewer", "Lab", &lab);
    engine.setExternalSingletonInstance("Viewer", "Selection", &selection);
    engine.setExternalSingletonInstance("Viewer", "Cameras", &cameras);
    engine.setExternalSingletonInstance("Viewer", "Timeline", &timeline);
    engine.setExternalSingletonInstance("Viewer", "Health", &health);
    engine.setExternalSingletonInstance("Viewer", "Network", &network);
    engine.setExternalSingletonInstance("Viewer", "Plugins", &plugins);
    engine.setExternalSingletonInstance("Viewer", "Diagnostics", &diagnostics);
    engine.setExternalSingletonInstance("Viewer", "Workspace", &layout);
    layout.track(&layout, "dataPanel", QStringLiteral("dataPanel"), given("dataPanel"));
    layout.track(&layout, "labPanel", QStringLiteral("labPanel"), given("labPanel"));
    layout.track(&lab, "viewMode", QStringLiteral("view"), given("view"));
    layout.track(&lab, "renderer", QStringLiteral("renderer"), !opts->variant().isEmpty());
    layout.track(&lab, "lodEnabled", QStringLiteral("lod"), given("lod"));
    layout.track(&timeline, "delayMs", QStringLiteral("delayMs"), given("delayMs"));
    engine.loadFromModule("Viewer", "Main");
    diagnostics.setStartup(QStringLiteral("qmlLoadedMs"), Diagnostics::msSinceProcessStart());
    plugins.start();
    layout.track(&plugins, "enabledLayers", QStringLiteral("layers"), given("layers"));
    // Started after the window exists, so the panels have their sinks.
    cameras.setEnabled(opts->extra(QStringLiteral("cameras"), true).toBool());
    layout.track(&cameras, "enabled", QStringLiteral("cameras"), given("cameras"));

    // --set fault=<stream>:<kind>@<s> [--set faultClear=<s>]: inject a fault (and clear it) on schedule.
    if (const QString fault = opts->extra(QStringLiteral("fault")).toString(); !fault.isEmpty()) {
        const QStringList at = fault.split(u'@');
        const QStringList what = at.value(0).split(u':');
        const int stream = what.value(0).toInt();
        const QString kind = what.value(1);
        QTimer::singleShot(int(at.value(1).toDouble() * 1000), &lab, [&lab, stream, kind] { lab.injectFault(stream, kind); });
        if (const double clear = opts->extra(QStringLiteral("faultClear"), 0).toDouble(); clear > 0)
            QTimer::singleShot(int(clear * 1000), &lab, [&lab, stream] { lab.injectFault(stream, QStringLiteral("clear")); });
    }
    // --set selectId=<id>: select an object after a second (screenshots).
    if (const int id = opts->extra(QStringLiteral("selectId"), -1).toInt(); id >= 0)
        QTimer::singleShot(1000, &selection, [&selection, id] { selection.select(id); });
    // --set sortKey=<role> [--set sortDescending=1]: the objects table's sort.
    if (const QString key = opts->extra(QStringLiteral("sortKey")).toString(); !key.isEmpty()) {
        objectTable.setSortKey(key);
        objectTable.setDescending(opts->extra(QStringLiteral("sortDescending"), false).toBool());
    }
    // --set searchCycle=<s>: search the event log for "late", then clear it, every <s> seconds.
    std::unique_ptr<QTimer> searchCycle;
    if (const double every = opts->extra(QStringLiteral("searchCycle"), 0).toDouble(); every > 0) {
        searchCycle = std::make_unique<QTimer>();
        QObject::connect(searchCycle.get(), &QTimer::timeout, eventLog, [eventLog] {
            eventLog->setProperty("text", eventLog->property("text").toString().isEmpty() ? QStringLiteral("late")
                                                                                          : QString());
        });
        searchCycle->start(int(every * 1000));
    }

    // --set robot=orbit|click: synthetic input for measuring input-to-photon latency.
    std::unique_ptr<InputRobot> robot;
    const QString robotMode = opts->extra(QStringLiteral("robot")).toString();
    auto *window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (window) {
        layout.track(window, "width", QStringLiteral("windowWidth"), opts->bench());
        layout.track(window, "height", QStringLiteral("windowHeight"), opts->bench());
        auto first = std::make_shared<QMetaObject::Connection>();
        *first = QObject::connect(window, &QQuickWindow::frameSwapped, &diagnostics, [&diagnostics, first] {
            diagnostics.setStartup(QStringLiteral("firstFrameMs"), Diagnostics::msSinceProcessStart());
            QObject::disconnect(*first);
        });
    }

    // Step 9: what the diagnostics view, the metrics endpoint and a bundle report.
    {
        QJsonObject build{{"version", QStringLiteral(QP_GIT_VERSION)},
                          {"qt", QString::fromLatin1(qVersion())},
                          {"installed", installed},
                          {"executable", QCoreApplication::applicationFilePath()},
                          {"pluginApi", plugins.api()}};
        diagnostics.setBuild(build);
        QObject *monitor = nullptr;
        for (QObject *o : window ? window->findChildren<QObject *>() : QList<QObject *>())
            if (o->inherits("qp::FrameMonitor"))
                monitor = o;
        diagnostics.addSection(QStringLiteral("frames"), [monitor] {
            QJsonObject o;
            if (!monitor)
                return o;
            for (const char *p : {"fps", "frameMsP99", "frameMsMax", "lagMsP99", "lagMsMax", "slowFrames", "latencyMsMean",
                                  "latencyMsMax", "gpuMsMean", "guiCpuPercent"})
                o[QLatin1String(p)] = monitor->property(p).toDouble();
            o[u"graphicsApi"] = monitor->property("graphicsApi").toString();
            return o;
        });
        diagnostics.addSection(QStringLiteral("timeline"), [&timeline, &events] {
            return QJsonObject{{"mode", timeline.mode()},
                               {"historyMb", timeline.historyMb()},
                               {"historySeconds", timeline.historySeconds()},
                               {"recording", timeline.recording()},
                               {"recordMb", timeline.recordMb()},
                               {"eventsKept", double(events.rowCount())}};
        });
        diagnostics.addSection(QStringLiteral("health"), [&health] {
            QJsonObject o;
            int notOk = 0;
            for (const QVariant &v : health.streams()) {
                const QVariantMap m = v.toMap();
                const QString status = m.value(QStringLiteral("status")).toString();
                o[m.value(QStringLiteral("name")).toString()] = status;
                notOk += status != u"ok" && status != u"off";
            }
            o[u"notOk"] = notOk;
            return o;
        });
        diagnostics.addSection(QStringLiteral("layers"), [&plugins] {
            QJsonObject o;
            int on = 0, failing = 0;
            for (const QVariant &v : plugins.layers()) {
                const QVariantMap m = v.toMap();
                if (!m.value(QStringLiteral("enabled")).toBool())
                    continue;
                ++on;
                const QString status = m.value(QStringLiteral("status")).toString();
                failing += status != u"ok" && status != u"slow" && status != u"starting";
                o[m.value(QStringLiteral("id")).toString()] = status;
            }
            o[u"enabled"] = on;
            o[u"failing"] = failing;
            return o;
        });
        diagnostics.addSection(QStringLiteral("stages"), [] {
            return QJsonObject{{"storedValues", double(qp::StageStats::instance().storedValues())}};
        });
        diagnostics.addSection(QStringLiteral("network"), [&network] {
            return QJsonObject{{"role", network.role()},
                               {"peers", double(network.lines().size())},
                               {"status", QJsonArray::fromStringList(network.lines())}};
        });
        diagnostics.setExtraBundle(QStringLiteral("layout"), [&layout] {
            return QJsonObject{{"path", layout.path()}, {"status", layout.status()}, {"contents", layout.contents()}};
        });
        diagnostics.setExtraBundle(QStringLiteral("layersDetail"), [&plugins] {
            return QJsonArray::fromVariantList(plugins.layers());
        });
        diagnostics.setExtraBundle(QStringLiteral("healthDetail"), [&health] {
            return QJsonArray::fromVariantList(health.streams());
        });
        diagnostics.start();
        if (const int port = opts->extra(QStringLiteral("metricsPort"), 0).toInt(); port > 0) {
            QString error;
            if (!diagnostics.startMetrics(quint16(port), &error)) {
                QTextStream(stderr) << "--set metricsPort=" << port << ": " << error << "\n";
                return 4;
            }
        }
    }
    // QML items declared in the window are QObject children of the window, not of its contentItem.
    auto *viewport = window ? window->findChild<QQuickItem *>(QStringLiteral("viewport")) : nullptr;
    const bool wantsRobot = robotMode == QLatin1String("orbit") || robotMode == QLatin1String("click");
    if (wantsRobot && !viewport) {
        QTextStream(stderr) << "--set robot=" << robotMode << ": viewport not found\n";
        return 3;
    }
    if (robotMode == QLatin1String("scrub")) {
        auto *bar = window ? window->findChild<QQuickItem *>(QStringLiteral("scrubBar")) : nullptr;
        if (!bar) {
            QTextStream(stderr) << "--set robot=scrub: scrub bar not found\n";
            return 3;
        }
        const QPointF centre = bar->mapToScene(QPointF(bar->width() / 2, bar->height() / 2));
        robot = std::make_unique<InputRobot>(window, InputRobot::Mode::Scrub, centre, nullptr, bar->width() * 0.45);
    }
    if (wantsRobot) {
        const QPointF centre = viewport->mapToScene(QPointF(viewport->width() / 2, viewport->height() / 2));
        const double pxPerMetre = 7; // ViewSettings.pxPerMetre
        robot = std::make_unique<InputRobot>(
            window, robotMode == QLatin1String("orbit") ? InputRobot::Mode::Orbit : InputRobot::Mode::Click, centre,
            [&store, centre, pxPerMetre] {
                const qp::TrackSetPtr set = store.latestTracks();
                if (!set || set->tracks.empty())
                    return QPointF();
                const qp::Track &t = set->tracks[size_t(std::rand()) % set->tracks.size()];
                return centre + QPointF(t.x * pxPerMetre, -t.y * pxPerMetre);
            });
    }
    std::unique_ptr<QSocketNotifier> signalNotifier;
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalPipe) == 0) {
        signalNotifier = std::make_unique<QSocketNotifier>(g_signalPipe[1], QSocketNotifier::Read);
        QObject::connect(signalNotifier.get(), &QSocketNotifier::activated, &app, [&diagnostics] {
            char c;
            [[maybe_unused]] const auto n = ::read(g_signalPipe[1], &c, 1);
            diagnostics.note(QStringLiteral("viewer"), QStringLiteral("asked to stop by a signal"));
            QCoreApplication::quit();
        });
        struct sigaction sa = {};
        sa.sa_handler = onSignal;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
    }
    const int rc = app.exec();

    layout.save();
    // --set bundleOnExit=<file>: a diagnostic bundle as the viewer stops (soak runs, measurements).
    if (const QString bundle = opts->extra(QStringLiteral("bundleOnExit")).toString(); !bundle.isEmpty())
        diagnostics.saveBundle(bundle);
    diagnostics.note(QStringLiteral("viewer"), QStringLiteral("stopped (exit %1)").arg(rc));

    client.reset(); // it feeds the timeline
    timeline.setPublisher(nullptr);
    server.reset();
    cameras.setMarkSink({}); // the timeline goes before the cameras
    pipeline.shutdown();
    if (sim)
        sim->stop();
    return rc;
}
