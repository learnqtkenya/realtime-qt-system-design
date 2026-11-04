#pragma once

#include "FrameStore.h"

#include <QAbstractListModel>
#include <QHash>

#include <vector>

class TrackListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        TrackIdRole = Qt::UserRole + 1,
        PosXRole,
        PosYRole,
        HeadingRole,
        SpeedRole,
        BoxLengthRole,
        BoxWidthRole,
        ClassRole,
        DistanceRole,
    };
    // Reset: replace everything on every update. Diff: rows keep their identity (by track id);
    // changed rows, new rows and gone rows are reported as such.
    enum class Updates { Reset, Diff };

    explicit TrackListModel(FrameStore *store, QObject *parent = nullptr);

    void setUpdates(Updates updates) { m_updates = updates; }
    Updates updates() const { return m_updates; }
    // The rows as the model holds them (for instancing and tests).
    const std::vector<qp::Track> &rows() const { return m_rows; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void apply(const qp::TrackSetPtr &set);

signals:
    // After every update, whichever way it was reported.
    void updated();

private:
    void reload();
    void diffTo(const std::vector<qp::Track> &next);

    FrameStore *m_store;
    Updates m_updates = Updates::Reset;
    std::vector<qp::Track> m_rows;
};
