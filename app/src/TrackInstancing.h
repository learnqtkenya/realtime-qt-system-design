#pragma once

#include <QAbstractItemModel>
#include <QPointer>
#include <QQuick3DInstancing>
#include <QQmlEngine>

class TrackListModel;

// Every track box as one instanced draw: a table of transforms and colours, rebuilt when the
// tracks change, instead of one scene node per box.
class TrackInstancing : public QQuick3DInstancing
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QAbstractItemModel *tracks READ tracks WRITE setTracks NOTIFY tracksChanged FINAL)
    Q_PROPERTY(int selectedId READ selectedId WRITE setSelectedId NOTIFY selectedIdChanged FINAL)

public:
    explicit TrackInstancing(QQuick3DObject *parent = nullptr);

    QAbstractItemModel *tracks() const;
    void setTracks(QAbstractItemModel *model);
    int selectedId() const { return m_selectedId; }
    void setSelectedId(int id);

signals:
    void tracksChanged();
    void selectedIdChanged();

protected:
    QByteArray getInstanceBuffer(int *instanceCount) override;

private:
    QPointer<TrackListModel> m_tracks;
    int m_selectedId = -1;
};
