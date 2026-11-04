#pragma once

#include "Plugins.h"

#include <QThread>

// update(input, draw) in an engine of its own: a JavaScript module (.mjs, a plain JS engine) or a
// QML object (.qml, a QML engine). On its own thread (default) or on the GUI thread. The viewer times every call; the watchdog interrupts one that runs too long.
class ScriptLayer : public Layer
{
    Q_OBJECT

public:
    ScriptLayer(const PluginManifest &m, PluginHost *host);
    ~ScriptLayer() override;

    void start() override;
    void stop() override;
    void feed(const qp::TrackSetPtr &tracks, int selectedId, const QByteArray &) override;
    QString cost() const override;

    class Runner;

    // From the runner, GUI thread.
    void finished(const OverlayFramePtr &frame, double updateMs, double marshalMs, const QString &error,
                  bool interrupted, quint32 overLimit);
    void loaded(const QString &error, const QString &where);

private:
    std::shared_ptr<PluginHost::Watched> m_watched;
    quint64 m_generation = 0; // results from a runner that has since been replaced are ignored
    std::unique_ptr<QThread> m_thread;
    Runner *m_runner = nullptr;
    struct Input
    {
        std::mutex mutex;
        qp::TrackSetPtr tracks;
        int selectedId = -1;
        bool scheduled = false;
        bool halted = false; // interrupted: no more calls into this engine
        quint64 skipped = 0, fed = 0;
    };
    std::shared_ptr<Input> m_input;
    int m_overBudget = 0;
    double m_lastUpdateMs = 0, m_lastMarshalMs = 0;
    quint64 m_skippedSeen = 0, m_fedSeen = 0;
    qint64 m_windowStart = 0;
    double m_skipShare = 0;
    QString m_where;
};
