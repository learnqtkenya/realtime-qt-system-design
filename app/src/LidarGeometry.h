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
    // Level of detail: keep every pointStep-th point. (Not "stride": the base class owns that name.)
    Q_PROPERTY(int pointStep READ pointStep WRITE setPointStep NOTIFY pointStepChanged FINAL)

public:
    LidarGeometry();

    LidarMailbox *mailbox() const { return m_mailbox; }
    void setMailbox(LidarMailbox *mailbox);
    int sensor() const { return m_sensor; }
    void setSensor(int sensor);
    int pointStep() const { return m_pointStep; }
    void setPointStep(int step);

signals:
    void mailboxChanged();
    void sensorChanged();
    void pointStepChanged();

private:
    void refresh();

    QPointer<LidarMailbox> m_mailbox;
    int m_sensor = 0;
    int m_pointStep = 1;
    quint64 m_serial = 0;
};
