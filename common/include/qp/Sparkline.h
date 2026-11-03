#pragma once

#include <QColor>
#include <QQmlEngine>
#include <QQuickItem>

namespace qp {

// A line chart drawn as one scene-graph node: no JavaScript, no Canvas.
class Sparkline : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QList<qreal> values READ values WRITE setValues NOTIFY valuesChanged FINAL)
    Q_PROPERTY(int capacity READ capacity WRITE setCapacity NOTIFY capacityChanged FINAL)
    // 0 = scale to the largest value (and the threshold).
    Q_PROPERTY(qreal maximum READ maximum WRITE setMaximum NOTIFY maximumChanged FINAL)
    // Negative = no threshold line.
    Q_PROPERTY(qreal threshold READ threshold WRITE setThreshold NOTIFY thresholdChanged FINAL)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged FINAL)
    Q_PROPERTY(QColor thresholdColor READ thresholdColor WRITE setThresholdColor NOTIFY thresholdColorChanged FINAL)
    Q_PROPERTY(qreal scaleMaximum READ scaleMaximum NOTIFY valuesChanged FINAL)

public:
    explicit Sparkline(QQuickItem *parent = nullptr);

    QList<qreal> values() const { return m_values; }
    void setValues(const QList<qreal> &v);
    int capacity() const { return m_capacity; }
    void setCapacity(int c);
    qreal maximum() const { return m_maximum; }
    void setMaximum(qreal m);
    qreal threshold() const { return m_threshold; }
    void setThreshold(qreal t);
    QColor color() const { return m_color; }
    void setColor(const QColor &c);
    QColor thresholdColor() const { return m_thresholdColor; }
    void setThresholdColor(const QColor &c);
    qreal scaleMaximum() const;

signals:
    void valuesChanged();
    void capacityChanged();
    void maximumChanged();
    void thresholdChanged();
    void colorChanged();
    void thresholdColorChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;

private:
    QList<qreal> m_values;
    int m_capacity = 60;
    qreal m_maximum = 0;
    qreal m_threshold = -1;
    QColor m_color = QColor(0x38BDF8);
    QColor m_thresholdColor = QColor(0xF87171);
};

} // namespace qp
