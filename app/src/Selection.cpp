#include "Selection.h"

#include "FrameStore.h"
#include "qp/StageStats.h"

#include <QLoggingCategory>

#include <cmath>
#include <numbers>

Q_LOGGING_CATEGORY(lcPick, "viewer.pick", QtWarningMsg)

namespace {

double msSince(int64_t t0)
{
    return double(qp::nowNs() - t0) / 1e6;
}

const char *className(quint8 cls)
{
    switch (cls) {
    case 0: return "car";
    case 1: return "truck";
    case 2: return "cyclist";
    default: return "pedestrian";
    }
}

} // namespace

bool Selection::annotate(const QString &text)
{
    const qp::TrackSetPtr set = m_store->latestTracks();
    if (!set || m_trackId < 0 || !m_noteSink || text.trimmed().isEmpty())
        return false;
    for (const qp::Track &t : set->tracks) {
        if (int(t.id) != m_trackId)
            continue;
        qp::EventRecord e;
        e.seq = ++m_noteSeq;
        e.captureNs = set->captureNs; // the scene the operator saw, not the wall clock
        e.source = QStringLiteral("note");
        e.text = QStringLiteral("#%1: %2").arg(t.id).arg(text.trimmed().left(200));
        e.trackId = qint32(t.id);
        e.x = t.x;
        e.y = t.y;
        e.anchored = true;
        m_noteSink(e);
        return true;
    }
    return false;
}

Selection::Selection(FrameStore *store, QObject *parent) : QObject(parent), m_store(store)
{
    connect(m_store, &FrameStore::tracksChanged, this, [this] {
        m_dirty = true; // rebuilt lazily: only picks pay for it
        if (m_trackId >= 0)
            refreshInfo();
    });
}

void Selection::ensureIndex()
{
    if (!m_dirty)
        return;
    const int64_t t0 = qp::nowNs();
    const qp::TrackSetPtr set = m_store->latestTracks();
    m_index.rebuild(set ? set->tracks : std::vector<qp::Track>{});
    m_dirty = false;
    qp::StageStats::instance().add(QStringLiteral("index_build_ms"), msSince(t0));
}

int Selection::pickTopDown(double x, double y)
{
    const int64_t t0 = qp::nowNs();
    ensureIndex();
    const int id = m_index.pickPoint(float(x), float(y));
    qp::StageStats::instance().add(QStringLiteral("pick_index_ms"), msSince(t0));
    select(id);
    return id;
}

int Selection::pickRay(const QVector3D &origin, const QVector3D &direction)
{
    const int64_t t0 = qp::nowNs();
    ensureIndex();
    const int id = m_index.pickRay(origin, direction.normalized());
    qp::StageStats::instance().add(QStringLiteral("pick_index_ms"), msSince(t0));
    select(id);
    return id;
}

void Selection::select(int trackId)
{
    if (trackId == m_trackId)
        return;
    m_trackId = trackId;
    refreshInfo(); // info first: bindings that read both must never see a new id with old info
    emit trackIdChanged();
}

void Selection::recordComparison(int quick3dId, int indexId, double quick3dMs, double quick3dDistance,
                                 const QVector3D &origin, const QVector3D &direction)
{
    auto &stats = qp::StageStats::instance();
    stats.add(QStringLiteral("pick_quick3d_ms"), quick3dMs);
    stats.add(QStringLiteral("pick_agree"), quick3dId == indexId ? 1.0 : 0.0);
    if (quick3dId == indexId)
        return;
    float ourT = -1;
    const QVector3D d = direction.normalized();
    m_index.pickRay(origin, d, &ourT);
    // Where our ray meets Quick 3D's box: equal to ourT means both boxes are hit at the same point (a tie).
    float theirT = -1;
    for (const TrackIndex::Box &b : m_index.boxes())
        if (int(b.id) == quick3dId && !TrackIndex::rayHitsBox(b, origin, d, &theirT))
            theirT = -2; // our ray misses their box entirely
    qCInfo(lcPick).nospace() << "disagree: ours " << indexId << " at " << ourT << " m, quick3d " << quick3dId
                             << " at " << quick3dDistance << " m from the camera; our ray meets their box at "
                             << theirT << " m";
}

void Selection::refreshInfo()
{
    QVariantMap info;
    if (m_trackId >= 0) {
        if (const qp::TrackSetPtr set = m_store->latestTracks()) {
            for (const qp::Track &t : set->tracks) {
                if (int(t.id) != m_trackId)
                    continue;
                double heading = std::fmod(t.heading * 180.0 / std::numbers::pi, 360.0);
                if (heading < 0)
                    heading += 360.0;
                info = {{"id", int(t.id)},
                        {"cls", QString::fromLatin1(className(t.cls))},
                        {"speed", t.speed},
                        {"heading", heading},
                        {"x", t.x},
                        {"y", t.y},
                        {"distance", std::hypot(t.x, t.y)},
                        {"length", t.length},
                        {"width", t.width},
                        {"height", TrackIndex::heightFor(t.cls)}};
                break;
            }
        }
    }
    const bool gone = info.isEmpty() && m_trackId >= 0; // the object left the scene
    if (gone)
        m_trackId = -1;
    m_info = info;
    emit infoChanged();
    if (gone)
        emit trackIdChanged();
}
