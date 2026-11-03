#pragma once

#include "FrameStore.h"

#include <QAbstractItemModel>
#include <QObject>
#include <QQmlEngine>

// Everything QML may see. Built in main() and handed to the engine.
class Scene : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    QML_UNCREATABLE("Provided by main() via QQmlEngine::setExternalSingletonInstance")

    Q_PROPERTY(FrameStore *store READ store CONSTANT FINAL)
    Q_PROPERTY(QAbstractItemModel *tracks READ tracks CONSTANT FINAL)
    Q_PROPERTY(QAbstractItemModel *events READ events CONSTANT FINAL)

public:
    Scene(FrameStore *store, QAbstractItemModel *tracks, QAbstractItemModel *events,
          QObject *parent = nullptr)
        : QObject(parent), m_store(store), m_tracks(tracks), m_events(events)
    {}

    FrameStore *store() const { return m_store; }
    QAbstractItemModel *tracks() const { return m_tracks; }
    QAbstractItemModel *events() const { return m_events; }

private:
    FrameStore *m_store;
    QAbstractItemModel *m_tracks;
    QAbstractItemModel *m_events;
};
