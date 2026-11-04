#include "PointCloudItem.h"

#include "qp/FrameMonitor.h"
#include "qp/StageStats.h"

#include <QPainter>

PointCloudItem::PointCloudItem(QQuickItem *parent) : QQuickPaintedItem(parent) {}

void PointCloudItem::setStore(FrameStore *store)
{
    if (m_store == store)
        return;
    if (m_store)
        m_store->disconnect(this);
    m_store = store;
    if (m_store)
        connect(m_store, &FrameStore::lidarChanged, this, [this] { update(); });
    update();
    emit storeChanged();
}

void PointCloudItem::setPxPerMetre(qreal v)
{
    if (qFuzzyCompare(m_pxPerMetre, v))
        return;
    m_pxPerMetre = v;
    update();
    emit pxPerMetreChanged();
}

// Render thread; the GUI thread is blocked, so reading the store is safe.
void PointCloudItem::paint(QPainter *painter)
{
    if (!m_store)
        return;
    const int64_t t0 = qp::nowNs();

    static const QColor colors[qp::kMaxSensors] = {QColor(0x38BDF8), QColor(0x34D399), QColor(0xFBBF24), QColor(0xF472B6), QColor(0xA78BFA), QColor(0xFB923C), QColor(0x2DD4BF), QColor(0xE879F9)};
    const qreal cx = width() / 2;
    const qreal cy = height() / 2;
    const qreal s = m_pxPerMetre;
    int64_t newest = 0;

    for (int k = 0; k < m_store->sensorCount(); ++k) {
        const qp::SensorFrame &f = m_store->latestLidar(k);
        if (!f.cloud)
            continue;
        newest = std::max(newest, f.captureNs);
        const QColor &c = colors[k % qp::kMaxSensors];
        const std::vector<float> &xyz = f.cloud->xyz;
        for (size_t i = 0; i + 2 < xyz.size(); i += 3)
            painter->fillRect(QRectF(cx + xyz[i] * s, cy - xyz[i + 1] * s, 1.5, 1.5), c);
    }

    qp::StageStats::instance().add(QStringLiteral("paint_ms"), double(qp::nowNs() - t0) / 1e6);
    if (newest > 0)
        qp::markContentCaptured(newest);
}
