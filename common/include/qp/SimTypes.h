#pragma once

#include <QMetaType>
#include <QString>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace qp {

// Added to every reading: --set clockOffsetMs=<ms> gives this process the skew another machine's
// clock would have (step 11). Set once at start-up.
inline std::atomic<int64_t> g_clockOffsetNs{0};

inline int64_t nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
               .count()
           + g_clockOffsetNs.load(std::memory_order_relaxed);
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

// What can go wrong with a stream (step 6). Delayed samples keep their capture time.
struct Fault
{
    double dropRate = 0; // 0..1; dropped samples still use up a sequence number
    double delayMs = 0;
    double jitterMs = 0; // delay spread uniformly over 0..2× this, on top of delayMs: samples arrive out of order
    bool corrupt = false; // lidar: 1% NaN points and 0.1% at 1e6 m; tracks: one NaN position
    bool stall = false;
    bool any() const { return dropRate > 0 || delayMs > 0 || jitterMs > 0 || corrupt || stall; }
};

// Stream numbers shared by the simulator, the faults and the health monitor. They are an interface
// (command-line faults, scripts, event text): new streams are appended, never renumbered.
enum StreamIndex { kLidarStream0 = 0, kTracksStream = 4, kEventsStream = 5, kCameraStream0 = 6, kGuiStream = 10,
                   kLidarStream4 = 11, kStreamCount = 15 };
constexpr int kMaxSensors = 8;

constexpr int lidarStream(int sensor)
{
    return sensor < 4 ? kLidarStream0 + sensor : kLidarStream4 + sensor - 4;
}

// The sensor of a lidar stream, or -1.
constexpr int lidarSensor(int stream)
{
    return stream >= kLidarStream0 && stream < kLidarStream0 + 4 ? stream - kLidarStream0
           : stream >= kLidarStream4 && stream < kLidarStream4 + 4 ? stream - kLidarStream4 + 4
                                                                   : -1;
}

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
    // An operator's note: anchored to an object and a place, at the time of the scene it was made on.
    qint32 trackId = -1;
    float x = 0, y = 0;
    bool anchored = false;
};
using EventBatch = std::vector<EventRecord>;
using EventBatchPtr = std::shared_ptr<const EventBatch>;

void registerSimTypes();

} // namespace qp

Q_DECLARE_METATYPE(qp::SensorFrame)
Q_DECLARE_METATYPE(qp::TrackSetPtr)
Q_DECLARE_METATYPE(qp::EventBatchPtr)
