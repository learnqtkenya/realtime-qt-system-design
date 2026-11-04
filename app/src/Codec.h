#pragma once

#include "qp/SimTypes.h"

#include <QByteArray>

#include <bit>
#include <cstring>
#include <optional>

// Record payloads, shared by the in-memory history and the recording file.
// Evolution rules: an incompatible change gets a new version; a compatible one appends fields,
// and readers skip what they don't know (item strides and record sizes are always stored).
namespace rec {

static_assert(std::endian::native == std::endian::little, "the format is little-endian");

// Overlay and Selection travel between the viewer and process plugins only (step 8).
enum class Type : quint16 { Lidar = 1, Tracks = 2, Events = 3, CameraMark = 4, Gap = 5, Overlay = 6, Selection = 7 };

// The Lidar record version. Int16Planar stores all low bytes, then all high bytes: same size,
// but it compresses better (results/step-04/codec.md).
enum class LidarEncoding : quint16 { Float32 = 1, Int16 = 2, Int16Planar = 3 };

constexpr quint16 kTracksVersion = 1;
constexpr quint16 kEventsVersion = 2; // 2: optional anchor appended to items (step 10)
constexpr quint16 kCameraMarkVersion = 1;
constexpr quint16 kGapVersion = 1;
constexpr float kInt16Scale = 0.01f; // metres per step: ±327 m in 1 cm steps
constexpr qint16 kInt16Invalid = -32768; // never a quantised value (those stop at ±32767)

struct CameraMark
{
    int camera = 0;
    qint64 captureNs = 0;
    qint64 mediaUs = 0; // position in the camera's source video
};

struct Gap
{
    quint32 records = 0;
    quint64 bytes = 0;
};

class ByteWriter
{
public:
    explicit ByteWriter(QByteArray &out) : m_out(out) {}
    template <typename T> void put(T v)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        m_out.append(reinterpret_cast<const char *>(&v), sizeof v);
    }
    void bytes(const void *p, size_t n) { m_out.append(static_cast<const char *>(p), qsizetype(n)); }
    void string(const QString &s);
    qsizetype size() const { return m_out.size(); }
    char *at(qsizetype offset) { return m_out.data() + offset; }

private:
    QByteArray &m_out;
};

// Never reads past the end; a short or corrupt payload makes ok() false.
class ByteReader
{
public:
    ByteReader(const char *data, size_t size) : m_p(data), m_end(data + size) {}
    template <typename T> T get()
    {
        T v{};
        if (size_t(m_end - m_p) < sizeof v) {
            m_ok = false;
            return v;
        }
        std::memcpy(&v, m_p, sizeof v);
        m_p += sizeof v;
        return v;
    }
    const char *take(size_t n);
    QString string();
    bool ok() const { return m_ok; }
    size_t left() const { return size_t(m_end - m_p); }

private:
    const char *m_p;
    const char *m_end;
    bool m_ok = true;
};

QByteArray encodeLidar(const qp::SensorFrame &frame, LidarEncoding encoding);
// Appends to out, so a writer can encode straight into its chunk buffer.
void appendLidar(QByteArray &out, const qp::SensorFrame &frame, LidarEncoding encoding);
std::optional<qp::SensorFrame> decodeLidar(quint16 version, int sensor, qint64 t, const char *data, size_t size);

QByteArray encodeTracks(const qp::TrackSet &set);
qp::TrackSetPtr decodeTracks(quint16 version, qint64 t, const char *data, size_t size);

QByteArray encodeEvents(const qp::EventBatch &batch);
qp::EventBatchPtr decodeEvents(quint16 version, const char *data, size_t size);

QByteArray encodeCameraMark(const CameraMark &mark);
std::optional<CameraMark> decodeCameraMark(quint16 version, int camera, qint64 t, const char *data, size_t size);

QByteArray encodeGap(const Gap &gap);
std::optional<Gap> decodeGap(quint16 version, const char *data, size_t size);

} // namespace rec
