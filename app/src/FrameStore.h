#pragma once

#include "qp/SimTypes.h"

#include <QObject>

#include <deque>
#include <vector>

// Domain layer: QtCore only. Holds the newest data per stream; frames are immutable and shared.
class FrameStore : public QObject
{
    Q_OBJECT

public:
    explicit FrameStore(int sensors, QObject *parent = nullptr);

    int sensorCount() const { return int(m_lidar.size()); }
    const qp::SensorFrame &latestLidar(int sensor) const { return m_lidar[size_t(sensor)]; }
    qp::TrackSetPtr latestTracks() const { return m_tracks; }

    void setLidar(const qp::SensorFrame &frame);
    void setTracks(const qp::TrackSetPtr &tracks);
    void addEvents(const qp::EventBatchPtr &batch);
    // The newest anchored events (notes), at most 50, for drawing.
    const std::deque<qp::EventRecord> &notes() const { return m_notes; }

signals:
    void lidarChanged(int sensorId);
    void tracksChanged();
    void eventsArrived(const qp::EventBatchPtr &batch);

private:
    std::vector<qp::SensorFrame> m_lidar;
    qp::TrackSetPtr m_tracks;
    std::deque<qp::EventRecord> m_notes;
};
