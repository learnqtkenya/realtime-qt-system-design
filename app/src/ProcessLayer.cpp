#include "ProcessLayer.h"

#include "Wire.h"
#include "qp/StageStats.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <unistd.h>

#include <deque>

namespace {

constexpr qint64 kHighWater = 256 * 1024;
constexpr quint32 kMaxRecord = 8u << 20;
constexpr qint64 kHelloTimeoutNs = 5'000'000'000;

// utime + stime of a process and everything under it, in clock ticks.
quint64 treeTicks(qint64 pid, int depth = 0)
{
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray s = stat.readAll();
    const QList<QByteArray> f = s.mid(s.lastIndexOf(')') + 2).split(' ');
    quint64 ticks = f.size() > 12 ? f[11].toULongLong() + f[12].toULongLong() : 0;
    if (depth > 4)
        return ticks;
    const QDir tasks(QStringLiteral("/proc/%1/task").arg(pid));
    for (const QString &tid : tasks.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile children(tasks.filePath(tid + QStringLiteral("/children")));
        if (!children.open(QIODevice::ReadOnly))
            continue;
        for (const QByteArray &c : children.readAll().split(' '))
            if (const qint64 child = c.trimmed().toLongLong(); child > 0)
                ticks += treeTicks(child, depth + 1);
    }
    return ticks;
}

} // namespace

class ProcessLayer::Worker : public QObject
{
public:
    using Status = Layer::Status;

    Worker(const PluginManifest &m, const PluginHost::Options &o, ProcessLayer *layer, quint64 generation)
        : m_manifest(m), m_options(o), m_layer(layer), m_generation(generation)
    {
    }

    ~Worker() override { shutdown(); }

    void begin()
    {
        m_liveness = new QTimer(this);
        connect(m_liveness, &QTimer::timeout, this, &Worker::checkLiveness);
        m_liveness->start(50);
        m_cpuTimer = new QTimer(this);
        connect(m_cpuTimer, &QTimer::timeout, this, &Worker::sampleCpu);
        m_cpuTimer->start(1000);
        launch();
    }

    void send(const QByteArray &record)
    {
        if (!m_proc || m_proc->state() != QProcess::Running) {
            m_pending = record;
            return;
        }
        if (m_proc->bytesToWrite() < kHighWater) {
            write(record);
        } else {
            if (!m_pending.isEmpty())
                ++m_skipped;
            m_pending = record; // latest wins
        }
    }

    void sendSelection(int id)
    {
        m_selection = id;
        if (m_proc && m_proc->state() == QProcess::Running)
            m_proc->write(wire::selectionRecord(qp::nowNs(), id));
    }

    void shutdown()
    {
        m_stopping = true;
        if (m_proc) {
            m_proc->disconnect(this);
            m_proc->kill();
            m_proc->waitForFinished(1000);
            delete m_proc;
            m_proc = nullptr;
        }
    }

private:
    template <typename F> void post(F f)
    {
        QPointer<ProcessLayer> layer = m_layer;
        const quint64 generation = m_generation;
        QMetaObject::invokeMethod(
            m_layer,
            [layer, generation, f] {
                if (layer && layer->m_generation == generation)
                    f(layer.data());
            },
            Qt::QueuedConnection);
    }

    void report(Status s, const QString &detail)
    {
        post([s, detail](ProcessLayer *l) { l->setStatus(s, detail); });
    }

    void launch()
    {
        if (m_stopping)
            return;
        QString program = m_manifest.exec.value(0);
        QStringList args = m_manifest.exec.mid(1);
        program = program.contains(u'/') ? QDir(m_manifest.dir).absoluteFilePath(program)
                                         : QStandardPaths::findExecutable(program);
        if (program.isEmpty() || !QFile::exists(program)) {
            m_stopping = true;
            report(Status::Error, QStringLiteral("%1 not found").arg(m_manifest.exec.value(0)));
            return;
        }
        if (m_options.sandbox) {
            const QString bwrap = QStandardPaths::findExecutable(QStringLiteral("bwrap"));
            if (bwrap.isEmpty()) {
                m_stopping = true;
                report(Status::Error, QStringLiteral("sandbox requested, but bwrap is not installed"));
                return;
            }
            // Everything read-only, no home, no network; the plugins directory visible.
            const QString root = QDir::cleanPath(m_manifest.dir + QStringLiteral("/.."));
            args = QStringList{QStringLiteral("--ro-bind"), QStringLiteral("/"), QStringLiteral("/"),
                               QStringLiteral("--tmpfs"), QStringLiteral("/home"),
                               QStringLiteral("--tmpfs"), QStringLiteral("/tmp"),
                               QStringLiteral("--tmpfs"), QStringLiteral("/run"),
                               QStringLiteral("--ro-bind"), root, root,
                               QStringLiteral("--dev"), QStringLiteral("/dev"),
                               QStringLiteral("--proc"), QStringLiteral("/proc"),
                               QStringLiteral("--unshare-net"), QStringLiteral("--unshare-pid"),
                               QStringLiteral("--die-with-parent"), QStringLiteral("--new-session"),
                               QStringLiteral("--chdir"), m_manifest.dir, QStringLiteral("--"), program}
                   + args;
            program = bwrap;
        }
        m_proc = new QProcess(this);
        m_proc->setWorkingDirectory(m_manifest.dir);
        m_proc->setProgram(program);
        m_proc->setArguments(args);
        connect(m_proc, &QProcess::readyReadStandardOutput, this, &Worker::readOutput);
        connect(m_proc, &QProcess::readyReadStandardError, this, &Worker::readErrors);
        connect(m_proc, &QProcess::bytesWritten, this, &Worker::flush);
        connect(m_proc, &QProcess::finished, this, &Worker::exited);
        connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart)
                exited(-1, QProcess::CrashExit);
        });
        m_parser = wire::Parser(kMaxRecord);
        if (!m_options.postEvery)
            m_parser.setLatestOnly(rec::Type::Overlay);
        m_helloSeen = false;
        m_awaiting = false;
        m_inputs = m_answers = 0;
        m_killReason.clear();
        m_startedNs = qp::nowNs();
        m_cpuTicks = 0;
        m_cpuAtNs = 0;
        m_proc->start();
        QJsonObject info;
        info[u"api"] = QStringLiteral("%1.%2").arg(PluginHost::kApiMajor).arg(PluginHost::kApiMinor);
        info[u"layer"] = m_manifest.id;
        info[u"inputs"] = QJsonArray{QStringLiteral("tracks"), QStringLiteral("selection")};
        info[u"properties"] = QJsonObject::fromVariantMap(m_manifest.properties);
        m_proc->write(wire::hello(info));
        m_proc->write(wire::selectionRecord(qp::nowNs(), m_selection));
        if (!m_pending.isEmpty())
            write(std::exchange(m_pending, {}));
    }

    void write(const QByteArray &record)
    {
        m_proc->write(record);
        ++m_inputs;
        if (!m_awaiting) {
            m_awaiting = true;
            m_awaitingSinceNs = qp::nowNs();
        }
    }

    void flush()
    {
        if (!m_pending.isEmpty() && m_proc && m_proc->bytesToWrite() < kHighWater)
            write(std::exchange(m_pending, {}));
    }

    void readOutput()
    {
        const QByteArray data = m_proc->readAllStandardOutput();
        OverlayFramePtr latest;
        const bool hadHello = m_parser.hasHello();
        const qint64 rejectedBefore = m_parser.unknownRecords();
        const qint64 supersededBefore = m_parser.superseded();
        const bool ok = m_parser.feed(data.constData(), data.size(), [&](const wire::Decoded &d) {
            if (d.overlay) {
                latest = d.overlay;
                ++m_answers;
            }
        });
        m_answers += m_parser.superseded() - supersededBefore;
        const qint64 now = qp::nowNs();
        if (!ok) {
            kill(Status::Error, QStringLiteral("protocol error: not an overlay stream"));
            return;
        }
        if (!hadHello && m_parser.hasHello()) {
            m_helloSeen = true;
            if (m_restartRequestedNs > 0) {
                qp::StageStats::instance().add(QStringLiteral("layer_%1_restart_ms").arg(m_manifest.id),
                                               double(now - m_restartRequestedNs) / 1e6);
                m_restartRequestedNs = 0;
            }
            const QString name = m_parser.info().value(u"name").toString();
            report(Status::Ok, QStringLiteral("pid %1%2%3")
                                   .arg(m_proc->processId())
                                   .arg(name.isEmpty() ? QString() : QStringLiteral(" · ") + name)
                                   .arg(m_options.sandbox ? QStringLiteral(" · sandboxed") : QString()));
        }
        if (const qint64 rejected = m_parser.unknownRecords() - rejectedBefore; rejected > 0)
            post([rejected](ProcessLayer *l) { l->m_rejected += rejected; });
        // The contract is one answer per input: far more than that is a flood, and costs this
        // thread (reading) whatever the plugin chooses to spend.
        if (m_options.answerSlack > 0 && m_answers > m_inputs + m_options.answerSlack) {
            kill(Status::Error, QStringLiteral("flooding: %1 answers for %2 inputs").arg(m_answers).arg(m_inputs));
            return;
        }
        if (latest) {
            m_awaiting = false;
            handOver(latest);
        }
    }

    // Latest wins across the thread boundary too: one hand-over in flight, carrying the newest frame.
    void handOver(const OverlayFramePtr &frame)
    {
        if (m_options.postEvery) {
            post([frame](ProcessLayer *l) { l->m_host->deliver(l, frame); });
            return;
        }
        {
            std::lock_guard lk(m_mailbox->mutex);
            if (frame) {
                if (m_mailbox->frame)
                    ++m_mailbox->replaced;
                m_mailbox->frame = frame;
            }
            if (m_mailbox->posted)
                return;
        }
        // No more than one hand-over per 8 ms: faster than the display, slower than a flood.
        const qint64 wait = m_lastHandOverNs + 8'000'000 - qp::nowNs();
        if (wait > 0) {
            if (!m_handOverTimer) {
                m_handOverTimer = new QTimer(this);
                m_handOverTimer->setSingleShot(true);
                connect(m_handOverTimer, &QTimer::timeout, this, [this] { handOver({}); });
            }
            if (!m_handOverTimer->isActive())
                m_handOverTimer->start(int(wait / 1'000'000) + 1);
            return;
        }
        {
            std::lock_guard lk(m_mailbox->mutex);
            if (!m_mailbox->frame)
                return;
            m_mailbox->posted = true;
        }
        m_lastHandOverNs = qp::nowNs();
        post([box = m_mailbox](ProcessLayer *l) {
            OverlayFramePtr f;
            {
                std::lock_guard lk(box->mutex);
                f = std::move(box->frame);
                box->posted = false;
            }
            if (f)
                l->m_host->deliver(l, f);
        });
    }

    void readErrors()
    {
        m_stderr += m_proc->readAllStandardError();
        qsizetype nl;
        while ((nl = m_stderr.indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(m_stderr.left(nl)).left(200);
            m_stderr.remove(0, nl + 1);
            post([line](ProcessLayer *l) { l->m_host->message(l, line, 0); });
        }
        if (m_stderr.size() > 4096)
            m_stderr.clear();
    }

    void kill(Status s, const QString &why)
    {
        if (!m_proc || !m_killReason.isEmpty())
            return;
        m_killStatus = s;
        m_killReason = why;
        m_proc->kill();
    }

    void checkLiveness()
    {
        if (!m_proc || m_proc->state() != QProcess::Running)
            return;
        const qint64 now = qp::nowNs();
        if (!m_helloSeen && now - m_startedNs > kHelloTimeoutNs)
            kill(Status::NotResponding, QStringLiteral("no hello within 5 s"));
        else if (m_awaiting && now - m_awaitingSinceNs > qint64(m_options.silenceMs) * 1'000'000)
            kill(Status::NotResponding, QStringLiteral("no answer for %1 s").arg(m_options.silenceMs / 1000.0));
    }

    void exited(int code, QProcess::ExitStatus status)
    {
        if (m_stopping || !m_proc)
            return;
        m_proc->deleteLater();
        m_proc = nullptr;
        const qint64 now = qp::nowNs();
        m_restartRequestedNs = now;
        m_crashes.push_back(now);
        while (!m_crashes.empty() && now - m_crashes.front() > 60'000'000'000LL)
            m_crashes.pop_front();
        Status s = m_killStatus;
        QString why = m_killReason;
        if (why.isEmpty()) {
            s = Status::Crashed;
            why = status == QProcess::CrashExit ? QStringLiteral("crashed") : QStringLiteral("exited with code %1").arg(code);
        }
        qp::StageStats::instance().add(QStringLiteral("layer_%1_restarts").arg(m_manifest.id), 0);
        post([](ProcessLayer *l) { ++l->m_restarts; });
        if (m_crashes.size() >= 5) {
            m_stopping = true;
            report(Status::Failed, why + QStringLiteral("; 5 times in a minute, not restarted"));
            return;
        }
        const int delay = 250 << std::min<int>(int(m_crashes.size()) - 1, 4);
        report(s, why + QStringLiteral("; restarting in %1 ms").arg(delay));
        QTimer::singleShot(delay, this, &Worker::launch);
    }

    void sampleCpu()
    {
        if (!m_proc || m_proc->state() != QProcess::Running)
            return;
        const quint64 ticks = treeTicks(m_proc->processId());
        const qint64 now = qp::nowNs();
        if (m_cpuAtNs > 0 && now > m_cpuAtNs) {
            const double pct = 100.0 * double(ticks - m_cpuTicks) / double(sysconf(_SC_CLK_TCK))
                               / (double(now - m_cpuAtNs) / 1e9);
            qp::StageStats::instance().add(QStringLiteral("layer_%1_cpu_pct").arg(m_manifest.id), pct);
            const qint64 skipped = m_skipped;
            post([pct, skipped](ProcessLayer *l) {
                l->m_cpuPct = pct;
                l->m_skipped = skipped;
            });
        }
        m_cpuTicks = ticks;
        m_cpuAtNs = now;
    }

    PluginManifest m_manifest;
    PluginHost::Options m_options;
    ProcessLayer *m_layer;
    quint64 m_generation;
    QProcess *m_proc = nullptr;
    QTimer *m_liveness = nullptr, *m_cpuTimer = nullptr;
    wire::Parser m_parser;
    QByteArray m_pending, m_stderr;
    bool m_stopping = false, m_helloSeen = false, m_awaiting = false;
    qint64 m_startedNs = 0, m_awaitingSinceNs = 0, m_restartRequestedNs = 0, m_cpuAtNs = 0;
    quint64 m_cpuTicks = 0;
    qint64 m_skipped = 0;
    qint64 m_inputs = 0, m_answers = 0;
    int m_selection = -1;
    Status m_killStatus = Status::Crashed;
    QString m_killReason;
    std::deque<qint64> m_crashes;
    struct Mailbox
    {
        std::mutex mutex;
        OverlayFramePtr frame;
        bool posted = false;
        qint64 replaced = 0;
    };
    std::shared_ptr<Mailbox> m_mailbox = std::make_shared<Mailbox>();
    QTimer *m_handOverTimer = nullptr;
    qint64 m_lastHandOverNs = 0;
};

ProcessLayer::ProcessLayer(const PluginManifest &m, PluginHost *host) : Layer(m, host)
{
    m_thread.setObjectName(QStringLiteral("layer ") + m.id);
}

ProcessLayer::~ProcessLayer()
{
    stop();
}

void ProcessLayer::start()
{
    stop();
    ++m_generation;
    m_restarts = 0;
    m_rejected = m_skipped = 0;
    m_cpuPct = 0;
    setStatus(Status::Starting);
    m_worker = new Worker(m_manifest, m_host->options(), this, m_generation);
    m_worker->moveToThread(&m_thread);
    m_thread.start();
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->begin(); }, Qt::QueuedConnection);
}

void ProcessLayer::stop()
{
    ++m_generation;
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, [w = m_worker] { delete w; }, Qt::BlockingQueuedConnection);
        m_worker = nullptr;
        m_thread.quit();
        m_thread.wait();
    }
    if (m_status != Status::Off)
        setStatus(Status::Off);
}

void ProcessLayer::feed(const qp::TrackSetPtr &, int, const QByteArray &tracksRecord)
{
    if (m_worker && m_status != Status::Failed && m_status != Status::Error)
        QMetaObject::invokeMethod(m_worker, [w = m_worker, tracksRecord] { w->send(tracksRecord); }, Qt::QueuedConnection);
}

void ProcessLayer::selectionChanged(int id)
{
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, [w = m_worker, id] { w->sendSelection(id); }, Qt::QueuedConnection);
}

QString ProcessLayer::cost() const
{
    QString s = QStringLiteral("CPU %1%").arg(m_cpuPct, 0, 'f', 1);
    if (m_restarts)
        s += QStringLiteral(" · %1 restarts").arg(m_restarts);
    if (m_rejected)
        s += QStringLiteral(" · %1 rejected").arg(m_rejected);
    if (m_skipped)
        s += QStringLiteral(" · %1 skipped").arg(m_skipped);
    return s;
}
