#pragma once

#include "TrackIndex.h"

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>
#include <QVector3D>

class FrameStore;

// Which object is selected, and the picking that chooses it. GUI thread only.
class Selection : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    QML_UNCREATABLE("Provided by main() via QQmlEngine::setExternalSingletonInstance")

    Q_PROPERTY(int trackId READ trackId NOTIFY trackIdChanged FINAL)
    Q_PROPERTY(QVariantMap info READ info NOTIFY infoChanged FINAL)

public:
    explicit Selection(FrameStore *store, QObject *parent = nullptr);

    int trackId() const { return m_trackId; }
    QVariantMap info() const { return m_info; }

    // Top-down: world metres. Returns the picked id, or -1 (which clears the selection).
    Q_INVOKABLE int pickTopDown(double x, double y);
    // 3D: a world-space ray (metres, z up).
    Q_INVOKABLE int pickRay(const QVector3D &origin, const QVector3D &direction);
    Q_INVOKABLE void select(int trackId);
    Q_INVOKABLE void clear() { select(-1); }
    // One source of truth for box heights, shared with the 3D view.
    Q_INVOKABLE double heightFor(int cls) const { return TrackIndex::heightFor(quint8(cls)); }
    Q_INVOKABLE double groundZ() const { return TrackIndex::kGroundZ; }
    // Records whether Quick 3D's own pick agreed with ours, and what it cost.
    // quick3dDistance: Quick 3D's hit distance (metres), or < 0 for no hit.
    Q_INVOKABLE void recordComparison(int quick3dId, int indexId, double quick3dMs, double quick3dDistance,
                                      const QVector3D &origin, const QVector3D &direction);

signals:
    void trackIdChanged();
    void infoChanged();

private:
    void ensureIndex();
    void refreshInfo();

    FrameStore *m_store;
    TrackIndex m_index;
    bool m_dirty = true;
    int m_trackId = -1;
    QVariantMap m_info;
};
