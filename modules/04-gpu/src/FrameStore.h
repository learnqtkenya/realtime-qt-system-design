#pragma once

#include "qp/SimTypes.h"

#include <QObject>

// Domain layer: QtCore only. Lidar goes through LidarMailbox instead.
class FrameStore : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    qp::TrackSetPtr latestTracks() const { return m_tracks; }

    void setTracks(const qp::TrackSetPtr &tracks);
    void addEvents(const qp::EventBatchPtr &batch);

signals:
    void tracksChanged();
    void eventsArrived(const qp::EventBatchPtr &batch);

private:
    qp::TrackSetPtr m_tracks;
};
