#pragma once

#include "qp/SensorSimulator.h"

#include <QAbstractItemModel>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>

class EventLogModel;

class SensorBridge : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged FINAL)
    Q_PROPERTY(QAbstractItemModel *events READ events CONSTANT FINAL)

public:
    static SensorBridge *create(QQmlEngine *, QJSEngine *);
    ~SensorBridge() override;

    QVariantList tracks() const { return m_tracks; }
    QAbstractItemModel *events() const;

signals:
    void lidarUpdated(int sensorId, const QVariantList &points, double captureNs);
    void tracksChanged();

private:
    explicit SensorBridge(QObject *parent = nullptr);
    void onLidar(qp::SensorFrame frame);
    void onTracks(qp::TrackSetPtr set);
    void onEvents(qp::EventBatchPtr batch);

    qp::SensorSimulator *m_sim = nullptr;
    EventLogModel *m_events = nullptr;
    QVariantList m_tracks;
    quint64 m_lidarConsumed = 0;
};
