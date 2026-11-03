#include "InputRobot.h"

#include "qp/SimTypes.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QWindow>

#include <cmath>

InputRobot::InputRobot(QWindow *window, Mode mode, QPointF centre, std::function<QPointF()> target,
                       QObject *parent)
    : QObject(parent), m_window(window), m_mode(mode), m_centre(centre), m_target(std::move(target))
{
    connect(&m_timer, &QTimer::timeout, this, &InputRobot::tick);
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.start(mode == Mode::Orbit ? 16 : 200);
}

void InputRobot::post(int type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    if (!m_window)
        return;
    auto *e = new QMouseEvent(QEvent::Type(type), pos, pos, m_window->mapToGlobal(pos), button, buttons,
                              Qt::NoModifier, QPointingDevice::primaryPointingDevice());
    e->setTimestamp(quint64(qp::nowNs() / 1'000'000));
    QCoreApplication::postEvent(m_window, e);
}

void InputRobot::tick()
{
    ++m_step;
    if (m_mode == Mode::Orbit) {
        // A slow circle while the button is held; release and press again every 3 s.
        const double a = m_step * 0.05;
        const QPointF p = m_centre + QPointF(80 * std::cos(a), 40 * std::sin(a));
        if (m_step % 180 == 1)
            post(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
        else if (m_step % 180 == 0)
            post(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
        else
            post(QEvent::MouseMove, p, Qt::NoButton, Qt::LeftButton);
        return;
    }
    // Click: press on even beats, release on odd beats, on whatever target() offers.
    if (m_step % 2 == 1) {
        m_pressed = m_target ? m_target() : QPointF();
        if (!m_pressed.isNull())
            post(QEvent::MouseButtonPress, m_pressed, Qt::LeftButton, Qt::LeftButton);
    } else if (!m_pressed.isNull()) {
        post(QEvent::MouseButtonRelease, m_pressed, Qt::LeftButton, Qt::NoButton);
    }
}
