#include "History.h"

#include "qp/StageStats.h"

#include <algorithm>

namespace {

constexpr qint64 kEntryOverhead = 96;

} // namespace

History::History(qint64 budgetBytes, Shape shape) : m_budget(budgetBytes), m_shape(shape) {}

void History::addLidar(const qp::SensorFrame &frame)
{
    if (!frame.cloud)
        return;
    const qint64 t0 = qp::nowNs();
    Entry e{frame.captureNs, {}, {}, kEntryOverhead};
    e.sample.type = rec::Type::Lidar;
    e.sample.stream = frame.sensorId;
    e.sample.t = frame.captureNs;
    if (m_shape == Shape::Int16) {
        e.blob = rec::encodeLidar(frame, rec::LidarEncoding::Int16);
        e.bytes += e.blob.size();
        qp::StageStats::instance().add(QStringLiteral("history_encode_ms"), double(qp::nowNs() - t0) / 1e6);
    } else {
        e.sample.lidar = frame;
        e.bytes += qint64(frame.cloud->xyz.size() * sizeof(float));
    }
    insert({rec::Type::Lidar, frame.sensorId}, std::move(e));
    qp::StageStats::instance().add(QStringLiteral("history_add_ms"), double(qp::nowNs() - t0) / 1e6);
}

void History::addTracks(const qp::TrackSetPtr &set)
{
    if (!set)
        return;
    Entry e{set->captureNs, {}, {}, kEntryOverhead + qint64(set->tracks.size() * sizeof(qp::Track))};
    e.sample.type = rec::Type::Tracks;
    e.sample.t = set->captureNs;
    e.sample.tracks = set;
    insert({rec::Type::Tracks, 0}, std::move(e));
}

void History::addEvents(const qp::EventBatchPtr &batch)
{
    if (!batch || batch->empty())
        return;
    const qint64 t = batch->front().captureNs;
    Entry e{t, {}, {}, kEntryOverhead + qint64(batch->size()) * qint64(sizeof(qp::EventRecord) + 48)};
    e.sample.type = rec::Type::Events;
    e.sample.t = t;
    e.sample.events = batch;
    insert({rec::Type::Events, 0}, std::move(e));
}

void History::addCameraMark(const rec::CameraMark &mark)
{
    Entry e{mark.captureNs, {}, {}, kEntryOverhead};
    e.sample.type = rec::Type::CameraMark;
    e.sample.stream = mark.camera;
    e.sample.t = mark.captureNs;
    e.sample.mark = mark;
    insert({rec::Type::CameraMark, mark.camera}, std::move(e));
}

History::Stream *History::find(StreamId id)
{
    for (Stream &s : m_streams)
        if (s.id == id)
            return &s;
    return nullptr;
}

void History::insert(StreamId id, Entry entry)
{
    std::lock_guard lk(m_mutex);
    Stream *s = find(id);
    if (!s) {
        m_streams.push_back({id, {}});
        s = &m_streams.back();
    }
    m_bytes += entry.bytes;
    if (s->entries.empty() || entry.t >= s->entries.back().t)
        s->entries.push_back(std::move(entry));
    else
        s->entries.insert(std::upper_bound(s->entries.begin(), s->entries.end(), entry.t,
                                           [](qint64 t, const Entry &e) { return t < e.t; }),
                          std::move(entry));
    while (m_bytes > m_budget) {
        Stream *oldest = nullptr;
        for (Stream &c : m_streams)
            if (!c.entries.empty() && (!oldest || c.entries.front().t < oldest->entries.front().t))
                oldest = &c;
        if (!oldest)
            break;
        m_bytes -= oldest->entries.front().bytes;
        oldest->entries.pop_front();
        ++m_evicted;
    }
}

void History::clear()
{
    std::lock_guard lk(m_mutex);
    m_streams.clear();
    m_bytes = 0;
}

qint64 History::bytes() const
{
    std::lock_guard lk(m_mutex);
    return m_bytes;
}

qint64 History::evicted() const
{
    std::lock_guard lk(m_mutex);
    return m_evicted;
}

std::vector<StreamId> History::streams() const
{
    std::lock_guard lk(m_mutex);
    std::vector<StreamId> out;
    for (const Stream &s : m_streams)
        out.push_back(s.id);
    return out;
}

qint64 History::startNs() const
{
    // Where every lidar sensor has data, so a seek to the start shows a whole frame.
    std::lock_guard lk(m_mutex);
    qint64 start = 0;
    bool any = false;
    for (const Stream &s : m_streams)
        if (s.id.type == rec::Type::Lidar && !s.entries.empty()) {
            start = any ? std::max(start, s.entries.front().t) : s.entries.front().t;
            any = true;
        }
    if (!any)
        for (const Stream &s : m_streams)
            if (!s.entries.empty()) {
                start = any ? std::min(start, s.entries.front().t) : s.entries.front().t;
                any = true;
            }
    return start;
}

qint64 History::endNs() const
{
    std::lock_guard lk(m_mutex);
    qint64 end = 0;
    for (const Stream &s : m_streams)
        if (!s.entries.empty())
            end = std::max(end, s.entries.back().t);
    return end;
}

Sample History::materialise(const Entry &e)
{
    if (e.blob.isEmpty())
        return e.sample;
    Sample s = e.sample;
    if (auto f = rec::decodeLidar(quint16(rec::LidarEncoding::Int16), e.sample.stream, e.t, e.blob.constData(),
                                  size_t(e.blob.size())))
        s.lidar = *f;
    return s;
}

std::optional<Sample> History::latestIn(StreamId id, qint64 from, qint64 to)
{
    Entry copy;
    {
        std::lock_guard lk(m_mutex);
        Stream *s = find(id);
        if (!s || s->entries.empty())
            return std::nullopt;
        auto it = std::upper_bound(s->entries.begin(), s->entries.end(), to,
                                   [](qint64 t, const Entry &e) { return t < e.t; });
        if (it == s->entries.begin())
            return std::nullopt;
        --it;
        if (it->t <= from)
            return std::nullopt;
        copy = *it; // shares the frame or blob; decoding happens outside the lock
    }
    return materialise(copy);
}

void History::allIn(StreamId id, qint64 from, qint64 to, std::vector<Sample> &out)
{
    std::vector<Entry> copies;
    {
        std::lock_guard lk(m_mutex);
        Stream *s = find(id);
        if (!s)
            return;
        for (auto it = std::upper_bound(s->entries.begin(), s->entries.end(), from,
                                        [](qint64 t, const Entry &e) { return t < e.t; });
             it != s->entries.end() && it->t <= to; ++it)
            copies.push_back(*it);
    }
    for (const Entry &e : copies)
        out.push_back(materialise(e));
}

std::optional<qint64> History::nextAfter(StreamId id, qint64 after)
{
    std::lock_guard lk(m_mutex);
    Stream *s = find(id);
    if (!s)
        return std::nullopt;
    auto it = std::upper_bound(s->entries.begin(), s->entries.end(), after,
                               [](qint64 t, const Entry &e) { return t < e.t; });
    if (it == s->entries.end())
        return std::nullopt;
    return it->t;
}

std::optional<qint64> History::prevBefore(StreamId id, qint64 before)
{
    std::lock_guard lk(m_mutex);
    Stream *s = find(id);
    if (!s)
        return std::nullopt;
    auto it = std::lower_bound(s->entries.begin(), s->entries.end(), before,
                               [](const Entry &e, qint64 t) { return e.t < t; });
    if (it == s->entries.begin())
        return std::nullopt;
    return std::prev(it)->t;
}
