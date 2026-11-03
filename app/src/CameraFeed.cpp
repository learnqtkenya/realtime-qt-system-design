#include "CameraFeed.h"

#include "LidarHub.h"
#include "qp/FrameMonitor.h"
#include "qp/StageStats.h"

#include <QCoreApplication>
#include <QThread>
#include <QTimer>
#include <QVideoFrameFormat>

#include <cmath>

namespace {

constexpr size_t kRing = 8; // 267 ms at 30 fps: enough to wait for a 10 Hz lidar
constexpr qint64 kFrameNs = 33'333'333;
constexpr qint64 kReplayDriftMs = 150;
// The replay player runs this far ahead of the timeline; its frames wait for their due time.
constexpr qint64 kReplayLeadNs = 150'000'000;
// After a seek, position() reports the old place for a while; drift checks wait this long.
constexpr qint64 kReplaySettleNs = 500'000'000;

} // namespace

CameraFeed::CameraFeed(int index, const QUrl &source, LidarHub *hub, QObject *parent)
    : QObject(parent), m_index(index), m_hub(hub), m_source(source)
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
    if (m_replay)
        m_replay->stop();
    m_replaying = false;
    m_ring.clear();
    m_image = QImage();
}

void CameraFeed::setPresentationDelay(qint64 ns)
{
    m_delayNs = std::max<qint64>(0, ns);
}

size_t CameraFeed::ringCapacity() const
{
    // Enough frames to cover the presentation delay, plus the 10 Hz lidar wait.
    return std::max(kRing, size_t((m_delayNs + 300'000'000) / kFrameNs));
}

void CameraFeed::onFrame(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;
    const qint64 now = qp::nowNs();
    // Sensor time: when the camera "exposed" the frame on our clock, not when it reached us.
    // (Approximate around a loop wrap: the decoder thread may already be one loop ahead.)
    const Entry entry{frame, m_baseNs.load() + m_loopOffsetNs.load() + frame.startTime() * 1000};

    if (!m_replaying)
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
    while (m_ring.size() > ringCapacity())
        m_ring.pop_front();
    if (m_markSink)
        m_markSink({m_index, entry.captureNs, frame.startTime()});

    if (!m_replaying) {
        if (m_delayNs > 0) {
            const qint64 waitNs = entry.captureNs + m_delayNs - now;
            QTimer::singleShot(int(std::max<qint64>(0, (waitNs + 999'999) / 1'000'000)), Qt::PreciseTimer, this,
                               &CameraFeed::presentDue);
        } else if (m_sync == Sync::Latest) {
            present(entry);
        } else {
            realign();
        }
    }
    updateStats();
}

void CameraFeed::presentDue()
{
    if (m_replaying || m_delayNs == 0)
        return;
    const qint64 now = qp::nowNs();
    const Entry *due = nullptr;
    for (const Entry &e : m_ring)
        if (e.captureNs + m_delayNs <= now + 500'000)
            due = &e;
    if (due && due->captureNs != m_presentedNs)
        present(*due);
}

void CameraFeed::follow(const rec::CameraMark &mark, qint64 playheadNs, double rate, bool playing, qint64 wallNs)
{
    if (!m_replay) {
        m_replay = std::make_unique<QMediaPlayer>();
        m_replaySink = std::make_unique<QVideoSink>();
        m_replay->setSource(m_source);
        m_replay->setVideoSink(m_replaySink.get());
        connect(m_replaySink.get(), &QVideoSink::videoFrameChanged, this, &CameraFeed::onReplayFrame);
        // A seek asked for while the media is still loading is lost; it waits for this instead.
        connect(m_replay.get(), &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus st) {
            if (st == QMediaPlayer::LoadedMedia && m_seekInFlight && !m_followPlaying)
                seekReplay(m_seekTargetMs);
        });
    }
    const bool wasReplaying = std::exchange(m_replaying, true);
    m_followMark = mark;
    m_followT = playheadNs;
    m_followWall = wallNs;
    m_followRate = rate;
    m_followPlaying = playing;

    const qint64 aimNs = playing ? playheadNs + qint64(double(kReplayLeadNs) * rate) : playheadNs;
    qint64 targetUs = mark.mediaUs + (aimNs - mark.captureNs) / 1000;
    if (const qint64 durUs = m_player.duration() * 1000; durUs > 0)
        targetUs = ((targetUs % durUs) + durUs) % durUs;
    const qint64 target = targetUs / 1000;
    if (playing) {
        if (!qFuzzyCompare(m_replay->playbackRate(), rate))
            m_replay->setPlaybackRate(rate);
        const qint64 now = qp::nowNs();
        if (m_replay->playbackState() != QMediaPlayer::PlayingState) {
            m_replayRing.clear();
            m_seekInFlight = false;
            m_pendingTargetMs = -1;
            m_replay->setPosition(target);
            m_replay->play();
            m_replaySyncedNs = now;
        } else if (now - m_replaySyncedNs > kReplaySettleNs
                   && std::llabs(m_replay->position() - target) > kReplayDriftMs * rate) {
            m_replay->setPosition(target);
            m_replaySyncedNs = now;
            qp::StageStats::instance().add(QStringLiteral("camera_replay_resync"), 1);
        }
    } else {
        m_replayRing.clear();
        if (m_replay->playbackState() != QMediaPlayer::PausedState)
            m_replay->pause();
        if (target != m_targetMs || !wasReplaying) {
            const bool stuck = m_seekInFlight && qp::nowNs() - m_seekRequestedNs > 500'000'000;
            if (m_seekInFlight && !stuck) {
                if (m_pendingTargetMs >= 0)
                    qp::StageStats::instance().add(QStringLiteral("camera_seek_skipped"), 1);
                m_pendingTargetMs = target;
            } else {
                seekReplay(target);
            }
        }
    }
    m_targetMs = target;
    emit statsChanged();
}

void CameraFeed::onReplayFrame(const QVideoFrame &frame)
{
    if (!m_replaying || !frame.isValid())
        return;
    const qint64 now = qp::nowNs();
    // This frame's original capture time, through the mark that aimed the player.
    qint64 deltaUs = frame.startTime() - m_followMark.mediaUs;
    if (const qint64 durUs = m_player.duration() * 1000; durUs > 0) {
        if (deltaUs > durUs / 2)
            deltaUs -= durUs;
        else if (deltaUs < -durUs / 2)
            deltaUs += durUs;
    }
    const qint64 originalNs = m_followMark.captureNs + deltaUs * 1000;
    ++m_decoded;
    if (m_followPlaying) {
        m_replayRing.push_back({frame, originalNs});
        // Frames wait up to the lead (times the rate) for their due time.
        while (m_replayRing.size() > 64)
            m_replayRing.pop_front();
        const qint64 waitNs = replayDueWall(originalNs) - now;
        QTimer::singleShot(int(std::max<qint64>(0, (waitNs + 999'999) / 1'000'000)), Qt::PreciseTimer, this,
                           &CameraFeed::presentReplayDue);
        updateStats();
        return;
    }
    // Only the frame at the target answers the seek (not, say, the first frame of a fresh player).
    if (m_seekInFlight && std::llabs(frame.startTime() / 1000 - m_seekTargetMs) > 50
        && now - m_seekRequestedNs < 500'000'000) {
        qp::StageStats::instance().add(QStringLiteral("camera_seek_stray_frame"), 1);
        return;
    }
    qint64 stamp = m_seekInFlight ? m_seekRequestedNs : now;
    if (m_seekInFlight) {
        qp::StageStats::instance().add(QStringLiteral("camera_seek_ms"), double(now - m_seekRequestedNs) / 1e6);
        if (m_pendingTargetMs < 0)
            qp::StageStats::instance().add(QStringLiteral("camera_seek_error_ms"),
                                           double(originalNs - m_followT) / 1e6);
    }
    m_seekInFlight = false;
    present(Entry{frame, stamp});
    if (m_pendingTargetMs >= 0)
        seekReplay(std::exchange(m_pendingTargetMs, -1));
    updateStats();
}

void CameraFeed::seekReplay(qint64 targetMs)
{
    m_seekRequestedNs = qp::nowNs();
    m_seekInFlight = true;
    m_seekTargetMs = targetMs;
    const auto st = m_replay->mediaStatus();
    if (st != QMediaPlayer::NoMedia && st != QMediaPlayer::LoadingMedia)
        m_replay->setPosition(targetMs);
}

qint64 CameraFeed::replayDueWall(qint64 originalNs) const
{
    return m_followWall + qint64(double(originalNs - m_followT) / m_followRate);
}

void CameraFeed::presentReplayDue()
{
    if (!m_replaying || !m_followPlaying)
        return;
    const qint64 now = qp::nowNs();
    const Entry *due = nullptr;
    for (const Entry &e : m_replayRing)
        if (replayDueWall(e.captureNs) <= now + 500'000)
            due = &e;
    if (!due)
        return;
    const Entry shown{due->frame, replayDueWall(due->captureNs)};
    while (!m_replayRing.empty() && m_replayRing.front().captureNs <= due->captureNs)
        m_replayRing.pop_front();
    if (shown.captureNs != m_presentedNs)
        present(shown);
}

void CameraFeed::goLive()
{
    if (!m_replaying)
        return;
    m_replaying = false;
    if (m_replay)
        m_replay->pause();
    m_replayRing.clear();
    m_seekInFlight = false;
    m_pendingTargetMs = -1;
    m_targetMs = -1;
    emit statsChanged();
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
