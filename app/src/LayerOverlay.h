#pragma once

#include "Plugins.h"

#include <QPointer>
#include <QQuickItem>

// Draws what every script and process layer produced: one geometry node and one text node per
// layer, rebuilt when that layer delivers or the view changes.
class LayerOverlay : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(PluginHost *host READ host WRITE setHost NOTIFY hostChanged FINAL)
    Q_PROPERTY(double pxPerMetre READ pxPerMetre WRITE setPxPerMetre NOTIFY pxPerMetreChanged FINAL)

public:
    explicit LayerOverlay(QQuickItem *parent = nullptr);

    PluginHost *host() const { return m_host; }
    void setHost(PluginHost *host);
    double pxPerMetre() const { return m_pxPerMetre; }
    void setPxPerMetre(double v);

signals:
    void hostChanged();
    void pxPerMetreChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    QPointer<PluginHost> m_host;
    double m_pxPerMetre = 7;
    bool m_viewChanged = true;
};
