#pragma once

#include "qp/SimTypes.h"

#include <QObject>

#include <atomic>

class LidarMailbox;
class LidarPipeline;

// Sends each lidar frame to the active renderer only, so inactive ones cost nothing.
class LidarHub : public QObject
{
    Q_OBJECT

public:
    enum class Route { None, Canvas, Store, Pipeline, Mailbox };

    LidarHub(LidarPipeline *pipeline, LidarMailbox *mailbox, QObject *parent = nullptr);

    void setRoute(Route route) { m_route.store(route); }
    Route route() const { return m_route.load(); }

    // Simulator thread.
    void submit(const qp::SensorFrame &frame);

signals:
    // Connect queued: these are emitted on the simulator thread.
    void canvasFrame(qp::SensorFrame frame);
    void storeFrame(qp::SensorFrame frame);

private:
    LidarPipeline *m_pipeline;
    LidarMailbox *m_mailbox;
    std::atomic<Route> m_route{Route::None};
};
