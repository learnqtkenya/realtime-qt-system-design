#pragma once

#include "LidarPipeline.h"

#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>

struct LidarPipelineForeign
{
    Q_GADGET
    QML_FOREIGN(LidarPipeline)
    QML_NAMED_ELEMENT(LidarPipeline)
    QML_UNCREATABLE("Owned by C++")
};

// Shows each sensor's newest layer as its own texture; only changed layers are uploaded.
class PointLayersItem : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PointLayersView)

    Q_PROPERTY(LidarPipeline *pipeline READ pipeline WRITE setPipeline NOTIFY pipelineChanged FINAL)
    Q_PROPERTY(qreal pxPerMetre READ pxPerMetre WRITE setPxPerMetre NOTIFY pxPerMetreChanged FINAL)

public:
    explicit PointLayersItem(QQuickItem *parent = nullptr);

    LidarPipeline *pipeline() const { return m_pipeline; }
    void setPipeline(LidarPipeline *pipeline);
    qreal pxPerMetre() const { return m_pxPerMetre; }
    void setPxPerMetre(qreal v);

signals:
    void pipelineChanged();
    void pxPerMetreChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void pushViewport();

    QPointer<LidarPipeline> m_pipeline;
    qreal m_pxPerMetre = 7;
};
