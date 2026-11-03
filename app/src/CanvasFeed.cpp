#include "CanvasFeed.h"

#include "qp/StageStats.h"

#include <QVector3D>

void CanvasFeed::onFrame(const qp::SensorFrame &frame)
{
    auto &stats = qp::StageStats::instance();
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
