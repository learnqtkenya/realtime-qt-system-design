#include "TrackListModel.h"

#include "qp/StageStats.h"

#include <cmath>

namespace {

bool same(const qp::Track &a, const qp::Track &b)
{
    return a.id == b.id && a.x == b.x && a.y == b.y && a.heading == b.heading && a.speed == b.speed
           && a.length == b.length && a.width == b.width && a.cls == b.cls;
}

} // namespace

TrackListModel::TrackListModel(FrameStore *store, QObject *parent)
    : QAbstractListModel(parent), m_store(store)
{
    if (m_store)
        connect(m_store, &FrameStore::tracksChanged, this, &TrackListModel::reload);
}

int TrackListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= int(m_rows.size()))
        return {};
    const qp::Track &t = m_rows[size_t(index.row())];
    switch (role) {
    case TrackIdRole: return t.id;
    case PosXRole: return t.x;
    case PosYRole: return t.y;
    case HeadingRole: return t.heading;
    case SpeedRole: return t.speed;
    case BoxLengthRole: return t.length;
    case BoxWidthRole: return t.width;
    case ClassRole: return int(t.cls);
    case DistanceRole: return std::hypot(t.x, t.y);
    default: return {};
    }
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return {
        {TrackIdRole, "trackId"},   {PosXRole, "posX"},         {PosYRole, "posY"},
        {HeadingRole, "heading"},   {SpeedRole, "speed"},       {BoxLengthRole, "boxLength"},
        {BoxWidthRole, "boxWidth"}, {ClassRole, "trackClass"},  {DistanceRole, "distance"},
    };
}

void TrackListModel::reload()
{
    apply(m_store->latestTracks());
}

void TrackListModel::apply(const qp::TrackSetPtr &set)
{
    const int64_t t0 = qp::nowNs();
    static const std::vector<qp::Track> none;
    const std::vector<qp::Track> &next = set ? set->tracks : none;
    if (m_updates == Updates::Reset) {
        beginResetModel();
        m_rows = next;
        endResetModel();
    } else {
        diffTo(next);
    }
    qp::StageStats::instance().add(QStringLiteral("tracks_rebuild_ms"), double(qp::nowNs() - t0) / 1e6);
    emit updated();
}

void TrackListModel::diffTo(const std::vector<qp::Track> &next)
{
    QHash<quint32, size_t> nextAt;
    nextAt.reserve(qsizetype(next.size()));
    for (size_t i = 0; i < next.size(); ++i)
        nextAt.insert(next[i].id, i);

    // Gone: remove runs of rows from the end, so earlier row numbers stay valid.
    for (int last = int(m_rows.size()) - 1; last >= 0;) {
        if (nextAt.contains(m_rows[size_t(last)].id)) {
            --last;
            continue;
        }
        int first = last;
        while (first > 0 && !nextAt.contains(m_rows[size_t(first - 1)].id))
            --first;
        beginRemoveRows({}, first, last);
        m_rows.erase(m_rows.begin() + first, m_rows.begin() + last + 1);
        endRemoveRows();
        last = first - 1;
    }

    // Kept: copy new values; report one range covering the rows that changed.
    QHash<quint32, bool> present;
    present.reserve(qsizetype(m_rows.size()));
    int firstChanged = -1, lastChanged = -1;
    bool shapeChanged = false;
    for (size_t r = 0; r < m_rows.size(); ++r) {
        qp::Track &row = m_rows[r];
        present.insert(row.id, true);
        const qp::Track &n = next[nextAt.value(row.id)];
        if (same(row, n))
            continue;
        shapeChanged |= row.length != n.length || row.width != n.width || row.cls != n.cls;
        row = n;
        if (firstChanged < 0)
            firstChanged = int(r);
        lastChanged = int(r);
    }
    if (firstChanged >= 0) {
        QList<int> roles{PosXRole, PosYRole, HeadingRole, SpeedRole, DistanceRole};
        if (shapeChanged)
            roles << BoxLengthRole << BoxWidthRole << ClassRole;
        emit dataChanged(index(firstChanged), index(lastChanged), roles);
    }

    // New: append in the order they arrived.
    std::vector<qp::Track> added;
    for (const qp::Track &n : next)
        if (!present.contains(n.id))
            added.push_back(n);
    if (!added.empty()) {
        const int at = int(m_rows.size());
        beginInsertRows({}, at, at + int(added.size()) - 1);
        m_rows.insert(m_rows.end(), added.begin(), added.end());
        endInsertRows();
    }
}
