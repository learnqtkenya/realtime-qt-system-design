#include "FrameStore.h"

#include "qp/StageStats.h"

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
    for (const qp::EventRecord &e : *batch)
        if (e.anchored) {
            qp::StageStats::instance().add(QStringLiteral("notes_presented"), 0);
            m_notes.push_back(e);
            if (m_notes.size() > 50)
                m_notes.pop_front();
        }
    emit eventsArrived(batch);
}
