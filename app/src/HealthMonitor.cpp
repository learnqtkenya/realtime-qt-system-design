#include "HealthMonitor.h"

#include "qp/StageStats.h"

#include <QVariantMap>

namespace {

constexpr qint64 kWindowNs = 1'000'000'000;
constexpr qint64 kRecoverNs = 300'000'000; // a better status must hold this long

int severity(HealthMonitor::Status s)
{
    switch (s) {
    case HealthMonitor::Status::Off: return 0;
    case HealthMonitor::Status::Ok: return 1;
    case HealthMonitor::Status::Degraded: return 2;
    case HealthMonitor::Status::Late: return 3;
    case HealthMonitor::Status::Corrupt: return 4;
    case HealthMonitor::Status::Stale: return 5;
    }
    return 0;
}

} // namespace

HealthMonitor::HealthMonitor(QObject *parent) : QObject(parent)
{
    m_thread.setObjectName(QStringLiteral("HealthMonitor"));
    connect(&m_heartbeat, &QTimer::timeout, this, [this] {
        static quint64 beat = 0;
        arrived(qp::kGuiStream, ++beat, qp::nowNs());
    });
    connect(&m_publish, &QTimer::timeout, this, [this] {
        {
            std::lock_guard lk(m_mutex);
            m_snapshot = m_rows;
        }
        emit changed();
    });
}

HealthMonitor::~HealthMonitor()
{
    m_thread.quit();
    m_thread.wait();
}

QString HealthMonitor::statusName(Status s)
{
    switch (s) {
    case Status::Off: return QStringLiteral("off");
    case Status::Ok: return QStringLiteral("ok");
    case Status::Degraded: return QStringLiteral("degraded");
    case Status::Late: return QStringLiteral("late");
    case Status::Corrupt: return QStringLiteral("corrupt");
    case Status::Stale: return QStringLiteral("stale");
    }
    return {};
}

void HealthMonitor::configure(int stream, const QString &name, double periodMs, double latencyBudgetMs)
{
    Stream &s = m_streams[size_t(stream)];
    s.name = name;
    s.periodMs = periodMs;
    s.latencyBudgetMs = latencyBudgetMs;
    s.configured = true;
}

void HealthMonitor::setPeriod(int stream, double periodMs)
{
    m_streams[size_t(stream)].periodMs = periodMs;
}

void HealthMonitor::start()
{
    m_timer = new QTimer;
    m_timer->setInterval(20);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->moveToThread(&m_thread);
    connect(m_timer, &QTimer::timeout, m_timer, [this] { evaluate(); });
    connect(&m_thread, &QThread::started, m_timer, [this] { m_timer->start(); });
    connect(&m_thread, &QThread::finished, m_timer, &QObject::deleteLater);
    m_thread.start();
    m_heartbeat.start(20);
    m_publish.start(200);
}

void HealthMonitor::arrived(int stream, quint64 seq, qint64 captureNs)
{
    Stream &s = m_streams[size_t(stream)];
    const qint64 now = qp::nowNs();
    if (s.newSession.exchange(false, std::memory_order_relaxed))
        s.lastSeq.store(seq, std::memory_order_relaxed);
    const quint64 last = s.lastSeq.load(std::memory_order_relaxed);
    if (s.arrivals.load(std::memory_order_relaxed) > 0 && seq > last + 1)
        s.gaps.fetch_add(seq - last - 1, std::memory_order_relaxed);
    if (s.arrivals.load(std::memory_order_relaxed) > 0 && seq < last) {
        s.reordered.fetch_add(1, std::memory_order_relaxed);
        if (s.gaps.load(std::memory_order_relaxed) > 0)
            s.gaps.fetch_sub(1, std::memory_order_relaxed); // it was counted missing when the newer one came
    }
    if (seq > last || s.arrivals.load(std::memory_order_relaxed) == 0)
        s.lastSeq.store(seq, std::memory_order_relaxed);
    s.latencySumNs.fetch_add(now - captureNs, std::memory_order_relaxed);
    s.arrivals.fetch_add(1, std::memory_order_relaxed);
    s.lastArrivalNs.store(now, std::memory_order_release);
}

void HealthMonitor::resetSequences()
{
    for (Stream &s : m_streams)
        s.newSession.store(true, std::memory_order_relaxed);
}

void HealthMonitor::rejected(int stream)
{
    m_streams[size_t(stream)].rejected.fetch_add(1, std::memory_order_relaxed);
}

void HealthMonitor::noteFault(int stream, const QString &kind)
{
    std::lock_guard lk(m_mutex);
    m_faultKind[size_t(stream)] = kind;
    m_faultNs[size_t(stream)] = qp::nowNs();
    m_faultReset[size_t(stream)] = true;
}

void HealthMonitor::evaluate()
{
    const qint64 now = qp::nowNs();
    QVariantList rows;
    for (size_t i = 0; i < m_streams.size(); ++i) {
        Stream &s = m_streams[i];
        if (!s.configured)
            continue;
        const qint64 last = s.lastArrivalNs.load(std::memory_order_acquire);
        s.window.push_back(
            {now, s.arrivals.load(), s.gaps.load(), s.rejected.load(), s.reordered.load(), s.latencySumNs.load()});
        while (s.window.size() > 2 && now - s.window[1].ns >= kWindowNs)
            s.window.pop_front();
        const Stream::Tick &a = s.window.front(), &b = s.window.back();
        const double seconds = std::max(1e-3, double(b.ns - a.ns) / 1e9);
        const quint64 dA = b.arrivals - a.arrivals, dG = b.gaps - a.gaps, dR = b.rejected - a.rejected;
        const quint64 dO = b.reordered - a.reordered;
        s.hz = double(dA) / seconds;
        s.dropPct = dA + dG ? 100.0 * double(dG) / double(dA + dG) : 0;
        s.latencyMs = dA ? double(b.latencySumNs - a.latencySumNs) / double(dA) / 1e6 : 0;
        s.ageMs = last ? double(now - last) / 1e6 : 0;
        s.rejectedTotal = b.rejected;
        s.reorderedTotal = b.reordered;

        const double period = s.periodMs.load();
        const double staleMs = std::max(5 * period, 100.0);
        const double lateMs = 2 * period + 20;
        Status next = Status::Ok;
        QString why;
        if (!last) {
            next = Status::Off;
        } else if (s.ageMs > staleMs) {
            next = Status::Stale;
            why = QStringLiteral("no data for %1 ms").arg(s.ageMs, 0, 'f', 0);
        } else if (dR > 0) {
            next = Status::Corrupt;
            why = QStringLiteral("%1 rejected in the last second").arg(dR);
        } else if (s.ageMs > lateMs || s.latencyMs > s.latencyBudgetMs) {
            next = Status::Late;
            why = s.latencyMs > s.latencyBudgetMs ? QStringLiteral("arriving %1 ms after capture").arg(s.latencyMs, 0, 'f', 0)
                                                  : QStringLiteral("nothing for %1 ms").arg(s.ageMs, 0, 'f', 0);
        } else if (s.dropPct > 10) {
            next = Status::Degraded;
            why = QStringLiteral("%1% of samples missing").arg(s.dropPct, 0, 'f', 0);
        } else if (dO > 0) {
            next = Status::Degraded;
            why = QStringLiteral("%1 arrived out of order in the last second").arg(dO);
        }

        // Worse applies at once; better must hold for a while, so a stream doesn't flap.
        if (next != s.status) {
            const bool worse = severity(next) > severity(s.status);
            if (next != s.candidate) {
                s.candidate = next;
                s.candidateSinceNs = now;
            }
            if (worse || now - s.candidateSinceNs >= kRecoverNs) {
                const Status from = s.status;
                s.status = next;
                s.why = why;
                // A fault is attributed to its stream until it's cleared: escalation (late, then
                // stale) and flapping under it are the fault, not false alarms.
                QString kind;
                qint64 faultNs = 0;
                {
                    std::lock_guard lk(m_mutex);
                    if (std::exchange(m_faultReset[i], false))
                        s.seenWhileFaulted = 0;
                    kind = m_faultKind[i];
                    faultNs = m_faultNs[i];
                    if (kind == QLatin1String("clear") && next == Status::Ok)
                        m_faultKind[i].clear();
                }
                auto &stats = qp::StageStats::instance();
                // Until the stream is ok again after "clear", what happens is still the fault's doing.
                const bool faulted = !kind.isEmpty();
                const quint32 bit = 1u << int(next);
                if (faulted && kind != QLatin1String("clear") && next != Status::Ok && !(s.seenWhileFaulted & bit)) {
                    if (!s.seenWhileFaulted)
                        stats.add(QStringLiteral("detect_%1_ms").arg(kind), double(now - faultNs) / 1e6);
                    stats.add(QStringLiteral("detect_%1_%2_ms").arg(kind, statusName(next)), double(now - faultNs) / 1e6);
                    s.seenWhileFaulted |= bit;
                } else if (kind == QLatin1String("clear") && next == Status::Ok) {
                    stats.add(QStringLiteral("recover_ms"), double(now - faultNs) / 1e6);
                    s.seenWhileFaulted = 0;
                } else if (!faulted && from != Status::Off && severity(next) > severity(Status::Ok)) {
                    stats.add(QStringLiteral("unexpected_%1_%2").arg(s.name, statusName(next)).replace(u' ', u'_'), 1);
                }
                if (m_sink && from != Status::Off)
                    m_sink(int(i), from, next, why);
            }
        } else {
            s.candidate = next;
            s.why = why;
        }

        rows << QVariantMap{{"stream", int(i)},
                            {"name", s.name},
                            {"status", statusName(s.status)},
                            {"why", s.why},
                            {"hz", s.hz},
                            {"ageMs", s.ageMs},
                            {"dropPct", s.dropPct},
                            {"rejected", qulonglong(s.rejectedTotal)},
                            {"reordered", qulonglong(s.reorderedTotal)},
                            {"latencyMs", s.latencyMs}};
    }
    std::lock_guard lk(m_mutex);
    m_rows = rows;
}
