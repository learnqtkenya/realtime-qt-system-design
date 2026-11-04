#include "Overlay.h"

#include "Codec.h"

#include <QColor>

#include <cmath>

namespace overlay {

namespace {
constexpr quint16 kSegmentStride = 20;
}

QByteArray encode(const OverlayFrame &frame)
{
    QByteArray out;
    out.reserve(qsizetype(10 + frame.segments.size() * kSegmentStride + frame.labels.size() * 32));
    rec::ByteWriter w(out);
    w.put<quint32>(quint32(frame.segments.size()));
    w.put<quint16>(kSegmentStride);
    for (const auto &s : frame.segments) {
        w.put<float>(s.x1);
        w.put<float>(s.y1);
        w.put<float>(s.x2);
        w.put<float>(s.y2);
        w.put<quint32>(s.rgba);
    }
    w.put<quint32>(quint32(frame.labels.size()));
    for (const auto &l : frame.labels) {
        const QByteArray u = l.text.toUtf8().left(256);
        w.put<quint16>(quint16(14 + u.size()));
        w.put<float>(l.x);
        w.put<float>(l.y);
        w.put<quint32>(l.rgba);
        w.put<quint16>(quint16(u.size()));
        w.bytes(u.constData(), size_t(u.size()));
    }
    return out;
}

OverlayFramePtr decode(quint16 version, qint64 t, const char *data, size_t size)
{
    if (version < 1)
        return nullptr;
    rec::ByteReader r(data, size);
    auto f = std::make_shared<OverlayFrame>();
    f->t = t;
    const quint32 n = r.get<quint32>();
    const quint16 stride = r.get<quint16>();
    if (!r.ok() || n > kMaxSegments || stride < kSegmentStride || r.left() < size_t(n) * stride)
        return nullptr;
    f->segments.resize(n);
    for (auto &s : f->segments) {
        rec::ByteReader item(r.take(stride), stride);
        s.x1 = item.get<float>();
        s.y1 = item.get<float>();
        s.x2 = item.get<float>();
        s.y2 = item.get<float>();
        s.rgba = item.get<quint32>();
        if (!std::isfinite(s.x1) || !std::isfinite(s.y1) || !std::isfinite(s.x2) || !std::isfinite(s.y2))
            return nullptr;
    }
    const quint32 m = r.get<quint32>();
    if (!r.ok() || m > kMaxLabels)
        return nullptr;
    f->labels.resize(m);
    for (auto &l : f->labels) {
        const quint16 itemSize = r.get<quint16>();
        const char *p = r.take(itemSize);
        if (!p || itemSize < 14)
            return nullptr;
        rec::ByteReader item(p, itemSize);
        l.x = item.get<float>();
        l.y = item.get<float>();
        l.rgba = item.get<quint32>();
        const quint16 bytes = item.get<quint16>();
        const char *text = item.take(bytes);
        if (!text || !std::isfinite(l.x) || !std::isfinite(l.y))
            return nullptr;
        l.text = QString::fromUtf8(text, bytes);
    }
    return r.ok() ? f : nullptr;
}

quint32 parseColor(const QString &s)
{
    const QColor c = QColor::fromString(s);
    return c.isValid() ? c.rgba() : 0;
}

} // namespace overlay

void OverlayBuilder::color(const QString &c)
{
    if (c == m_colorName)
        return;
    m_colorName = c;
    m_rgba = overlay::parseColor(c);
}

void OverlayBuilder::line(double x1, double y1, double x2, double y2)
{
    if (m_frame->segments.size() >= overlay::kMaxSegments || !std::isfinite(x1 + y1 + x2 + y2)) {
        ++m_overLimit;
        return;
    }
    m_frame->segments.push_back({float(x1), float(y1), float(x2), float(y2), m_rgba});
}

void OverlayBuilder::circle(double x, double y, double r)
{
    constexpr int kSides = 48;
    double px = x + r, py = y;
    for (int i = 1; i <= kSides; ++i) {
        const double a = 2 * M_PI * i / kSides;
        const double nx = x + r * std::cos(a), ny = y + r * std::sin(a);
        line(px, py, nx, ny);
        px = nx;
        py = ny;
    }
}

void OverlayBuilder::text(double x, double y, const QString &s)
{
    if (m_frame->labels.size() >= overlay::kMaxLabels || !std::isfinite(x + y)) {
        ++m_overLimit;
        return;
    }
    m_frame->labels.push_back({float(x), float(y), m_rgba, s.left(256)});
}

void OverlayBuilder::reset()
{
    m_frame = std::make_shared<OverlayFrame>();
    m_overLimit = 0;
}

OverlayFramePtr OverlayBuilder::take(qint64 t)
{
    m_frame->t = t;
    OverlayFramePtr out = std::move(m_frame);
    m_frame = std::make_shared<OverlayFrame>();
    return out;
}
