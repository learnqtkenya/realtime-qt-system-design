#pragma once

#include "FrameStore.h"

#include <QAbstractListModel>

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
    };

    explicit TrackListModel(FrameStore *store, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    void reload();

    FrameStore *m_store;
    qp::TrackSetPtr m_tracks;
};
