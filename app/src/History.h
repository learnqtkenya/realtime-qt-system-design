#pragma once

#include "Source.h"

#include <deque>
#include <mutex>

// The last few seconds of every stream, in memory, within a byte budget. Any thread may add;
// the oldest samples go first when the budget is reached.
class History : public Source
{
public:
    // Shared: keep the frame itself (a pointer). Int16: keep a 1 cm copy at half the bytes.
    enum class Shape { Shared, Int16 };

    History(qint64 budgetBytes, Shape shape);

    void addLidar(const qp::SensorFrame &frame);
    void addTracks(const qp::TrackSetPtr &set);
    void addEvents(const qp::EventBatchPtr &batch);
    void addCameraMark(const rec::CameraMark &mark);
    void clear();

    qint64 bytes() const;
    qint64 budget() const { return m_budget; }
    Shape shape() const { return m_shape; }
    qint64 evicted() const;

    std::vector<StreamId> streams() const override;
    qint64 startNs() const override;
    qint64 endNs() const override;
    std::optional<Sample> latestIn(StreamId id, qint64 from, qint64 to) override;
    void allIn(StreamId id, qint64 from, qint64 to, std::vector<Sample> &out) override;
    std::optional<qint64> nextAfter(StreamId id, qint64 after) override;
    std::optional<qint64> prevBefore(StreamId id, qint64 before) override;

private:
    struct Entry
    {
        qint64 t;
        Sample sample;
        QByteArray blob; // Int16 lidar
        qint64 bytes;
    };
    struct Stream
    {
        StreamId id;
        std::deque<Entry> entries;
    };

    void insert(StreamId id, Entry entry);
    Stream *find(StreamId id);
    static Sample materialise(const Entry &e);

    const qint64 m_budget;
    const Shape m_shape;
    mutable std::mutex m_mutex;
    std::vector<Stream> m_streams;
    qint64 m_bytes = 0;
    qint64 m_evicted = 0;
};
