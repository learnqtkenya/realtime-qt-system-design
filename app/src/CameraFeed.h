#pragma once

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

class LidarHub;

// One camera: decodes a video, keeps the last few frames, and presents one according to the
// sync policy. Frames are reference-counted, so keeping them copies nothing.
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

    void setSync(Sync sync) { m_sync = sync; }
    void setImagePath(bool on) { m_imagePath = on; }
    void start();
    void stop();
    QImage image() const { return m_image; } // GUI thread; image path only
    // Aligned sync: re-pick from the buffer because the lidar clock moved. GUI thread.
    void realign();

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
    void present(const Entry &entry);
    void updateStats();

    int m_index;
    LidarHub *m_hub;
    QMediaPlayer m_player;
    QVideoSink m_input;
    QPointer<QVideoSink> m_output;
    std::deque<Entry> m_ring;
    Sync m_sync = Sync::Latest;
    bool m_imagePath = false;
    QImage m_image;

    // Our clock at media time 0: the minimum of (arrival - media time) over all frames, taken on
    // the decoder thread before any queuing. The least-delayed frame says most about the offset.
    std::atomic<qint64> m_baseNs{0};
    std::atomic<bool> m_baseSet{false};
    // Decoder thread only.
    qint64 m_lastPtsUs = -1;
    std::atomic<qint64> m_loopOffsetNs{0};
    qint64 m_presentedNs = -1;

    int m_decoded = 0, m_presented = 0;
    qint64 m_statsSinceNs = 0;
    double m_decodedFps = 0, m_presentedFps = 0, m_skewMs = 0;
    QString m_frameInfo;
};
