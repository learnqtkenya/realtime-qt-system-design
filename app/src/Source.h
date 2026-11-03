#pragma once

#include "Codec.h"

#include <optional>
#include <vector>

// What the presenter plays from: the in-memory history or a recording file.
struct Sample
{
    rec::Type type{};
    int stream = 0;
    qint64 t = 0;
    qp::SensorFrame lidar;
    qp::TrackSetPtr tracks;
    qp::EventBatchPtr events;
    rec::CameraMark mark;
};

struct StreamId
{
    rec::Type type{};
    int stream = 0;
    bool operator==(const StreamId &) const = default;
};

class Source
{
public:
    virtual ~Source() = default;

    virtual std::vector<StreamId> streams() const = 0;
    // [start, end] of what can be shown; start == end when empty.
    virtual qint64 startNs() const = 0;
    virtual qint64 endNs() const = 0;
    // The newest sample with from < t <= to.
    virtual std::optional<Sample> latestIn(StreamId id, qint64 from, qint64 to) = 0;
    // Every sample with from < t <= to, oldest first.
    virtual void allIn(StreamId id, qint64 from, qint64 to, std::vector<Sample> &out) = 0;
    virtual std::optional<qint64> nextAfter(StreamId id, qint64 after) = 0;
    virtual std::optional<qint64> prevBefore(StreamId id, qint64 before) = 0;
};
