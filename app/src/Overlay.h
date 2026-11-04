#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>
#include <vector>

// What a layer draws: line segments and labels in world metres (x right, y up), for the tracks
// captured at t. Plugins of every kind produce this; the viewer draws it.
struct OverlayFrame
{
    struct Segment
    {
        float x1, y1, x2, y2;
        quint32 rgba;
    };
    struct Label
    {
        float x, y;
        quint32 rgba;
        QString text;
    };
    qint64 t = 0;
    std::vector<Segment> segments;
    std::vector<Label> labels;
};
using OverlayFramePtr = std::shared_ptr<const OverlayFrame>;

namespace overlay {

constexpr quint16 kVersion = 1;
constexpr size_t kMaxSegments = 50'000;
constexpr size_t kMaxLabels = 1'000;

// Version 1: u32 segments, u16 stride, {f32 x1 y1 x2 y2, u32 rgba}...; u32 labels,
// {u16 size, f32 x y, u32 rgba, u16 bytes, utf8}... Newer versions append fields.
QByteArray encode(const OverlayFrame &frame);
// nullptr: malformed, or over the limits.
OverlayFramePtr decode(quint16 version, qint64 t, const char *data, size_t size);

quint32 parseColor(const QString &s); // "#rgb", "#rrggbb", "#aarrggbb", names; 0 if invalid

} // namespace overlay

// The `draw` object a script plugin's update() receives.
class OverlayBuilder : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    Q_INVOKABLE void color(const QString &c);
    Q_INVOKABLE void line(double x1, double y1, double x2, double y2);
    Q_INVOKABLE void circle(double x, double y, double r);
    Q_INVOKABLE void text(double x, double y, const QString &s);

    void reset();
    OverlayFramePtr take(qint64 t);
    quint32 overLimit() const { return m_overLimit; }

private:
    std::shared_ptr<OverlayFrame> m_frame = std::make_shared<OverlayFrame>();
    QString m_colorName;
    quint32 m_rgba = 0xFFFFFFFF;
    quint32 m_overLimit = 0;
};
