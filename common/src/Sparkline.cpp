#include "qp/Sparkline.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>

#include <algorithm>

namespace qp {

Sparkline::Sparkline(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

void Sparkline::setValues(const QList<qreal> &v)
{
    m_values = v;
    update();
    emit valuesChanged();
}

void Sparkline::setCapacity(int c)
{
    c = std::max(2, c);
    if (m_capacity == c)
        return;
    m_capacity = c;
    update();
    emit capacityChanged();
}

void Sparkline::setMaximum(qreal m)
{
    if (qFuzzyCompare(m_maximum, m))
        return;
    m_maximum = m;
    update();
    emit maximumChanged();
}

void Sparkline::setThreshold(qreal t)
{
    if (qFuzzyCompare(m_threshold, t))
        return;
    m_threshold = t;
    update();
    emit thresholdChanged();
}

void Sparkline::setColor(const QColor &c)
{
    if (m_color == c)
        return;
    m_color = c;
    update();
    emit colorChanged();
}

void Sparkline::setThresholdColor(const QColor &c)
{
    if (m_thresholdColor == c)
        return;
    m_thresholdColor = c;
    update();
    emit thresholdColorChanged();
}

qreal Sparkline::scaleMaximum() const
{
    if (m_maximum > 0)
        return m_maximum;
    qreal top = m_threshold > 0 ? m_threshold * 1.25 : 0;
    for (qreal v : m_values)
        top = std::max(top, v);
    return top > 0 ? top * 1.1 : 1.0;
}

namespace {

QSGGeometryNode *lineNode(QSGGeometry::DrawingMode mode)
{
    auto *node = new QSGGeometryNode;
    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    geometry->setDrawingMode(mode);
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(new QSGFlatColorMaterial);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

void setNodeColor(QSGGeometryNode *node, const QColor &c)
{
    auto *m = static_cast<QSGFlatColorMaterial *>(node->material());
    if (m->color() != c) {
        m->setColor(c);
        node->markDirty(QSGNode::DirtyMaterial);
    }
}

} // namespace

QSGNode *Sparkline::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    QSGNode *root = old;
    if (!root) {
        root = new QSGNode;
        root->appendChildNode(lineNode(QSGGeometry::DrawLines));     // threshold
        root->appendChildNode(lineNode(QSGGeometry::DrawLineStrip)); // values
    }
    auto *thresholdNode = static_cast<QSGGeometryNode *>(root->firstChild());
    auto *valuesNode = static_cast<QSGGeometryNode *>(root->lastChild());
    setNodeColor(thresholdNode, m_thresholdColor);
    setNodeColor(valuesNode, m_color);

    const float w = float(width());
    const float h = float(height());
    const qreal top = scaleMaximum();
    auto yOf = [&](qreal v) { return h - float(std::clamp(v / top, 0.0, 1.0)) * h; };

    QSGGeometry *tg = thresholdNode->geometry();
    if (m_threshold >= 0) {
        tg->allocate(2);
        tg->vertexDataAsPoint2D()[0].set(0, yOf(m_threshold));
        tg->vertexDataAsPoint2D()[1].set(w, yOf(m_threshold));
    } else {
        tg->allocate(0);
    }
    thresholdNode->markDirty(QSGNode::DirtyGeometry);

    // Right-aligned so the newest value is always at the right edge.
    QSGGeometry *vg = valuesNode->geometry();
    const int n = std::min<int>(int(m_values.size()), m_capacity);
    vg->allocate(n);
    const qsizetype first = m_values.size() - n;
    const float step = w / float(m_capacity - 1);
    for (int i = 0; i < n; ++i)
        vg->vertexDataAsPoint2D()[i].set(w - float(n - 1 - i) * step, yOf(m_values[first + i]));
    valuesNode->markDirty(QSGNode::DirtyGeometry);
    return root;
}

} // namespace qp
