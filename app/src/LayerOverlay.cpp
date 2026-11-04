#include "LayerOverlay.h"

#include "Plugins.h"
#include "qp/StageStats.h"

#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGTextNode>
#include <QSGVertexColorMaterial>
#include <QTextLayout>

namespace {

class LayerNode : public QSGNode
{
public:
    explicit LayerNode(QQuickWindow *window)
    {
        lines = new QSGGeometryNode;
        auto *g = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        g->setDrawingMode(QSGGeometry::DrawLines);
        lines->setGeometry(g);
        lines->setFlag(QSGNode::OwnsGeometry);
        lines->setMaterial(new QSGVertexColorMaterial);
        lines->setFlag(QSGNode::OwnsMaterial);
        appendChildNode(lines);
        text = window->createTextNode();
        appendChildNode(text);
    }

    const void *layer = nullptr;
    OverlayFramePtr frame;
    QSGGeometryNode *lines;
    QSGTextNode *text;
};

void build(LayerNode *n, const OverlayFrame &f, float s, float cx, float cy)
{
    QSGGeometry *g = n->lines->geometry();
    g->allocate(int(f.segments.size()) * 2);
    auto *v = g->vertexDataAsColoredPoint2D();
    for (size_t i = 0; i < f.segments.size(); ++i) {
        const auto &seg = f.segments[i];
        const quint32 c = seg.rgba;
        const int a = int(c >> 24);
        // Premultiplied, as the vertex colour material expects.
        const auto r = uchar(((c >> 16) & 0xFF) * a / 255), gr = uchar(((c >> 8) & 0xFF) * a / 255),
                   b = uchar((c & 0xFF) * a / 255);
        v[i * 2].set(cx + seg.x1 * s, cy - seg.y1 * s, r, gr, b, uchar(a));
        v[i * 2 + 1].set(cx + seg.x2 * s, cy - seg.y2 * s, r, gr, b, uchar(a));
    }
    n->lines->markDirty(QSGNode::DirtyGeometry);

    n->text->clear();
    QFont font;
    font.setPixelSize(11);
    for (const auto &l : f.labels) {
        QTextLayout layout(l.text, font);
        layout.beginLayout();
        layout.createLine();
        layout.endLayout();
        n->text->setColor(QColor::fromRgba(l.rgba));
        n->text->addTextLayout(QPointF(cx + l.x * s, cy - l.y * s - 12), &layout);
    }
}

} // namespace

LayerOverlay::LayerOverlay(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

void LayerOverlay::setHost(PluginHost *host)
{
    if (host == m_host)
        return;
    if (m_host)
        m_host->disconnect(this);
    m_host = host;
    if (m_host)
        connect(m_host, &PluginHost::overlaysChanged, this, &QQuickItem::update);
    update();
    emit hostChanged();
}

void LayerOverlay::setPxPerMetre(double v)
{
    if (qFuzzyCompare(v, m_pxPerMetre))
        return;
    m_pxPerMetre = v;
    m_viewChanged = true;
    update();
    emit pxPerMetreChanged();
}

void LayerOverlay::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    m_viewChanged = true;
    update();
}

// Render thread, GUI thread blocked: reading the host's frames is safe here.
QSGNode *LayerOverlay::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    const qint64 t0 = qp::nowNs();
    QSGNode *root = old ? old : new QSGNode;
    const std::vector<PluginHost::Drawn> drawn = m_host ? m_host->drawn() : std::vector<PluginHost::Drawn>{};
    const float s = float(m_pxPerMetre), cx = float(width() / 2), cy = float(height() / 2);
    while (root->childCount() > int(drawn.size())) {
        QSGNode *last = root->lastChild();
        root->removeChildNode(last);
        delete last;
    }
    while (root->childCount() < int(drawn.size()))
        root->appendChildNode(new LayerNode(window()));
    int i = 0;
    for (const auto &d : drawn) {
        auto *n = static_cast<LayerNode *>(root->childAtIndex(i++));
        if (!m_viewChanged && n->layer == d.layer && n->frame == d.frame)
            continue;
        n->layer = d.layer;
        n->frame = d.frame;
        build(n, *d.frame, s, cx, cy);
    }
    m_viewChanged = false;
    qp::StageStats::instance().add(QStringLiteral("layers_sync_ms"), double(qp::nowNs() - t0) / 1e6);
    return root;
}
