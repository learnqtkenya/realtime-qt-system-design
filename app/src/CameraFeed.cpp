#include "CameraFeed.h"

#include "LidarHub.h"
#include "qp/FrameMonitor.h"
#include "qp/StageStats.h"

#include <QCoreApplication>
#include <QThread>
#include <QVideoFrameFormat>

namespace {

constexpr size_t kRing = 8; // 267 ms at 30 fps: enough to wait for a 10 Hz lidar

} // namespace

CameraFeed::CameraFeed(int index, const QUrl &source, LidarHub *hub, QObject *parent)
    : QObject(parent), m_index(index), m_hub(hub)
{
    m_player.setSource(source);
    m_player.setLoops(QMediaPlayer::Infinite);
    m_player.setVideoSink(&m_input);
    // Runs where the decoder delivers, before any queuing of ours.
    connect(&m_input, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        const qint64 now = qp::nowNs();
        qp::StageStats::instance().add(QStringLiteral("camera_signal_on_gui"),
                                       QThread::currentThread() == qApp->thread() ? 1.0 : 0.0);
        const qint64 pts = frame.startTime();
        if (m_lastPtsUs >= 0 && pts < m_lastPtsUs) // the video looped
            m_loopOffsetNs += m_player.duration() > 0 ? m_player.duration() * 1'000'000
                                                      : (m_lastPtsUs + 33'333) * 1000;
        m_lastPtsUs = pts;
        const qint64 offset = now - (m_loopOffsetNs + pts * 1000);
        qint64 cur = m_baseNs.load();
        while ((!m_baseSet || offset < cur) && !m_baseNs.compare_exchange_weak(cur, offset)) {
        }
        m_baseSet = true;
    }, Qt::DirectConnection);
    connect(&m_input, &QVideoSink::videoFrameChanged, this, &CameraFeed::onFrame);
}

void CameraFeed::setOutput(QVideoSink *sink)
{
    if (m_output == sink)
        return;
    m_output = sink;
    emit outputChanged();
}

void CameraFeed::start()
{
    m_baseSet = false;
    m_lastPtsUs = -1;
    m_loopOffsetNs = 0;
    m_ring.clear();
    m_statsSinceNs = qp::nowNs();
    m_player.play();
}

void CameraFeed::stop()
{
    m_player.stop();
    m_ring.clear();
    m_image = QImage();
}

void CameraFeed::onFrame(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;
    const qint64 now = qp::nowNs();
    // Sensor time: when the camera "exposed" the frame on our clock, not when it reached us.
    // (Approximate around a loop wrap: the decoder thread may already be one loop ahead.)
    const Entry entry{frame, m_baseNs.load() + m_loopOffsetNs.load() + frame.startTime() * 1000};

    ++m_decoded;
    auto &stats = qp::StageStats::instance();
    stats.add(QStringLiteral("camera_decoded"), 1);
    stats.add(QStringLiteral("camera_arrival_ms"), double(now - entry.captureNs) / 1e6);
    if (m_frameInfo.isEmpty()) {
        const bool gpu = frame.handleType() == QVideoFrame::RhiTextureHandle;
        m_frameInfo = QStringLiteral("%1 · %2 · %3×%4")
                          .arg(QVideoFrameFormat::pixelFormatToString(frame.pixelFormat()),
                               gpu ? QStringLiteral("GPU texture") : QStringLiteral("CPU memory"))
                          .arg(frame.width())
                          .arg(frame.height());
        stats.add(QStringLiteral("camera_frame_on_gpu"), gpu ? 1.0 : 0.0);
    }

    m_ring.push_back(entry);
    if (m_ring.size() > kRing)
        m_ring.pop_front();

    if (m_sync == Sync::Latest)
        present(entry);
    else
        realign();
    updateStats();
}

void CameraFeed::realign()
{
    if (m_sync != Sync::Aligned || m_ring.empty())
        return;
    // The buffered frame nearest in time to the newest lidar frame.
    const qint64 target = m_hub->newestCaptureNs();
    const Entry *best = &m_ring.front();
    for (const Entry &e : m_ring)
        if (std::llabs(e.captureNs - target) < std::llabs(best->captureNs - target))
            best = &e;
    if (best->captureNs != m_presentedNs)
        present(*best);
}

void CameraFeed::present(const Entry &entry)
{
    m_presentedNs = entry.captureNs;
    ++m_presented;
    auto &stats = qp::StageStats::instance();
    stats.add(QStringLiteral("camera_presented"), 1);
    if (m_imagePath) {
        const int64_t t0 = qp::nowNs();
        m_image = entry.frame.toImage();
        stats.add(QStringLiteral("camera_toimage_ms"), double(qp::nowNs() - t0) / 1e6);
        emit imageChanged();
    } else if (m_output) {
        m_output->setVideoFrame(entry.frame);
    }
    qp::markContentCaptured(entry.captureNs, 1);
    m_skewMs = double(entry.captureNs - m_hub->newestCaptureNs()) / 1e6;
}

void CameraFeed::updateStats()
{
    const qint64 now = qp::nowNs();
    const double seconds = double(now - m_statsSinceNs) / 1e9;
    if (seconds < 1.0)
        return;
    m_decodedFps = m_decoded / seconds;
    m_presentedFps = m_presented / seconds;
    m_decoded = m_presented = 0;
    m_statsSinceNs = now;
    emit statsChanged();
}
