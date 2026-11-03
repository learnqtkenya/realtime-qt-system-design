#pragma once

#include "LidarMailbox.h"

#include <QPointer>
#include <QQmlEngine>
#include <QQuick3DGeometry>

// Variant "quick3d": one sensor's newest cloud as a point geometry.
class LidarGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(LidarGeometry)

    Q_PROPERTY(LidarMailbox *mailbox READ mailbox WRITE setMailbox NOTIFY mailboxChanged FINAL)
    Q_PROPERTY(int sensor READ sensor WRITE setSensor NOTIFY sensorChanged FINAL)

public:
    LidarGeometry();

    LidarMailbox *mailbox() const { return m_mailbox; }
    void setMailbox(LidarMailbox *mailbox);
    int sensor() const { return m_sensor; }
    void setSensor(int sensor);

signals:
    void mailboxChanged();
    void sensorChanged();

private:
    void refresh();

    QPointer<LidarMailbox> m_mailbox;
    int m_sensor = 0;
    quint64 m_serial = 0;
};
