#include "Diagnostics.h"

#include "qp/SimTypes.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <malloc.h>
#include <unistd.h>
#ifdef Q_OS_WASM
#include <emscripten.h>
#endif

namespace {

Diagnostics *g_diagnostics = nullptr;
QtMessageHandler g_previous = nullptr;
constexpr int kRecentLines = 200;

void handler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    static const char *levels[] = {"debug", "warning", "critical", "fatal", "info"};
    if (g_diagnostics) {
        const QJsonObject o{{QStringLiteral("t"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                            {QStringLiteral("level"), QLatin1String(levels[type])},
                            {QStringLiteral("category"), QLatin1String(context.category ? context.category : "")},
                            {QStringLiteral("msg"), msg}};
        g_diagnostics->note(QString(), QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
    }
    if (g_previous)
        g_previous(type, context, msg);
}

qint64 readLong(const QString &path, const QByteArray &key)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    for (const QByteArray &line : f.readAll().split('\n'))
        if (line.startsWith(key))
            return line.mid(key.size()).trimmed().split(' ').value(0).toLongLong();
    return -1;
}

void flatten(const QString &prefix, const QJsonObject &o, QByteArray &out)
{
    for (auto it = o.begin(); it != o.end(); ++it) {
        QString name = prefix + u'_' + it.key();
        for (QChar &c : name)
            if (!c.isLetterOrNumber() && c != u'_')
                c = u'_';
        if (it->isDouble())
            out += name.toLatin1() + ' ' + QByteArray::number(it->toDouble(), 'g', 10) + '\n';
        else if (it->isBool())
            out += name.toLatin1() + ' ' + (it->toBool() ? "1" : "0") + '\n';
        else if (it->isObject())
            flatten(name, it->toObject(), out);
    }
}

// HTTP/1.0, GET only, one response per connection: enough for curl and a scraper.
class MetricsServer : public QObject
{
public:
    explicit MetricsServer(const Diagnostics *d) : m_d(d) {}

    bool listen(quint16 port, QString *error)
    {
        m_server = new QTcpServer(this);
        if (!m_server->listen(QHostAddress::LocalHost, port)) {
            *error = m_server->errorString();
            return false;
        }
        connect(m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server->nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    if (!s->canReadLine())
                        return;
                    const QByteArray request = s->readLine();
                    QByteArray body;
                    QByteArray status = "200 OK";
                    QByteArray type = "text/plain; version=0.0.4";
                    if (request.startsWith("GET /metrics")) {
                        body = m_d->metricsText();
                    } else if (request.startsWith("GET /bundle")) {
                        body = QJsonDocument(m_d->bundle()).toJson();
                        type = "application/json";
                    } else {
                        status = "404 Not Found";
                        body = "try /metrics or /bundle\n";
                    }
                    s->write("HTTP/1.0 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: "
                             + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        return true;
    }

private:
    const Diagnostics *m_d;
    QTcpServer *m_server = nullptr;
};

} // namespace

Diagnostics::Diagnostics(QObject *parent) : QObject(parent) {}

Diagnostics::~Diagnostics()
{
    if (m_sampler.joinable()) {
        {
            std::lock_guard lk(m_samplerMutex);
            m_samplerStop = true;
        }
        m_samplerWake.notify_all();
        m_sampler.join();
    }
    if (g_diagnostics == this) {
        qInstallMessageHandler(g_previous);
        g_diagnostics = nullptr;
    }
    if (m_metricsServer) {
        QMetaObject::invokeMethod(m_metricsServer, [s = m_metricsServer] { delete s; }, Qt::BlockingQueuedConnection);
        m_metricsThread.quit();
        m_metricsThread.wait();
    }
    delete m_log;
}

double Diagnostics::msSinceProcessStart()
{
#ifdef Q_OS_WASM
    // emscripten_get_now() counts from the epoch here (the same clock on every thread); the page's
    // own origin is read once, first called from main() on the browser's main thread.
    static const double origin = EM_ASM_DOUBLE({ return performance.timeOrigin; });
    return emscripten_get_now() - origin;
#endif
    QFile stat(QStringLiteral("/proc/self/stat"));
    QFile uptime(QStringLiteral("/proc/uptime"));
    if (!stat.open(QIODevice::ReadOnly) || !uptime.open(QIODevice::ReadOnly))
        return -1;
    const QByteArray s = stat.readAll();
    const QList<QByteArray> f = s.mid(s.lastIndexOf(')') + 2).split(' ');
    const double startTicks = f.value(19).toDouble(); // field 22: starttime
    const double up = uptime.readAll().split(' ').value(0).toDouble();
    return (up - startTicks / double(sysconf(_SC_CLK_TCK))) * 1000;
}

void Diagnostics::installLog(const QString &dir, qint64 maxBytes, int keep)
{
    QDir().mkpath(dir);
    m_logDir = dir;
    m_logPath = QDir(dir).filePath(QStringLiteral("viewer.log"));
    m_logMax = maxBytes;
    m_logKeep = keep;
    m_log = new QFile(m_logPath);
    if (!m_log->open(QIODevice::Append | QIODevice::Text)) {
        delete m_log;
        m_log = nullptr;
    }
    g_diagnostics = this;
    g_previous = qInstallMessageHandler(handler);
}

void Diagnostics::writeLog(const QByteArray &line)
{
    // m_mutex held
    m_recent.append(QString::fromUtf8(line));
    while (m_recent.size() > kRecentLines)
        m_recent.removeFirst();
    if (!m_log)
        return;
    m_log->write(line + '\n');
    m_log->flush();
    if (m_log->size() < m_logMax)
        return;
    m_log->close();
    for (int i = m_logKeep - 1; i >= 1; --i) {
        QFile::remove(m_logPath + u'.' + QString::number(i + 1));
        QFile::rename(m_logPath + u'.' + QString::number(i), m_logPath + u'.' + QString::number(i + 1));
    }
    QFile::remove(m_logPath + QStringLiteral(".1"));
    QFile::rename(m_logPath, m_logPath + QStringLiteral(".1"));
    if (!m_log->open(QIODevice::WriteOnly | QIODevice::Text)) {
        delete m_log;
        m_log = nullptr;
    }
}

void Diagnostics::note(const QString &source, const QString &text)
{
    QByteArray line;
    if (source.isEmpty()) {
        line = text.toUtf8(); // already a log line
    } else {
        const QJsonObject o{{QStringLiteral("t"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                            {QStringLiteral("level"), QStringLiteral("info")},
                            {QStringLiteral("category"), source},
                            {QStringLiteral("msg"), text}};
        line = QJsonDocument(o).toJson(QJsonDocument::Compact);
    }
    std::lock_guard lk(m_mutex);
    writeLog(line);
}

void Diagnostics::setStartup(const QString &key, double ms)
{
    m_startup[key] = ms;
}

void Diagnostics::addSection(const QString &name, std::function<QJsonObject()> sample)
{
    m_sections[name] = std::move(sample);
}

void Diagnostics::start()
{
    m_sampler = std::thread([this] { sampleProcess(); });
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Diagnostics::sample);
    timer->start(1000);
    sample();
}

bool Diagnostics::startMetrics(quint16 port, QString *error)
{
    auto *server = new MetricsServer(this);
    m_metricsThread.setObjectName(QStringLiteral("metrics"));
    server->moveToThread(&m_metricsThread);
    m_metricsThread.start();
    bool ok = false;
    QMetaObject::invokeMethod(server, [&] { ok = server->listen(port, error); }, Qt::BlockingQueuedConnection);
    m_metricsServer = server;
    if (ok) {
        m_metricsUrl = QStringLiteral("http://127.0.0.1:%1/metrics").arg(port);
        emit metricsUrlChanged();
    }
    return ok;
}

// Its own thread: reading /proc costs about a millisecond, and mallinfo2() walks every malloc
// arena holding its lock (1-4 ms here, blocking any thread that allocates meanwhile).
void Diagnostics::sampleProcess()
{
    qint64 cpuTicks = 0, cpuAtNs = 0, heapAtNs = 0;
    QJsonObject heap;
    std::unique_lock lk(m_samplerMutex);
    while (!m_samplerStop) {
        const qint64 t0 = qp::nowNs();
        QJsonObject process;
        process[u"rssMb"] = double(readLong(QStringLiteral("/proc/self/status"), "VmRSS:")) / 1024;
        process[u"threads"] = double(readLong(QStringLiteral("/proc/self/status"), "Threads:"));
        process[u"openFiles"] = double(QDir(QStringLiteral("/proc/self/fd")).entryList(QDir::Files | QDir::System).size());
        process[u"uptimeS"] = msSinceProcessStart() / 1000;
        QFile stat(QStringLiteral("/proc/self/stat"));
        if (stat.open(QIODevice::ReadOnly)) {
            const QByteArray s = stat.readAll();
            const QList<QByteArray> f = s.mid(s.lastIndexOf(')') + 2).split(' ');
            const qint64 ticks = f.value(11).toLongLong() + f.value(12).toLongLong();
            if (cpuAtNs > 0)
                process[u"cpuPercent"] = 100.0 * double(ticks - cpuTicks) / double(sysconf(_SC_CLK_TCK))
                                         / (double(t0 - cpuAtNs) / 1e9);
            cpuTicks = ticks;
            cpuAtNs = t0;
        }
        if (t0 - heapAtNs >= 10'000'000'000LL) {
            const qint64 tm = qp::nowNs();
#ifdef Q_OS_WASM
            const struct mallinfo mi = mallinfo(); // Emscripten's dlmalloc; no mallinfo2
#else
            const struct mallinfo2 mi = mallinfo2();
#endif
            heap = {{QStringLiteral("heapInUseMb"), double(mi.uordblks) / 1e6},
                    {QStringLiteral("heapFreeMb"), double(mi.fordblks) / 1e6},
                    {QStringLiteral("mmapMb"), double(mi.hblkhd) / 1e6},
                    {QStringLiteral("mallinfoMs"), double(qp::nowNs() - tm) / 1e6}};
            heapAtNs = t0;
        }
        for (auto it = heap.begin(); it != heap.end(); ++it)
            process[it.key()] = it.value();
        process[u"sampleMs"] = double(qp::nowNs() - t0) / 1e6;
        {
            std::lock_guard g(m_mutex);
            m_process = process;
        }
        m_samplerWake.wait_for(lk, std::chrono::seconds(1), [this] { return m_samplerStop; });
    }
}

void Diagnostics::sample()
{
    const qint64 t0 = qp::nowNs();
    QJsonObject current;
    current[u"startup"] = m_startup;
    for (const auto &[name, f] : m_sections)
        current[name] = f();
    current[u"diagnostics"] = QJsonObject{{QStringLiteral("sampleMs"), double(qp::nowNs() - t0) / 1e6}};
    {
        std::lock_guard lk(m_mutex);
        m_current = current;
    }
    emit changed();
}

QVariantMap Diagnostics::snapshot() const
{
    std::lock_guard lk(m_mutex);
    QJsonObject c = m_current;
    c[u"process"] = m_process;
    return c.toVariantMap();
}

QStringList Diagnostics::recentLog() const
{
    std::lock_guard lk(m_mutex);
    return m_recent.mid(std::max<qsizetype>(0, m_recent.size() - 30));
}

QByteArray Diagnostics::metricsText() const
{
    QByteArray out;
    {
        std::lock_guard lk(m_mutex);
        QJsonObject c = m_current;
        c[u"process"] = m_process;
        flatten(QStringLiteral("qp"), c, out);
    }
    return out;
}

QJsonObject Diagnostics::bundle() const
{
    QJsonObject b;
    b[u"savedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    b[u"build"] = m_build;
    b[u"arguments"] = QJsonArray::fromStringList(QCoreApplication::arguments());
    QJsonObject env;
    const QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
    for (const QString &k : e.keys())
        if (k.startsWith(u"QT_") || k.startsWith(u"QSG_") || k.startsWith(u"QML") || k == u"XDG_SESSION_TYPE"
            || k == u"WAYLAND_DISPLAY" || k == u"DISPLAY" || k == u"LANG")
            env[k] = e.value(k);
    b[u"environment"] = env;
    for (const auto &[key, f] : m_extraBundle)
        b[key] = f();
    std::lock_guard lk(m_mutex);
    QJsonObject c = m_current;
    c[u"process"] = m_process;
    b[u"state"] = c;
    b[u"log"] = QJsonArray::fromStringList(m_recent);
    return b;
}

QString Diagnostics::saveBundle(const QString &path)
{
    QString target = path;
    if (target.isEmpty()) {
        QDir().mkpath(m_bundleDir);
        target = QDir(m_bundleDir).filePath(
            QStringLiteral("bundle-%1.json").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    }
    QDir().mkpath(QFileInfo(target).absolutePath());
    QSaveFile f(target);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(QJsonDocument(bundle()).toJson());
    if (!f.commit())
        return {};
    note(QStringLiteral("diagnostics"), QStringLiteral("bundle saved to ") + target);
    return target;
}
