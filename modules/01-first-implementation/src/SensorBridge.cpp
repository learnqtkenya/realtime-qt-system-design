#include "SensorBridge.h"

#include "EventLogModel.h"
#include "qp/AppOptions.h"
#include "qp/StageStats.h"

#include <QVariantMap>
#include <QVector3D>

SensorBridge *SensorBridge::create(QQmlEngine *, QJSEngine *)
{
    return new SensorBridge;
}

SensorBridge::SensorBridge(QObject *parent) : QObject(parent)
{
    m_events = new EventLogModel(this);

    qp::AppOptions *opts = qp::AppOptions::instance();
    if (opts->noSim())
        return;

    m_sim = new qp::SensorSimulator(opts->workload(), this);
    connect(m_sim, &qp::SensorSimulator::lidarFrame, this, &SensorBridge::onLidar);
    connect(m_sim, &qp::SensorSimulator::trackSet, this, &SensorBridge::onTracks);
    connect(m_sim, &qp::SensorSimulator::eventBatch, this, &SensorBridge::onEvents);
    m_sim->start();
}

SensorBridge::~SensorBridge()
{
    if (m_sim)
        m_sim->stop();
}

QAbstractItemModel *SensorBridge::events() const
{
    return m_events;
}

void SensorBridge::onLidar(qp::SensorFrame frame)
{
    auto &stats = qp::StageStats::instance();

    ++m_lidarConsumed;
    stats.add(QStringLiteral("backlog_frames"),
              double(m_sim->lidarFramesEmitted()) - double(m_lidarConsumed));
    stats.add(QStringLiteral("queue_wait_ms"), double(qp::nowNs() - frame.captureNs) / 1e6);

    const int64_t t0 = qp::nowNs();
    const qp::PointCloud &cloud = *frame.cloud;
    QVariantList points;
    points.reserve(cloud.count());
    for (int i = 0; i < cloud.count(); ++i)
        points.append(QVector3D(cloud.xyz[size_t(3 * i)], cloud.xyz[size_t(3 * i + 1)],
                                cloud.xyz[size_t(3 * i + 2)]));
    stats.add(QStringLiteral("convert_points_ms"), double(qp::nowNs() - t0) / 1e6);

    // Includes the QML handler, which runs inside the emit.
    const int64_t t1 = qp::nowNs();
    emit lidarUpdated(frame.sensorId, points, double(frame.captureNs));
    stats.add(QStringLiteral("lidar_signal_ms"), double(qp::nowNs() - t1) / 1e6);
}

void SensorBridge::onTracks(qp::TrackSetPtr set)
{
    auto &stats = qp::StageStats::instance();

    const int64_t t0 = qp::nowNs();
    QVariantList list;
    list.reserve(int(set->tracks.size()));
    for (const qp::Track &t : set->tracks) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), t.id);
        m.insert(QStringLiteral("x"), t.x);
        m.insert(QStringLiteral("y"), t.y);
        m.insert(QStringLiteral("heading"), t.heading);
        m.insert(QStringLiteral("speed"), t.speed);
        m.insert(QStringLiteral("length"), t.length);
        m.insert(QStringLiteral("width"), t.width);
        m.insert(QStringLiteral("cls"), int(t.cls));
        list.append(m);
    }
    stats.add(QStringLiteral("convert_tracks_ms"), double(qp::nowNs() - t0) / 1e6);

    // Includes re-evaluating everything bound to tracks.
    const int64_t t1 = qp::nowNs();
    m_tracks = list;
    emit tracksChanged();
    stats.add(QStringLiteral("tracks_rebuild_ms"), double(qp::nowNs() - t1) / 1e6);
}

void SensorBridge::onEvents(qp::EventBatchPtr batch)
{
    m_events->appendBatch(*batch);
}
