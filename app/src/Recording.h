#pragma once

#include "Codec.h"

#include <QFile>
#include <QJsonObject>
#include <QString>

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <limits>
#include <optional>
#include <variant>
#include <vector>

// The .qprec file: a header, then self-checking chunks of records in arrival order, then an
// index in time order. Without the index (the writer died) the chunks are scanned instead.
namespace rec {

constexpr quint16 kFormatMajor = 1;
constexpr quint16 kFormatMinor = 0;

struct RecordRef
{
    Type type{};
    quint16 version = 0;
    int stream = 0;
    qint64 t = 0;
    quint32 chunk = 0;
    quint32 offset = 0; // of the record header inside the chunk's decompressed bytes
};

struct WriterOptions
{
    LidarEncoding lidar = LidarEncoding::Int16Planar;
    int zstdLevel = 1; // 0: store uncompressed
    qsizetype chunkBytes = 4 << 20;
    qint64 chunkMaxNs = 1'000'000'000;
    qsizetype queueBytes = qsizetype(512) << 20;
    // Lidar in its own chunks, everything else (small, often read together) in others.
    bool separateSmall = true;
    QJsonObject info;
};

struct WriterStats
{
    qint64 fileBytes = 0;
    qint64 records = 0;
    qint64 chunks = 0;
    qint64 droppedRecords = 0;
    qint64 droppedBytes = 0;
    qint64 queueBytes = 0;
    qint64 queueBytesMax = 0;
    qint64 rawBytes = 0; // before compression
    qint64 firstT = 0, lastT = 0;
    double encodeMs = 0, compressMs = 0, writeMs = 0; // totals on the writer thread
    qint64 outOfOrder = 0; // records older than one already written, from another stream
    qint64 maxDisorderNs = 0;
    qint64 atRiskBytesMax = 0; // queued or in an unwritten chunk: lost if the process dies
};

class Writer
{
public:
    Writer(const QString &path, const WriterOptions &options);
    ~Writer();

    bool start(QString *error);
    // Any thread; never blocks on the disk. Over the queue budget, the record is dropped and a
    // Gap record says so.
    void addLidar(const qp::SensorFrame &frame);
    void addTracks(const qp::TrackSetPtr &set);
    void addEvents(const qp::EventBatchPtr &batch);
    void addCameraMark(const CameraMark &mark);
    // A record this writer has no type for, such as one from a newer version.
    void addRaw(Type type, quint16 version, int stream, qint64 t, const QByteArray &payload);
    // Flushes, writes the index and closes the file.
    void stop();

    WriterStats stats() const;
    QString path() const { return m_path; }

private:
    using Payload = std::variant<qp::SensorFrame, qp::TrackSetPtr, qp::EventBatchPtr, CameraMark, Gap, QByteArray>;
    struct Item
    {
        Type type;
        int stream;
        qint64 t;
        qsizetype bytes;
        Payload payload;
        quint16 version = 0; // raw records only
    };

    struct Open
    {
        QByteArray raw;
        quint32 records = 0;
        qint64 minT = 0, maxT = 0, startedNs = 0;
        std::vector<RecordRef> refs;
    };

    void push(Item item);
    void run();
    Open &openFor(Type type);
    void append(Open &o, const Item &item);
    void flushChunk(Open &o);
    bool writeIndex();

    QString m_path;
    WriterOptions m_opt;
    QFile m_file;

    mutable std::mutex m_mutex; // guards the queue, m_stop, m_pendingGap and m_stats
    std::condition_variable m_cv;
    std::deque<Item> m_queue;
    bool m_stop = false;
    Gap m_pendingGap;
    qint64 m_pendingGapT = 0;
    WriterStats m_stats;
    std::thread m_thread;

    // writer thread only
    Open m_open[2]; // [0] lidar (or everything), [1] the rest
    QByteArray m_compressed;
    struct ChunkInfo
    {
        quint64 offset;
        qint64 minT, maxT;
        quint32 records;
    };
    std::vector<ChunkInfo> m_chunks;
    std::vector<RecordRef> m_refs;
    qint64 m_newestWritten = std::numeric_limits<qint64>::min();
};

struct Record
{
    Type type{};
    quint16 version = 0;
    int stream = 0;
    qint64 t = 0;
    const char *data = nullptr;
    size_t size = 0;
    std::shared_ptr<const QByteArray> keepAlive;
};

struct ReaderInfo
{
    quint16 major = 0, minor = 0;
    QJsonObject info;
    bool recovered = false;  // the index was missing or bad; it was rebuilt by scanning
    qint64 recoveredChunks = 0;
    qint64 badChunks = 0;    // failed their checksum (skipped)
    qint64 truncatedBytes = 0;
    qint64 unknownRecords = 0; // types or versions this reader doesn't know
    qint64 indexBytes = 0;
    qint64 fileBytes = 0;
    double openMs = 0;
};

// Not thread-safe: one thread reads (the presenter). Keeps the index in memory and a few
// decompressed chunks.
class Reader
{
public:
    bool open(const QString &path, QString *error);
    const ReaderInfo &info() const { return m_info; }
    qint64 startNs() const { return m_start; }
    qint64 endNs() const { return m_end; }

    using Key = std::pair<Type, int>;
    // Every stream's records in time order.
    const std::map<Key, std::vector<RecordRef>> &streams() const { return m_streams; }
    std::optional<Record> read(const RecordRef &ref);

    // For tests and measurements.
    void dropCache() { m_cache.clear(); }
    // Every chunk read comes from the disk: no chunk cache, and the OS page cache is dropped first.
    void setColdReads(bool on) { m_coldReads = on; }
    qint64 chunkReads() const { return m_chunkReads; }

private:
    struct Chunk
    {
        quint64 offset = 0;
        qint64 minT = 0, maxT = 0;
        quint32 records = 0;
    };

    bool readIndex();
    void recover();
    std::shared_ptr<const QByteArray> loadChunk(quint32 index);
    void addRef(const RecordRef &ref);

    QFile m_file;
    ReaderInfo m_info;
    qint64 m_dataStart = 0;
    std::vector<Chunk> m_chunks;
    std::map<Key, std::vector<RecordRef>> m_streams;
    std::vector<std::pair<quint32, std::shared_ptr<const QByteArray>>> m_cache; // most recent last
    qint64 m_start = 0, m_end = 0;
    qint64 m_chunkReads = 0;
    bool m_coldReads = false;
};

bool isKnown(Type type, quint16 version);

} // namespace rec
