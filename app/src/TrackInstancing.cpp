#include "TrackInstancing.h"

#include "TrackIndex.h"
#include "TrackListModel.h"
#include "qp/StageStats.h"

#include <numbers>

TrackInstancing::TrackInstancing(QQuick3DObject *parent) : QQuick3DInstancing(parent)
{
    setHasTransparency(true);
}

QAbstractItemModel *TrackInstancing::tracks() const
{
    return m_tracks;
}

void TrackInstancing::setTracks(QAbstractItemModel *model)
{
    auto *t = qobject_cast<TrackListModel *>(model);
    if (t == m_tracks)
        return;
    if (m_tracks)
        m_tracks->disconnect(this);
    m_tracks = t;
    if (m_tracks)
        connect(m_tracks, &TrackListModel::updated, this, &TrackInstancing::markDirty);
    markDirty();
    emit tracksChanged();
}

void TrackInstancing::setSelectedId(int id)
{
    if (id == m_selectedId)
        return;
    m_selectedId = id;
    markDirty();
    emit selectedIdChanged();
}

QByteArray TrackInstancing::getInstanceBuffer(int *instanceCount)
{
    const qint64 t0 = qp::nowNs();
    QByteArray table;
    const int n = m_tracks ? int(m_tracks->rows().size()) : 0;
    table.resize(qsizetype(n) * qsizetype(sizeof(InstanceTableEntry)));
    auto *out = reinterpret_cast<InstanceTableEntry *>(table.data());
    const QColor box(226, 232, 240, 64), selected(251, 191, 36, 217);
    for (int i = 0; i < n; ++i) {
        const qp::Track &t = m_tracks->rows()[size_t(i)];
        const float h = float(TrackIndex::heightFor(t.cls));
        out[i] = calculateTableEntry({t.x, t.y, float(TrackIndex::kGroundZ) + h / 2},
                                     {t.length / 100, t.width / 100, h / 100},
                                     {0, 0, t.heading * 180 / std::numbers::pi_v<float>},
                                     int(t.id) == m_selectedId ? selected : box);
    }
    if (instanceCount)
        *instanceCount = n;
    qp::StageStats::instance().add(QStringLiteral("instancing_build_ms"), double(qp::nowNs() - t0) / 1e6);
    return table;
}
