#include "PointViews.h"

#include "qp/FrameMonitor.h"
#include "qp/StageStats.h"

#include <QFile>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGRenderNode>

#include <rhi/qrhi.h>

#include <cstring>
#include <memory>

namespace {

const QColor kColors[4] = {QColor(0x38BDF8), QColor(0x34D399), QColor(0xFBBF24), QColor(0xF472B6)};

// std140 layout shared by points.vert / points.frag.
constexpr int kOffMvp = 0, kOffColor = 64, kOffXform = 80, kOffOpacity = 96, kUniformSize = 112;

void writeColorAndXform(char *dst, const QColor &c, const QVector4D &xform)
{
    const float color[4] = {c.redF(), c.greenF(), c.blueF(), c.alphaF()};
    const float x[4] = {xform.x(), xform.y(), xform.z(), xform.w()};
    std::memcpy(dst + kOffColor, color, sizeof color);
    std::memcpy(dst + kOffXform, x, sizeof x);
}

double msSince(int64_t t0)
{
    return double(qp::nowNs() - t0) / 1e6;
}

// ---- sg ---------------------------------------------------------------------

class PointMaterial : public QSGMaterial
{
public:
    QColor color;
    QVector4D xform; // px per metre, centre x, centre y, point size (logical px)

    QSGMaterialType *type() const override
    {
        static QSGMaterialType t;
        return &t;
    }
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial *other) const override
    {
        const auto *o = static_cast<const PointMaterial *>(other);
        if (color.rgba() != o->color.rgba())
            return color.rgba() < o->color.rgba() ? -1 : 1;
        for (int i = 0; i < 4; ++i)
            if (xform[i] != o->xform[i])
                return xform[i] < o->xform[i] ? -1 : 1;
        return 0;
    }
};

class PointShader : public QSGMaterialShader
{
public:
    PointShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/m04/shaders/points.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/m04/shaders/points.frag.qsb"));
    }

    bool updateUniformData(RenderState &state, QSGMaterial *newMaterial, QSGMaterial *) override
    {
        QByteArray *buf = state.uniformData();
        Q_ASSERT(buf->size() >= kOffOpacity + 4);
        if (state.isMatrixDirty()) {
            const QMatrix4x4 m = state.combinedMatrix();
            std::memcpy(buf->data() + kOffMvp, m.constData(), 64);
        }
        const auto *mat = static_cast<PointMaterial *>(newMaterial);
        QVector4D xform = mat->xform;
        xform.setW(xform.w() * state.devicePixelRatio());
        writeColorAndXform(buf->data(), mat->color, xform);
        if (state.isOpacityDirty()) {
            const float opacity = state.opacity();
            std::memcpy(buf->data() + kOffOpacity, &opacity, 4);
        }
        return true;
    }
};

QSGMaterialShader *PointMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new PointShader;
}

// x,y of each interleaved x,y,z point; z is skipped by the 12-byte stride.
const QSGGeometry::AttributeSet &pointAttributes()
{
    static const QSGGeometry::Attribute attrs[] = {QSGGeometry::Attribute::createWithAttributeType(
        0, 2, QSGGeometry::FloatType, QSGGeometry::PositionAttribute)};
    static const QSGGeometry::AttributeSet set = {1, 12, attrs};
    return set;
}

class SgRoot : public QSGNode
{
public:
    std::vector<QSGGeometryNode *> nodes;
    std::vector<quint64> serials;
};

// ---- rhi --------------------------------------------------------------------

QShader loadShader(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("cannot open shader %s", qPrintable(path));
    QShader s = QShader::fromSerialized(f.readAll());
    if (!s.isValid())
        qFatal("invalid shader %s", qPrintable(path));
    return s;
}

class PointsRenderNode final : public QSGRenderNode
{
public:
    // ringSize > 1: each update goes to a buffer the GPU is not reading from.
    PointsRenderNode(QQuickWindow *window, LidarMailbox *mailbox, int ringSize)
        : m_window(window), m_mailbox(mailbox), m_sensors(size_t(mailbox->sensorCount()))
    {
        for (Sensor &s : m_sensors)
            s.ring.resize(size_t(std::max(1, ringSize)));
    }
    ~PointsRenderNode() override { releaseResources(); }

    LidarMailbox *mailbox() const { return m_mailbox; }
    int ringSize() const { return int(m_sensors.front().ring.size()); }

    // Render thread, GUI thread blocked.
    void setParams(const QVector4D &xform, float dpr)
    {
        m_xform = xform;
        m_dpr = dpr;
    }

    RenderingFlags flags() const override { return NoExternalRendering | DepthAwareRendering; }
    StateFlags changedStates() const override { return ViewportState; }

    void prepare() override
    {
        const int64_t t0 = qp::nowNs();
        QRhi *rhi = m_window->rhi();
        QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
        if (!m_pipeline)
            createPipeline(rhi);

        const QMatrix4x4 mvp = *projectionMatrix() * *matrix();
        const float opacity = inheritedOpacity();
        QVector4D xform = m_xform;
        xform.setW(xform.w() * m_dpr);
        int64_t newest = 0;

        for (size_t i = 0; i < m_sensors.size(); ++i) {
            Sensor &s = m_sensors[i];
            const LidarMailbox::Entry e = m_mailbox->latest(int(i));
            if (e.serial != s.serial && e.frame.cloud) {
                const quint32 bytes = quint32(e.frame.cloud->xyz.size() * sizeof(float));
                s.current = (s.current + 1) % int(s.ring.size());
                Slot &slot = s.ring[size_t(s.current)];
                if (!slot.vbuf || bytes > slot.capacity) {
                    slot.capacity = std::max<quint32>(bytes, 64 * 1024);
                    slot.vbuf.reset(rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::VertexBuffer, slot.capacity));
                    if (!slot.vbuf->create())
                        qFatal("cannot create vertex buffer");
                }
                if (bytes > 0)
                    u->uploadStaticBuffer(slot.vbuf.get(), 0, bytes, e.frame.cloud->xyz.data());
                s.count = quint32(e.frame.cloud->count());
                s.serial = e.serial;
                newest = std::max(newest, e.frame.captureNs);
                qp::StageStats::instance().add(QStringLiteral("uploaded"), 1);
            }
            char data[kUniformSize] = {};
            std::memcpy(data + kOffMvp, mvp.constData(), 64);
            writeColorAndXform(data, kColors[i & 3], xform);
            std::memcpy(data + kOffOpacity, &opacity, 4);
            u->updateDynamicBuffer(s.ubuf.get(), 0, kUniformSize, data);
        }
        commandBuffer()->resourceUpdate(u);
        qp::StageStats::instance().add(QStringLiteral("rhi_prepare_ms"), msSince(t0));
        if (newest > 0)
            qp::markContentCaptured(newest);
    }

    void render(const RenderState *) override
    {
        QRhiCommandBuffer *cb = commandBuffer();
        cb->setGraphicsPipeline(m_pipeline.get());
        const QSize size = renderTarget()->pixelSize();
        cb->setViewport(QRhiViewport(0, 0, float(size.width()), float(size.height())));
        for (const Sensor &s : m_sensors) {
            if (s.current < 0 || s.count == 0)
                continue;
            cb->setShaderResources(s.srb.get());
            const QRhiCommandBuffer::VertexInput input(s.ring[size_t(s.current)].vbuf.get(), 0);
            cb->setVertexInput(0, 1, &input);
            cb->draw(s.count);
        }
    }

    void releaseResources() override
    {
        m_pipeline.reset();
        for (Sensor &s : m_sensors) {
            s.srb.reset();
            s.ubuf.reset();
            for (Slot &slot : s.ring)
                slot = Slot{};
            s.current = -1;
            s.count = 0;
            s.serial = 0;
        }
    }

private:
    struct Slot
    {
        std::unique_ptr<QRhiBuffer> vbuf;
        quint32 capacity = 0;
    };
    struct Sensor
    {
        std::vector<Slot> ring;
        int current = -1;
        std::unique_ptr<QRhiBuffer> ubuf;
        std::unique_ptr<QRhiShaderResourceBindings> srb;
        quint32 count = 0;
        quint64 serial = 0;
    };

    void createPipeline(QRhi *rhi)
    {
        for (Sensor &s : m_sensors) {
            s.ubuf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUniformSize));
            s.ubuf->create();
            s.srb.reset(rhi->newShaderResourceBindings());
            s.srb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                s.ubuf.get())});
            s.srb->create();
        }
        m_pipeline.reset(rhi->newGraphicsPipeline());
        m_pipeline->setTopology(QRhiGraphicsPipeline::Points);
        m_pipeline->setDepthTest(true); // stacking comes from the projection matrix; no depth writes
        m_pipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/m04/shaders/points.vert.qsb"))},
             {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/m04/shaders/points.frag.qsb"))}});
        QRhiVertexInputLayout layout;
        layout.setBindings({{12}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}});
        m_pipeline->setVertexInputLayout(layout);
        m_pipeline->setShaderResourceBindings(m_sensors.front().srb.get());
        m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        m_pipeline->setSampleCount(renderTarget()->sampleCount());
        if (!m_pipeline->create())
            qFatal("cannot create pipeline");
    }

    QQuickWindow *m_window;
    LidarMailbox *m_mailbox;
    std::vector<Sensor> m_sensors;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    QVector4D m_xform;
    float m_dpr = 1;
};

} // namespace

// ---- items ------------------------------------------------------------------

PointViewBase::PointViewBase(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

void PointViewBase::setMailbox(LidarMailbox *mailbox)
{
    if (m_mailbox == mailbox)
        return;
    if (m_mailbox)
        m_mailbox->disconnect(this);
    m_mailbox = mailbox;
    if (m_mailbox)
        connect(m_mailbox, &LidarMailbox::changed, this, &QQuickItem::update);
    update();
    emit mailboxChanged();
}

void PointViewBase::setPxPerMetre(qreal v)
{
    if (qFuzzyCompare(m_pxPerMetre, v))
        return;
    m_pxPerMetre = v;
    update();
    emit pxPerMetreChanged();
}

void PointViewBase::setPointSize(qreal v)
{
    if (qFuzzyCompare(m_pointSize, v))
        return;
    m_pointSize = v;
    update();
    emit pointSizeChanged();
}

QSGNode *SgPointsItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    if (!m_mailbox) {
        delete old;
        return nullptr;
    }
    auto *root = static_cast<SgRoot *>(old);
    if (!root) {
        root = new SgRoot;
        for (int i = 0; i < m_mailbox->sensorCount(); ++i) {
            auto *node = new QSGGeometryNode;
            auto *geometry = new QSGGeometry(pointAttributes(), 0);
            geometry->setDrawingMode(QSGGeometry::DrawPoints);
            geometry->setVertexDataPattern(QSGGeometry::StreamPattern);
            node->setGeometry(geometry);
            node->setFlag(QSGNode::OwnsGeometry);
            auto *material = new PointMaterial;
            material->color = kColors[i & 3];
            node->setMaterial(material);
            node->setFlag(QSGNode::OwnsMaterial);
            root->appendChildNode(node);
            root->nodes.push_back(node);
            root->serials.push_back(0);
        }
    }

    const QVector4D xform(float(m_pxPerMetre), float(width() / 2), float(height() / 2), float(m_pointSize));
    int64_t newest = 0;
    for (size_t i = 0; i < root->nodes.size(); ++i) {
        QSGGeometryNode *node = root->nodes[i];
        auto *material = static_cast<PointMaterial *>(node->material());
        if (material->xform != xform) {
            material->xform = xform;
            node->markDirty(QSGNode::DirtyMaterial);
        }
        const LidarMailbox::Entry e = m_mailbox->latest(int(i));
        if (e.serial == root->serials[i] || !e.frame.cloud)
            continue;
        const int64_t t0 = qp::nowNs();
        const int n = e.frame.cloud->count();
        node->geometry()->allocate(n);
        std::memcpy(node->geometry()->vertexData(), e.frame.cloud->xyz.data(), size_t(n) * 12);
        node->markDirty(QSGNode::DirtyGeometry);
        root->serials[i] = e.serial;
        newest = std::max(newest, e.frame.captureNs);
        qp::StageStats::instance().add(QStringLiteral("sg_copy_ms"), msSince(t0));
        qp::StageStats::instance().add(QStringLiteral("uploaded"), 1);
    }
    if (newest > 0)
        qp::markContentCaptured(newest);
    return root;
}

void RhiPointsItem::setBufferRing(int n)
{
    n = std::clamp(n, 1, 8);
    if (m_bufferRing == n)
        return;
    m_bufferRing = n;
    update();
    emit bufferRingChanged();
}

QSGNode *RhiPointsItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    auto *node = static_cast<PointsRenderNode *>(old);
    if (!m_mailbox || (node && (node->mailbox() != m_mailbox || node->ringSize() != m_bufferRing))) {
        delete node;
        node = nullptr;
    }
    if (!m_mailbox)
        return nullptr;
    if (!node)
        node = new PointsRenderNode(window(), m_mailbox, m_bufferRing);
    node->setParams(QVector4D(float(m_pxPerMetre), float(width() / 2), float(height() / 2), float(m_pointSize)),
                    float(window()->effectiveDevicePixelRatio()));
    node->markDirty(QSGNode::DirtyMaterial);
    return node;
}
