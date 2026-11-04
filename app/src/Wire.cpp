#include "Wire.h"

#include "Recording.h"

#include <QJsonDocument>

#include <zstd.h>

namespace wire {

namespace {

template <typename T> T load(const char *p)
{
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

} // namespace

QByteArray hello(const QJsonObject &info)
{
    const QByteArray json = QJsonDocument(info).toJson(QJsonDocument::Compact);
    QByteArray out;
    rec::ByteWriter w(out);
    w.bytes(kMagic, sizeof kMagic);
    w.put<quint16>(kMajor);
    w.put<quint16>(0);
    w.put<quint32>(quint32(json.size()));
    w.bytes(json.constData(), size_t(json.size()));
    return out;
}

QByteArray record(rec::Type type, quint16 version, int stream, qint64 t, const QByteArray &payload, quint16 flags)
{
    QByteArray out;
    out.reserve(kRecordHeader + payload.size());
    rec::ByteWriter w(out);
    w.put<quint16>(quint16(type));
    w.put<quint16>(version);
    w.put<quint16>(quint16(stream));
    w.put<quint16>(flags);
    w.put<qint64>(t);
    w.put<quint32>(quint32(payload.size()));
    out.append(payload);
    return out;
}

QByteArray lidarRecord(const qp::SensorFrame &frame, int zstdLevel)
{
    const QByteArray raw = rec::encodeLidar(frame, rec::LidarEncoding::Int16Planar);
    if (zstdLevel <= 0)
        return record(rec::Type::Lidar, quint16(rec::LidarEncoding::Int16Planar), frame.sensorId, frame.captureNs, raw);
    QByteArray packed(qsizetype(ZSTD_compressBound(size_t(raw.size()))) + 4, Qt::Uninitialized);
    const auto rawSize = quint32(raw.size());
    std::memcpy(packed.data(), &rawSize, 4);
    const size_t n = ZSTD_compress(packed.data() + 4, size_t(packed.size() - 4), raw.constData(), size_t(raw.size()),
                                   zstdLevel);
    if (ZSTD_isError(n))
        return record(rec::Type::Lidar, quint16(rec::LidarEncoding::Int16Planar), frame.sensorId, frame.captureNs, raw);
    packed.resize(qsizetype(n) + 4);
    return record(rec::Type::Lidar, quint16(rec::LidarEncoding::Int16Planar), frame.sensorId, frame.captureNs, packed,
                  kFlagZstd);
}

QByteArray tracksRecord(const qp::TrackSetPtr &set)
{
    return record(rec::Type::Tracks, rec::kTracksVersion, 0, set->captureNs, rec::encodeTracks(*set));
}

QByteArray eventsRecord(const qp::EventBatchPtr &batch)
{
    return record(rec::Type::Events, rec::kEventsVersion, 0, batch->empty() ? 0 : batch->front().captureNs,
                  rec::encodeEvents(*batch));
}

QByteArray overlayRecord(const OverlayFrame &frame)
{
    return record(rec::Type::Overlay, overlay::kVersion, 0, frame.t, overlay::encode(frame));
}

QByteArray selectionRecord(qint64 t, int trackId)
{
    QByteArray payload;
    rec::ByteWriter(payload).put<qint32>(trackId);
    return record(rec::Type::Selection, 1, 0, t, payload);
}

// One complete record at p. false: the stream is broken.
bool Parser::decode(const char *p, const std::function<void(const Decoded &)> &out)
{
    const quint32 n = load<quint32>(p + 16);
    const auto type = rec::Type(load<quint16>(p));
    const auto version = load<quint16>(p + 2);
    const int stream = load<quint16>(p + 4);
    const auto flags = load<quint16>(p + 6);
    Decoded d;
    d.type = type;
    d.stream = stream;
    d.t = load<qint64>(p + 8);
    const char *payload = p + kRecordHeader;
    size_t payloadSize = n;
    QByteArray unpacked;
    if (flags & kFlagZstd) {
        if (n < 4)
            return false;
        const quint32 rawSize = load<quint32>(payload);
        if (rawSize > m_maxRecord)
            return false;
        unpacked.resize(rawSize);
        const size_t got = ZSTD_decompress(unpacked.data(), rawSize, payload + 4, n - 4);
        if (ZSTD_isError(got) || got != rawSize)
            return false;
        payload = unpacked.constData();
        payloadSize = rawSize;
    }
    bool ok = rec::isKnown(type, version);
    if (ok) {
        switch (type) {
        case rec::Type::Lidar:
            if (auto f = rec::decodeLidar(version, stream, d.t, payload, payloadSize))
                d.lidar = *f;
            else
                ok = false;
            break;
        case rec::Type::Tracks: ok = (d.tracks = rec::decodeTracks(version, d.t, payload, payloadSize)) != nullptr; break;
        case rec::Type::Events: ok = (d.events = rec::decodeEvents(version, payload, payloadSize)) != nullptr; break;
        case rec::Type::Overlay: ok = (d.overlay = overlay::decode(version, d.t, payload, payloadSize)) != nullptr; break;
        case rec::Type::Selection:
            ok = payloadSize >= 4;
            if (ok)
                d.selection = load<qint32>(payload);
            break;
        default: ok = false; break;
        }
    }
    if (ok)
        out(d);
    else
        ++m_unknown;
    return true;
}

bool Parser::feed(const char *data, qsizetype size, const std::function<void(const Decoded &)> &out)
{
    m_buffer.append(data, size);
    m_bytes += size;
    qsizetype lastAt = -1;
    for (;;) {
        const qsizetype left = m_buffer.size() - m_at;
        const char *p = m_buffer.constData() + m_at;
        if (!m_hello) {
            if (left < 16)
                break;
            if (std::memcmp(p, kMagic, 8) != 0 || load<quint16>(p + 8) > kMajor)
                return false;
            const quint32 n = load<quint32>(p + 12);
            if (n > m_maxRecord)
                return false;
            if (left < 16 + qsizetype(n))
                break;
            m_info = QJsonDocument::fromJson(QByteArray(p + 16, n)).object();
            m_hello = true;
            m_at += 16 + n;
            continue;
        }
        if (left < kRecordHeader)
            break;
        const quint32 n = load<quint32>(p + 16);
        if (n > m_maxRecord)
            return false;
        if (left < kRecordHeader + qsizetype(n))
            break;
        if (m_lastOnly && rec::Type(load<quint16>(p)) == *m_lastOnly && !(load<quint16>(p + 6) & kFlagZstd)) {
            if (lastAt >= 0)
                ++m_superseded;
            lastAt = m_at;
            m_at += kRecordHeader + n;
            continue;
        }
        m_at += kRecordHeader + n;
        if (!decode(p, out))
            return false;
    }
    if (lastAt >= 0 && !decode(m_buffer.constData() + lastAt, out))
        return false;
    // Keep the buffer from growing: drop what has been consumed once it's most of it.
    if (m_at > (1 << 20) && m_at * 2 > m_buffer.size()) {
        m_buffer.remove(0, m_at);
        m_at = 0;
    }
    return true;
}

} // namespace wire
