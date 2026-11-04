#include "TrackOverlay.h"

#include "FrameStore.h"
#include "TrackListModel.h"
#include "qp/StageStats.h"

#include <QFont>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGTextNode>
#include <QTextLayout>

#include <cmath>

namespace {

QSGGeometryNode *makeNode(QSGGeometry::DrawingMode mode, const QColor &color)
{
    auto *node = new QSGGeometryNode;
    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    geometry->setDrawingMode(mode);
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    auto *material = new QSGFlatColorMaterial;
    material->setColor(color);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

} // namespace

TrackOverlay::TrackOverlay(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

QAbstractItemModel *TrackOverlay::tracks() const
{
    return m_tracks;
}

void TrackOverlay::setTracks(QAbstractItemModel *model)
{
    auto *t = qobject_cast<TrackListModel *>(model);
    if (t == m_tracks)
        return;
    if (m_tracks)
        m_tracks->disconnect(this);
    m_tracks = t;
    if (m_tracks)
        connect(m_tracks, &TrackListModel::updated, this, &QQuickItem::update);
    update();
    emit tracksChanged();
}

void TrackOverlay::setPxPerMetre(double v)
{
    if (qFuzzyCompare(v, m_pxPerMetre))
        return;
    m_pxPerMetre = v;
    update();
    emit pxPerMetreChanged();
}

void TrackOverlay::setSelectedId(int id)
{
    if (id == m_selectedId)
        return;
    m_selectedId = id;
    update();
    emit selectedIdChanged();
}

void TrackOverlay::setLabelLimit(int n)
{
    if (n == m_labelLimit)
        return;
    m_labelLimit = n;
    update();
    emit labelLimitChanged();
}

// Render thread, GUI thread blocked: reading the model's rows is safe here.
QSGNode *TrackOverlay::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    const qint64 t0 = qp::nowNs();
    const std::vector<qp::Track> none;
    const std::vector<qp::Track> &rows = m_tracks ? m_tracks->rows() : none;
    QSGNode *root = old;
    QSGGeometryNode *outlines, *fill, *selectedOutline;
    QSGTextNode *labels, *selectedLabel;
    if (!root) {
        root = new QSGNode;
        outlines = makeNode(QSGGeometry::DrawLines, QColor(0xF8, 0xFA, 0xFC));
        fill = makeNode(QSGGeometry::DrawTriangles, QColor(0xFB, 0xBF, 0x24, 0x40));
        selectedOutline = makeNode(QSGGeometry::DrawLines, QColor(0xFB, 0xBF, 0x24));
        labels = window()->createTextNode();
        labels->setColor(QColor(0xCB, 0xD5, 0xE1));
        selectedLabel = window()->createTextNode();
        selectedLabel->setColor(QColor(0xFB, 0xBF, 0x24));
        selectedLabel->setTextStyle(QSGTextNode::Outline);
        selectedLabel->setStyleColor(QColor(0x0B, 0x10, 0x20));
        root->appendChildNode(outlines);
        root->appendChildNode(fill);
        root->appendChildNode(selectedOutline);
        root->appendChildNode(labels);
        root->appendChildNode(selectedLabel);
    } else {
        outlines = static_cast<QSGGeometryNode *>(root->childAtIndex(0));
        fill = static_cast<QSGGeometryNode *>(root->childAtIndex(1));
        selectedOutline = static_cast<QSGGeometryNode *>(root->childAtIndex(2));
        labels = static_cast<QSGTextNode *>(root->childAtIndex(3));
        selectedLabel = static_cast<QSGTextNode *>(root->childAtIndex(4));
    }

    const float s = float(m_pxPerMetre);
    const float cx = float(width() / 2), cy = float(height() / 2);
    auto corners = [&](const qp::Track &t, QPointF out[4]) {
        const float c = std::cos(t.heading), sn = std::sin(t.heading);
        const float hl = t.length / 2, hw = t.width / 2;
        const float lx[4] = {-hl, hl, hl, -hl}, ly[4] = {-hw, -hw, hw, hw};
        for (int k = 0; k < 4; ++k) {
            const float wx = t.x + lx[k] * c - ly[k] * sn;
            const float wy = t.y + lx[k] * sn + ly[k] * c;
            out[k] = QPointF(cx + wx * s, cy - wy * s);
        }
    };

    QSGGeometry *g = outlines->geometry();
    g->allocate(int(rows.size()) * 8);
    auto *v = g->vertexDataAsPoint2D();
    const qp::Track *selected = nullptr;
    for (size_t i = 0; i < rows.size(); ++i) {
        QPointF p[4];
        corners(rows[i], p);
        for (int k = 0; k < 4; ++k) {
            v[i * 8 + size_t(k) * 2].set(float(p[k].x()), float(p[k].y()));
            v[i * 8 + size_t(k) * 2 + 1].set(float(p[(k + 1) % 4].x()), float(p[(k + 1) % 4].y()));
        }
        if (int(rows[i].id) == m_selectedId)
            selected = &rows[i];
    }
    outlines->markDirty(QSGNode::DirtyGeometry);

    QSGGeometry *fg = fill->geometry();
    QSGGeometry *sg = selectedOutline->geometry();
    if (selected) {
        QPointF p[4];
        corners(*selected, p);
        fg->allocate(6);
        auto *f = fg->vertexDataAsPoint2D();
        const int tri[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; ++k)
            f[k].set(float(p[tri[k]].x()), float(p[tri[k]].y()));
        sg->allocate(8);
        auto *o = sg->vertexDataAsPoint2D();
        for (int k = 0; k < 4; ++k) {
            o[k * 2].set(float(p[k].x()), float(p[k].y()));
            o[k * 2 + 1].set(float(p[(k + 1) % 4].x()), float(p[(k + 1) % 4].y()));
        }
    } else {
        fg->allocate(0);
        sg->allocate(0);
    }
    fill->markDirty(QSGNode::DirtyGeometry);
    selectedOutline->markDirty(QSGNode::DirtyGeometry);

    const qint64 tl = qp::nowNs();
    labels->clear();
    selectedLabel->clear();
    QFont font;
    font.setPixelSize(10);
    const bool all = int(rows.size()) <= m_labelLimit;
    // All labels in one layout, a line each, placed where its object is: one layout's set-up costs
    // more than shaping ten characters (results/step-11).
    QString text;
    std::vector<QPointF> at;
    for (const qp::Track &t : rows) {
        const QPointF pos(cx + t.x * s + 8, cy - t.y * s - 20);
        const QString label = QStringLiteral("#%1  %2 m/s").arg(t.id).arg(t.speed, 0, 'f', 1);
        if (int(t.id) == m_selectedId) {
            QFont bold = font;
            bold.setBold(true);
            QTextLayout layout(label, bold);
            layout.beginLayout();
            layout.createLine();
            layout.endLayout();
            selectedLabel->addTextLayout(pos, &layout);
        } else if (all) {
            if (!text.isEmpty())
                text += QChar::LineSeparator;
            text += label;
            at.push_back(pos);
        }
    }
    if (!at.empty()) {
        QTextLayout layout(text, font);
        layout.beginLayout();
        for (const QPointF &pos : at) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(1e6);
            line.setPosition(pos);
        }
        layout.endLayout();
        labels->addTextLayout(QPointF(0, 0), &layout);
    }
    // Operator notes from the last 5 s of scene time, where they were made.
    if (m_tracks && m_tracks->store()) {
        const qp::TrackSetPtr set = m_tracks->store()->latestTracks();
        const qint64 now = set ? set->captureNs : 0;
        QFont noteFont = font;
        noteFont.setBold(true);
        for (const qp::EventRecord &n : m_tracks->store()->notes()) {
            if (n.captureNs > now || now - n.captureNs > 5'000'000'000LL)
                continue;
            QTextLayout layout(QStringLiteral("◆ ") + n.text, noteFont);
            layout.beginLayout();
            layout.createLine();
            layout.endLayout();
            selectedLabel->addTextLayout(QPointF(cx + n.x * s - 4, cy - n.y * s + 6), &layout);
        }
    }
    qp::StageStats::instance().add(QStringLiteral("overlay_labels_ms"), double(qp::nowNs() - tl) / 1e6);
    qp::StageStats::instance().add(QStringLiteral("overlay_sync_ms"), double(qp::nowNs() - t0) / 1e6);
    return root;
}
