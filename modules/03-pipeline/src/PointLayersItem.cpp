#include "PointLayersItem.h"

#include "qp/FrameMonitor.h"
#include "qp/StageStats.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

#include <memory>

namespace {

// Owns its textures explicitly; the texture nodes never do.
class LayersNode : public QSGNode
{
public:
    explicit LayersNode(int layers)
        : nodes(size_t(layers)), textures(size_t(layers)), serials(size_t(layers), 0)
    {
        for (auto &n : nodes) {
            n = new QSGSimpleTextureNode;
            n->setOwnsTexture(false);
        }
    }

    ~LayersNode() override
    {
        for (QSGSimpleTextureNode *n : nodes)
            if (!n->parent())
                delete n;
    }

    std::vector<QSGSimpleTextureNode *> nodes; // children once attached
    std::vector<std::unique_ptr<QSGTexture>> textures;
    std::vector<quint64> serials;
};

} // namespace

PointLayersItem::PointLayersItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

void PointLayersItem::setPipeline(LidarPipeline *pipeline)
{
    if (m_pipeline == pipeline)
        return;
    if (m_pipeline)
        m_pipeline->disconnect(this);
    m_pipeline = pipeline;
    if (m_pipeline)
        connect(m_pipeline, &LidarPipeline::layerReady, this, &QQuickItem::update);
    pushViewport();
    update();
    emit pipelineChanged();
}

void PointLayersItem::setPxPerMetre(qreal v)
{
    if (qFuzzyCompare(m_pxPerMetre, v))
        return;
    m_pxPerMetre = v;
    pushViewport();
    emit pxPerMetreChanged();
}

void PointLayersItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        pushViewport();
}

void PointLayersItem::pushViewport()
{
    if (m_pipeline)
        m_pipeline->setViewport({int(width()), int(height()), m_pxPerMetre});
}

QSGNode *PointLayersItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    if (!m_pipeline) {
        delete old;
        return nullptr;
    }
    auto *root = static_cast<LayersNode *>(old);
    if (!root)
        root = new LayersNode(m_pipeline->sensorCount());

    int64_t newest = 0;
    for (size_t i = 0; i < root->nodes.size(); ++i) {
        const LidarPipeline::Layer layer = m_pipeline->layer(int(i));
        if (layer.serial == root->serials[i] || layer.image.isNull())
            continue;

        const int64_t t0 = qp::nowNs();
        std::unique_ptr<QSGTexture> texture(window()->createTextureFromImage(layer.image));
        QSGSimpleTextureNode *node = root->nodes[i];
        node->setTexture(texture.get());
        node->setRect(0, 0, layer.image.width(), layer.image.height());
        if (!node->parent())
            root->appendChildNode(node);
        root->textures[i] = std::move(texture);
        root->serials[i] = layer.serial;
        newest = std::max(newest, layer.captureNs);
        qp::StageStats::instance().add(QStringLiteral("upload_ms"), double(qp::nowNs() - t0) / 1e6);
    }
    if (newest > 0)
        qp::markContentCaptured(newest);
    return root;
}
