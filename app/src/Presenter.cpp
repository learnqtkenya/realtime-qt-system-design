#include "Presenter.h"

#include "qp/StageStats.h"

#include <limits>

namespace {

constexpr qint64 kMin = std::numeric_limits<qint64>::min();
constexpr qint64 kMax = std::numeric_limits<qint64>::max();
constexpr qint64 kEventBacklogNs = 5'000'000'000; // what the log shows after a seek
constexpr qint64 kMaxSleepNs = 20'000'000;

double ms(qint64 ns)
{
    return double(ns) / 1e6;
}

} // namespace

Presenter::Presenter(Sinks sinks) : m_sinks(std::move(sinks)) {}

Presenter::~Presenter()
{
    stop();
}

void Presenter::start()
{
    if (!m_thread.joinable())
        m_thread = std::thread([this] { run(); });
}

void Presenter::stop()
{
    {
        std::lock_guard lk(m_mutex);
        m_stop = true;
    }
    m_cv.notify_one();
    if (m_thread.joinable())
        m_thread.join();
}

void Presenter::setSource(std::shared_ptr<Source> source, bool live)
{
    std::lock_guard lk(m_mutex);
    m_pendingSource = std::move(source);
    m_pendingLive = live;
    m_cmd.source = true;
    m_cv.notify_one();
}

void Presenter::goLive()
{
    std::lock_guard lk(m_mutex);
    m_cmd.mode = Mode::Live;
    m_cv.notify_one();
}

void Presenter::pause()
{
    std::lock_guard lk(m_mutex);
    m_cmd.mode = Mode::Paused;
    m_cv.notify_one();
}

void Presenter::play()
{
    std::lock_guard lk(m_mutex);
    m_cmd.mode = Mode::Playing;
    m_cv.notify_one();
}

void Presenter::setRate(double rate)
{
    std::lock_guard lk(m_mutex);
    m_cmd.rate = rate;
    m_cv.notify_one();
}

void Presenter::setDelay(qint64 delayNs)
{
    std::lock_guard lk(m_mutex);
    m_cmd.delay = std::max<qint64>(0, delayNs);
    m_cv.notify_one();
}

void Presenter::seek(qint64 t)
{
    ++m_seeksRequested;
    qp::StageStats::instance().add(QStringLiteral("seek_requested"), 1);
    std::lock_guard lk(m_mutex);
    m_cmd.seek = t;
    m_cmd.step = 0;
    m_cv.notify_one();
}

void Presenter::step(int direction)
{
    std::lock_guard lk(m_mutex);
    m_cmd.step += direction;
    m_cv.notify_one();
}

qint64 Presenter::clock(qint64 now) const
{
    switch (m_mode) {
    case Mode::Live: return now - m_delay;
    case Mode::Paused: return m_pausedT;
    case Mode::Playing: return m_anchorT + qint64(double(now - m_anchorWall) * m_rate);
    }
    return now;
}

qint64 Presenter::dueWall(qint64 t) const
{
    switch (m_mode) {
    case Mode::Live: return t + m_delay;
    case Mode::Paused: return kMax;
    case Mode::Playing: return m_anchorWall + qint64(double(t - m_anchorT) / m_rate);
    }
    return t;
}

void Presenter::anchor(qint64 t, qint64 now)
{
    m_anchorT = t;
    m_anchorWall = now;
}

void Presenter::deliver(const Sample &s, qint64 T, qint64 stampNs, qint64 now, bool timed)
{
    switch (s.type) {
    case rec::Type::Lidar: {
        if (!m_sinks.lidar)
            break;
        // Live keeps capture time, so latency includes the delay. Replay is timed from when the
        // sample was due: the latency of the replay path itself.
        if (m_mode == Mode::Live) {
            m_sinks.lidar(s.lidar);
        } else {
            qp::SensorFrame f = s.lidar;
            f.captureNs = stampNs;
            m_sinks.lidar(f);
        }
        if (timed)
            qp::StageStats::instance().add(m_mode == Mode::Live ? QStringLiteral("live_late_ms")
                                                                : QStringLiteral("present_late_ms"),
                                           ms(now - stampNs));
        break;
    }
    case rec::Type::Tracks:
        if (m_sinks.tracks && s.tracks)
            m_sinks.tracks(s.tracks);
        break;
    case rec::Type::Events:
        if (m_sinks.events && s.events)
            m_sinks.events(s.events, false);
        break;
    case rec::Type::CameraMark:
        if (m_sinks.camera)
            m_sinks.camera(s.mark, T, m_rate, m_mode == Mode::Playing, now);
        break;
    case rec::Type::Gap:
    case rec::Type::Overlay:
    case rec::Type::Selection: break;
    }
}

void Presenter::advance(qint64 T, qint64 now)
{
    if (!m_src)
        return;
    for (const StreamId &id : m_src->streams()) {
        // Live cameras play by themselves; the presenter drives them only in replay.
        if (id.type == rec::Type::CameraMark && m_mode == Mode::Live)
            continue;
        auto [it, fresh] = m_cursors.try_emplace({id.type, id.stream}, kMin);
        qint64 &cursor = it->second;
        if (id.type == rec::Type::Events) {
            std::vector<Sample> batch;
            m_src->allIn(id, cursor, T, batch);
            for (const Sample &s : batch)
                deliver(s, T, dueWall(s.t), now, m_mode != Mode::Paused);
            if (!batch.empty())
                cursor = batch.back().t;
        } else if (auto s = m_src->latestIn(id, cursor, T)) {
            deliver(*s, T, m_mode == Mode::Paused ? now : dueWall(s->t), now, m_mode != Mode::Paused);
            cursor = s->t;
        }
    }
}

void Presenter::doSeek(qint64 target, qint64 now)
{
    if (!m_src)
        return;
    const qint64 t0 = qp::nowNs();
    for (const StreamId &id : m_src->streams()) {
        if (id.type == rec::Type::Events) {
            std::vector<Sample> backlog;
            m_src->allIn(id, target - kEventBacklogNs, target, backlog);
            auto all = std::make_shared<qp::EventBatch>();
            for (const Sample &s : backlog)
                all->insert(all->end(), s.events->begin(), s.events->end());
            if (m_sinks.events)
                m_sinks.events(all, true);
        } else if (id.type != rec::Type::CameraMark || m_mode != Mode::Live) {
            if (auto s = m_src->latestIn(id, kMin, target))
                deliver(*s, target, now, now, false);
        }
        m_cursors[{id.type, id.stream}] = target;
    }
    ++m_seeksDone;
    qp::StageStats::instance().add(QStringLiteral("seek_ms"), ms(qp::nowNs() - t0));
}

void Presenter::setPass(bool on, qint64 now)
{
    std::lock_guard g(m_passMutex);
    if (on == m_pass)
        return;
    if (on) {
        advance(now, now); // whatever arrived since the last refresh, then hand over
    } else if (m_src) {
        // Everything up to the newest sample was shown by pass-through.
        for (const StreamId &id : m_src->streams())
            m_cursors[{id.type, id.stream}] = m_src->prevBefore(id, kMax).value_or(kMin);
    }
    m_pass = on;
    m_passOut = on;
}

std::optional<qint64> Presenter::stepTarget(qint64 T, int direction)
{
    std::optional<qint64> best;
    for (const StreamId &id : m_src->streams()) {
        if (id.type != rec::Type::Lidar)
            continue;
        const auto t = direction > 0 ? m_src->nextAfter(id, T) : m_src->prevBefore(id, T);
        if (t && (!best || (direction > 0 ? *t < *best : *t > *best)))
            best = t;
    }
    return best;
}

void Presenter::publish(qint64 T)
{
    m_playheadOut = T;
    m_modeOut = m_mode;
    m_rateOut = m_rate;
    m_delayOut = m_delay;
    m_liveOut = m_live;
}

void Presenter::run()
{
    std::unique_lock lk(m_mutex);
    while (!m_stop) {
        const Commands c = std::exchange(m_cmd, {});
        std::shared_ptr<Source> pendingSource;
        bool pendingLive = false;
        if (c.source) {
            pendingSource = std::move(m_pendingSource);
            pendingLive = m_pendingLive;
        }
        lk.unlock();

        const qint64 now = qp::nowNs();
        qint64 T = clock(now);
        bool refresh = false;
        std::optional<qint64> target;

        if (c.source) {
            setPass(false, now);
            m_src = std::move(pendingSource);
            m_live = pendingLive;
            m_cursors.clear();
            if (m_live) {
                m_mode = Mode::Live;
                refresh = true;
            } else {
                m_mode = Mode::Paused;
                target = m_src ? m_src->startNs() : 0;
            }
        }
        if (c.delay)
            m_delay = *c.delay;
        if (c.rate) {
            if (m_mode == Mode::Playing)
                anchor(T, now);
            m_rate = std::clamp(*c.rate, 0.05, 16.0);
        }
        if (c.mode) {
            switch (*c.mode) {
            case Mode::Live:
                if (m_live && m_mode != Mode::Live) {
                    m_mode = Mode::Live;
                    refresh = true;
                }
                break;
            case Mode::Paused:
                m_pausedT = T;
                m_mode = Mode::Paused;
                break;
            case Mode::Playing:
                if (m_src && !m_live && T >= m_src->endNs()) // replay a finished file from the start
                    target = T = m_src->startNs();
                anchor(T, now);
                m_mode = Mode::Playing;
                break;
            }
        }
        if (c.step && m_src) {
            if (const auto t = stepTarget(T, c.step)) {
                m_mode = Mode::Paused;
                target = t;
            }
        }
        if (c.seek && m_src) {
            if (m_mode == Mode::Live)
                m_mode = Mode::Paused;
            target = std::clamp(*c.seek, m_src->startNs(), std::max(m_src->startNs(), m_src->endNs()));
        }

        const bool wantPass = m_live && m_mode == Mode::Live && m_delay == 0;
        if (!wantPass)
            setPass(false, now);
        if (target) {
            T = *target;
            if (m_mode == Mode::Paused)
                m_pausedT = T;
            else
                anchor(T, now);
            doSeek(T, now);
        } else if (refresh || (c.delay && m_mode == Mode::Live)) {
            T = clock(now);
            doSeek(T, now);
        }
        if (wantPass)
            setPass(true, now);

        if (!m_pass && m_mode != Mode::Paused && m_src) {
            T = clock(now);
            const qint64 end = m_src->endNs();
            if (m_mode == Mode::Playing && T >= end) {
                if (m_live) {
                    m_mode = Mode::Live; // caught up with the present
                    T = clock(now);
                    advance(T, now);
                    if (m_delay == 0)
                        setPass(true, now);
                } else {
                    advance(end, now);
                    m_mode = Mode::Paused;
                    m_pausedT = T = end;
                }
            } else {
                advance(T, now);
            }
        }
        publish(T);

        lk.lock();
        if (m_stop || m_cmd.any() || std::exchange(m_dataArrived, false))
            continue;
        if (m_pass || m_mode == Mode::Paused || !m_src) {
            m_cv.wait_for(lk, std::chrono::milliseconds(100));
            continue;
        }
        // Sleep until the next sample is due.
        qint64 wake = qp::nowNs() + kMaxSleepNs;
        lk.unlock();
        for (const StreamId &id : m_src->streams()) {
            if (id.type == rec::Type::CameraMark && m_mode == Mode::Live)
                continue;
            const auto it = m_cursors.find({id.type, id.stream});
            if (const auto t = m_src->nextAfter(id, it == m_cursors.end() ? kMin : it->second))
                wake = std::min(wake, dueWall(*t));
        }
        lk.lock();
        if (!m_stop && !m_cmd.any() && !m_dataArrived && wake > qp::nowNs())
            m_cv.wait_for(lk, std::chrono::nanoseconds(wake - qp::nowNs()));
        m_dataArrived = false;
    }
}
