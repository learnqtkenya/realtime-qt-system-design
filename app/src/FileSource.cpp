#include "FileSource.h"

#include <algorithm>

namespace {

auto tBefore = [](qint64 t, const rec::RecordRef &r) { return t < r.t; };

} // namespace

bool FileSource::open(const QString &path, QString *error)
{
    return m_reader.open(path, error);
}

std::vector<StreamId> FileSource::streams() const
{
    std::vector<StreamId> out;
    for (const auto &[key, refs] : m_reader.streams())
        if (key.first != rec::Type::Gap)
            out.push_back({key.first, key.second});
    return out;
}

const std::vector<rec::RecordRef> *FileSource::refs(StreamId id) const
{
    const auto it = m_reader.streams().find({id.type, id.stream});
    return it == m_reader.streams().end() ? nullptr : &it->second;
}

std::optional<Sample> FileSource::decode(const rec::RecordRef &ref)
{
    const std::optional<rec::Record> r = m_reader.read(ref);
    if (!r)
        return std::nullopt;
    Sample s;
    s.type = r->type;
    s.stream = r->stream;
    s.t = r->t;
    switch (r->type) {
    case rec::Type::Lidar:
        if (auto f = rec::decodeLidar(r->version, r->stream, r->t, r->data, r->size))
            s.lidar = *f;
        else
            return std::nullopt;
        break;
    case rec::Type::Tracks:
        s.tracks = rec::decodeTracks(r->version, r->t, r->data, r->size);
        if (!s.tracks)
            return std::nullopt;
        break;
    case rec::Type::Events:
        s.events = rec::decodeEvents(r->version, r->data, r->size);
        if (!s.events)
            return std::nullopt;
        break;
    case rec::Type::CameraMark:
        if (auto m = rec::decodeCameraMark(r->version, r->stream, r->t, r->data, r->size))
            s.mark = *m;
        else
            return std::nullopt;
        break;
    case rec::Type::Gap:
    case rec::Type::Overlay:
    case rec::Type::Selection:
    case rec::Type::Clock:
    case rec::Type::Credit: return std::nullopt;
    }
    return s;
}

std::optional<Sample> FileSource::latestIn(StreamId id, qint64 from, qint64 to)
{
    const auto *v = refs(id);
    if (!v)
        return std::nullopt;
    auto it = std::upper_bound(v->begin(), v->end(), to, tBefore);
    // A record that fails its checks is skipped, and the one before it is tried.
    while (it != v->begin()) {
        --it;
        if (it->t <= from)
            return std::nullopt;
        if (auto s = decode(*it))
            return s;
    }
    return std::nullopt;
}

void FileSource::allIn(StreamId id, qint64 from, qint64 to, std::vector<Sample> &out)
{
    const auto *v = refs(id);
    if (!v)
        return;
    for (auto it = std::upper_bound(v->begin(), v->end(), from, tBefore); it != v->end() && it->t <= to; ++it)
        if (auto s = decode(*it))
            out.push_back(std::move(*s));
}

std::optional<qint64> FileSource::nextAfter(StreamId id, qint64 after)
{
    const auto *v = refs(id);
    if (!v)
        return std::nullopt;
    const auto it = std::upper_bound(v->begin(), v->end(), after, tBefore);
    if (it == v->end())
        return std::nullopt;
    return it->t;
}

std::optional<qint64> FileSource::prevBefore(StreamId id, qint64 before)
{
    const auto *v = refs(id);
    if (!v)
        return std::nullopt;
    const auto it = std::lower_bound(v->begin(), v->end(), before,
                                     [](const rec::RecordRef &r, qint64 t) { return r.t < t; });
    if (it == v->begin())
        return std::nullopt;
    return std::prev(it)->t;
}
