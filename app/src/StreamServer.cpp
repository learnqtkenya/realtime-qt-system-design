#include "StreamServer.h"

#include "Wire.h"
#include "qp/StageStats.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <QWebSocketCorsAuthenticator>
#include <QWebSocketServer>

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
        : m_owner(owner), m_policy(policy), m_zstd(zstd), m_info(info), m_highWater(highWater), m_sendBuffer(sendBuffer)
    {
        m_info[u"clock"] = 1;  // answers clock probes
        m_info[u"credit"] = 1; // holds records back on a client's credit
    }

    struct Session
    {
        QObject *link = nullptr; // a QTcpSocket or a QWebSocket
        std::function<qint64(const QByteArray &)> write;
        std::function<qint64()> backlog; // bytes accepted but not yet sent
        std::function<void()> close;
        wire::Parser in{4096}; // what the client sends: a hello, then clock probes and credit
        // Step 7 used the socket's backlog as the client's credit. A browser reads every message
        // as it arrives and queues it inside the page, so its socket never backs up: a client
        // that says so reports what it has processed, and the server holds back on that.
        bool credit = false;
        qint64 written = 0, acked = 0;
        QString peer;
        struct Held
        {
            qint64 t;
            QByteArray record;
        };
        std::map<std::pair<int, int>, Held> latest; // (type, stream) -> newest record not yet sent
        std::deque<std::pair<QByteArray, bool>> events; // record, holds a note (never dropped)
        qint64 sent = 0, sentAtLastTick = 0;
        qint64 replaced = 0, eventsDropped = 0, backlogMax = 0, probes = 0;
    };

    void listen(quint16 port, quint16 wsPort, const QStringList &origins, bool *ok, QString *error)
    {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &Worker::accept);
        *ok = m_server->listen(QHostAddress::Any, port);
        if (!*ok)
            *error = m_server->errorString();
        if (*ok && wsPort > 0) {
            m_wsOrigins = origins;
            m_ws = new QWebSocketServer(QStringLiteral("qtqmlpro"), QWebSocketServer::NonSecureMode, this);
            connect(m_ws, &QWebSocketServer::originAuthenticationRequired, this,
                    [this](QWebSocketCorsAuthenticator *a) { a->setAllowed(originAllowed(a->origin())); });
            connect(m_ws, &QWebSocketServer::newConnection, this, &Worker::acceptWs);
            *ok = m_ws->listen(QHostAddress::Any, wsPort);
            if (!*ok)
                *error = QStringLiteral("WebSocket: ") + m_ws->errorString();
        }
        auto *tick = new QTimer(this);
        connect(tick, &QTimer::timeout, this, &Worker::second);
        tick->start(1000);
    }

    void publish(rec::Type type, int stream, qint64 t, const std::function<QByteArray()> &encode, bool keep = false)
    {
        if (m_sessions.empty())
            return; // nobody listening: don't even encode
        const qint64 t0 = qp::nowNs();
        const QByteArray record = encode(); // once, for every client
        if (type == rec::Type::Lidar)
            qp::StageStats::instance().add(QStringLiteral("server_encode_ms"), double(qp::nowNs() - t0) / 1e6);
        for (const auto &s : m_sessions) {
            if (m_policy == Policy::All) {
                s->write(record);
                s->sent += record.size();
            } else if (type == rec::Type::Events) {
                s->events.emplace_back(record, keep);
                if (s->events.size() > kEventQueue) {
                    const auto victim = std::find_if(s->events.begin(), s->events.end(), [](const auto &e) { return !e.second; });
                    if (victim != s->events.end()) {
                        s->events.erase(victim);
                        ++s->eventsDropped;
                    }
                }
                pump(*s);
            } else {
                auto [it, fresh] = s->latest.insert_or_assign({int(type), stream}, Session::Held{t, record});
                if (!fresh)
                    ++s->replaced;
                pump(*s);
            }
            s->backlogMax = std::max(s->backlogMax, behind(*s));
        }
    }

private:
    void accept()
    {
        while (QTcpSocket *socket = m_server->nextPendingConnection()) {
            auto s = std::make_shared<Session>();
            s->link = socket;
            s->write = [socket, sp = s.get()](const QByteArray &b) {
                const qint64 n = socket->write(b);
                sp->written += std::max<qint64>(n, 0);
                return n;
            };
            s->backlog = [socket] { return socket->bytesToWrite(); };
            s->close = [socket] { socket->abort(); };
            s->peer = QStringLiteral("%1:%2").arg(socket->peerAddress().toString()).arg(socket->peerPort());
            socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
            if (m_sendBuffer > 0) // the kernel's buffer is a queue too
                socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, m_sendBuffer);
            s->write(hello());
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, w = std::weak_ptr(s)] {
                const QByteArray b = socket->readAll();
                if (auto s = w.lock())
                    inbound(*s, b);
            });
            connect(socket, &QTcpSocket::bytesWritten, this, [this, w = std::weak_ptr(s)] {
                if (auto s = w.lock())
                    pump(*s);
            });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket] { drop(socket); });
            m_sessions.push_back(s);
        }
    }

    // One record per binary message; the client's parser takes the bytes as if they were a stream.
    void acceptWs()
    {
        while (QWebSocket *socket = m_ws->nextPendingConnection()) {
            auto s = std::make_shared<Session>();
            s->link = socket;
            s->write = [socket, sp = s.get()](const QByteArray &b) {
                const qint64 n = socket->sendBinaryMessage(b);
                sp->written += std::max<qint64>(n, 0);
                return n;
            };
            s->backlog = [socket] { return socket->bytesToWrite(); };
            s->close = [socket] { socket->abort(); };
            s->peer = QStringLiteral("ws %1:%2").arg(socket->peerAddress().toString()).arg(socket->peerPort());
            s->write(hello());
            connect(socket, &QWebSocket::binaryMessageReceived, this, [this, w = std::weak_ptr(s)](const QByteArray &b) {
                if (auto s = w.lock())
                    inbound(*s, b);
            });
            connect(socket, &QWebSocket::bytesWritten, this, [this, w = std::weak_ptr(s)] {
                if (auto s = w.lock())
                    pump(*s);
            });
            connect(socket, &QWebSocket::disconnected, this, [this, socket] { drop(socket); });
            m_sessions.push_back(s);
        }
    }

    QByteArray hello() const
    {
        QJsonObject info = m_info;
        info[u"clockNs"] = QString::number(qp::nowNs()); // a string: JSON numbers are doubles
        return wire::hello(info);
    }

    // Answered at once, past the held records: a probe's worth is a short round trip.
    void inbound(Session &s, const QByteArray &bytes)
    {
        const bool hadHello = s.in.hasHello();
        bool credited = false;
        const bool ok = s.in.feed(bytes.constData(), bytes.size(), [&](const wire::Decoded &d) {
            if (d.type == rec::Type::Clock) {
                s.write(wire::clockRecord(d.clockClientNs, qp::nowNs()));
                ++s.probes;
            } else if (d.type == rec::Type::Credit && d.credit > s.acked) {
                s.acked = std::min(d.credit, s.written); // never credit for bytes not sent
                credited = true;
            }
        });
        if (!ok) {
            s.close(); // not our protocol
            return;
        }
        if (!hadHello && s.in.hasHello())
            s.credit = s.in.info().value(u"credit").toInt() >= 1;
        if (credited)
            pump(s);
    }

    // What this client has yet to take: in the socket, or (with credit) sent and not yet processed.
    static qint64 behind(const Session &s) { return std::max(s.backlog(), s.credit ? s.written - s.acked : 0); }

    void drop(QObject *link)
    {
        std::erase_if(m_sessions, [link](const auto &s) { return s->link == link; });
        link->deleteLater();
    }

    bool originAllowed(const QString &origin) const
    {
        if (origin.isEmpty() || m_wsOrigins.contains(origin))
            return true;
        const QString host = QUrl(origin).host();
        return host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host == QLatin1String("[::1]")
               || host == QLatin1String("::1");
    }

    // Writes held records while the socket's backlog is small and, for clients that send credit, while
    // what they have not yet processed is under the window (a browser drains its socket at once).
    void pump(Session &s)
    {
        while (s.backlog() < m_highWater && (!s.credit || s.written - s.acked < m_creditWindow)) {
            if (!s.events.empty()) {
                s.sent += s.write(s.events.front().first);
                s.events.pop_front();
                continue;
            }
            if (s.latest.empty())
                break;
            // Oldest capture first, so no stream starves when the link is short of bandwidth.
            auto oldest = std::min_element(s.latest.begin(), s.latest.end(),
                                           [](const auto &a, const auto &b) { return a.second.t < b.second.t; });
            s.sent += s.write(oldest->second.record);
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
            lines << QStringLiteral("%1%2 · %3 MB/s · backlog %4 MB · %5 replaced · %6 events dropped")
                         .arg(s->peer, s->credit ? QStringLiteral(" (credit)") : QString())
                         .arg(mbps, 0, 'f', 1)
                         .arg(double(behind(*s)) / 1e6, 0, 'f', 1)
                         .arg(s->replaced)
                         .arg(s->eventsDropped);
            qp::StageStats::instance().add(QStringLiteral("server_client_mbps"), mbps);
            qp::StageStats::instance().add(QStringLiteral("server_backlog_mb"), double(behind(*s)) / 1e6);
            s->backlogMax = 0;
        }
        std::lock_guard lk(m_owner->m_mutex);
        m_owner->m_status = lines;
    }

    StreamServer *m_owner;
    Policy m_policy;
    int m_zstd;
    QJsonObject m_info;
    qint64 m_highWater; // a client's backlog before records are held back
    qint64 m_creditWindow = 4 << 20; // sent and not yet processed: several records, or a fast link stops and waits

public:
    void setCreditWindow(qint64 bytes) { m_creditWindow = bytes; }

private:
    int m_sendBuffer;
    QTcpServer *m_server = nullptr;
    QWebSocketServer *m_ws = nullptr;
    QStringList m_wsOrigins;
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

void StreamServer::setCreditWindow(qint64 bytes)
{
    m_worker->setCreditWindow(bytes);
}

void StreamServer::setWebSocket(quint16 port, const QStringList &origins)
{
    m_wsPort = port;
    m_wsOrigins = origins;
}

bool StreamServer::start(QString *error)
{
    m_thread.start();
    bool ok = false;
    QString why;
    QMetaObject::invokeMethod(m_worker, [this, &ok, &why] { m_worker->listen(m_port, m_wsPort, m_wsOrigins, &ok, &why); },
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
            const bool notes = std::any_of(batch->begin(), batch->end(), [](const qp::EventRecord &e) { return e.anchored; });
            w->publish(rec::Type::Events, 0, batch->front().captureNs, [&] { return wire::eventsRecord(batch); }, notes);
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
        m_probe = new QTimer(this);
        m_probe->setSingleShot(true);
        connect(m_probe, &QTimer::timeout, this, &Worker::probe);
        auto *tick = new QTimer(this);
        connect(tick, &QTimer::timeout, this, &Worker::second);
        tick->start(1000);
        if (m_host.startsWith(QLatin1String("ws://")) || m_host.startsWith(QLatin1String("wss://")))
            beginWs();
        else
            beginTcp();
    }

private:
    void beginWs()
    {
        m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
        connect(m_ws, &QWebSocket::connected, this, [this] { connected(); });
        connect(m_ws, &QWebSocket::disconnected, this, &Worker::retry);
        connect(m_ws, &QWebSocket::errorOccurred, this, [this] {
            if (m_ws->state() != QAbstractSocket::ConnectedState)
                retry();
        });
        connect(m_ws, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray &m) {
            if (!feed(m.constData(), m.size()))
                m_ws->abort();
        });
        m_ws->open(QUrl(m_host));
    }

    void beginTcp()
    {
        m_socket = new QTcpSocket(this);
        // Small, so a slow reader pushes back through TCP instead of buffering here without end.
        m_socket->setReadBufferSize(256 << 10);
        if (m_receiveBuffer > 0)
            m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, m_receiveBuffer);
        connect(m_socket, &QTcpSocket::connected, this, [this] {
            connected();
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
        m_socket->connectToHost(m_host, m_port);
    }

    void connected()
    {
        qp::StageStats::instance().add(QStringLiteral("client_connected"), 1);
        m_backoffMs = 250;
        m_parser = wire::Parser();
        m_clock = wire::ClockSync();
        m_probe->stop();
        m_credit = false;
        m_consumed = m_creditSent = 0;
    }

    void send(const QByteArray &b)
    {
        if (m_ws)
            m_ws->sendBinaryMessage(b);
        else
            m_socket->write(b);
    }

    // What the server's hello offers: answers to clock probes (its clock is not ours: another
    // machine, or a browser's clock; five quick probes, then one a second for drift), and credit.
    void startUpstream()
    {
        const QJsonObject &info = m_parser.info();
        const bool clock = info.value(u"clock").toInt() >= 1;
        m_credit = info.value(u"credit").toInt() >= 1;
        if (!clock && !m_credit)
            return; // a server that never reads what we'd send
        send(wire::hello(QJsonObject{{"app", "viewer"}, {"role", "client"}, {"credit", m_credit ? 1 : 0}}));
        if (clock) {
            m_clock.seed(info.value(u"clockNs").toString().toLongLong(), qp::nowNs());
            m_probesLeftFast = 5;
            probe();
        }
    }

    void probe()
    {
        send(wire::clockRecord(qp::nowNs(), 0));
        m_probe->start(m_probesLeftFast-- > 0 ? 50 : 1000);
    }

    bool isConnected() const
    {
        return (m_ws ? m_ws->state() : m_socket->state()) == QAbstractSocket::ConnectedState;
    }

    void retry()
    {
        if (m_retrying)
            return;
        m_retrying = true;
        QTimer::singleShot(m_backoffMs, this, [this] {
            m_retrying = false;
            if (m_ws && m_ws->state() == QAbstractSocket::UnconnectedState)
                m_ws->open(QUrl(m_host));
            else if (m_socket && m_socket->state() == QAbstractSocket::UnconnectedState)
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
            if (!feed(chunk.constData(), chunk.size())) {
                m_socket->abort(); // not our protocol, or broken: start again
                retry();
                return;
            }
        }
    }

    bool feed(const char *data, qsizetype size)
    {
        m_bytes += size;
        const bool hadHello = m_parser.hasHello();
        const bool ok = m_parser.feed(data, size, [this](const wire::Decoded &d) {
            ++m_records;
            // Capture times arrive in the server's clock; everything here runs on ours.
            switch (d.type) {
            case rec::Type::Lidar: {
                if (m_owner->m_recordDelayMs > 0)
                    QThread::msleep(ulong(m_owner->m_recordDelayMs));
                qp::SensorFrame f = d.lidar;
                f.captureNs = m_clock.toLocal(f.captureNs);
                m_sinks.lidar(f);
                break;
            }
            case rec::Type::Tracks: {
                auto t = std::make_shared<qp::TrackSet>(*d.tracks);
                t->captureNs = m_clock.toLocal(t->captureNs);
                m_sinks.tracks(t);
                break;
            }
            case rec::Type::Events: {
                auto b = std::make_shared<qp::EventBatch>(*d.events);
                for (auto &e : *b)
                    e.captureNs = m_clock.toLocal(e.captureNs);
                m_sinks.events(b);
                break;
            }
            case rec::Type::Clock: m_clock.add(d.clockClientNs, d.clockServerNs, qp::nowNs()); break;
            default: break;
            }
        });
        if (ok && !hadHello && m_parser.hasHello()) {
            startUpstream();
            if (m_sinks.session)
                m_sinks.session();
        }
        // Processed, not merely received: what the server may count as taken.
        m_consumed += size;
        if (ok && m_credit && m_consumed - m_creditSent >= kCreditStep) {
            send(wire::creditRecord(m_consumed));
            m_creditSent = m_consumed;
        }
        return ok;
    }

    void second()
    {
        const qint64 now = qp::nowNs();
        // A busy worker ticks late: divide by the time that passed, not by the second asked for.
        const double mbps = m_tickAtNs > 0 ? double(m_bytes - m_bytesAtTick) / 1e6 / (double(now - m_tickAtNs) / 1e9) : 0;
        m_tickAtNs = now;
        m_bytesAtTick = m_bytes;
        qp::StageStats::instance().add(QStringLiteral("client_mbps"), mbps);
        if (m_clock.rttNs() >= 0) {
            qp::StageStats::instance().add(QStringLiteral("client_clock_offset_ms"), double(m_clock.offsetNs()) / 1e6);
            qp::StageStats::instance().add(QStringLiteral("client_clock_rtt_ms"), double(m_clock.rttNs()) / 1e6);
        }
        const QString clock = !m_clock.known() ? QStringLiteral("server clock unknown")
                              : m_clock.rttNs() < 0
                                  ? QStringLiteral("clock %1 ms (from hello)").arg(double(m_clock.offsetNs()) / 1e6, 0, 'f', 1)
                                  : QStringLiteral("clock %1 ± %2 ms").arg(double(m_clock.offsetNs()) / 1e6, 0, 'f', 1)
                                        .arg(double(m_clock.rttNs()) / 2e6, 0, 'f', 2);
        const QString where = m_ws ? m_host : QStringLiteral("%1:%2").arg(m_host).arg(m_port);
        const QString s = isConnected()
                              ? QStringLiteral("connected to %1 · %2 MB/s · %3 records · %4 reconnects · %5")
                                    .arg(where)
                                    .arg(mbps, 0, 'f', 1)
                                    .arg(m_records)
                                    .arg(m_reconnects)
                                    .arg(clock)
                              : QStringLiteral("connecting to %1 (retry in %2 ms)").arg(where).arg(m_backoffMs);
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
    QWebSocket *m_ws = nullptr;
    wire::Parser m_parser;
    wire::ClockSync m_clock;
    QTimer *m_probe = nullptr;
    int m_probesLeftFast = 0;
    static constexpr qint64 kCreditStep = 64 << 10; // well under the server's window
    bool m_credit = false;
    qint64 m_consumed = 0, m_creditSent = 0, m_tickAtNs = 0;
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
