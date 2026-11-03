#include "Recording.h"

#include "qp/StageStats.h"

#include <QJsonDocument>

#include <zlib.h>
#include <zstd.h>

#include <algorithm>
#include <fcntl.h>
#include <unistd.h>

namespace rec {

namespace {

constexpr char kMagic[8] = {'Q', 'P', 'R', 'E', 'C', '\r', '\n', '\x1a'};
constexpr quint32 kChunkMagic = 0x4B435051; // "QPCK"
constexpr quint32 kIndexMagic = 0x58495051; // "QPIX"
constexpr quint32 kEndMagic = 0x4E455051;   // "QPEN"
constexpr qsizetype kChunkHeader = 44;
constexpr qsizetype kRecordHeader = 20;
constexpr qsizetype kTrailer = 20;
constexpr size_t kCacheChunks = 6;

quint32 crc(const char *p, qsizetype n)
{
    return quint32(crc32(0, reinterpret_cast<const Bytef *>(p), uInt(n)));
}

template <typename T> T load(const char *p)
{
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

qsizetype payloadBytes(const qp::SensorFrame &f)
{
    return f.cloud ? qsizetype(f.cloud->xyz.size() * sizeof(float)) : 0;
}

} // namespace

bool isKnown(Type type, quint16 version)
{
    switch (type) {
    case Type::Lidar: return version >= 1 && version <= 3;
    case Type::Tracks:
    case Type::Events:
    case Type::CameraMark:
    case Type::Gap: return version >= 1; // newer versions only append fields
    }
    return false;
}

// ---- Writer -------------------------------------------------------------------------------

Writer::Writer(const QString &path, const WriterOptions &options) : m_path(path), m_opt(options), m_file(path) {}

Writer::~Writer()
{
    stop();
}

bool Writer::start(QString *error)
{
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = m_file.errorString();
        return false;
    }
    QJsonObject info = m_opt.info;
    info["lidarEncoding"] = int(m_opt.lidar);
    info["zstdLevel"] = m_opt.zstdLevel;
    const QByteArray json = QJsonDocument(info).toJson(QJsonDocument::Compact);
    QByteArray header;
    ByteWriter w(header);
    w.bytes(kMagic, sizeof kMagic);
    w.put<quint16>(kFormatMajor);
    w.put<quint16>(kFormatMinor);
    w.put<quint32>(quint32(json.size()));
    w.bytes(json.constData(), size_t(json.size()));
    m_file.write(header);
    m_stats.fileBytes = header.size();
    m_thread = std::thread([this] { run(); });
    return true;
}

void Writer::addLidar(const qp::SensorFrame &frame)
{
    push({Type::Lidar, frame.sensorId, frame.captureNs, payloadBytes(frame), frame});
}

void Writer::addTracks(const qp::TrackSetPtr &set)
{
    if (set)
        push({Type::Tracks, 0, set->captureNs, qsizetype(set->tracks.size() * sizeof(qp::Track)), set});
}

void Writer::addEvents(const qp::EventBatchPtr &batch)
{
    if (batch && !batch->empty())
        push({Type::Events, 0, batch->front().captureNs, qsizetype(batch->size() * 64), batch});
}

void Writer::addCameraMark(const CameraMark &mark)
{
    push({Type::CameraMark, mark.camera, mark.captureNs, 16, mark});
}

void Writer::addRaw(Type type, quint16 version, int stream, qint64 t, const QByteArray &payload)
{
    push({type, stream, t, payload.size(), payload, version});
}

void Writer::push(Item item)
{
    {
        std::lock_guard lk(m_mutex);
        if (m_stop || !m_thread.joinable())
            return;
        if (m_stats.queueBytes + item.bytes > m_opt.queueBytes) {
            if (m_pendingGap.records == 0)
                m_pendingGapT = item.t;
            ++m_pendingGap.records;
            m_pendingGap.bytes += quint64(item.bytes);
            ++m_stats.droppedRecords;
            m_stats.droppedBytes += item.bytes;
            return;
        }
        if (m_pendingGap.records) {
            m_queue.push_back({Type::Gap, 0, m_pendingGapT, 16, m_pendingGap});
            m_pendingGap = {};
        }
        m_stats.queueBytes += item.bytes;
        m_stats.queueBytesMax = std::max(m_stats.queueBytesMax, m_stats.queueBytes);
        m_queue.push_back(std::move(item));
    }
    m_cv.notify_one();
}

Writer::Open &Writer::openFor(Type type)
{
    return m_open[m_opt.separateSmall && type != Type::Lidar ? 1 : 0];
}

void Writer::run()
{
    auto flushOld = [this] {
        for (Open &o : m_open)
            if (o.records && qp::nowNs() - o.startedNs > m_opt.chunkMaxNs)
                flushChunk(o);
    };
    for (;;) {
        Item item;
        {
            std::unique_lock lk(m_mutex);
            m_cv.wait_for(lk, std::chrono::milliseconds(100), [this] { return m_stop || !m_queue.empty(); });
            if (m_queue.empty()) {
                if (m_stop)
                    break;
                lk.unlock();
                flushOld();
                continue;
            }
            item = std::move(m_queue.front());
            m_queue.pop_front();
            m_stats.queueBytes -= item.bytes;
            m_stats.atRiskBytesMax = std::max(m_stats.atRiskBytesMax, m_stats.queueBytes + item.bytes
                                                                          + qint64(m_open[0].raw.size())
                                                                          + qint64(m_open[1].raw.size()));
        }
        Open &o = openFor(item.type);
        append(o, item);
        if (o.raw.size() >= m_opt.chunkBytes)
            flushChunk(o);
        flushOld();
    }
    for (Open &o : m_open)
        flushChunk(o);
}

void Writer::append(Open &o, const Item &item)
{
    const qint64 t0 = qp::nowNs();
    if (o.records == 0) {
        o.minT = o.maxT = item.t;
        o.startedNs = t0;
        // Keeps its capacity from chunk to chunk: no regrowing, no fresh pages to fault in.
        o.raw.reserve(m_opt.chunkBytes + (4 << 20));
    }
    quint16 version = 1;
    if (std::holds_alternative<QByteArray>(item.payload))
        version = item.version;
    else if (item.type == Type::Lidar)
        version = quint16(m_opt.lidar);
    const qsizetype at = o.raw.size();
    o.refs.push_back({item.type, version, item.stream, item.t, 0, quint32(at)}); // chunk set at flush
    ByteWriter w(o.raw);
    w.put<quint16>(quint16(item.type));
    w.put<quint16>(version);
    w.put<quint16>(quint16(item.stream));
    w.put<quint16>(0);
    w.put<qint64>(item.t);
    w.put<quint32>(0); // size, patched below
    if (const auto *raw = std::get_if<QByteArray>(&item.payload)) {
        o.raw.append(*raw);
    } else {
        switch (item.type) {
        case Type::Lidar: appendLidar(o.raw, std::get<qp::SensorFrame>(item.payload), m_opt.lidar); break;
        case Type::Tracks: o.raw.append(encodeTracks(*std::get<qp::TrackSetPtr>(item.payload))); break;
        case Type::Events: o.raw.append(encodeEvents(*std::get<qp::EventBatchPtr>(item.payload))); break;
        case Type::CameraMark: o.raw.append(encodeCameraMark(std::get<CameraMark>(item.payload))); break;
        case Type::Gap: o.raw.append(encodeGap(std::get<Gap>(item.payload))); break;
        }
    }
    const auto size = quint32(o.raw.size() - at - 20);
    std::memcpy(o.raw.data() + at + 16, &size, 4);
    ++o.records;
    o.minT = std::min(o.minT, item.t);
    o.maxT = std::max(o.maxT, item.t);
    const qint64 t1 = qp::nowNs();
    if (item.type == Type::Lidar)
        qp::StageStats::instance().add(QStringLiteral("record_lidar_encode_ms"), double(t1 - t0) / 1e6);

    std::lock_guard lk(m_mutex);
    m_stats.encodeMs += double(t1 - t0) / 1e6;
    ++m_stats.records;
    if (m_stats.firstT == 0)
        m_stats.firstT = item.t;
    m_stats.lastT = std::max(m_stats.lastT, item.t);
    if (item.t < m_newestWritten) {
        ++m_stats.outOfOrder;
        m_stats.maxDisorderNs = std::max(m_stats.maxDisorderNs, m_newestWritten - item.t);
    }
    m_newestWritten = std::max(m_newestWritten, item.t);
}

void Writer::flushChunk(Open &o)
{
    if (o.records == 0)
        return;
    const qint64 t0 = qp::nowNs();
    quint8 compression = 0;
    if (m_opt.zstdLevel > 0) {
        m_compressed.resize(qsizetype(ZSTD_compressBound(size_t(o.raw.size()))));
        const size_t n = ZSTD_compress(m_compressed.data(), size_t(m_compressed.size()), o.raw.constData(),
                                       size_t(o.raw.size()), m_opt.zstdLevel);
        if (!ZSTD_isError(n)) {
            m_compressed.resize(qsizetype(n));
            compression = 1;
        }
    }
    const QByteArray &stored = compression ? m_compressed : o.raw;
    const qint64 t1 = qp::nowNs();

    QByteArray header;
    ByteWriter w(header);
    w.put<quint32>(kChunkMagic);
    w.put<quint32>(quint32(stored.size()));
    w.put<quint32>(quint32(o.raw.size()));
    w.put<quint32>(o.records);
    w.put<qint64>(o.minT);
    w.put<qint64>(o.maxT);
    w.put<quint8>(compression);
    w.put<quint8>(0);
    w.put<quint16>(0);
    w.put<quint32>(crc(stored.constData(), stored.size()));
    w.put<quint32>(crc(header.constData(), header.size()));

    const quint64 offset = quint64(m_file.pos());
    m_file.write(header);
    m_file.write(stored);
    m_file.flush(); // to the OS: a crash of this process loses nothing written so far
    const qint64 t2 = qp::nowNs();
    const auto index = quint32(m_chunks.size());
    m_chunks.push_back({offset, o.minT, o.maxT, o.records});
    for (RecordRef &r : o.refs) {
        r.chunk = index;
        m_refs.push_back(r);
    }
    o.refs.clear();

    {
        std::lock_guard lk(m_mutex);
        m_stats.compressMs += double(t1 - t0) / 1e6;
        m_stats.writeMs += double(t2 - t1) / 1e6;
        m_stats.fileBytes += header.size() + stored.size();
        m_stats.rawBytes += o.raw.size();
        ++m_stats.chunks;
    }
    qp::StageStats::instance().add(QStringLiteral("record_chunk_write_ms"), double(t2 - t1) / 1e6);
    o.raw.resize(0);
    o.records = 0;
}

bool Writer::writeIndex()
{
    QByteArray index;
    ByteWriter w(index);
    w.put<quint32>(kIndexMagic);
    w.put<quint32>(1);
    w.put<quint32>(quint32(m_chunks.size()));
    for (const ChunkInfo &c : m_chunks) {
        w.put<quint64>(c.offset);
        w.put<qint64>(c.minT);
        w.put<qint64>(c.maxT);
        w.put<quint32>(c.records);
    }
    // Time order, so a reader can binary-search without sorting.
    std::stable_sort(m_refs.begin(), m_refs.end(), [](const RecordRef &a, const RecordRef &b) { return a.t < b.t; });
    w.put<quint64>(m_refs.size());
    for (const RecordRef &r : m_refs) {
        w.put<quint16>(quint16(r.type));
        w.put<quint16>(r.version);
        w.put<quint16>(quint16(r.stream));
        w.put<quint16>(0);
        w.put<qint64>(r.t);
        w.put<quint32>(r.chunk);
        w.put<quint32>(r.offset);
    }
    const quint64 at = quint64(m_file.pos());
    QByteArray trailer;
    ByteWriter t(trailer);
    t.put<quint64>(at);
    t.put<quint32>(quint32(index.size()));
    t.put<quint32>(crc(index.constData(), index.size()));
    t.put<quint32>(kEndMagic);
    return m_file.write(index) == index.size() && m_file.write(trailer) == trailer.size();
}

void Writer::stop()
{
    {
        std::lock_guard lk(m_mutex);
        if (m_stop)
            return;
        m_stop = true;
    }
    m_cv.notify_one();
    if (m_thread.joinable())
        m_thread.join();
    if (!m_file.isOpen())
        return;
    writeIndex();
    m_file.flush();
    ::fdatasync(m_file.handle());
    std::lock_guard lk(m_mutex);
    m_stats.fileBytes = m_file.size();
    m_file.close();
}

WriterStats Writer::stats() const
{
    std::lock_guard lk(m_mutex);
    return m_stats;
}

// ---- Reader -------------------------------------------------------------------------------

bool Reader::open(const QString &path, QString *error)
{
    const qint64 t0 = qp::nowNs();
    auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly))
        return fail(m_file.errorString());
    m_info = {};
    m_info.fileBytes = m_file.size();
    char head[16];
    if (m_file.read(head, 16) != 16 || std::memcmp(head, kMagic, 8) != 0)
        return fail(QStringLiteral("not a recording"));
    m_info.major = load<quint16>(head + 8);
    m_info.minor = load<quint16>(head + 10);
    if (m_info.major > kFormatMajor)
        return fail(QStringLiteral("format %1.%2 is newer than this reader (%3.x)")
                        .arg(m_info.major)
                        .arg(m_info.minor)
                        .arg(kFormatMajor));
    const quint32 infoSize = load<quint32>(head + 12);
    if (16 + qint64(infoSize) > m_info.fileBytes)
        return fail(QStringLiteral("header is cut short"));
    m_info.info = QJsonDocument::fromJson(m_file.read(infoSize)).object();
    m_dataStart = 16 + infoSize;

    m_chunks.clear();
    m_streams.clear();
    m_cache.clear();
    if (!readIndex()) {
        m_chunks.clear();
        m_streams.clear();
        recover();
    }
    m_start = std::numeric_limits<qint64>::max();
    m_end = std::numeric_limits<qint64>::min();
    for (auto &[key, refs] : m_streams) {
        std::stable_sort(refs.begin(), refs.end(), [](const RecordRef &a, const RecordRef &b) { return a.t < b.t; });
        if (!refs.empty()) {
            m_start = std::min(m_start, refs.front().t);
            m_end = std::max(m_end, refs.back().t);
        }
    }
    if (m_streams.empty())
        m_start = m_end = 0;
    m_info.openMs = double(qp::nowNs() - t0) / 1e6;
    return true;
}

void Reader::addRef(const RecordRef &ref)
{
    if (!isKnown(ref.type, ref.version)) {
        ++m_info.unknownRecords;
        return;
    }
    m_streams[{ref.type, ref.stream}].push_back(ref);
}

bool Reader::readIndex()
{
    const qint64 size = m_info.fileBytes;
    if (size < m_dataStart + kTrailer || !m_file.seek(size - kTrailer))
        return false;
    char trailer[kTrailer];
    if (m_file.read(trailer, kTrailer) != kTrailer || load<quint32>(trailer + 16) != kEndMagic)
        return false;
    const auto at = qint64(load<quint64>(trailer));
    const quint32 n = load<quint32>(trailer + 8);
    if (at < m_dataStart || at + n + kTrailer != size || !m_file.seek(at))
        return false;
    const QByteArray index = m_file.read(n);
    if (index.size() != qsizetype(n) || crc(index.constData(), index.size()) != load<quint32>(trailer + 12))
        return false;
    ByteReader r(index.constData(), size_t(index.size()));
    if (r.get<quint32>() != kIndexMagic || r.get<quint32>() != 1)
        return false;
    const quint32 chunks = r.get<quint32>();
    for (quint32 i = 0; i < chunks && r.ok(); ++i) {
        Chunk c;
        c.offset = r.get<quint64>();
        c.minT = r.get<qint64>();
        c.maxT = r.get<qint64>();
        c.records = r.get<quint32>();
        if (qint64(c.offset) < m_dataStart || qint64(c.offset) >= at)
            return false;
        m_chunks.push_back(c);
    }
    const quint64 refs = r.get<quint64>();
    if (!r.ok() || r.left() != refs * 24)
        return false;
    for (quint64 i = 0; i < refs; ++i) {
        RecordRef ref;
        ref.type = Type(r.get<quint16>());
        ref.version = r.get<quint16>();
        ref.stream = r.get<quint16>();
        r.get<quint16>();
        ref.t = r.get<qint64>();
        ref.chunk = r.get<quint32>();
        ref.offset = r.get<quint32>();
        if (ref.chunk >= m_chunks.size())
            return false;
        addRef(ref);
    }
    m_info.indexBytes = n + kTrailer;
    return r.ok();
}

void Reader::recover()
{
    m_info.recovered = true;
    const qint64 size = m_info.fileBytes;
    qint64 pos = m_dataStart;
    while (pos + kChunkHeader <= size) {
        m_file.seek(pos);
        char h[kChunkHeader];
        if (m_file.read(h, kChunkHeader) != kChunkHeader)
            break;
        const bool headerOk = load<quint32>(h) == kChunkMagic && crc(h, kChunkHeader - 4) == load<quint32>(h + 40);
        if (!headerOk) {
            // Resynchronise: the next chunk header is the next magic whose header checksum holds.
            if (load<quint32>(h) == kIndexMagic)
                break; // a damaged index: everything before it was scanned
            ++m_info.badChunks;
            qint64 next = -1;
            std::vector<char> block((1 << 20) + kChunkHeader);
            for (qint64 p = pos + 1; p + kChunkHeader <= size && next < 0; p += 1 << 20) {
                m_file.seek(p);
                const qint64 got = m_file.read(block.data(), qint64(block.size()));
                for (qint64 i = 0; i + kChunkHeader <= got && i < (1 << 20); ++i) {
                    const char *b = block.data() + i;
                    if (load<quint32>(b) == kChunkMagic && crc(b, kChunkHeader - 4) == load<quint32>(b + 40)) {
                        next = p + i;
                        break;
                    }
                }
            }
            if (next < 0) {
                m_info.truncatedBytes = size - pos;
                break;
            }
            pos = next;
            continue;
        }
        const quint32 stored = load<quint32>(h + 4);
        if (pos + kChunkHeader + stored > size) {
            m_info.truncatedBytes = size - pos;
            break;
        }
        Chunk c;
        c.offset = quint64(pos);
        c.minT = load<qint64>(h + 16);
        c.maxT = load<qint64>(h + 24);
        c.records = load<quint32>(h + 12);
        m_chunks.push_back(c);
        const auto index = quint32(m_chunks.size() - 1);
        const std::shared_ptr<const QByteArray> raw = loadChunk(index);
        if (!raw) {
            ++m_info.badChunks;
            m_chunks.pop_back();
            pos += kChunkHeader + stored;
            continue;
        }
        for (qsizetype off = 0; off + kRecordHeader <= raw->size();) {
            const char *p = raw->constData() + off;
            RecordRef ref;
            ref.type = Type(load<quint16>(p));
            ref.version = load<quint16>(p + 2);
            ref.stream = load<quint16>(p + 4);
            ref.t = load<qint64>(p + 8);
            ref.chunk = index;
            ref.offset = quint32(off);
            const quint32 n = load<quint32>(p + 16);
            if (off + kRecordHeader + n > raw->size())
                break;
            addRef(ref);
            off += kRecordHeader + n;
        }
        ++m_info.recoveredChunks;
        pos += kChunkHeader + stored;
    }
    m_cache.clear();
}

std::shared_ptr<const QByteArray> Reader::loadChunk(quint32 index)
{
    if (m_coldReads) {
        m_cache.clear();
        ::posix_fadvise(m_file.handle(), 0, 0, POSIX_FADV_DONTNEED);
    }
    for (size_t i = 0; i < m_cache.size(); ++i) {
        if (m_cache[i].first == index) {
            auto hit = m_cache[i];
            m_cache.erase(m_cache.begin() + qsizetype(i));
            m_cache.push_back(hit);
            return hit.second;
        }
    }
    if (index >= m_chunks.size() || !m_file.seek(qint64(m_chunks[index].offset)))
        return nullptr;
    ++m_chunkReads;
    char h[kChunkHeader];
    if (m_file.read(h, kChunkHeader) != kChunkHeader || load<quint32>(h) != kChunkMagic)
        return nullptr;
    const quint32 storedSize = load<quint32>(h + 4);
    const quint32 rawSize = load<quint32>(h + 8);
    const auto compression = quint8(h[32]);
    const QByteArray stored = m_file.read(storedSize);
    if (stored.size() != qsizetype(storedSize) || crc(stored.constData(), stored.size()) != load<quint32>(h + 36))
        return nullptr;
    auto raw = std::make_shared<QByteArray>();
    if (compression == 0) {
        *raw = stored;
    } else if (compression == 1) {
        raw->resize(rawSize);
        const size_t n = ZSTD_decompress(raw->data(), rawSize, stored.constData(), size_t(stored.size()));
        if (ZSTD_isError(n) || n != rawSize)
            return nullptr;
    } else {
        return nullptr;
    }
    m_cache.emplace_back(index, raw);
    if (m_cache.size() > kCacheChunks)
        m_cache.erase(m_cache.begin());
    return raw;
}

std::optional<Record> Reader::read(const RecordRef &ref)
{
    std::shared_ptr<const QByteArray> raw = loadChunk(ref.chunk);
    if (!raw || qsizetype(ref.offset) + kRecordHeader > raw->size())
        return std::nullopt;
    const char *p = raw->constData() + ref.offset;
    const quint32 n = load<quint32>(p + 16);
    if (qsizetype(ref.offset) + kRecordHeader + n > raw->size())
        return std::nullopt;
    Record r;
    r.type = Type(load<quint16>(p));
    r.version = load<quint16>(p + 2);
    r.stream = load<quint16>(p + 4);
    r.t = load<qint64>(p + 8);
    r.data = p + kRecordHeader;
    r.size = n;
    r.keepAlive = std::move(raw);
    return r;
}

} // namespace rec
