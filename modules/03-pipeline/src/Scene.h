#pragma once

#include "LidarPipeline.h"

#include <QAbstractItemModel>
#include <QObject>
#include <QQmlEngine>

class Scene : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    QML_UNCREATABLE("Provided by main() via QQmlEngine::setExternalSingletonInstance")

    Q_PROPERTY(LidarPipeline *pipeline READ pipeline CONSTANT FINAL)
    Q_PROPERTY(QAbstractItemModel *tracks READ tracks CONSTANT FINAL)
    Q_PROPERTY(QAbstractItemModel *events READ events CONSTANT FINAL)

public:
    Scene(LidarPipeline *pipeline, QAbstractItemModel *tracks, QAbstractItemModel *events,
          QObject *parent = nullptr)
        : QObject(parent), m_pipeline(pipeline), m_tracks(tracks), m_events(events)
    {}

    LidarPipeline *pipeline() const { return m_pipeline; }
    QAbstractItemModel *tracks() const { return m_tracks; }
    QAbstractItemModel *events() const { return m_events; }

private:
    LidarPipeline *m_pipeline;
    QAbstractItemModel *m_tracks;
    QAbstractItemModel *m_events;
};
