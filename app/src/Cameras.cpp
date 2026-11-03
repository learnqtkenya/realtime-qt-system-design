#include "Cameras.h"

#include "qp/StageStats.h"

#include <QDir>
#include <QFileInfo>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

#include <memory>

Cameras::Cameras(const QString &mediaDir, LidarHub *hub, const QString &decoder, QObject *parent)
    : QObject(parent), m_decoder(decoder)
{
    for (int i = 0; i < 4; ++i) {
        const QString file = QDir(mediaDir).filePath(QStringLiteral("cam%1.mp4").arg(i));
        if (!QFileInfo::exists(file))
            break;
        m_feeds << new CameraFeed(i, QUrl::fromLocalFile(file), hub, this);
    }
    if (m_feeds.size() != 4) { // all or nothing
        qDeleteAll(m_feeds);
        m_feeds.clear();
    }
    if (!m_feeds.isEmpty())
        connect(m_feeds.first(), &CameraFeed::statsChanged, this, &Cameras::frameInfoChanged);
}

void Cameras::lidarAdvanced()
{
    if (!m_enabled || m_sync != QLatin1String("aligned"))
        return;
    for (CameraFeed *f : std::as_const(m_feeds))
        f->realign();
}

void Cameras::setMarkSink(const std::function<void(const rec::CameraMark &)> &sink)
{
    for (CameraFeed *f : std::as_const(m_feeds))
        f->setMarkSink(sink);
}

void Cameras::setLive(bool live, qint64 delayNs)
{
    for (CameraFeed *f : std::as_const(m_feeds)) {
        f->setPresentationDelay(delayNs);
        if (live)
            f->goLive();
    }
}

void Cameras::follow(const rec::CameraMark &mark, qint64 playheadNs, double rate, bool playing, qint64 wallNs)
{
    if (m_enabled && mark.camera >= 0 && mark.camera < m_feeds.size())
        m_feeds[mark.camera]->follow(mark, playheadNs, rate, playing, wallNs);
}

void Cameras::setEnabled(bool on)
{
    on = on && available();
    if (on == m_enabled)
        return;
    m_enabled = on;
    for (CameraFeed *f : std::as_const(m_feeds))
        on ? f->start() : f->stop();
    emit enabledChanged();
}

void Cameras::setPath(const QString &path)
{
    if ((path != QLatin1String("forward") && path != QLatin1String("image")) || path == m_path)
        return;
    m_path = path;
    for (CameraFeed *f : std::as_const(m_feeds))
        f->setImagePath(path == QLatin1String("image"));
    emit pathChanged();
}

void Cameras::setSync(const QString &sync)
{
    if ((sync != QLatin1String("latest") && sync != QLatin1String("aligned")) || sync == m_sync)
        return;
    m_sync = sync;
    for (CameraFeed *f : std::as_const(m_feeds))
        f->setSync(sync == QLatin1String("aligned") ? CameraFeed::Sync::Aligned : CameraFeed::Sync::Latest);
    emit syncChanged();
}

namespace {

// Owns its texture explicitly (QSGSimpleTextureNode's ownership on replace isn't documented).
class ImageNode : public QSGSimpleTextureNode
{
public:
    std::unique_ptr<QSGTexture> texture;
};

} // namespace

CameraImageItem::CameraImageItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

void CameraImageItem::setFeed(CameraFeed *feed)
{
    if (m_feed == feed)
        return;
    if (m_feed)
        m_feed->disconnect(this);
    m_feed = feed;
    if (m_feed)
        connect(m_feed, &CameraFeed::imageChanged, this, &QQuickItem::update);
    update();
    emit feedChanged();
}

QSGNode *CameraImageItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    const QImage image = m_feed ? m_feed->image() : QImage();
    if (image.isNull()) {
        delete old;
        return nullptr;
    }
    auto *node = static_cast<ImageNode *>(old);
    if (!node)
        node = new ImageNode;
    std::unique_ptr<QSGTexture> texture(window()->createTextureFromImage(image));
    node->setTexture(texture.get());
    node->texture = std::move(texture);
    // Fit, keeping the aspect ratio.
    const QSizeF s = QSizeF(image.size()).scaled(size(), Qt::KeepAspectRatio);
    node->setRect(QRectF(QPointF((width() - s.width()) / 2, (height() - s.height()) / 2), s));
    qp::StageStats::instance().add(QStringLiteral("camera_image_upload_bytes"), double(image.sizeInBytes()));
    return node;
}
