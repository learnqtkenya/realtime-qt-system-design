#pragma once

#include "qp/SimTypes.h"

#include <QImage>
#include <QObject>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

// One lane per sensor. Workers rasterise one frame of one sensor into that sensor's layer.
class LidarPipeline : public QObject
{
    Q_OBJECT

public:
    enum class Policy { Block, DropNewest, DropOldest };

    struct Viewport
    {
        int width = 0;
        int height = 0;
        double pxPerMetre = 7;
    };

    struct Layer
    {
        QImage image;
        int64_t captureNs = 0;
        quint64 serial = 0;
    };

    LidarPipeline(int sensors, Policy policy, int queueDepth, int workers, QObject *parent = nullptr);
    ~LidarPipeline() override;

    static Policy policyFromString(const QString &name);

    int sensorCount() const { return int(m_lanes.size()); }

    // Any thread. With Policy::Block this waits until the lane has room.
    void submit(const qp::SensorFrame &frame);
    // Wakes blocked producers and stops the workers.
    void shutdown();

    void setViewport(const Viewport &viewport);
    Layer layer(int sensor) const;
    std::vector<quint64> queuedSequence(int sensor) const;

signals:
    void layerReady();

private:
    struct Lane
    {
        std::deque<qp::SensorFrame> queue;
        bool busy = false;
    };

    void workerMain();
    int readyLane() const;
    static QImage rasterise(const qp::SensorFrame &frame, const Viewport &viewport);

    const Policy m_policy;
    const size_t m_depth;

    mutable std::mutex m_mutex;
    std::condition_variable m_work;
    std::condition_variable m_space;
    std::vector<Lane> m_lanes;
    std::vector<Layer> m_layers;
    Viewport m_viewport;
    quint64 m_serial = 0;
    mutable size_t m_nextLane = 0;
    bool m_stop = false;

    std::vector<std::thread> m_workers;
};
