#include "LidarMailbox.h"

#include "qp/StageStats.h"

LidarMailbox::LidarMailbox(int sensors, QObject *parent)
    : QObject(parent), m_entries(size_t(std::max(0, sensors)))
{}

void LidarMailbox::submit(const qp::SensorFrame &frame)
{
    if (frame.sensorId < 0 || frame.sensorId >= sensorCount())
        return;
    {
        std::lock_guard lk(m_mutex);
        m_entries[size_t(frame.sensorId)] = Entry{frame, ++m_serial};
    }
    qp::StageStats::instance().add(QStringLiteral("submitted"), 1);
    emit changed(frame.sensorId);
}

LidarMailbox::Entry LidarMailbox::latest(int sensor) const
{
    if (sensor < 0 || sensor >= sensorCount())
        return {};
    std::lock_guard lk(m_mutex);
    return m_entries[size_t(sensor)];
}
