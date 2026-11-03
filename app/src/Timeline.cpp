#include "Timeline.h"

#include "FileSource.h"
#include "qp/StageStats.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QPointer>

Timeline::Timeline(const Presenter::Sinks &sinks, const Options &options, QObject *parent)
    : QObject(parent),
      m_sinks(sinks),
      m_options(options),
      m_history(std::make_shared<History>(options.historyBytes, options.shape)),
      m_presenter(sinks)
{
    m_presenter.setSource(m_history, true);
    m_presenter.start();
    m_poll.setInterval(66);
    connect(&m_poll, &QTimer::timeout, this, &Timeline::poll);
    m_poll.start();
    refreshRecordings();
}

Timeline::~Timeline()
{
    m_presenter.stop();
    stopRecording();
    for (std::thread &t : m_background)
        if (t.joinable())
            t.join();
}

std::shared_ptr<rec::Writer> Timeline::writer() const
{
    std::lock_guard lk(m_writerMutex);
    return m_writer;
}

void Timeline::ingestLidar(const qp::SensorFrame &frame)
{
    if (const auto w = writer())
        w->addLidar(frame);
    m_presenter.ingest([&] { m_history->addLidar(frame); }, [&] { m_sinks.lidar(frame); });
}

void Timeline::ingestTracks(const qp::TrackSetPtr &set)
{
    if (const auto w = writer())
        w->addTracks(set);
    m_presenter.ingest([&] { m_history->addTracks(set); }, [&] { m_sinks.tracks(set); });
}

void Timeline::ingestEvents(const qp::EventBatchPtr &batch)
{
    if (const auto w = writer())
        w->addEvents(batch);
    m_presenter.ingest([&] { m_history->addEvents(batch); }, [&] { m_sinks.events(batch, false); });
}

void Timeline::ingestCameraMark(const rec::CameraMark &mark)
{
    if (const auto w = writer())
        w->addCameraMark(mark);
    m_history->addCameraMark(mark);
}

QString Timeline::mode() const
{
    switch (m_presenter.mode()) {
    case Presenter::Mode::Live: return QStringLiteral("live");
    case Presenter::Mode::Paused: return QStringLiteral("paused");
    case Presenter::Mode::Playing: return QStringLiteral("playing");
    }
    return {};
}

QString Timeline::historyShape() const
{
    return m_options.shape == History::Shape::Int16 ? QStringLiteral("int16") : QStringLiteral("shared");
}

void Timeline::setRate(double rate)
{
    rate = std::clamp(rate, 0.1, 8.0);
    if (qFuzzyCompare(rate, m_rate))
        return;
    m_rate = rate;
    m_presenter.setRate(rate);
    emit stateChanged();
}

void Timeline::setDelayMs(int ms)
{
    ms = std::clamp(ms, 0, 1000);
    if (ms == m_delayMs)
        return;
    m_delayMs = ms;
    m_presenter.setDelay(qint64(ms) * 1'000'000);
    emit stateChanged();
}

void Timeline::play()
{
    m_presenter.play();
}

void Timeline::pause()
{
    m_presenter.pause();
}

void Timeline::togglePlay()
{
    m_presenter.mode() == Presenter::Mode::Paused ? play() : pause();
}

void Timeline::goLive()
{
    if (m_file) {
        m_file.reset();
        m_fileName.clear();
        m_presenter.setSource(m_history, true);
        setStatus({});
    } else {
        m_presenter.goLive();
    }
}

void Timeline::seek(double seconds)
{
    const Source &src = m_file ? static_cast<const Source &>(*m_file) : *m_history;
    m_presenter.seek(src.startNs() + qint64(seconds * 1e9));
}

void Timeline::step(int direction)
{
    m_presenter.step(direction);
}

QString Timeline::recordPath() const
{
    const auto w = writer();
    return w ? w->path() : QString();
}

void Timeline::startRecording(const QString &path)
{
    if (writer())
        return;
    QDir().mkpath(m_options.recordDir);
    const QString file = !path.isEmpty() ? path
                                         : QDir(m_options.recordDir)
                                               .filePath(QDateTime::currentDateTime().toString(
                                                   QStringLiteral("'rec-'yyyyMMdd-HHmmss'.qprec'")));
    QDir().mkpath(QFileInfo(file).absolutePath());
    rec::WriterOptions opt = m_options.writer;
    opt.info = m_info;
    opt.info["created"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    auto w = std::make_shared<rec::Writer>(file, opt);
    QString error;
    if (!w->start(&error)) {
        setStatus(QStringLiteral("Can't record: %1").arg(error));
        return;
    }
    {
        std::lock_guard lk(m_writerMutex);
        m_writer = w;
    }
    m_lastRecordBytes = 0;
    m_lastRecordPollNs = qp::nowNs();
    emit recordingChanged();
}

void Timeline::stopRecording()
{
    std::shared_ptr<rec::Writer> w;
    {
        std::lock_guard lk(m_writerMutex);
        w = std::exchange(m_writer, nullptr);
    }
    if (!w)
        return;
    emit recordingChanged();
    // The queue may hold seconds of data; finishing it must not stall the GUI.
    QPointer<Timeline> self(this);
    m_background.emplace_back([w, self] {
        const qint64 t0 = qp::nowNs();
        w->stop(); // drains the queue, writes the index, then fdatasync
        const rec::WriterStats st = w->stats();
        qp::StageStats &stats = qp::StageStats::instance();
        stats.add(QStringLiteral("record_stop_ms"), double(qp::nowNs() - t0) / 1e6);
        stats.add(QStringLiteral("record_seconds"), double(st.lastT - st.firstT) / 1e9);
        stats.add(QStringLiteral("record_file_mb"), double(st.fileBytes) / 1e6);
        stats.add(QStringLiteral("record_raw_mb"), double(st.rawBytes) / 1e6);
        stats.add(QStringLiteral("record_dropped"), double(st.droppedRecords));
        stats.add(QStringLiteral("record_queue_max_mb"), double(st.queueBytesMax) / 1e6);
        stats.add(QStringLiteral("record_at_risk_max_mb"), double(st.atRiskBytesMax) / 1e6);
        stats.add(QStringLiteral("record_out_of_order"), double(st.outOfOrder));
        stats.add(QStringLiteral("record_max_disorder_ms"), double(st.maxDisorderNs) / 1e6);
        stats.add(QStringLiteral("record_encode_ms_total"), st.encodeMs);
        stats.add(QStringLiteral("record_compress_ms_total"), st.compressMs);
        stats.add(QStringLiteral("record_write_ms_total"), st.writeMs);
        const QString path = w->path();
        QMetaObject::invokeMethod(qApp, [self, path] {
            if (self) {
                self->refreshRecordings();
                emit self->recordingStopped(path);
            }
        });
    });
}

void Timeline::toggleRecording()
{
    writer() ? stopRecording() : startRecording();
}

void Timeline::openRecording(const QString &path)
{
    setStatus(QStringLiteral("Opening %1…").arg(QFileInfo(path).fileName()));
    QPointer<Timeline> self(this);
    m_background.emplace_back([self, path, cold = m_options.coldReads] {
        auto src = std::make_shared<FileSource>();
        QString error;
        const bool ok = src->open(path, &error);
        src->reader().setColdReads(cold);
        QMetaObject::invokeMethod(qApp, [self, src, ok, error, path] {
            if (!self)
                return;
            if (!ok) {
                self->setStatus(QStringLiteral("Can't open: %1").arg(error));
                emit self->fileOpened(false);
                return;
            }
            const rec::ReaderInfo &i = src->reader().info();
            qp::StageStats::instance().add(QStringLiteral("file_open_ms"), i.openMs);
            self->m_file = src;
            self->m_fileName = QFileInfo(path).fileName();
            self->m_presenter.setSource(src, false);
            QString s = QStringLiteral("%1 · %2 MB · index %3 KB · opened in %4 ms")
                            .arg(self->m_fileName)
                            .arg(double(i.fileBytes) / 1e6, 0, 'f', 1)
                            .arg(double(i.indexBytes) / 1e3, 0, 'f', 0)
                            .arg(i.openMs, 0, 'f', 0);
            if (i.recovered)
                s += QStringLiteral(" · no index: recovered %1 chunks").arg(i.recoveredChunks);
            if (i.badChunks)
                s += QStringLiteral(", %1 damaged").arg(i.badChunks);
            if (i.unknownRecords)
                s += QStringLiteral(" · %1 records of unknown types skipped").arg(i.unknownRecords);
            self->setStatus(s);
            emit self->fileOpened(true);
        });
    });
}

void Timeline::refreshRecordings()
{
    QVariantList out;
    const QFileInfoList files = QDir(m_options.recordDir)
                                    .entryInfoList({QStringLiteral("*.qprec")}, QDir::Files, QDir::Time);
    for (const QFileInfo &f : files)
        out << QVariantMap{{"path", f.absoluteFilePath()}, {"name", f.fileName()}, {"mb", double(f.size()) / 1e6}};
    m_recordings = out;
    emit recordingsChanged();
}

void Timeline::setStatus(const QString &status)
{
    if (status == m_status)
        return;
    m_status = status;
    emit stateChanged();
}

void Timeline::poll()
{
    const qint64 now = qp::nowNs();
    const Source &src = m_file ? static_cast<const Source &>(*m_file) : *m_history;
    const qint64 start = src.startNs();
    const qint64 end = m_file ? src.endNs() : std::max(src.endNs(), now - qint64(m_delayMs) * 1'000'000);
    const qint64 T = m_presenter.passingThrough() ? now : m_presenter.playheadNs();
    m_length = start > 0 ? double(end - start) / 1e9 : 0;
    m_position = std::clamp(double(T - start) / 1e9, 0.0, m_length);
    m_behindLive = m_file || m_presenter.mode() == Presenter::Mode::Live ? 0 : double(now - T) / 1e9;
    m_historyMb = double(m_history->bytes()) / 1e6;
    const qint64 hs = m_history->startNs();
    m_historySeconds = hs > 0 ? double(m_history->endNs() - hs) / 1e9 : 0;
    if (const auto w = writer()) {
        const rec::WriterStats st = w->stats();
        m_recordSeconds = st.firstT ? double(st.lastT - st.firstT) / 1e9 : 0;
        m_recordMb = double(st.fileBytes) / 1e6;
        m_recordDropped = int(st.droppedRecords);
        if (now - m_lastRecordPollNs >= 1'000'000'000) {
            m_recordMbps = double(st.fileBytes - m_lastRecordBytes) / 1e6 / (double(now - m_lastRecordPollNs) / 1e9);
            m_lastRecordBytes = st.fileBytes;
            m_lastRecordPollNs = now;
        }
    }
    const QString m = mode();
    if (m != m_lastMode) {
        m_lastMode = m;
        emit stateChanged();
    }
    emit ticked();
}
