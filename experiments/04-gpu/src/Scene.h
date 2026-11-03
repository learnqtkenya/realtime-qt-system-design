#pragma once

#include "LidarMailbox.h"

#include <QAbstractItemModel>
#include <QObject>
#include <QQmlEngine>

class Scene : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    QML_UNCREATABLE("Provided by main() via QQmlEngine::setExternalSingletonInstance")

    Q_PROPERTY(LidarMailbox *mailbox READ mailbox CONSTANT FINAL)
    Q_PROPERTY(QAbstractItemModel *tracks READ tracks CONSTANT FINAL)
    Q_PROPERTY(QAbstractItemModel *events READ events CONSTANT FINAL)

public:
    Scene(LidarMailbox *mailbox, QAbstractItemModel *tracks, QAbstractItemModel *events,
          QObject *parent = nullptr)
        : QObject(parent), m_mailbox(mailbox), m_tracks(tracks), m_events(events)
    {}

    LidarMailbox *mailbox() const { return m_mailbox; }
    QAbstractItemModel *tracks() const { return m_tracks; }
    QAbstractItemModel *events() const { return m_events; }

private:
    LidarMailbox *m_mailbox;
    QAbstractItemModel *m_tracks;
    QAbstractItemModel *m_events;
};
