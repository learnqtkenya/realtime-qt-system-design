#pragma once

#include "LidarMailbox.h"

#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>

struct LidarMailboxForeign
{
    Q_GADGET
    QML_FOREIGN(LidarMailbox)
    QML_NAMED_ELEMENT(LidarMailbox)
    QML_UNCREATABLE("Owned by C++")
};

// Shared plumbing for the two scene-graph variants.
class PointViewBase : public QQuickItem
{
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(LidarMailbox *mailbox READ mailbox WRITE setMailbox NOTIFY mailboxChanged FINAL)
    Q_PROPERTY(qreal pxPerMetre READ pxPerMetre WRITE setPxPerMetre NOTIFY pxPerMetreChanged FINAL)
    Q_PROPERTY(qreal pointSize READ pointSize WRITE setPointSize NOTIFY pointSizeChanged FINAL)

public:
    explicit PointViewBase(QQuickItem *parent = nullptr);

    LidarMailbox *mailbox() const { return m_mailbox; }
    void setMailbox(LidarMailbox *mailbox);
    qreal pxPerMetre() const { return m_pxPerMetre; }
    void setPxPerMetre(qreal v);
    qreal pointSize() const { return m_pointSize; }
    void setPointSize(qreal v);

signals:
    void mailboxChanged();
    void pxPerMetreChanged();
    void pointSizeChanged();

protected:
    QPointer<LidarMailbox> m_mailbox;
    qreal m_pxPerMetre = 7;
    qreal m_pointSize = 1.5;
};

// Variant "sg": QSGGeometryNode per sensor, copied during sync.
class SgPointsItem : public PointViewBase
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SgPointCloudView)

public:
    using PointViewBase::PointViewBase;

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
};

// Variant "rhi": QSGRenderNode with its own QRhi buffers, uploaded in prepare().
class RhiPointsItem : public PointViewBase
{
    Q_OBJECT
    QML_NAMED_ELEMENT(RhiPointCloudView)

    Q_PROPERTY(int bufferRing READ bufferRing WRITE setBufferRing NOTIFY bufferRingChanged FINAL)

public:
    using PointViewBase::PointViewBase;

    int bufferRing() const { return m_bufferRing; }
    void setBufferRing(int n);

signals:
    void bufferRingChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;

private:
    int m_bufferRing = 1;
};
