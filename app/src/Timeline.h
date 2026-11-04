#pragma once

#include "History.h"
#include "Presenter.h"
#include "Recording.h"

#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class FileSource;
class HealthMonitor;
class StreamServer;

// The timeline bar's model: live, pause, play, scrub, record, open a recording.
class Timeline : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    QML_UNCREATABLE("Provided by main() via QQmlEngine::setExternalSingletonInstance")

    Q_PROPERTY(QString mode READ mode NOTIFY stateChanged FINAL) // live, paused or playing
    Q_PROPERTY(bool replayingFile READ replayingFile NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString fileName READ fileName NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged FINAL)
    Q_PROPERTY(double rate READ rate WRITE setRate NOTIFY stateChanged FINAL)
    Q_PROPERTY(int delayMs READ delayMs WRITE setDelayMs NOTIFY stateChanged FINAL)
    Q_PROPERTY(double position READ position NOTIFY ticked FINAL) // seconds from the start of the range
    Q_PROPERTY(double length READ length NOTIFY ticked FINAL)
    Q_PROPERTY(double behindLive READ behindLive NOTIFY ticked FINAL)
    Q_PROPERTY(double historyMb READ historyMb NOTIFY ticked FINAL)
    Q_PROPERTY(double historySeconds READ historySeconds NOTIFY ticked FINAL)
    Q_PROPERTY(double historyBudgetMb READ historyBudgetMb CONSTANT FINAL)
    Q_PROPERTY(QString historyShape READ historyShape CONSTANT FINAL)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged FINAL)
    Q_PROPERTY(double recordSeconds READ recordSeconds NOTIFY ticked FINAL)
    Q_PROPERTY(double recordMb READ recordMb NOTIFY ticked FINAL)
    Q_PROPERTY(double recordMbps READ recordMbps NOTIFY ticked FINAL)
    Q_PROPERTY(int recordDropped READ recordDropped NOTIFY ticked FINAL)
    Q_PROPERTY(QVariantList recordings READ recordings NOTIFY recordingsChanged FINAL)

public:
    struct Options
    {
        qint64 historyBytes = qint64(1) << 30;
        History::Shape shape = History::Shape::Shared;
        QString recordDir;
        rec::WriterOptions writer;
        bool coldReads = false; // measurements: file reads bypass every cache
    };

    Timeline(const Presenter::Sinks &sinks, const Options &options, QObject *parent = nullptr);
    ~Timeline() override;

    // Simulator thread.
    void ingestLidar(const qp::SensorFrame &frame);
    void ingestTracks(const qp::TrackSetPtr &set);
    void ingestEvents(const qp::EventBatchPtr &batch);
    // Any thread.
    void ingestCameraMark(const rec::CameraMark &mark);
    // Arrivals and rejections are reported here; with validation, bad samples stop at ingest.
    // keepOrder: in live pass-through, a lidar frame older than the newest already shown for
    // its sensor is not shown (history and recordings still keep it).
    void setHealth(HealthMonitor *health, bool validate, bool keepOrder)
    {
        m_health = health;
        m_validate = validate;
        m_keepOrder = keepOrder;
    }
    // Validated samples (and health events) also go to other viewers.
    void setPublisher(StreamServer *server) { m_publisher = server; }
    // Written into every new recording's header.
    void setRecordingInfo(const QJsonObject &info) { m_info = info; }

    QString mode() const;
    bool replayingFile() const { return m_file != nullptr; }
    QString fileName() const { return m_fileName; }
    QString status() const { return m_status; }
    double rate() const { return m_rate; }
    void setRate(double rate);
    int delayMs() const { return m_delayMs; }
    void setDelayMs(int ms);
    double position() const { return m_position; }
    double length() const { return m_length; }
    double behindLive() const { return m_behindLive; }
    double historyMb() const { return m_historyMb; }
    double historySeconds() const { return m_historySeconds; }
    double historyBudgetMb() const { return double(m_options.historyBytes) / 1e6; }
    QString historyShape() const;
    bool recording() const { return m_writer != nullptr; }
    double recordSeconds() const { return m_recordSeconds; }
    double recordMb() const { return m_recordMb; }
    double recordMbps() const { return m_recordMbps; }
    int recordDropped() const { return m_recordDropped; }
    QVariantList recordings() const { return m_recordings; }
    QString recordPath() const;

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void goLive();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void step(int direction);
    Q_INVOKABLE void startRecording(const QString &path = {});
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void openRecording(const QString &path);
    Q_INVOKABLE void refreshRecordings();

signals:
    void stateChanged();
    void ticked();
    void recordingChanged();
    void recordingsChanged();
    // A recording finished opening (GUI thread).
    void fileOpened(bool ok);
    void recordingStopped(const QString &path);

private:
    void poll();
    void setStatus(const QString &status);
    void sweepThreads();
    std::shared_ptr<rec::Writer> writer() const;

    HealthMonitor *m_health = nullptr;
    StreamServer *m_publisher = nullptr;
    bool m_validate = true;
    bool m_keepOrder = true;
    std::array<qint64, 16> m_newestShownLidar{}; // pass-through only; under the presenter's ingest lock
    std::atomic<quint64> m_eventBatches{0};
    std::array<std::atomic<quint64>, 4> m_cameraFrames{};
    Presenter::Sinks m_sinks;
    Options m_options;
    QJsonObject m_info;
    std::shared_ptr<History> m_history;
    Presenter m_presenter;
    std::shared_ptr<FileSource> m_file;
    QString m_fileName;

    mutable std::mutex m_writerMutex;
    std::shared_ptr<rec::Writer> m_writer; // also read by the ingest thread, under m_writerMutex
    std::vector<std::thread> m_background; // opening files and finishing recordings

    QTimer m_poll;
    QString m_status;
    QString m_lastMode;
    double m_rate = 1.0;
    int m_delayMs = 0;
    double m_position = 0, m_length = 0, m_behindLive = 0, m_historyMb = 0, m_historySeconds = 0;
    double m_recordSeconds = 0, m_recordMb = 0, m_recordMbps = 0;
    int m_recordDropped = 0;
    qint64 m_lastRecordBytes = 0, m_lastRecordPollNs = 0;
    QVariantList m_recordings;
};
