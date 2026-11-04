#include "LidarPipeline.h"

#include "qp/StageStats.h"

#include <QPainter>

namespace {

double msSince(int64_t t0)
{
    return double(qp::nowNs() - t0) / 1e6;
}

} // namespace

LidarPipeline::LidarPipeline(int sensors, Policy policy, int queueDepth, int workers, QObject *parent)
    : QObject(parent),
      m_policy(policy),
      m_depth(size_t(std::max(1, queueDepth))),
      m_lanes(size_t(sensors)),
      m_layers(size_t(sensors))
{
    for (int i = 0; i < workers; ++i) // 0 is allowed: tests drive the queues directly
        m_workers.emplace_back([this] { workerMain(); });
}

LidarPipeline::~LidarPipeline()
{
    shutdown();
    for (std::thread &t : m_workers)
        t.join();
}

LidarPipeline::Policy LidarPipeline::policyFromString(const QString &name)
{
    if (name == QLatin1String("block"))
        return Policy::Block;
    if (name == QLatin1String("drop-newest"))
        return Policy::DropNewest;
    return Policy::DropOldest;
}

void LidarPipeline::shutdown()
{
    {
        std::lock_guard lk(m_mutex);
        m_stop = true;
    }
    m_work.notify_all();
    m_space.notify_all();
}

void LidarPipeline::setViewport(const Viewport &viewport)
{
    std::lock_guard lk(m_mutex);
    m_viewport = viewport;
}

LidarPipeline::Layer LidarPipeline::layer(int sensor) const
{
    std::lock_guard lk(m_mutex);
    return m_layers[size_t(sensor)];
}

std::vector<quint64> LidarPipeline::queuedSequence(int sensor) const
{
    std::lock_guard lk(m_mutex);
    std::vector<quint64> out;
    for (const qp::SensorFrame &f : m_lanes[size_t(sensor)].queue)
        out.push_back(f.seq);
    return out;
}

void LidarPipeline::submit(const qp::SensorFrame &frame)
{
    auto &stats = qp::StageStats::instance();
    std::unique_lock lk(m_mutex);
    if (m_stop || frame.sensorId < 0 || frame.sensorId >= sensorCount())
        return;
    Lane &lane = m_lanes[size_t(frame.sensorId)];
    stats.add(QStringLiteral("submitted"), 1);
    stats.add(QStringLiteral("queue_depth"), double(lane.queue.size()));

    if (lane.queue.size() >= m_depth) {
        switch (m_policy) {
        case Policy::Block: {
            const int64_t t0 = qp::nowNs();
            m_space.wait(lk, [&] { return m_stop || lane.queue.size() < m_depth; });
            stats.add(QStringLiteral("source_blocked_ms"), msSince(t0));
            if (m_stop)
                return;
            break;
        }
        case Policy::DropNewest:
            stats.add(QStringLiteral("dropped"), 1);
            return;
        case Policy::DropOldest:
            lane.queue.pop_front();
            stats.add(QStringLiteral("dropped"), 1);
            break;
        }
    }
    lane.queue.push_back(frame);
    lk.unlock();
    m_work.notify_one();
}

// Caller holds m_mutex. Round-robin so one busy sensor cannot starve the others.
int LidarPipeline::readyLane() const
{
    for (size_t k = 0; k < m_lanes.size(); ++k) {
        const size_t i = (m_nextLane + k) % m_lanes.size();
        if (!m_lanes[i].busy && !m_lanes[i].queue.empty()) {
            m_nextLane = i + 1;
            return int(i);
        }
    }
    return -1;
}

void LidarPipeline::workerMain()
{
    auto &stats = qp::StageStats::instance();
    std::unique_lock lk(m_mutex);
    for (;;) {
        int i = -1;
        m_work.wait(lk, [&] { return m_stop || (i = readyLane()) >= 0; });
        if (m_stop)
            return;

        Lane &lane = m_lanes[size_t(i)];
        const qp::SensorFrame frame = lane.queue.front();
        lane.queue.pop_front();
        lane.busy = true; // keeps one sensor's frames in order
        const Viewport viewport = m_viewport;
        lk.unlock();
        m_space.notify_all();

        stats.add(QStringLiteral("queue_wait_ms"), double(qp::nowNs() - frame.captureNs) / 1e6);
        const int64_t t0 = qp::nowNs();
        QImage image = rasterise(frame, viewport);
        stats.add(QStringLiteral("raster_ms"), msSince(t0));

        lk.lock();
        lane.busy = false;
        if (!image.isNull())
            m_layers[size_t(i)] = Layer{std::move(image), frame.captureNs, ++m_serial};
        const bool more = !lane.queue.empty();
        lk.unlock();
        if (more)
            m_work.notify_one();
        emit layerReady();
        lk.lock();
    }
}

QImage LidarPipeline::rasterise(const qp::SensorFrame &frame, const Viewport &viewport)
{
    if (!frame.cloud || viewport.width <= 0 || viewport.height <= 0)
        return {};

    static const QColor colors[qp::kMaxSensors] = {QColor(0x38BDF8), QColor(0x34D399), QColor(0xFBBF24), QColor(0xF472B6), QColor(0xA78BFA), QColor(0xFB923C), QColor(0x2DD4BF), QColor(0xE879F9)};
    QImage image(viewport.width, viewport.height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    const QColor &c = colors[frame.sensorId % qp::kMaxSensors];
    const double cx = viewport.width / 2.0;
    const double cy = viewport.height / 2.0;
    const double s = viewport.pxPerMetre;
    const std::vector<float> &xyz = frame.cloud->xyz;
    for (size_t i = 0; i + 2 < xyz.size(); i += 3)
        painter.fillRect(QRectF(cx + xyz[i] * s, cy - xyz[i + 1] * s, 1.5, 1.5), c);
    return image;
}
