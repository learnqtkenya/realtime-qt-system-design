#pragma once

#include "CameraFeed.h"

#include <QObject>
#include <QQmlEngine>
#include <QQuickItem>

class LidarHub;

class Cameras : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    QML_UNCREATABLE("Provided by main() via QQmlEngine::setExternalSingletonInstance")

    Q_PROPERTY(bool available READ available CONSTANT FINAL)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged FINAL)
    Q_PROPERTY(QString sync READ sync WRITE setSync NOTIFY syncChanged FINAL)
    Q_PROPERTY(QString decoder READ decoder CONSTANT FINAL)
    Q_PROPERTY(QList<CameraFeed *> feeds READ feeds CONSTANT FINAL)
    // What the decoder hands us: pixel format, CPU memory or GPU texture, size.
    Q_PROPERTY(QString frameInfo READ frameInfo NOTIFY frameInfoChanged FINAL)

public:
    Cameras(const QString &mediaDir, LidarHub *hub, const QString &decoder, QObject *parent = nullptr);
    // Call (GUI thread) when lidar data arrives, so aligned cameras follow the lidar clock.
    void lidarAdvanced();

    bool available() const { return !m_feeds.isEmpty(); }
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    QString path() const { return m_path; }
    void setPath(const QString &path); // "forward" or "image"
    QString sync() const { return m_sync; }
    void setSync(const QString &sync); // "latest" or "aligned"
    QString decoder() const { return m_decoder; }
    QList<CameraFeed *> feeds() const { return m_feeds; }
    QString frameInfo() const { return m_feeds.isEmpty() ? QString() : m_feeds.first()->frameInfo(); }

signals:
    void enabledChanged();
    void pathChanged();
    void syncChanged();
    void frameInfoChanged();

private:
    QList<CameraFeed *> m_feeds;
    bool m_enabled = false;
    QString m_path = QStringLiteral("forward");
    QString m_sync = QStringLiteral("latest");
    QString m_decoder;
};

// The "image" path: shows CameraFeed::image() as our own texture.
class CameraImageItem : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CameraImageView)

    Q_PROPERTY(CameraFeed *feed READ feed WRITE setFeed NOTIFY feedChanged FINAL)

public:
    explicit CameraImageItem(QQuickItem *parent = nullptr);
    CameraFeed *feed() const { return m_feed; }
    void setFeed(CameraFeed *feed);

signals:
    void feedChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;

private:
    QPointer<CameraFeed> m_feed;
};
