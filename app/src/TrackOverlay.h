#pragma once

#include <QAbstractItemModel>
#include <QPointer>
#include <QQuickItem>

class TrackListModel;

// The top-down track boxes and labels as one scene graph subtree, built on the render thread
// from the model's rows: no QML item per object.
class TrackOverlay : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QAbstractItemModel *tracks READ tracks WRITE setTracks NOTIFY tracksChanged FINAL)
    Q_PROPERTY(double pxPerMetre READ pxPerMetre WRITE setPxPerMetre NOTIFY pxPerMetreChanged FINAL)
    Q_PROPERTY(int selectedId READ selectedId WRITE setSelectedId NOTIFY selectedIdChanged FINAL)
    // Above this many objects only the selected one is labelled.
    Q_PROPERTY(int labelLimit READ labelLimit WRITE setLabelLimit NOTIFY labelLimitChanged FINAL)

public:
    explicit TrackOverlay(QQuickItem *parent = nullptr);

    QAbstractItemModel *tracks() const;
    void setTracks(QAbstractItemModel *model);
    double pxPerMetre() const { return m_pxPerMetre; }
    void setPxPerMetre(double v);
    int selectedId() const { return m_selectedId; }
    void setSelectedId(int id);
    int labelLimit() const { return m_labelLimit; }
    void setLabelLimit(int n);

signals:
    void tracksChanged();
    void pxPerMetreChanged();
    void selectedIdChanged();
    void labelLimitChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;

private:
    QPointer<TrackListModel> m_tracks;
    double m_pxPerMetre = 7;
    int m_selectedId = -1;
    int m_labelLimit = 200;
};
