#pragma once

#include "Codec.h"

#include <QJsonObject>

#include <functional>

// The stream between viewers: a hello (format version, JSON info), then records laid out as in a
// recording chunk (type, version, stream, flags, capture time, size, payload).
namespace wire {

constexpr char kMagic[8] = {'Q', 'P', 'S', 'T', 'R', 'E', 'A', 'M'};
constexpr quint16 kMajor = 1;
constexpr quint16 kFlagZstd = 1; // the payload is zstd-compressed
constexpr qsizetype kRecordHeader = 20;

QByteArray hello(const QJsonObject &info);
QByteArray record(rec::Type type, quint16 version, int stream, qint64 t, const QByteArray &payload,
                  quint16 flags = 0);

// Lidar as planar int16, zstd-compressed when level > 0.
QByteArray lidarRecord(const qp::SensorFrame &frame, int zstdLevel);
QByteArray tracksRecord(const qp::TrackSetPtr &set);
QByteArray eventsRecord(const qp::EventBatchPtr &batch);

struct Decoded
{
    rec::Type type{};
    int stream = 0;
    qint64 t = 0;
    qp::SensorFrame lidar;
    qp::TrackSetPtr tracks;
    qp::EventBatchPtr events;
};

// Incremental: feed bytes as they arrive; complete records come out.
class Parser
{
public:
    // false: the stream is not ours or is broken; the connection should close.
    bool feed(const char *data, qsizetype size, const std::function<void(const Decoded &)> &out);
    bool hasHello() const { return m_hello; }
    const QJsonObject &info() const { return m_info; }
    qint64 unknownRecords() const { return m_unknown; }

private:
    QByteArray m_buffer;
    qsizetype m_at = 0;
    bool m_hello = false;
    QJsonObject m_info;
    qint64 m_unknown = 0;
};

} // namespace wire
