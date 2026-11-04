#pragma once

#include "qp/SimTypes.h"

#include <QObject>
#include <QQmlEngine>
#include <QThread>
#include <QTimer>
#include <QVariantList>

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>

// Watches every stream, and the GUI thread itself, from its own thread: a stalled GUI can't
// report its own stall. Ingest paths report arrivals and rejections; every 20 ms each stream
// gets a status.
class HealthMonitor : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Health)
    QML_SINGLETON
    QML_UNCREATABLE("Provided by main() via QQmlEngine::setExternalSingletonInstance")

    Q_PROPERTY(QVariantList streams READ streams NOTIFY changed FINAL)

public:
    enum class Status { Off, Ok, Degraded, Late, Corrupt, Stale };
    Q_ENUM(Status)

    explicit HealthMonitor(QObject *parent = nullptr);
    ~HealthMonitor() override;

    // Before start(). periodMs: expected time between samples; latencyBudgetMs: capture to arrival.
    void configure(int stream, const QString &name, double periodMs, double latencyBudgetMs);
    void setPeriod(int stream, double periodMs);
    void start();
    // Called (health thread) when a stream changes state.
    void setStatusSink(std::function<void(int stream, Status from, Status to, const QString &why)> sink)
    {
        m_sink = std::move(sink);
    }

    // Any thread.
    void arrived(int stream, quint64 seq, qint64 captureNs);
    void rejected(int stream);
    void noteFault(int stream, const QString &kind);
    // A new source session (e.g. a reconnect): sequence numbers start again; don't call that loss.
    void resetSequences();

    QVariantList streams() const { return m_snapshot; } // GUI thread

    static QString statusName(Status s);
    // Set by configure() before start(); any thread after that.
    QString streamName(int stream) const { return stream >= 0 && stream < qp::kStreamCount ? m_streams[size_t(stream)].name : QString(); }

signals:
    void changed();

private:
    struct Stream
    {
        QString name;
        std::atomic<double> periodMs{100};
        double latencyBudgetMs = 100;
        bool configured = false;
        std::atomic<qint64> lastArrivalNs{0};
        std::atomic<quint64> lastSeq{0};
        std::atomic<quint64> arrivals{0}, gaps{0}, rejected{0}, reordered{0};
        std::atomic<qint64> latencySumNs{0};
        std::atomic<bool> newSession{false};
        // health thread only
        struct Tick
        {
            qint64 ns;
            quint64 arrivals, gaps, rejected, reordered;
            qint64 latencySumNs;
        };
        std::deque<Tick> window;
        Status status = Status::Off;
        Status candidate = Status::Off;
        qint64 candidateSinceNs = 0;
        QString why;
        double hz = 0, dropPct = 0, latencyMs = 0, ageMs = 0;
        quint64 rejectedTotal = 0, reorderedTotal = 0;
        quint32 seenWhileFaulted = 0; // bit per Status reached since the fault was injected
    };

    void evaluate();

    std::array<Stream, qp::kStreamCount> m_streams;
    QThread m_thread;
    QTimer *m_timer = nullptr; // lives on m_thread
    QTimer m_heartbeat;        // GUI thread: feeds the GUI stream
    QTimer m_publish;          // GUI thread: snapshot for QML
    std::function<void(int, Status, Status, const QString &)> m_sink;

    std::mutex m_mutex; // guards m_faultKind, m_faultNs and m_rows
    std::array<QString, qp::kStreamCount> m_faultKind;
    std::array<qint64, qp::kStreamCount> m_faultNs{};
    std::array<bool, qp::kStreamCount> m_faultReset{};
    QVariantList m_rows;
    QVariantList m_snapshot;
};
