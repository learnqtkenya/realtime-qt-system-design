#include "LidarGeometry.h"

#include "qp/FrameMonitor.h"
#include "qp/StageStats.h"

LidarGeometry::LidarGeometry()
{
    setStride(12);
    setPrimitiveType(PrimitiveType::Points);
    addAttribute(Attribute::PositionSemantic, 0, Attribute::F32Type);
    setBounds(QVector3D(-60, -60, -5), QVector3D(60, 60, 5)); // the simulator's world, so nothing is culled
}

void LidarGeometry::setMailbox(LidarMailbox *mailbox)
{
    if (m_mailbox == mailbox)
        return;
    if (m_mailbox)
        m_mailbox->disconnect(this);
    m_mailbox = mailbox;
    if (m_mailbox)
        connect(m_mailbox, &LidarMailbox::changed, this, [this](int sensor) {
            if (sensor == m_sensor)
                refresh();
        });
    m_serial = 0;
    refresh();
    emit mailboxChanged();
}

void LidarGeometry::setSensor(int sensor)
{
    if (m_sensor == sensor)
        return;
    m_sensor = sensor;
    m_serial = 0;
    refresh();
    emit sensorChanged();
}

void LidarGeometry::refresh()
{
    if (!m_mailbox)
        return;
    const LidarMailbox::Entry e = m_mailbox->latest(m_sensor);
    if (e.serial == m_serial || !e.frame.cloud)
        return;
    const int64_t t0 = qp::nowNs();
    const std::vector<float> &xyz = e.frame.cloud->xyz;
    setVertexData(QByteArray(reinterpret_cast<const char *>(xyz.data()), qsizetype(xyz.size() * sizeof(float))));
    update();
    m_serial = e.serial;
    qp::StageStats::instance().add(QStringLiteral("q3d_setdata_ms"), double(qp::nowNs() - t0) / 1e6);
    qp::StageStats::instance().add(QStringLiteral("uploaded"), 1);
    qp::markContentCaptured(e.frame.captureNs);
}
