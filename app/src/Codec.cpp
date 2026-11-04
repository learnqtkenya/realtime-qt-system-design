#include "Codec.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rec {

namespace {

// Records start at any byte offset in a chunk; this type makes unaligned int16 access legal.
typedef qint16 UnalignedInt16 __attribute__((aligned(1)));

constexpr quint16 kTrackStride = 4 + 6 * 4 + 1;

} // namespace

void ByteWriter::string(const QString &s)
{
    const QByteArray u = s.toUtf8().left(65535);
    put<quint16>(quint16(u.size()));
    bytes(u.constData(), size_t(u.size()));
}

const char *ByteReader::take(size_t n)
{
    if (left() < n) {
        m_ok = false;
        return nullptr;
    }
    const char *p = m_p;
    m_p += n;
    return p;
}

QString ByteReader::string()
{
    const quint16 n = get<quint16>();
    const char *p = take(n);
    return p ? QString::fromUtf8(p, n) : QString();
}

QByteArray encodeLidar(const qp::SensorFrame &frame, LidarEncoding encoding)
{
    QByteArray out;
    appendLidar(out, frame, encoding);
    return out;
}

namespace {

// For the rare frame with values int16 can't hold (NaN, beyond ±327.67 m): those are stored as
// kInt16Invalid and read back as NaN, so the record keeps "bad" as bad instead of clamping it into
// something plausible. Scalar and branchy; the usual frame never comes here.
void quantiseCarefully(const float *src, char *dst, size_t values, bool planar)
{
    constexpr float inv = 1.0f / kInt16Scale;
    for (size_t i = 0; i < values; ++i) {
        const float v = src[i] * inv;
        const quint16 q = std::abs(v) <= 32767.0f ? quint16(qint16(v + (v >= 0 ? 0.5f : -0.5f)))
                                                  : quint16(kInt16Invalid);
        if (planar) {
            dst[i] = char(q & 0xFF);
            dst[values + i] = char(q >> 8);
        } else {
            std::memcpy(dst + 2 * i, &q, 2);
        }
    }
}

} // namespace

void appendLidar(QByteArray &out, const qp::SensorFrame &frame, LidarEncoding encoding)
{
    const int n = frame.cloud ? frame.cloud->count() : 0;
    const size_t values = size_t(n) * 3;
    out.reserve(out.size() + 16 + qsizetype(values) * (encoding == LidarEncoding::Float32 ? 4 : 2));
    ByteWriter w(out);
    w.put<quint64>(frame.seq);
    w.put<quint32>(quint32(n));
    if (encoding == LidarEncoding::Float32) {
        if (n)
            w.bytes(frame.cloud->xyz.data(), values * 4);
        return;
    }
    w.put<float>(kInt16Scale);
    const qsizetype at = out.size();
    out.resize(at + qsizetype(values) * 2);
    // restrict: writes through char* could alias the input, which stops vectorisation.
    char *__restrict dst = out.data() + at;
    const float *__restrict src = n ? frame.cloud->xyz.data() : nullptr;
    constexpr float inv = 1.0f / kInt16Scale;
    // A separate counting pass (an int sum: GCC won't vectorise a bool reduction). It costs a
    // second read of the frame, 0.18 ms; folding it into the loop below made that loop 3x slower.
    int unfit = 0;
    for (size_t i = 0; i < values; ++i)
        unfit += !(std::abs(src[i] * inv) <= 32767.0f); // NaN fails this
    if (unfit) {
        quantiseCarefully(src, dst, values, encoding == LidarEncoding::Int16Planar);
        return;
    }
    const auto quantise = [inv](float x) {
        const float v = std::clamp(x * inv, -32767.0f, 32767.0f);
        return quint16(qint16(v + (v >= 0 ? 0.5f : -0.5f)));
    };
    if (encoding == LidarEncoding::Int16Planar) {
        // In blocks, so each loop is simple enough to vectorise.
        constexpr size_t kBlock = 1024;
        quint16 block[kBlock];
        for (size_t at = 0; at < values; at += kBlock) {
            const size_t n = std::min(kBlock, values - at);
            for (size_t i = 0; i < n; ++i)
                block[i] = quantise(src[at + i]);
            for (size_t i = 0; i < n; ++i)
                dst[at + i] = char(block[i] & 0xFF);
            for (size_t i = 0; i < n; ++i)
                dst[values + at + i] = char(block[i] >> 8);
        }
    } else {
        auto *q = reinterpret_cast<UnalignedInt16 *>(dst);
        for (size_t i = 0; i < values; ++i)
            q[i] = qint16(quantise(src[i]));
    }
}

std::optional<qp::SensorFrame> decodeLidar(quint16 version, int sensor, qint64 t, const char *data, size_t size)
{
    ByteReader r(data, size);
    qp::SensorFrame f;
    f.sensorId = sensor;
    f.captureNs = t;
    f.seq = r.get<quint64>();
    const quint32 n = r.get<quint32>();
    const size_t values = size_t(n) * 3;
    auto cloud = std::make_shared<qp::PointCloud>();
    if (version == quint16(LidarEncoding::Float32)) {
        const char *p = r.take(values * 4);
        if (!r.ok())
            return std::nullopt;
        cloud->xyz.resize(values);
        if (n)
            std::memcpy(cloud->xyz.data(), p, values * 4);
    } else if (version == quint16(LidarEncoding::Int16) || version == quint16(LidarEncoding::Int16Planar)) {
        const float scale = r.get<float>();
        const char *p = r.take(values * 2);
        if (!r.ok() || !(scale > 0))
            return std::nullopt;
        cloud->xyz.resize(values);
        float *dst = cloud->xyz.data();
        if (version == quint16(LidarEncoding::Int16)) {
            const auto *q = reinterpret_cast<const UnalignedInt16 *>(p);
            int invalid = 0;
            for (size_t i = 0; i < values; ++i) {
                dst[i] = float(q[i]) * scale;
                invalid += q[i] == kInt16Invalid;
            }
            for (size_t i = 0; invalid && i < values; ++i)
                if (q[i] == kInt16Invalid)
                    dst[i] = std::numeric_limits<float>::quiet_NaN();
        } else {
            const auto *lo = reinterpret_cast<const quint8 *>(p);
            const quint8 *hi = lo + values;
            int invalid = 0;
            for (size_t i = 0; i < values; ++i) {
                const auto q = qint16(quint16(lo[i] | (hi[i] << 8)));
                dst[i] = float(q) * scale;
                invalid += q == kInt16Invalid;
            }
            for (size_t i = 0; invalid && i < values; ++i)
                if (lo[i] == 0x00 && hi[i] == 0x80)
                    dst[i] = std::numeric_limits<float>::quiet_NaN();
        }
    } else {
        return std::nullopt;
    }
    f.cloud = std::move(cloud);
    return f;
}

QByteArray encodeTracks(const qp::TrackSet &set)
{
    QByteArray out;
    out.reserve(16 + qsizetype(set.tracks.size()) * kTrackStride);
    ByteWriter w(out);
    w.put<quint64>(set.seq);
    w.put<quint32>(quint32(set.tracks.size()));
    w.put<quint16>(kTrackStride);
    for (const qp::Track &t : set.tracks) {
        w.put<quint32>(t.id);
        w.put<float>(t.x);
        w.put<float>(t.y);
        w.put<float>(t.heading);
        w.put<float>(t.speed);
        w.put<float>(t.length);
        w.put<float>(t.width);
        w.put<quint8>(t.cls);
    }
    return out;
}

qp::TrackSetPtr decodeTracks(quint16 version, qint64 t, const char *data, size_t size)
{
    if (version < 1)
        return nullptr;
    ByteReader r(data, size);
    auto set = std::make_shared<qp::TrackSet>();
    set->captureNs = t;
    set->seq = r.get<quint64>();
    const quint32 n = r.get<quint32>();
    const quint16 stride = r.get<quint16>();
    if (!r.ok() || stride < kTrackStride || r.left() < size_t(n) * stride)
        return nullptr;
    set->tracks.resize(n);
    for (qp::Track &tr : set->tracks) {
        ByteReader item(r.take(stride), stride);
        tr.id = item.get<quint32>();
        tr.x = item.get<float>();
        tr.y = item.get<float>();
        tr.heading = item.get<float>();
        tr.speed = item.get<float>();
        tr.length = item.get<float>();
        tr.width = item.get<float>();
        tr.cls = item.get<quint8>();
    }
    return set;
}

QByteArray encodeEvents(const qp::EventBatch &batch)
{
    QByteArray out;
    ByteWriter w(out);
    w.put<quint32>(quint32(batch.size()));
    for (const qp::EventRecord &e : batch) {
        const qsizetype lenAt = w.size();
        w.put<quint16>(0);
        w.put<quint64>(e.seq);
        w.put<qint64>(e.captureNs);
        w.put<quint8>(e.severity);
        w.string(e.source);
        w.string(e.text);
        const quint16 len = quint16(w.size() - lenAt - 2);
        std::memcpy(w.at(lenAt), &len, 2);
    }
    return out;
}

qp::EventBatchPtr decodeEvents(quint16 version, const char *data, size_t size)
{
    if (version < 1)
        return nullptr;
    ByteReader r(data, size);
    const quint32 n = r.get<quint32>();
    if (!r.ok() || n > size / 2)
        return nullptr;
    auto batch = std::make_shared<qp::EventBatch>();
    batch->reserve(n);
    for (quint32 i = 0; i < n; ++i) {
        const quint16 len = r.get<quint16>();
        const char *p = r.take(len);
        if (!r.ok())
            return nullptr;
        ByteReader item(p, len);
        qp::EventRecord e;
        e.seq = item.get<quint64>();
        e.captureNs = item.get<qint64>();
        e.severity = item.get<quint8>();
        e.source = item.string();
        e.text = item.string();
        if (!item.ok())
            return nullptr;
        batch->push_back(std::move(e));
    }
    return batch;
}

QByteArray encodeCameraMark(const CameraMark &mark)
{
    QByteArray out;
    ByteWriter(out).put<qint64>(mark.mediaUs);
    return out;
}

std::optional<CameraMark> decodeCameraMark(quint16 version, int camera, qint64 t, const char *data, size_t size)
{
    ByteReader r(data, size);
    const qint64 media = r.get<qint64>();
    if (version < 1 || !r.ok())
        return std::nullopt;
    return CameraMark{camera, t, media};
}

QByteArray encodeGap(const Gap &gap)
{
    QByteArray out;
    ByteWriter w(out);
    w.put<quint32>(gap.records);
    w.put<quint64>(gap.bytes);
    return out;
}

std::optional<Gap> decodeGap(quint16 version, const char *data, size_t size)
{
    ByteReader r(data, size);
    Gap g;
    g.records = r.get<quint32>();
    g.bytes = r.get<quint64>();
    if (version < 1 || !r.ok())
        return std::nullopt;
    return g;
}

} // namespace rec
