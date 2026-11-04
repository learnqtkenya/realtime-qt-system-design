#include "Lab.h"

#include "HealthMonitor.h"
#include "qp/SensorSimulator.h"
#include "qp/StageStats.h"

#include <QVariantMap>

namespace {

struct RendererInfo
{
    const char *id;
    const char *name;
    const char *detail;
    double maxPointsPerSecond; // what the path can sustain; the slider cap follows the rate
    LidarHub::Route route;
};

const RendererInfo kRenderers[] = {
    {"canvas", "JS Canvas", "Points become a QVariantList, then a JS array; Canvas draws them on the GUI thread.",
     0.5e6, LidarHub::Route::Canvas},
    {"painted", "Painted item", "C++ owns the frames; QPainter draws on the render thread while the GUI thread waits.",
     6e6, LidarHub::Route::Store},
    {"worker", "Worker raster", "Per-sensor lanes, latest frame wins; QPainter on four worker threads.",
     80e6, LidarHub::Route::Pipeline},
    {"sg", "Scene graph GPU", "QSGGeometryNode with a point-size shader; points copied during sync.",
     80e6, LidarHub::Route::Mailbox},
    {"rhi", "RHI GPU", "QSGRenderNode with its own QRhi buffers; the GUI thread is not involved.",
     80e6, LidarHub::Route::Mailbox},
    {"quick3d", "Quick 3D GPU", "QQuick3DGeometry in a View3D; points copied on the GUI thread.",
     80e6, LidarHub::Route::Mailbox},
};

const RendererInfo *find(const QString &id)
{
    for (const RendererInfo &r : kRenderers)
        if (id == QLatin1String(r.id))
            return &r;
    return nullptr;
}

} // namespace

Lab::Lab(LidarHub *hub, const qp::Workload &workload, const QString &renderer, bool chartsEnabled,
         QObject *parent)
    : QObject(parent), m_hub(hub), m_workload(workload), m_chartsEnabled(chartsEnabled)
{
    m_apply.setSingleShot(true);
    m_apply.setInterval(250);
    connect(&m_apply, &QTimer::timeout, this, [this] {
        if (m_sim)
            m_sim->setWorkload(m_workload);
    });
    connect(&m_guiBlock, &QTimer::timeout, this, &Lab::blockGui);
    connect(&m_loadCycle, &QTimer::timeout, this, [this] {
        setPointsPerSensor(pointsPerSensor() == 20'000 ? maxPoints() : 20'000);
    });
    connect(&m_cycle, &QTimer::timeout, this, [this] {
        setRenderer(m_cycleIds[(m_cycleIds.indexOf(m_renderer) + 1) % m_cycleIds.size()]);
    });
    setRenderer(find(renderer) ? renderer : QStringLiteral("rhi"));
}

QStringList Lab::rendererIds()
{
    QStringList ids;
    for (const RendererInfo &r : kRenderers)
        ids << QLatin1String(r.id);
    return ids;
}

void Lab::setRenderer(const QString &id)
{
    const RendererInfo *info = find(id);
    if (!info || id == m_renderer)
        return;
    m_renderer = id;
    applyRoute();
    m_switchedAtNs = qp::nowNs();
    emit rendererChanged();
    enforceCap();
}

QVariantList Lab::renderers() const
{
    QVariantList out;
    for (const RendererInfo &r : kRenderers)
        out << QVariantMap{{"id", QLatin1String(r.id)},
                           {"name", QLatin1String(r.name)},
                           {"detail", QLatin1String(r.detail)},
                           {"maxPointsPerSecond", r.maxPointsPerSecond}};
    return out;
}

void Lab::applyRoute()
{
    // The orbit view always draws with Quick 3D, which reads the mailbox.
    const RendererInfo *info = find(m_renderer);
    m_hub->setRoute(m_viewMode == QLatin1String("orbit") ? LidarHub::Route::Mailbox
                    : info                               ? info->route
                                                         : LidarHub::Route::None);
}

void Lab::setViewMode(const QString &mode)
{
    if ((mode != QLatin1String("top") && mode != QLatin1String("orbit")) || mode == m_viewMode)
        return;
    m_viewMode = mode;
    applyRoute();
    m_switchedAtNs = qp::nowNs();
    emit viewModeChanged();
    emit rendererChanged(); // the cap depends on what draws
    enforceCap();
}

void Lab::setLodEnabled(bool on)
{
    if (on == m_lodEnabled)
        return;
    m_lodEnabled = on;
    emit lodEnabledChanged();
}

int Lab::maxPoints() const
{
    const RendererInfo *info = m_viewMode == QLatin1String("orbit") ? find(QStringLiteral("quick3d")) : find(m_renderer);
    const double pps = info ? info->maxPointsPerSecond : 80e6;
    const double perFrame = pps / (std::max(1, m_workload.sensors) * std::max(1.0, m_workload.lidarRateHz));
    return int(std::min(1'000'000.0, std::floor(perFrame / 1000) * 1000));
}

void Lab::enforceCap()
{
    if (m_workload.pointsPerFrame > maxPoints()) {
        m_workload.pointsPerFrame = maxPoints();
        workloadEdited();
    }
}

void Lab::setPointsPerSensor(int n)
{
    n = std::clamp(n, 0, maxPoints());
    if (n == m_workload.pointsPerFrame)
        return;
    m_workload.pointsPerFrame = n;
    workloadEdited();
}

void Lab::setRateHz(double hz)
{
    hz = std::clamp(hz, 1.0, 60.0);
    if (qFuzzyCompare(hz, m_workload.lidarRateHz))
        return;
    m_workload.lidarRateHz = hz;
    workloadEdited();
    emit rendererChanged(); // maxPoints depends on the rate
    enforceCap();
}

void Lab::setObjects(int n)
{
    n = std::clamp(n, 0, 2000);
    if (n == m_workload.objects)
        return;
    m_workload.objects = n;
    workloadEdited();
}

void Lab::setEventsPerSecond(int n)
{
    n = std::clamp(n, 0, 5000);
    if (n == m_workload.eventsPerSecond)
        return;
    m_workload.eventsPerSecond = n;
    workloadEdited();
}

void Lab::setLidarDelayMs(int ms)
{
    ms = std::clamp(ms, 0, 1000);
    if (ms == lidarDelayMs())
        return;
    m_workload.lidarDelayMs = ms;
    workloadEdited();
}

void Lab::resetToNominal()
{
    m_workload.pointsPerFrame = std::min(150'000, maxPoints());
    m_workload.lidarRateHz = 10;
    m_workload.objects = 50;
    m_workload.eventsPerSecond = 50;
    workloadEdited();
}

void Lab::startCycle(double intervalSec, const QStringList &ids)
{
    m_cycleIds = ids.isEmpty() ? rendererIds() : ids;
    m_cycle.start(int(intervalSec * 1000));
}

void Lab::startLoadCycle(double intervalSec)
{
    m_loadCycle.start(int(intervalSec * 1000));
}

// The gui300 fault: the GUI thread busy for 300 ms, as a long synchronous call would make it.
void Lab::blockGui()
{
    const qint64 until = qp::nowNs() + 300'000'000;
    while (qp::nowNs() < until) {
    }
}

void Lab::injectFault(int stream, const QString &kind)
{
    if (kind == QLatin1String("gui300")) {
        m_guiBlock.start(2000);
        QTimer::singleShot(0, this, &Lab::blockGui); // the first block now, not in 2 s
        stream = qp::kGuiStream;
    } else if (kind == QLatin1String("clear") && stream == qp::kGuiStream) {
        m_guiBlock.stop();
    } else {
        qp::Fault f;
        if (kind == QLatin1String("drop50"))
            f.dropRate = 0.5;
        else if (kind == QLatin1String("delay300"))
            f.delayMs = 300;
        else if (kind == QLatin1String("jitter100"))
            f.jitterMs = 100;
        else if (kind == QLatin1String("corrupt"))
            f.corrupt = true;
        else if (kind == QLatin1String("stall"))
            f.stall = true;
        else if (kind != QLatin1String("clear"))
            return;
        if (m_sim)
            m_sim->setFault(stream, f);
    }
    if (m_health)
        m_health->noteFault(stream, kind);
    if (kind == QLatin1String("clear"))
        m_faults.remove(QString::number(stream));
    else
        m_faults.insert(QString::number(stream), kind);
    emit faultsChanged();
}

void Lab::noteContentShown()
{
    if (m_switchedAtNs == 0)
        return;
    qp::StageStats::instance().add(QStringLiteral("switch_to_content_ms"),
                                   double(qp::nowNs() - m_switchedAtNs) / 1e6);
    m_switchedAtNs = 0;
}

void Lab::workloadEdited()
{
    m_apply.start();
    emit workloadChanged();
}
