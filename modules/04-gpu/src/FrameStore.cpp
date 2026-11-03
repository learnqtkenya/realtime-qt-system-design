#include "FrameStore.h"

void FrameStore::setTracks(const qp::TrackSetPtr &tracks)
{
    m_tracks = tracks;
    emit tracksChanged();
}

void FrameStore::addEvents(const qp::EventBatchPtr &batch)
{
    emit eventsArrived(batch);
}
