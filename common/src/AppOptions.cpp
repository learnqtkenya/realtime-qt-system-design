#include "qp/AppOptions.h"

#include <QCommandLineParser>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTextStream>

namespace qp {

AppOptions *AppOptions::instance()
{
    static AppOptions *s = new AppOptions;
    return s;
}

AppOptions *AppOptions::create(QQmlEngine *, QJSEngine *)
{
    AppOptions *o = instance();
    QJSEngine::setObjectOwnership(o, QJSEngine::CppOwnership);
    return o;
}

bool AppOptions::parse(const QStringList &arguments)
{
    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("QtQMLPro module app"));
    p.addHelpOption();
    p.addOption({"bench", "Run a timed benchmark, write a JSON report, then exit."});
    p.addOption({"warmup", "Warm-up seconds discarded before measuring.", "s", "3"});
    p.addOption({"duration", "Measured seconds.", "s", "20"});
    p.addOption({"out", "Report path.", "file", "report.json"});
    p.addOption({"label", "Free-form run label.", "text", ""});
    p.addOption({"sensors", "Number of lidar sensors (1-8).", "n", "4"});
    p.addOption({"points", "Points per sensor per frame.", "n", "10000"});
    p.addOption({"rate", "Lidar frame rate in Hz.", "hz", "10"});
    p.addOption({"objects", "Tracked objects.", "n", "50"});
    p.addOption({"events", "Events per second.", "n", "50"});
    p.addOption({"seed", "Simulator seed.", "n", "1"});
    p.addOption({"no-sim", "Do not start the simulator (idle control)."});
    p.addOption({"api", "Graphics API: default|opengl|vulkan|software.", "name", "default"});
    p.addOption({"policy", "Full-queue policy: block|drop-newest|drop-oldest.", "name", "drop-oldest"});
    p.addOption({"queue", "Queue depth per lane.", "n", "1"});
    p.addOption({"workers", "Worker threads.", "n", "4"});
    p.addOption({"variant", "Module-specific implementation variant.", "name", ""});
    p.addOption({"still", "Freeze the scene: fixed lidar cloud, stationary tracks."});
    p.addOption({"grab", "Save a window screenshot to this file, then exit.", "file", ""});
    p.addOption({"grab-after", "Seconds before --grab.", "s", "3"});
    p.addOption({"set", "App-specific setting, repeatable.", "key=value"});

    QTextStream err(stderr);
    if (!p.parse(arguments)) {
        err << p.errorText() << '\n';
        return false;
    }
    if (p.isSet("help")) {
        err << p.helpText() << '\n';
        return false;
    }

    auto num = [&](const char *name, double lo, double hi, double &out) {
        bool ok = false;
        const double v = p.value(name).toDouble(&ok);
        if (!ok || v < lo || v > hi) {
            err << "invalid value for --" << name << ": " << p.value(name) << '\n';
            return false;
        }
        out = v;
        return true;
    };

    double v = 0;
    if (!num("warmup", 0, 3600, m_warmupSec) || !num("duration", 1, 36000, m_durationSec))
        return false;
    if (!num("sensors", 1, kMaxSensors, v))
        return false;
    m_workload.sensors = int(v);
    if (!num("points", 0, 5'000'000, v))
        return false;
    m_workload.pointsPerFrame = int(v);
    if (!num("rate", 0.1, 240, m_workload.lidarRateHz))
        return false;
    if (!num("objects", 0, 100000, v))
        return false;
    m_workload.objects = int(v);
    if (!num("events", 0, 1'000'000, v))
        return false;
    m_workload.eventsPerSecond = int(v);
    if (!num("seed", 0, 4294967295.0, v))
        return false;
    m_workload.seed = uint32_t(v);

    m_bench = p.isSet("bench");
    m_outPath = p.value("out");
    m_label = p.value("label");
    m_noSim = p.isSet("no-sim");
    m_graphicsApi = p.value("api");
    m_policy = p.value("policy");
    if (!QStringList{"block", "drop-newest", "drop-oldest"}.contains(m_policy)) {
        err << "invalid value for --policy: " << m_policy << '\n';
        return false;
    }
    if (!num("queue", 1, 1024, v))
        return false;
    m_queueDepth = int(v);
    if (!num("workers", 1, 64, v))
        return false;
    m_workers = int(v);
    m_variant = p.value("variant");
    m_workload.still = p.isSet("still");
    m_grabPath = p.value("grab");
    for (const QString &kv : p.values("set")) {
        const qsizetype eq = kv.indexOf(u'=');
        if (eq <= 0) {
            err << "invalid --set (expected key=value): " << kv << '\n';
            return false;
        }
        m_extra.insert(kv.left(eq), kv.mid(eq + 1));
    }
    if (!num("grab-after", 0.5, 600, m_grabAfterSec))
        return false;
    if (!QStringList{"default", "opengl", "vulkan", "software"}.contains(m_graphicsApi)) {
        err << "invalid value for --api: " << m_graphicsApi << '\n';
        return false;
    }
    return true;
}

void AppOptions::applyGraphicsApi() const
{
    if (m_graphicsApi == QLatin1String("opengl"))
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    else if (m_graphicsApi == QLatin1String("vulkan"))
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    else if (m_graphicsApi == QLatin1String("software"))
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
}

QVariant AppOptions::extra(const QString &key, const QVariant &fallback) const
{
    return m_extra.value(key, fallback);
}

QVariantMap AppOptions::workloadMap() const
{
    return {
        {"sensors", m_workload.sensors},
        {"pointsPerFrame", m_workload.pointsPerFrame},
        {"lidarRateHz", m_workload.lidarRateHz},
        {"pointsPerSecond", m_workload.pointsPerSecond()},
        {"objects", m_workload.objects},
        {"trackRateHz", m_workload.trackRateHz},
        {"eventsPerSecond", m_workload.eventsPerSecond},
        {"seed", m_workload.seed},
        {"noSim", m_noSim},
        {"requestedGraphicsApi", m_graphicsApi},
        {"policy", m_policy},
        {"queueDepth", m_queueDepth},
        {"workers", m_workers},
        {"variant", m_variant},
        {"still", m_workload.still},
        {"set", m_extra},
    };
}

} // namespace qp
