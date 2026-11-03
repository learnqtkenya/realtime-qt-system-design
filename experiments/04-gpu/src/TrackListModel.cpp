#include "TrackListModel.h"

#include "qp/StageStats.h"

TrackListModel::TrackListModel(FrameStore *store, QObject *parent)
    : QAbstractListModel(parent), m_store(store)
{
    connect(m_store, &FrameStore::tracksChanged, this, &TrackListModel::reload);
}

int TrackListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() || !m_tracks ? 0 : int(m_tracks->tracks.size());
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const
{
    if (!m_tracks || !index.isValid() || index.row() >= int(m_tracks->tracks.size()))
        return {};
    const qp::Track &t = m_tracks->tracks[size_t(index.row())];
    switch (role) {
    case TrackIdRole: return t.id;
    case PosXRole: return t.x;
    case PosYRole: return t.y;
    case HeadingRole: return t.heading;
    case SpeedRole: return t.speed;
    case BoxLengthRole: return t.length;
    case BoxWidthRole: return t.width;
    case ClassRole: return int(t.cls);
    default: return {};
    }
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return {
        {TrackIdRole, "trackId"}, {PosXRole, "posX"},           {PosYRole, "posY"},
        {HeadingRole, "heading"}, {SpeedRole, "speed"},         {BoxLengthRole, "boxLength"},
        {BoxWidthRole, "boxWidth"}, {ClassRole, "trackClass"},
    };
}

void TrackListModel::reload()
{
    const int64_t t0 = qp::nowNs();
    beginResetModel();
    m_tracks = m_store->latestTracks();
    endResetModel();
    qp::StageStats::instance().add(QStringLiteral("tracks_rebuild_ms"), double(qp::nowNs() - t0) / 1e6);
}
