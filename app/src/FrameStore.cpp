#include "FrameStore.h"

FrameStore::FrameStore(int sensors, QObject *parent) : QObject(parent), m_lidar(size_t(sensors)) {}

void FrameStore::setLidar(const qp::SensorFrame &frame)
{
    if (frame.sensorId < 0 || frame.sensorId >= sensorCount())
        return;
    m_lidar[size_t(frame.sensorId)] = frame;
    emit lidarChanged(frame.sensorId);
}

void FrameStore::setTracks(const qp::TrackSetPtr &tracks)
{
    m_tracks = tracks;
    emit tracksChanged();
}

void FrameStore::addEvents(const qp::EventBatchPtr &batch)
{
    emit eventsArrived(batch);
}
