#pragma once

#include <QMetaType>
#include <QString>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace qp {

inline int64_t nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct Workload
{
    int sensors = 4;
    int pointsPerFrame = 10'000; // per sensor
    double lidarRateHz = 10.0;
    int objects = 50;
    double trackRateHz = 10.0;
    int eventsPerSecond = 50;
    uint32_t seed = 1;
    bool still = false; // same cloud every frame, tracks do not move
    // Each frame gets its own allocation, as from a real sensor. Off: frames share a small pool,
    // so anything that keeps frames looks free.
    bool freshClouds = false;
    // Time from capture to emission, as if the lidar had a processing pipeline.
    double lidarDelayMs = 0;

    double pointsPerSecond() const { return sensors * double(pointsPerFrame) * lidarRateHz; }
};

struct PointCloud
{
    std::vector<float> xyz;
    int count() const { return int(xyz.size() / 3); }
};

// Immutable and shared: crossing threads copies a pointer, not the points.
struct SensorFrame
{
    int sensorId = 0;
    quint64 seq = 0;
    int64_t captureNs = 0;
    std::shared_ptr<const PointCloud> cloud;
};

struct Track
{
    quint32 id = 0;
    float x = 0, y = 0;
    float heading = 0;
    float speed = 0;
    float length = 4, width = 2;
    quint8 cls = 0; // 0 car, 1 truck, 2 cyclist, 3 pedestrian
};

struct TrackSet
{
    quint64 seq = 0;
    int64_t captureNs = 0;
    std::vector<Track> tracks;
};
using TrackSetPtr = std::shared_ptr<const TrackSet>;

struct EventRecord
{
    quint64 seq = 0;
    int64_t captureNs = 0;
    quint8 severity = 0; // 0 info, 1 warning, 2 error
    QString source;
    QString text;
};
using EventBatch = std::vector<EventRecord>;
using EventBatchPtr = std::shared_ptr<const EventBatch>;

void registerSimTypes();

} // namespace qp

Q_DECLARE_METATYPE(qp::SensorFrame)
Q_DECLARE_METATYPE(qp::TrackSetPtr)
Q_DECLARE_METATYPE(qp::EventBatchPtr)
