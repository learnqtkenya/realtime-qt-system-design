#pragma once

#include "qp/RunningSamples.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QVariantMap>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <time.h>

namespace qp {

// Any thread. Latency is recorded at the swap of the first frame that can contain the content.
void markContentCaptured(qint64 captureNs);

// CPU, RSS and the benchmark run on their own threads: a stuck GUI thread must not
// stop us measuring it.
class FrameMonitor : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY windowChanged FINAL)

    Q_PROPERTY(double fps READ fps NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(double frameMsMean READ frameMsMean NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(double frameMsP99 READ frameMsP99 NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(double frameMsMax READ frameMsMax NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(double lagMsP99 READ lagMsP99 NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(double lagMsMax READ lagMsMax NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(double guiCpuPercent READ guiCpuPercent NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(double processCpuPercent READ processCpuPercent NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(double rssMb READ rssMb NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(double latencyMsMean READ latencyMsMean NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(double latencyMsMax READ latencyMsMax NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(int slowFrames READ slowFrames NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(double refreshHz READ refreshHz NOTIFY windowChanged FINAL)
    Q_PROPERTY(QString graphicsApi READ graphicsApi NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(double gpuMsMean READ gpuMsMean NOTIFY snapshotChanged FINAL)
    // One value per second, oldest first, at most historyLength values.
    Q_PROPERTY(QList<qreal> historyFrameP99 READ historyFrameP99 NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(QList<qreal> historyLagP99 READ historyLagP99 NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(QList<qreal> historyCpu READ historyCpu NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(QList<qreal> historyLatency READ historyLatency NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(QList<qreal> historyGpu READ historyGpu NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(int historyLength READ historyLength CONSTANT FINAL)
    Q_PROPERTY(QList<qreal> historyInput READ historyInput NOTIFY snapshotChanged FINAL)

public:
    explicit FrameMonitor(QObject *parent = nullptr);
    ~FrameMonitor() override;

    QQuickWindow *window() const { return m_window; }
    void setWindow(QQuickWindow *window);

    double fps() const { return m_snap.fps; }
    double frameMsMean() const { return m_snap.frameMean; }
    double frameMsP99() const { return m_snap.frameP99; }
    double frameMsMax() const { return m_snap.frameMax; }
    double lagMsP99() const { return m_snap.lagP99; }
    double lagMsMax() const { return m_snap.lagMax; }
    double guiCpuPercent() const { return m_snap.guiCpu; }
    double processCpuPercent() const { return m_snap.procCpu; }
    double rssMb() const { return m_snap.rss; }
    double latencyMsMean() const { return m_snap.latMean; }
    double latencyMsMax() const { return m_snap.latMax; }
    int slowFrames() const { return m_snap.slow; }
    double gpuMsMean() const { return m_snap.gpuMean; }
    QList<qreal> historyFrameP99() const { return m_hist[0]; }
    QList<qreal> historyLagP99() const { return m_hist[1]; }
    QList<qreal> historyCpu() const { return m_hist[2]; }
    QList<qreal> historyLatency() const { return m_hist[3]; }
    QList<qreal> historyGpu() const { return m_hist[4]; }
    QList<qreal> historyInput() const { return m_hist[5]; }
    int historyLength() const { return kHistoryLength; }
    double refreshHz() const { return m_refreshHz; }
    QString graphicsApi() const;

    Q_INVOKABLE void reportDisplayed(qint64 captureNs);

    Q_INVOKABLE void startBenchmark(double warmupSec, double durationSec, const QString &outPath,
                                    const QString &label, const QVariantMap &extras);

signals:
    void windowChanged();
    void snapshotChanged();
    // Queued to the GUI thread when a swap showed newly marked content.
    void contentShown();

private:
    struct Series
    {
        RunningSamples frameMs, syncMs, renderMs, latencyMs, lagMs, gpuMs;
        RunningSamples inputMs, inputDispatchMs;
        RunningSamples guiCpuPct, procCpuPct, rssMb;
        void clear();
    };
    struct Snapshot
    {
        double fps = 0, frameMean = 0, frameP99 = 0, frameMax = 0;
        double lagP99 = 0, lagMax = 0, guiCpu = 0, procCpu = 0, rss = 0;
        double latMean = 0, latMax = 0, gpuMean = 0, inputMean = 0;
        int slow = 0;
    };

    // render thread
    void onBeforeSync();
    void onAfterSync();
    void onBeforeRender();
    void onAfterRender();
    void onFrameSwapped();
    void onSceneGraphInitialized();
    // GUI thread
    void onLagTick();
    void onSecondTick();

    void samplerMain();
    void watchdogMain(double warmupSec, double durationSec);
    bool eventFilter(QObject *watched, QEvent *event) override;
    QJsonObject buildReport(double measuredSec);

    QPointer<QQuickWindow> m_window;
    double m_refreshHz = 0;
    double m_dpr = 1;

    mutable std::mutex m_mutex; // guards everything up to m_snap
    Series m_cur;
    Series m_bench;
    quint64 m_curFrames = 0, m_benchFrames = 0;
    bool m_benchActive = false;
    QString m_graphicsApi = QStringLiteral("unknown");
    QJsonObject m_rhiInfo;
    Snapshot m_snap;
    static constexpr int kHistoryLength = 60;
    QList<qreal> m_hist[6]; // GUI thread only

    // render thread only
    int64_t m_syncStart = 0, m_renderStart = 0;
    std::atomic<int64_t> m_lastSwapNs{0};
    std::atomic<int64_t> m_lastLagTickNs{0};

    // GUI thread only
    QTimer m_lagTimer, m_secondTimer;
    int64_t m_prevLagTickNs = 0;
    int64_t m_prevSecondNs = 0;

    clockid_t m_guiCpuClock{};
    std::atomic<double> m_lastGuiCpu{0}, m_lastProcCpu{0}, m_lastRss{0};
    std::thread m_sampler;
    std::thread m_watchdog;
    std::mutex m_wdMutex;
    std::condition_variable m_wdCv;
    bool m_wdStop = false;
    QString m_benchOut, m_benchLabel;
    QJsonObject m_benchExtras;
    int64_t m_benchStartNs = 0;
    std::atomic<bool> m_reportWritten{false};
    QList<double> m_benchFpsSeries, m_benchFrameP99Series, m_benchLagP99Series; // guarded by m_mutex
    void writeReport(const QJsonObject &report);
};

} // namespace qp
