#include "qp/FrameMonitor.h"

#include "qp/AppOptions.h"
#include "qp/SimTypes.h"
#include "qp/StageStats.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QScreen>
#include <QSGRendererInterface>
#include <QSysInfo>
#include <QThread>
#include <QTimer>

#include <rhi/qrhi.h>

#include <pthread.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>

Q_LOGGING_CATEGORY(lcFrameMonitor, "qp.framemonitor")

namespace qp {

namespace {

constexpr double kBudgetMs = 1000.0 / 60.0;

std::atomic<qint64> g_pendingCaptureNs{0};    // shown by the next swap
std::atomic<qint64> g_pendingGuiCaptureNs{0}; // enters the scene graph at the next sync
// Input: the oldest event not yet shown, because that is the one the user is waiting for.
std::atomic<qint64> g_pendingInputNs{0};
std::atomic<qint64> g_pendingGuiInputNs{0};

void storeOldest(std::atomic<qint64> &slot, qint64 value)
{
    qint64 cur = slot.load();
    while ((cur == 0 || value < cur) && !slot.compare_exchange_weak(cur, value)) {
    }
}

void storeNewest(std::atomic<qint64> &slot, qint64 value)
{
    qint64 cur = slot.load();
    while (value > cur && !slot.compare_exchange_weak(cur, value)) {
    }
}

double clockSeconds(clockid_t clock)
{
    timespec ts{};
    clock_gettime(clock, &ts);
    return double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9;
}

double processCpuSeconds()
{
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    return double(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec)
           + double(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) * 1e-6;
}

double rssMegabytes()
{
    QFile f(QStringLiteral("/proc/self/statm"));
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    const QList<QByteArray> parts = f.readAll().split(' ');
    if (parts.size() < 2)
        return 0;
    return double(parts[1].toLongLong()) * double(sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0);
}

QString apiName(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::OpenGL: return QStringLiteral("opengl");
    case QSGRendererInterface::Vulkan: return QStringLiteral("vulkan");
    case QSGRendererInterface::Software: return QStringLiteral("software");
    case QSGRendererInterface::Direct3D11: return QStringLiteral("d3d11");
    case QSGRendererInterface::Direct3D12: return QStringLiteral("d3d12");
    case QSGRendererInterface::Metal: return QStringLiteral("metal");
    case QSGRendererInterface::Null: return QStringLiteral("null");
    default: return QStringLiteral("other");
    }
}

} // namespace

void FrameMonitor::Series::clear()
{
    frameMs.clear();
    syncMs.clear();
    renderMs.clear();
    gpuMs.clear();
    inputMs.clear();
    inputDispatchMs.clear();
    latencyMs.clear();
    lagMs.clear();
    guiCpuPct.clear();
    procCpuPct.clear();
    rssMb.clear();
}

FrameMonitor::FrameMonitor(QObject *parent) : QObject(parent)
{
    m_lagTimer.setTimerType(Qt::PreciseTimer);
    m_lagTimer.setInterval(2);
    connect(&m_lagTimer, &QTimer::timeout, this, &FrameMonitor::onLagTick);

    m_secondTimer.setTimerType(Qt::CoarseTimer);
    m_secondTimer.setInterval(1000);
    connect(&m_secondTimer, &QTimer::timeout, this, &FrameMonitor::onSecondTick);

    m_prevLagTickNs = nowNs();
    m_lastLagTickNs = m_prevLagTickNs;
    m_prevSecondNs = m_prevLagTickNs;
    pthread_getcpuclockid(pthread_self(), &m_guiCpuClock); // constructed on the GUI thread
    m_lagTimer.start();
    m_secondTimer.start();
    m_sampler = std::thread([this] { samplerMain(); });
}

FrameMonitor::~FrameMonitor()
{
    {
        std::lock_guard lk(m_wdMutex);
        m_wdStop = true;
    }
    m_wdCv.notify_all();
    if (m_watchdog.joinable())
        m_watchdog.join();
    if (m_sampler.joinable())
        m_sampler.join();
}

QString FrameMonitor::graphicsApi() const
{
    std::lock_guard lk(m_mutex);
    return m_graphicsApi;
}

void FrameMonitor::setWindow(QQuickWindow *window)
{
    if (m_window == window)
        return;
    if (m_window)
        m_window->disconnect(this);
    m_window = window;
    if (m_window) {
        // Emitted on the render thread.
        connect(m_window, &QQuickWindow::beforeSynchronizing, this, &FrameMonitor::onBeforeSync,
                Qt::DirectConnection);
        connect(m_window, &QQuickWindow::afterSynchronizing, this, &FrameMonitor::onAfterSync,
                Qt::DirectConnection);
        connect(m_window, &QQuickWindow::beforeRendering, this, &FrameMonitor::onBeforeRender,
                Qt::DirectConnection);
        connect(m_window, &QQuickWindow::afterRendering, this, &FrameMonitor::onAfterRender,
                Qt::DirectConnection);
        connect(m_window, &QQuickWindow::frameSwapped, this, &FrameMonitor::onFrameSwapped,
                Qt::DirectConnection);
        connect(m_window, &QQuickWindow::sceneGraphInitialized, this,
                &FrameMonitor::onSceneGraphInitialized, Qt::DirectConnection);
        m_window->installEventFilter(this);
        if (QScreen *s = m_window->screen())
            m_refreshHz = s->refreshRate();
        m_dpr = m_window->effectiveDevicePixelRatio();

        const AppOptions *opts = AppOptions::instance();
        if (!opts->grabPath().isEmpty()) {
            QTimer::singleShot(int(opts->grabAfterSec() * 1000), this, [this, path = opts->grabPath()] {
                if (!m_window)
                    return;
                const QImage image = m_window->grabWindow();
                QDir().mkpath(QFileInfo(path).absolutePath());
                if (!image.save(path))
                    qCWarning(lcFrameMonitor) << "cannot save grab" << path;
                QCoreApplication::quit();
            });
        }
    }
    emit windowChanged();
}

void FrameMonitor::onSceneGraphInitialized()
{
    if (!m_window)
        return;
    QSGRendererInterface *ri = m_window->rendererInterface();
    if (!ri)
        return;
    QJsonObject info;
    if (QRhi *rhi = m_window->rhi()) {
        info["backend"] = QString::fromLatin1(rhi->backendName());
        info["device"] = QString::fromUtf8(rhi->driverInfo().deviceName);
        info["vertexShaderPointSize"] = rhi->isFeatureSupported(QRhi::VertexShaderPointSize);
        info["timestamps"] = rhi->isFeatureSupported(QRhi::Timestamps);
    }
    std::lock_guard lk(m_mutex);
    m_graphicsApi = apiName(ri->graphicsApi());
    m_rhiInfo = info;
}

void FrameMonitor::onBeforeSync()
{
    m_syncStart = nowNs();
    const qint64 gui = g_pendingGuiCaptureNs.exchange(0);
    if (gui > 0)
        storeNewest(g_pendingCaptureNs, gui);
    const qint64 input = g_pendingGuiInputNs.exchange(0);
    if (input > 0)
        storeOldest(g_pendingInputNs, input);
}

void FrameMonitor::onAfterSync()
{
    const double ms = double(nowNs() - m_syncStart) / 1e6;
    std::lock_guard lk(m_mutex);
    m_cur.syncMs.add(ms);
    if (m_benchActive)
        m_bench.syncMs.add(ms);
}

void FrameMonitor::onBeforeRender()
{
    m_renderStart = nowNs();
    // Refers to an earlier, completed frame; 0 unless QSG_RHI_PROFILE=1.
    QRhiSwapChain *sc = m_window ? m_window->swapChain() : nullptr;
    QRhiCommandBuffer *cb = sc ? sc->currentFrameCommandBuffer() : nullptr;
    const double gpuMs = cb ? cb->lastCompletedGpuTime() * 1000.0 : 0.0;
    if (gpuMs <= 0)
        return;
    std::lock_guard lk(m_mutex);
    m_cur.gpuMs.add(gpuMs);
    if (m_benchActive)
        m_bench.gpuMs.add(gpuMs);
}

void FrameMonitor::onAfterRender()
{
    const double ms = double(nowNs() - m_renderStart) / 1e6;
    std::lock_guard lk(m_mutex);
    m_cur.renderMs.add(ms);
    if (m_benchActive)
        m_bench.renderMs.add(ms);
}

void FrameMonitor::onFrameSwapped()
{
    const int64_t now = nowNs();
    const int64_t prev = m_lastSwapNs.exchange(now);
    const qint64 capture = g_pendingCaptureNs.exchange(0);
    const qint64 input = g_pendingInputNs.exchange(0);

    std::lock_guard lk(m_mutex);
    ++m_curFrames;
    if (m_benchActive)
        ++m_benchFrames;
    if (prev > 0) {
        const double ms = double(now - prev) / 1e6;
        m_cur.frameMs.add(ms);
        if (m_benchActive)
            m_bench.frameMs.add(ms);
    }
    if (input > 0) {
        const double ms = double(now - input) / 1e6;
        m_cur.inputMs.add(ms);
        if (m_benchActive)
            m_bench.inputMs.add(ms);
    }
    if (capture > 0) {
        QMetaObject::invokeMethod(this, &FrameMonitor::contentShown, Qt::QueuedConnection);
        const double ms = double(now - capture) / 1e6;
        m_cur.latencyMs.add(ms);
        if (m_benchActive)
            m_bench.latencyMs.add(ms);
    }
}

void markContentCaptured(qint64 captureNs)
{
    // GUI-thread content can only reach the screen after the next sync.
    const QCoreApplication *app = QCoreApplication::instance();
    const bool onGuiThread = app && QThread::currentThread() == app->thread();
    storeNewest(onGuiThread ? g_pendingGuiCaptureNs : g_pendingCaptureNs, captureNs);
}

bool FrameMonitor::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::KeyPress: {
        // Event timestamps are 32-bit milliseconds; compare the same way they wrap.
        const qint64 now = nowNs();
        const quint32 waitedMs = quint32(now / 1'000'000) - quint32(static_cast<QInputEvent *>(event)->timestamp());
        if (waitedMs > 10'000)
            break; // another clock (for example X11 server time): unusable
        storeOldest(g_pendingGuiInputNs, now - qint64(waitedMs) * 1'000'000);
        std::lock_guard lk(m_mutex);
        m_cur.inputDispatchMs.add(waitedMs);
        if (m_benchActive)
            m_bench.inputDispatchMs.add(waitedMs);
        break;
    }
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void FrameMonitor::reportDisplayed(qint64 captureNs)
{
    markContentCaptured(captureNs);
    if (m_window)
        m_window->update();
}

void FrameMonitor::onLagTick()
{
    const int64_t now = nowNs();
    const double lateMs = std::max(0.0, double(now - m_prevLagTickNs) / 1e6 - 2.0);
    m_prevLagTickNs = now;
    m_lastLagTickNs.store(now);
    std::lock_guard lk(m_mutex);
    m_cur.lagMs.add(lateMs);
    if (m_benchActive)
        m_bench.lagMs.add(lateMs);
}

void FrameMonitor::samplerMain()
{
    int64_t prevNs = nowNs();
    double prevGui = clockSeconds(m_guiCpuClock);
    double prevProc = processCpuSeconds();

    std::unique_lock lk(m_wdMutex);
    while (!m_wdCv.wait_for(lk, std::chrono::seconds(1), [this] { return m_wdStop; })) {
        const int64_t now = nowNs();
        const double wall = double(now - prevNs) / 1e9;
        const double gui = clockSeconds(m_guiCpuClock);
        const double proc = processCpuSeconds();
        const double guiPct = (gui - prevGui) / wall * 100.0;
        const double procPct = (proc - prevProc) / wall * 100.0;
        const double rss = rssMegabytes();
        prevNs = now;
        prevGui = gui;
        prevProc = proc;

        m_lastGuiCpu = guiPct;
        m_lastProcCpu = procPct;
        m_lastRss = rss;

        std::lock_guard sl(m_mutex);
        if (m_benchActive) {
            m_bench.guiCpuPct.add(guiPct);
            m_bench.procCpuPct.add(procPct);
            m_bench.rssMb.add(rss);
        }
    }
}

void FrameMonitor::onSecondTick()
{
    const int64_t now = nowNs();
    const double wall = double(now - m_prevSecondNs) / 1e9;
    m_prevSecondNs = now;
    if (wall <= 0)
        return;

    {
        std::lock_guard lk(m_mutex);
        Snapshot s;
        s.fps = double(m_curFrames) / wall;
        s.frameMean = m_cur.frameMs.mean();
        s.frameP99 = m_cur.frameMs.percentile(99);
        s.frameMax = m_cur.frameMs.max();
        s.lagP99 = m_cur.lagMs.percentile(99);
        s.lagMax = m_cur.lagMs.max();
        s.guiCpu = m_lastGuiCpu;
        s.procCpu = m_lastProcCpu;
        s.rss = m_lastRss;
        s.latMean = m_cur.latencyMs.mean();
        s.latMax = m_cur.latencyMs.max();
        s.gpuMean = m_cur.gpuMs.mean();
        s.inputMean = m_cur.inputMs.mean();
        s.slow = int(m_cur.frameMs.countAbove(kBudgetMs * 1.2));
        m_snap = s;
        if (m_benchActive) {
            m_benchFpsSeries.append(s.fps);
            m_benchFrameP99Series.append(s.frameP99);
            m_benchLagP99Series.append(s.lagP99);
        }
        m_cur.clear();
        m_curFrames = 0;
    }
    const qreal values[6] = {m_snap.frameP99, m_snap.lagP99, m_snap.procCpu, m_snap.latMean, m_snap.gpuMean,
                             m_snap.inputMean};
    for (int i = 0; i < 6; ++i) {
        m_hist[i].append(values[i]);
        if (m_hist[i].size() > kHistoryLength)
            m_hist[i].removeFirst();
    }
    emit snapshotChanged();
}

void FrameMonitor::startBenchmark(double warmupSec, double durationSec, const QString &outPath,
                                  const QString &label, const QVariantMap &extras)
{
    if (m_watchdog.joinable())
        return;
    m_benchOut = outPath;
    m_benchLabel = label;
    m_benchExtras = QJsonObject::fromVariantMap(extras);
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (m_reportWritten)
            return;
        const double measured = m_benchStartNs > 0 ? double(nowNs() - m_benchStartNs) / 1e9 : 0.0;
        QJsonObject report = buildReport(measured);
        report["aborted"] = QStringLiteral("application quit before the benchmark ended");
        writeReport(report);
    });
    m_watchdog = std::thread([this, warmupSec, durationSec] { watchdogMain(warmupSec, durationSec); });
}

void FrameMonitor::watchdogMain(double warmupSec, double durationSec)
{
    using namespace std::chrono;
    auto stopped = [this] { return m_wdStop; };
    std::unique_lock lk(m_wdMutex);

    if (m_wdCv.wait_for(lk, duration<double>(warmupSec), stopped))
        return;

    {
        std::lock_guard sl(m_mutex);
        m_bench.clear();
        m_benchFrames = 0;
        m_benchFpsSeries.clear();
        m_benchFrameP99Series.clear();
        m_benchLagP99Series.clear();
        m_benchActive = true;
        m_benchStartNs = nowNs();
    }
    StageStats::instance().reset();

    if (m_wdCv.wait_for(lk, duration<double>(durationSec), stopped))
        return;

    const double measured = double(nowNs() - m_benchStartNs) / 1e9;
    writeReport(buildReport(measured));

    QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);

    if (m_wdCv.wait_for(lk, seconds(8), stopped))
        return;
    qCWarning(lcFrameMonitor) << "GUI thread did not exit after the benchmark; forcing exit";
    std::_Exit(4);
}

void FrameMonitor::writeReport(const QJsonObject &report)
{
    if (m_reportWritten.exchange(true))
        return;
    QDir().mkpath(QFileInfo(m_benchOut).absolutePath());
    QFile f(m_benchOut);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    else
        qCWarning(lcFrameMonitor) << "cannot write report" << m_benchOut;
}

QJsonObject FrameMonitor::buildReport(double measuredSec)
{
    const int64_t now = nowNs();
    QJsonObject r;
    r["label"] = m_benchLabel;
    r["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    r["qtVersion"] = QString::fromLatin1(qVersion());
    r["kernel"] = QSysInfo::kernelVersion();
    r["measuredSeconds"] = measuredSec;
    r["budgetMs"] = kBudgetMs;
    r["extras"] = m_benchExtras;
    r["refreshHz"] = m_refreshHz;
    r["devicePixelRatio"] = m_dpr;

    std::lock_guard lk(m_mutex);
    r["graphicsApi"] = m_graphicsApi;
    r["rhi"] = m_rhiInfo;
    r["gpuMs"] = m_bench.gpuMs.toJson();
    r["inputLatencyMs"] = m_bench.inputMs.toJson();
    r["inputDispatchMs"] = m_bench.inputDispatchMs.toJson();
    r["frames"] = double(m_benchFrames);
    r["fps"] = measuredSec > 0 ? double(m_benchFrames) / measuredSec : 0.0;

    r["frameMs"] = m_bench.frameMs.toJson();
    r["syncMs"] = m_bench.syncMs.toJson();
    r["renderMs"] = m_bench.renderMs.toJson();
    r["latencyMs"] = m_bench.latencyMs.toJson();
    r["eventLoopLagMs"] = m_bench.lagMs.toJson();
    r["guiCpuPercent"] = m_bench.guiCpuPct.toJson();
    r["processCpuPercent"] = m_bench.procCpuPct.toJson();
    r["rssMb"] = m_bench.rssMb.toJson();

    QJsonObject over;
    over["gt20ms"] = double(m_bench.frameMs.countAbove(20.0));
    over["gt33ms"] = double(m_bench.frameMs.countAbove(33.4));
    over["gt100ms"] = double(m_bench.frameMs.countAbove(100.0));
    r["framesOver"] = over;

    // Least-squares slope; samples are one second apart.
    const std::vector<double> &rss = m_bench.rssMb.values();
    double slope = 0;
    if (rss.size() >= 3) {
        const double n = double(rss.size());
        const double mx = (n - 1) / 2.0;
        const double my = m_bench.rssMb.mean();
        double sxy = 0, sxx = 0;
        for (size_t i = 0; i < rss.size(); ++i) {
            sxy += (double(i) - mx) * (rss[i] - my);
            sxx += (double(i) - mx) * (double(i) - mx);
        }
        slope = sxy / sxx;
    }
    r["rssGrowthMb"] = rss.empty() ? 0.0 : rss.back() - rss.front();
    QJsonArray rssSeries;
    for (double v : rss)
        rssSeries.append(v);
    r["rssSeriesMb"] = rssSeries;
    r["fpsSeries"] = QJsonArray::fromVariantList(QVariantList(m_benchFpsSeries.begin(), m_benchFpsSeries.end()));
    r["frameP99Series"] = QJsonArray::fromVariantList(QVariantList(m_benchFrameP99Series.begin(), m_benchFrameP99Series.end()));
    r["lagP99Series"] = QJsonArray::fromVariantList(QVariantList(m_benchLagP99Series.begin(), m_benchLagP99Series.end()));
    r["rssGrowthMbPerMinute"] = slope * 60.0;

    const int64_t lastSwap = m_lastSwapNs.load();
    const int64_t lastTick = m_lastLagTickNs.load();
    r["msSinceLastFrame"] = lastSwap > 0 ? double(now - lastSwap) / 1e6 : -1.0;
    r["msSinceLastGuiTick"] = double(now - lastTick) / 1e6;
    r["stalled"] = (lastSwap > 0 && double(now - lastSwap) / 1e6 > 2000.0)
                   || double(now - lastTick) / 1e6 > 2000.0;

    // Presentation stopped while the GUI thread stayed responsive: the compositor, not us.
    double pauseMs = 0;
    for (double v : m_bench.frameMs.values())
        if (v > 250.0)
            pauseMs += v;
    const double tailMs = lastSwap > 0 ? double(now - lastSwap) / 1e6 : 0.0;
    if (tailMs > 250.0)
        pauseMs += tailMs;
    r["presentationPauseMs"] = pauseMs;
    r["suspectExternalPause"] = pauseMs > 1000.0 && m_bench.lagMs.max() < 500.0;

    r["stages"] = StageStats::instance().toJson();
    return r;
}

} // namespace qp
