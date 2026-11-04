#pragma once

#include "qp/SimTypes.h"

#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QThread>

#include <functional>
#include <mutex>

// Serves the live streams to other viewers over TCP (wire.h). Its own thread: encoding, socket
// writes and slow clients never touch the GUI thread or the simulator.
class StreamServer : public QObject
{
    Q_OBJECT

public:
    // All: write every record to every client as it arrives. Latest: per client, the newest record
    // per stream waits until the socket's backlog drains; events wait in a bounded queue.
    enum class Policy { All, Latest };

    // highWater: a client's backlog (bytes) before records are held back; sendBuffer: the kernel's
    // send buffer per client (0: the system's choice).
    StreamServer(quint16 port, Policy policy, int zstdLevel, const QJsonObject &info, qint64 highWater = 1 << 20,
                 int sendBuffer = 0, QObject *parent = nullptr);
    ~StreamServer() override;

    bool start(QString *error);

    // Any thread.
    void publishLidar(const qp::SensorFrame &frame);
    void publishTracks(const qp::TrackSetPtr &set);
    void publishEvents(const qp::EventBatchPtr &batch);

    // One line per client, refreshed every second (any thread).
    QStringList status() const;

private:
    class Worker;
    QThread m_thread;
    Worker *m_worker = nullptr;
    quint16 m_port;
    mutable std::mutex m_mutex;
    QStringList m_status;
    friend class Worker;
};

// Connects to a StreamServer and hands what arrives to the sinks, on its own thread.
class StreamClient : public QObject
{
    Q_OBJECT

public:
    struct Sinks
    {
        std::function<void(const qp::SensorFrame &)> lidar;
        std::function<void(const qp::TrackSetPtr &)> tracks;
        std::function<void(const qp::EventBatchPtr &)> events;
        std::function<void()> session; // a hello arrived: a new session (sequence numbers restart)
    };
    // readMbps > 0: read no faster than this (a slow link or a slow machine).
    // receiveBuffer: the kernel's receive buffer (0: the system's choice).
    StreamClient(const QString &host, quint16 port, double readMbps, int receiveBuffer, Sinks sinks,
                 QObject *parent = nullptr);
    ~StreamClient() override;

    void start();
    QString status() const;

private:
    class Worker;
    QThread m_thread;
    Worker *m_worker = nullptr;
    mutable std::mutex m_mutex;
    QString m_status;
    friend class Worker;
};

// What the lab panel shows about the network (refreshed by main every second).
class NetworkStatus : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Network)
    QML_SINGLETON
    QML_UNCREATABLE("Provided by main() via QQmlEngine::setExternalSingletonInstance")

    Q_PROPERTY(QString role READ role CONSTANT FINAL)
    Q_PROPERTY(QStringList lines READ lines NOTIFY linesChanged FINAL)

public:
    explicit NetworkStatus(const QString &role, QObject *parent = nullptr) : QObject(parent), m_role(role) {}
    QString role() const { return m_role; }
    QStringList lines() const { return m_lines; }
    void setLines(const QStringList &lines)
    {
        if (lines == m_lines)
            return;
        m_lines = lines;
        emit linesChanged();
    }

signals:
    void linesChanged();

private:
    QString m_role;
    QStringList m_lines;
};
