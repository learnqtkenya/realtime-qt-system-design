#pragma once

#include "FrameStore.h"

#include <QPointer>
#include <QQmlEngine>
#include <QQuickPaintedItem>

// QML can name the store's type and pass it around, but never create one.
struct FrameStoreForeign
{
    Q_GADGET
    QML_FOREIGN(FrameStore)
    QML_NAMED_ELEMENT(FrameStore)
    QML_UNCREATABLE("Owned by C++")
};

class PointCloudItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PointCloudView)

    Q_PROPERTY(FrameStore *store READ store WRITE setStore NOTIFY storeChanged FINAL)
    Q_PROPERTY(qreal pxPerMetre READ pxPerMetre WRITE setPxPerMetre NOTIFY pxPerMetreChanged FINAL)

public:
    explicit PointCloudItem(QQuickItem *parent = nullptr);

    FrameStore *store() const { return m_store; }
    void setStore(FrameStore *store);
    qreal pxPerMetre() const { return m_pxPerMetre; }
    void setPxPerMetre(qreal v);

    void paint(QPainter *painter) override;

signals:
    void storeChanged();
    void pxPerMetreChanged();

private:
    QPointer<FrameStore> m_store;
    qreal m_pxPerMetre = 7;
};
