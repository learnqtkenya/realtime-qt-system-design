#pragma once

#include "Plugins.h"

#include <QThread>

// An executable, in any language, started by the viewer. It reads a hello and tracks records
// on stdin (the step 7 wire) and answers each tracks record with one overlay record on stdout.
// It is restarted when it crashes, and killed and restarted when it stops answering.
class ProcessLayer : public Layer
{
    Q_OBJECT

public:
    ProcessLayer(const PluginManifest &m, PluginHost *host);
    ~ProcessLayer() override;

    void start() override;
    void stop() override;
    void feed(const qp::TrackSetPtr &tracks, int selectedId, const QByteArray &tracksRecord) override;
    void selectionChanged(int id) override;
    QString cost() const override;

    class Worker;

private:
    QThread m_thread;
    Worker *m_worker = nullptr;
    quint64 m_generation = 0;
    double m_cpuPct = 0;
    int m_restarts = 0;
    qint64 m_rejected = 0, m_skipped = 0;
};
