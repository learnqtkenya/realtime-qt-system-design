#pragma once

#include "CanvasFeed.h"
#include "FrameStore.h"
#include "LidarMailbox.h"
#include "LidarPipeline.h"
#include "Proxies.h"

#include <QAbstractItemModel>
#include <QObject>
#include <QQmlEngine>

// Everything QML may see of the data. Built in main().
class Scene : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    QML_UNCREATABLE("Provided by main() via QQmlEngine::setExternalSingletonInstance")

    Q_PROPERTY(FrameStore *store READ store CONSTANT FINAL)
    Q_PROPERTY(LidarPipeline *pipeline READ pipeline CONSTANT FINAL)
    Q_PROPERTY(LidarMailbox *mailbox READ mailbox CONSTANT FINAL)
    Q_PROPERTY(CanvasFeed *canvasFeed READ canvasFeed CONSTANT FINAL)
    Q_PROPERTY(QAbstractItemModel *tracks READ tracks CONSTANT FINAL)
    Q_PROPERTY(QAbstractItemModel *events READ events CONSTANT FINAL)
    Q_PROPERTY(TrackTableProxy *objectTable READ objectTable CONSTANT FINAL)
    // EventFilterProxy or EventFilterModel (--set eventFilter=proxy|index); both have the same properties.
    Q_PROPERTY(QAbstractItemModel *eventLog READ eventLog CONSTANT FINAL)

public:
    struct Parts
    {
        FrameStore *store;
        LidarPipeline *pipeline;
        LidarMailbox *mailbox;
        CanvasFeed *canvasFeed;
        QAbstractItemModel *tracks;
        QAbstractItemModel *events;
        TrackTableProxy *objectTable;
        QAbstractItemModel *eventLog;
    };

    explicit Scene(const Parts &parts, QObject *parent = nullptr) : QObject(parent), m(parts) {}

    FrameStore *store() const { return m.store; }
    LidarPipeline *pipeline() const { return m.pipeline; }
    LidarMailbox *mailbox() const { return m.mailbox; }
    CanvasFeed *canvasFeed() const { return m.canvasFeed; }
    QAbstractItemModel *tracks() const { return m.tracks; }
    QAbstractItemModel *events() const { return m.events; }
    TrackTableProxy *objectTable() const { return m.objectTable; }
    QAbstractItemModel *eventLog() const { return m.eventLog; }

private:
    Parts m;
};
