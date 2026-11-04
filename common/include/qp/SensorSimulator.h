#pragma once

#include "qp/SimTypes.h"

#include <QObject>
#include <QThread>

#include <atomic>
#include <memory>

namespace qp {

// Runs on its own thread and never waits for consumers, like a real sensor.
class SensorSimulator : public QObject
{
    Q_OBJECT

public:
    explicit SensorSimulator(const Workload &workload, QObject *parent = nullptr);
    ~SensorSimulator() override;

    void start();
    void stop();
    // Any time, from the owner's thread; takes effect on the simulator thread.
    void setWorkload(const Workload &workload);
    // Any time, from the owner's thread. stream: lidarStream(sensor), or kTracksStream.
    void setFault(int stream, const Fault &fault);

    const Workload &workload() const { return m_workload; }
    quint64 lidarFramesEmitted() const { return m_lidarEmitted.load(std::memory_order_relaxed); }
    quint64 trackSetsEmitted() const { return m_trackEmitted.load(std::memory_order_relaxed); }
    quint64 eventsEmitted() const { return m_eventsEmitted.load(std::memory_order_relaxed); }

    static std::shared_ptr<const PointCloud>
    makeCloud(const Workload &w, int sensorId, int variant);

signals:
    void lidarFrame(qp::SensorFrame frame);
    void trackSet(qp::TrackSetPtr set);
    void eventBatch(qp::EventBatchPtr batch);

private:
    class Worker;
    Workload m_workload;
    QThread m_thread;
    Worker *m_worker = nullptr;
    std::atomic<quint64> m_lidarEmitted{0};
    std::atomic<quint64> m_trackEmitted{0};
    std::atomic<quint64> m_eventsEmitted{0};
};

} // namespace qp
