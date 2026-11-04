#pragma once

#include "Codec.h"
#include "Overlay.h"

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
QByteArray overlayRecord(const OverlayFrame &frame);
QByteArray selectionRecord(qint64 t, int trackId);
// A clock probe: the client sends its time (server 0); the server echoes it with its own (step 11).
QByteArray clockRecord(qint64 clientNs, qint64 serverNs);
// The client's credit: every byte of the stream it has finished with, counted from its first byte.
QByteArray creditRecord(qint64 consumedBytes);

struct Decoded
{
    rec::Type type{};
    int stream = 0;
    qint64 t = 0;
    qp::SensorFrame lidar;
    qp::TrackSetPtr tracks;
    qp::EventBatchPtr events;
    OverlayFramePtr overlay;
    int selection = -1;
    qint64 clockClientNs = 0, clockServerNs = 0;
    qint64 credit = -1;
};

// Incremental: feed bytes as they arrive; complete records come out.
class Parser
{
public:
    explicit Parser(quint32 maxRecord = 64u << 20) : m_maxRecord(maxRecord) {}

    // false: the stream is not ours or is broken; the connection should close.
    bool feed(const char *data, qsizetype size, const std::function<void(const Decoded &)> &out);
    bool hasHello() const { return m_hello; }
    const QJsonObject &info() const { return m_info; }
    qint64 unknownRecords() const { return m_unknown; }
    qint64 bytes() const { return m_bytes; }
    // Records of this type that arrive together: only the newest is decoded (it comes out last).
    void setLatestOnly(rec::Type type) { m_lastOnly = type; }
    qint64 superseded() const { return m_superseded; }

private:
    bool decode(const char *p, const std::function<void(const Decoded &)> &out);

    quint32 m_maxRecord;
    std::optional<rec::Type> m_lastOnly;
    qint64 m_superseded = 0;
    qint64 m_bytes = 0;
    QByteArray m_buffer;
    qsizetype m_at = 0;
    bool m_hello = false;
    QJsonObject m_info;
    qint64 m_unknown = 0;
};

// Another process's clock, from probes (Cristian's algorithm): offset = server - (sent + rtt / 2),
// wrong by at most rtt / 2. The lowest-rtt probe of the recent ones wins: a probe that waited
// behind a backlog carries a long rtt, not a wrong offset.
class ClockSync
{
public:
    // Before any probe returns: the server's clock in its hello, read at arrival (wrong by the
    // one-way delay).
    void seed(qint64 serverNs, qint64 localNs);
    void add(qint64 sentNs, qint64 serverNs, qint64 receivedNs);
    bool known() const { return m_known; }
    qint64 offsetNs() const { return m_offset; } // server clock - local clock
    qint64 rttNs() const { return m_rtt; }       // of the probe in use; -1 while seeded only
    qint64 toLocal(qint64 serverNs) const { return serverNs - m_offset; }
    int probes() const { return m_count; }

private:
    struct Sample
    {
        qint64 offset, rtt;
    };
    static constexpr int kWindow = 16;
    Sample m_samples[kWindow]{};
    int m_count = 0;
    bool m_known = false;
    qint64 m_offset = 0, m_rtt = -1;
};

} // namespace wire
