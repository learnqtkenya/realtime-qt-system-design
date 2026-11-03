#pragma once

#include "Codec.h"

#include <QImage>
#include <QMediaPlayer>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>

class LidarHub;

// One camera: decodes a video, keeps the last few frames, and presents one according to the
// sync policy. Frames are reference-counted, so keeping them copies nothing.
// In replay a second player seeks the same video to the timeline, so the live camera keeps
// running (and recording) while the user looks at the past.
class CameraFeed : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by C++")

    Q_PROPERTY(int index READ index CONSTANT FINAL)
    Q_PROPERTY(QVideoSink *output READ output WRITE setOutput NOTIFY outputChanged FINAL)
    Q_PROPERTY(double decodedFps READ decodedFps NOTIFY statsChanged FINAL)
    Q_PROPERTY(double presentedFps READ presentedFps NOTIFY statsChanged FINAL)
    Q_PROPERTY(double skewMs READ skewMs NOTIFY statsChanged FINAL)
    Q_PROPERTY(QString frameInfo READ frameInfo NOTIFY statsChanged FINAL)
    Q_PROPERTY(bool replaying READ replaying NOTIFY statsChanged FINAL)

public:
    enum class Sync { Latest, Aligned };

    CameraFeed(int index, const QUrl &source, LidarHub *hub, QObject *parent = nullptr);

    int index() const { return m_index; }
    QVideoSink *output() const { return m_output; }
    void setOutput(QVideoSink *sink);
    double decodedFps() const { return m_decodedFps; }
    double presentedFps() const { return m_presentedFps; }
    double skewMs() const { return m_skewMs; }
    QString frameInfo() const { return m_frameInfo; }
    bool replaying() const { return m_replaying; }

    void setSync(Sync sync) { m_sync = sync; }
    void setImagePath(bool on) { m_imagePath = on; }
    // Live: show each frame at capture + delay (the shared presentation clock); 0 = as it arrives.
    void setPresentationDelay(qint64 ns);
    // Called (GUI thread) with every live frame's capture time and media position.
    void setMarkSink(std::function<void(const rec::CameraMark &)> sink) { m_markSink = std::move(sink); }
    void start();
    void stop();
    QImage image() const { return m_image; } // GUI thread; image path only
    // Aligned sync: re-pick from the buffer because the lidar clock moved. GUI thread.
    void realign();

    // Replay: show the frame the camera captured at playheadNs. wallNs is the wall time of the
    // playhead, so frames can be timed against it.
    void follow(const rec::CameraMark &mark, qint64 playheadNs, double rate, bool playing, qint64 wallNs);
    void goLive();

signals:
    void outputChanged();
    void statsChanged();
    void imageChanged();

private:
    struct Entry
    {
        QVideoFrame frame;
        qint64 captureNs;
    };

    void onFrame(const QVideoFrame &frame);
    void onReplayFrame(const QVideoFrame &frame);
    void presentDue();
    void presentReplayDue();
    void seekReplay(qint64 targetMs);
    qint64 replayDueWall(qint64 originalNs) const;
    void present(const Entry &entry);
    void updateStats();
    size_t ringCapacity() const;

    int m_index;
    LidarHub *m_hub;
    QUrl m_source;
    QMediaPlayer m_player;
    QVideoSink m_input;
    QPointer<QVideoSink> m_output;
    std::deque<Entry> m_ring;
    Sync m_sync = Sync::Latest;
    bool m_imagePath = false;
    QImage m_image;
    qint64 m_delayNs = 0;
    std::function<void(const rec::CameraMark &)> m_markSink;

    // Our clock at media time 0: the minimum of (arrival - media time) over all frames, taken on
    // the decoder thread before any queuing. The least-delayed frame says most about the offset.
    std::atomic<qint64> m_baseNs{0};
    std::atomic<bool> m_baseSet{false};
    // Decoder thread only.
    qint64 m_lastPtsUs = -1;
    std::atomic<qint64> m_loopOffsetNs{0};
    qint64 m_presentedNs = -1;

    // Replay (GUI thread).
    std::unique_ptr<QMediaPlayer> m_replay;
    std::unique_ptr<QVideoSink> m_replaySink;
    std::deque<Entry> m_replayRing; // captureNs: the original capture time
    bool m_replaying = false;
    rec::CameraMark m_followMark;
    qint64 m_followT = 0, m_followWall = 0;
    double m_followRate = 1;
    bool m_followPlaying = false;
    qint64 m_targetMs = -1;
    qint64 m_seekRequestedNs = 0;
    // One paused seek at a time; a newer target waits here and replaces older ones.
    bool m_seekInFlight = false;
    qint64 m_pendingTargetMs = -1;
    qint64 m_seekTargetMs = -1;
    qint64 m_replaySyncedNs = 0; // last setPosition while playing: position() lags it

    int m_decoded = 0, m_presented = 0;
    qint64 m_statsSinceNs = 0;
    double m_decodedFps = 0, m_presentedFps = 0, m_skewMs = 0;
    QString m_frameInfo;
};
