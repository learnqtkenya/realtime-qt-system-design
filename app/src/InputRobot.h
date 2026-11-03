#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QTimer>

#include <functional>

class QWindow;

// Posts synthetic mouse input with our monotonic clock as the event timestamp, so
// input-to-photon latency can be measured without a human.
class InputRobot : public QObject
{
    Q_OBJECT

public:
    enum class Mode { Orbit, Click, Scrub };

    // target() returns where to click (window coordinates), or a null point to skip a beat.
    // Scrub drags back and forth through centre ± span horizontally.
    InputRobot(QWindow *window, Mode mode, QPointF centre, std::function<QPointF()> target = {},
               qreal span = 0, QObject *parent = nullptr);

private:
    void tick();
    void post(int type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons);

    QPointer<QWindow> m_window;
    Mode m_mode;
    QPointF m_centre;
    qreal m_span;
    std::function<QPointF()> m_target;
    QTimer m_timer;
    int m_step = 0;
    QPointF m_pressed;
};
