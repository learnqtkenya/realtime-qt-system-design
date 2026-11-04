#include "StreamServer.h"

#include "Wire.h"
#include "qp/StageStats.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <deque>
#include <map>
#include <memory>

namespace {

constexpr size_t kEventQueue = 200;          // event batches waiting per client: 10 s at 20 a second

} // namespace

// ---- server -------------------------------------------------------------------------------------

class StreamServer::Worker : public QObject
{
    Q_OBJECT

public:
    Worker(StreamServer *owner, Policy policy, int zstd, const QJsonObject &info, qint64 highWater, int sendBuffer)
        : m_owner(owner), m_policy(policy), m_zstd(zstd), m_hello(wire::hello(info)), m_highWater(highWater),
          m_sendBuffer(sendBuffer)
    {}

    struct Session
    {
        QTcpSocket *socket = nullptr;
        QString peer;
        struct Held
        {
            qint64 t;
            QByteArray record;
        };
        std::map<std::pair<int, int>, Held> latest; // (type, stream) -> newest record not yet sent
        std::deque<QByteArray> events;
        qint64 sent = 0, sentAtLastTick = 0;
        qint64 replaced = 0, eventsDropped = 0, backlogMax = 0;
    };

    void listen(quint16 port, bool *ok, QString *error)
    {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &Worker::accept);
        *ok = m_server->listen(QHostAddress::Any, port);
        if (!*ok)
            *error = m_server->errorString();
        auto *tick = new QTimer(this);
        connect(tick, &QTimer::timeout, this, &Worker::second);
        tick->start(1000);
    }

    void publish(rec::Type type, int stream, qint64 t, const std::function<QByteArray()> &encode)
    {
        if (m_sessions.empty())
            return; // nobody listening: don't even encode
        const qint64 t0 = qp::nowNs();
        const QByteArray record = encode(); // once, for every client
        if (type == rec::Type::Lidar)
            qp::StageStats::instance().add(QStringLiteral("server_encode_ms"), double(qp::nowNs() - t0) / 1e6);
        for (const auto &s : m_sessions) {
            if (m_policy == Policy::All) {
                s->socket->write(record);
                s->sent += record.size();
            } else if (type == rec::Type::Events) {
                s->events.push_back(record);
                if (s->events.size() > kEventQueue) {
                    s->events.pop_front();
                    ++s->eventsDropped;
                }
                pump(*s);
            } else {
                auto [it, fresh] = s->latest.insert_or_assign({int(type), stream}, Session::Held{t, record});
                if (!fresh)
                    ++s->replaced;
                pump(*s);
            }
            s->backlogMax = std::max(s->backlogMax, s->socket->bytesToWrite());
        }
    }

private:
    void accept()
    {
        while (QTcpSocket *socket = m_server->nextPendingConnection()) {
            auto s = std::make_shared<Session>();
            s->socket = socket;
            s->peer = QStringLiteral("%1:%2").arg(socket->peerAddress().toString()).arg(socket->peerPort());
            socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
            if (m_sendBuffer > 0) // the kernel's buffer is a queue too
                socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, m_sendBuffer);
            socket->write(m_hello);
            connect(socket, &QTcpSocket::bytesWritten, this, [this, w = std::weak_ptr(s)] {
                if (auto s = w.lock())
                    pump(*s);
            });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                std::erase_if(m_sessions, [socket](const auto &s) { return s->socket == socket; });
                socket->deleteLater();
            });
            m_sessions.push_back(s);
        }
    }

    // Writes held records while the socket's backlog is small: TCP's own flow control, used as the
    // client's credit.
    void pump(Session &s)
    {
        while (s.socket->bytesToWrite() < m_highWater) {
            if (!s.events.empty()) {
                s.sent += s.socket->write(s.events.front());
                s.events.pop_front();
                continue;
            }
            if (s.latest.empty())
                break;
            // Oldest capture first, so no stream starves when the link is short of bandwidth.
            auto oldest = std::min_element(s.latest.begin(), s.latest.end(),
                                           [](const auto &a, const auto &b) { return a.second.t < b.second.t; });
            s.sent += s.socket->write(oldest->second.record);
            s.latest.erase(oldest);
        }
    }

    void second()
    {
        QStringList lines;
        qint64 backlog = 0;
        for (const auto &s : m_sessions) {
            const double mbps = double(s->sent - s->sentAtLastTick) / 1e6;
            s->sentAtLastTick = s->sent;
            backlog = std::max(backlog, s->backlogMax);
            lines << QStringLiteral("%1 · %2 MB/s · backlog %3 MB · %4 replaced · %5 events dropped")
                         .arg(s->peer)
                         .arg(mbps, 0, 'f', 1)
                         .arg(double(s->socket->bytesToWrite()) / 1e6, 0, 'f', 1)
                         .arg(s->replaced)
                         .arg(s->eventsDropped);
            qp::StageStats::instance().add(QStringLiteral("server_client_mbps"), mbps);
            qp::StageStats::instance().add(QStringLiteral("server_backlog_mb"), double(s->socket->bytesToWrite()) / 1e6);
            s->backlogMax = 0;
        }
        std::lock_guard lk(m_owner->m_mutex);
        m_owner->m_status = lines;
    }

    StreamServer *m_owner;
    Policy m_policy;
    int m_zstd;
    QByteArray m_hello;
    qint64 m_highWater; // a client's backlog before records are held back
    int m_sendBuffer;
    QTcpServer *m_server = nullptr;
    std::vector<std::shared_ptr<Session>> m_sessions;

public:
    int zstd() const { return m_zstd; }
};

StreamServer::StreamServer(quint16 port, Policy policy, int zstdLevel, const QJsonObject &info, qint64 highWater,
                           int sendBuffer, QObject *parent)
    : QObject(parent), m_port(port)
{
    m_thread.setObjectName(QStringLiteral("StreamServer"));
    m_worker = new Worker(this, policy, zstdLevel, info, highWater, sendBuffer);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
}

StreamServer::~StreamServer()
{
    m_thread.quit();
    m_thread.wait();
}

bool StreamServer::start(QString *error)
{
    m_thread.start();
    bool ok = false;
    QString why;
    QMetaObject::invokeMethod(m_worker, [this, &ok, &why] { m_worker->listen(m_port, &ok, &why); },
                              Qt::BlockingQueuedConnection);
    if (!ok && error)
        *error = why;
    return ok;
}

void StreamServer::publishLidar(const qp::SensorFrame &frame)
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker, frame] {
        w->publish(rec::Type::Lidar, frame.sensorId, frame.captureNs, [&] { return wire::lidarRecord(frame, w->zstd()); });
    });
}

void StreamServer::publishTracks(const qp::TrackSetPtr &set)
{
    if (set)
        QMetaObject::invokeMethod(m_worker, [w = m_worker, set] {
            w->publish(rec::Type::Tracks, 0, set->captureNs, [&] { return wire::tracksRecord(set); });
        });
}

void StreamServer::publishEvents(const qp::EventBatchPtr &batch)
{
    if (batch && !batch->empty())
        QMetaObject::invokeMethod(m_worker, [w = m_worker, batch] {
            w->publish(rec::Type::Events, 0, batch->front().captureNs, [&] { return wire::eventsRecord(batch); });
        });
}

QStringList StreamServer::status() const
{
    std::lock_guard lk(m_mutex);
    return m_status;
}

// ---- client -------------------------------------------------------------------------------------

class StreamClient::Worker : public QObject
{
    Q_OBJECT

public:
    Worker(StreamClient *owner, const QString &host, quint16 port, double readMbps, int receiveBuffer, Sinks sinks)
        : m_owner(owner), m_host(host), m_port(port), m_readMbps(readMbps), m_receiveBuffer(receiveBuffer),
          m_sinks(std::move(sinks))
    {}

    void begin()
    {
        m_socket = new QTcpSocket(this);
        // Small, so a slow reader pushes back through TCP instead of buffering here without end.
        m_socket->setReadBufferSize(256 << 10);
        if (m_receiveBuffer > 0)
            m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, m_receiveBuffer);
        connect(m_socket, &QTcpSocket::connected, this, [this] {
            qp::StageStats::instance().add(QStringLiteral("client_connected"), 1);
            m_backoffMs = 250;
            m_parser = wire::Parser();
            m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        });
        connect(m_socket, &QTcpSocket::disconnected, this, &Worker::retry);
        connect(m_socket, &QTcpSocket::errorOccurred, this, [this] {
            if (m_socket->state() != QAbstractSocket::ConnectedState)
                retry();
        });
        connect(m_socket, &QTcpSocket::readyRead, this, [this] {
            if (m_readMbps <= 0)
                read(m_socket->bytesAvailable());
        });
        if (m_readMbps > 0) {
            auto *pace = new QTimer(this);
            pace->setTimerType(Qt::PreciseTimer);
            connect(pace, &QTimer::timeout, this, [this] { read(qint64(m_readMbps * 1e6 / 100)); });
            pace->start(10);
        }
        auto *tick = new QTimer(this);
        connect(tick, &QTimer::timeout, this, &Worker::second);
        tick->start(1000);
        m_socket->connectToHost(m_host, m_port);
    }

private:
    void retry()
    {
        if (m_retrying)
            return;
        m_retrying = true;
        QTimer::singleShot(m_backoffMs, this, [this] {
            m_retrying = false;
            if (m_socket->state() == QAbstractSocket::UnconnectedState)
                m_socket->connectToHost(m_host, m_port);
        });
        m_backoffMs = std::min(m_backoffMs * 2, 2000);
        ++m_reconnects;
    }

    void read(qint64 budget)
    {
        while (budget > 0 && m_socket->bytesAvailable() > 0) {
            const QByteArray chunk = m_socket->read(std::min<qint64>(budget, 64 << 10));
            if (chunk.isEmpty())
                break;
            budget -= chunk.size();
            m_bytes += chunk.size();
            const bool hadHello = m_parser.hasHello();
            const bool ok = m_parser.feed(chunk.constData(), chunk.size(), [this](const wire::Decoded &d) {
                ++m_records;
                switch (d.type) {
                case rec::Type::Lidar: m_sinks.lidar(d.lidar); break;
                case rec::Type::Tracks: m_sinks.tracks(d.tracks); break;
                case rec::Type::Events: m_sinks.events(d.events); break;
                default: break;
                }
            });
            if (!ok) {
                m_socket->abort(); // not our protocol, or broken: start again
                retry();
                return;
            }
            if (!hadHello && m_parser.hasHello() && m_sinks.session)
                m_sinks.session();
        }
    }

    void second()
    {
        const double mbps = double(m_bytes - m_bytesAtTick) / 1e6;
        m_bytesAtTick = m_bytes;
        qp::StageStats::instance().add(QStringLiteral("client_mbps"), mbps);
        const QString s = m_socket->state() == QAbstractSocket::ConnectedState
                              ? QStringLiteral("connected to %1:%2 · %3 MB/s · %4 records · %5 reconnects")
                                    .arg(m_host)
                                    .arg(m_port)
                                    .arg(mbps, 0, 'f', 1)
                                    .arg(m_records)
                                    .arg(m_reconnects)
                              : QStringLiteral("connecting to %1:%2 (retry in %3 ms)").arg(m_host).arg(m_port).arg(m_backoffMs);
        std::lock_guard lk(m_owner->m_mutex);
        m_owner->m_status = s;
    }

    StreamClient *m_owner;
    QString m_host;
    quint16 m_port;
    double m_readMbps;
    int m_receiveBuffer;
    Sinks m_sinks;
    QTcpSocket *m_socket = nullptr;
    wire::Parser m_parser;
    int m_backoffMs = 250;
    bool m_retrying = false;
    qint64 m_bytes = 0, m_bytesAtTick = 0, m_records = 0, m_reconnects = 0;
};

StreamClient::StreamClient(const QString &host, quint16 port, double readMbps, int receiveBuffer, Sinks sinks,
                           QObject *parent)
    : QObject(parent)
{
    m_thread.setObjectName(QStringLiteral("StreamClient"));
    m_worker = new Worker(this, host, port, readMbps, receiveBuffer, std::move(sinks));
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, m_worker, &Worker::begin);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
}

StreamClient::~StreamClient()
{
    m_thread.quit();
    m_thread.wait();
}

void StreamClient::start()
{
    m_thread.start();
}

QString StreamClient::status() const
{
    std::lock_guard lk(m_mutex);
    return m_status;
}

#include "StreamServer.moc"
