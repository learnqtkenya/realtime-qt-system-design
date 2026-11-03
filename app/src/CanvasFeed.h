#pragma once

#include "qp/SimTypes.h"

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>

// The "canvas" renderer's data path (experiment 01): points become a QVariantList for QML.
class CanvasFeed : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by C++")

public:
    using QObject::QObject;

    void onFrame(const qp::SensorFrame &frame);

signals:
    void lidarUpdated(int sensorId, const QVariantList &points, double captureNs);
};
