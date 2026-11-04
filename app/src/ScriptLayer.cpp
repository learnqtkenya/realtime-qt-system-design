#include "ScriptLayer.h"

#include "qp/StageStats.h"

#include <QDir>
#include <QLibraryInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQmlAbstractUrlInterceptor>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlNetworkAccessManagerFactory>

namespace {

// A script plugin's engine can't import the viewer's own modules, or QML from outside its directory
// and Qt's.
class PluginUrlInterceptor : public QQmlAbstractUrlInterceptor
{
public:
    explicit PluginUrlInterceptor(const QString &dir)
        : m_dir(dir + u'/'), m_qt(QLibraryInfo::path(QLibraryInfo::QmlImportsPath) + u'/')
    {
    }
    QUrl intercept(const QUrl &url, DataType type) override
    {
        const QString s = url.toString();
        if (type == QmldirFile && (s.contains(u"/Viewer/") || s.contains(u"/QtQMLPro/")))
            return blocked(url);
        if (url.isLocalFile() && (type == QmlFile || type == JavaScriptFile || type == QmldirFile)) {
            const QString path = QDir::cleanPath(url.toLocalFile());
            if (!path.startsWith(m_dir) && !path.startsWith(m_qt))
                return blocked(url);
        }
        return url;
    }

private:
    // A local file that can't exist: any other scheme would make the loader fetch it asynchronously.
    static QUrl blocked(const QUrl &url) { return QUrl::fromLocalFile(QStringLiteral("/dev/null/blocked/") + url.fileName()); }

public:

private:
    QString m_dir, m_qt;
};

class NoNetwork : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *data) override
    {
        const QString scheme = request.url().scheme();
        if (scheme == u"file" || scheme == u"qrc" || scheme.isEmpty())
            return QNetworkAccessManager::createRequest(op, request, data);
        QNetworkRequest blocked(request);
        blocked.setUrl(QUrl(QStringLiteral("blocked:")));
        return QNetworkAccessManager::createRequest(op, blocked, data);
    }
};

class NoNetworkFactory : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override { return new NoNetwork(parent); }
};

NoNetworkFactory &noNetwork()
{
    static NoNetworkFactory f;
    return f;
}

} // namespace

class ScriptLayer::Runner : public QObject
{
public:
    Runner(const PluginManifest &m, std::shared_ptr<PluginHost::Watched> watched, std::shared_ptr<Input> input,
           ScriptLayer *layer, quint64 generation, bool onGui)
        : m_manifest(m), m_watched(std::move(watched)), m_input(std::move(input)), m_layer(layer),
          m_generation(generation), m_onGui(onGui)
    {
    }

    ~Runner() override
    {
        {
            std::lock_guard lk(m_watched->mutex);
            m_watched->engine = nullptr;
        }
        delete m_object;
        delete m_engine;
    }

    void load()
    {
        const QString path = QDir(m_manifest.dir).filePath(m_manifest.entry);
        const bool qml = m_manifest.entry.endsWith(u".qml");
        if (qml) {
            auto *engine = new QQmlEngine;
            engine->addUrlInterceptor(new PluginUrlInterceptor(m_manifest.dir));
            engine->setNetworkAccessManagerFactory(&noNetwork());
            m_engine = engine;
        } else {
            // Plain JavaScript: no imports, no Qt object, no XMLHttpRequest; only what update() is given.
            m_engine = new QJSEngine;
            m_engine->installExtensions(QJSEngine::ConsoleExtension);
        }
        {
            std::lock_guard lk(m_watched->mutex);
            m_watched->engine = m_engine;
        }
        QString error;
        if (qml) {
            QQmlComponent component(static_cast<QQmlEngine *>(m_engine), QUrl::fromLocalFile(path));
            m_object = component.createWithInitialProperties(m_manifest.properties);
            if (!m_object)
                error = component.isError() ? component.errors().value(0).toString()
                                            : QStringLiteral("%1 could not be loaded").arg(m_manifest.entry);
            else
                m_self = m_engine->toScriptValue(m_object);
        } else {
            m_self = m_engine->importModule(path);
            if (m_self.isError())
                error = m_self.toString();
            else if (const QJSValue init = m_self.property(QStringLiteral("init")); init.isCallable())
                init.call({m_engine->toScriptValue(m_manifest.properties)});
        }
        if (error.isEmpty()) {
            m_update = m_self.property(QStringLiteral("update"));
            if (!m_update.isCallable())
                error = QStringLiteral("the plugin has no update(input, draw) function");
            m_draw = new OverlayBuilder(m_engine);
            m_drawValue = m_engine->newQObject(m_draw);
        }
        const QString where = (qml ? QStringLiteral("own QML engine") : QStringLiteral("own JS engine"))
                              + (m_onGui ? QStringLiteral(", GUI thread") : QStringLiteral(", own thread"));
        post([error, where](ScriptLayer *l) { l->loaded(error, where); });
    }

    void run()
    {
        qp::TrackSetPtr tracks;
        int selected = -1;
        {
            std::lock_guard lk(m_input->mutex);
            tracks = std::move(m_input->tracks);
            selected = m_input->selectedId;
            m_input->scheduled = false;
            if (m_input->halted)
                return;
        }
        if (!tracks || !m_update.isCallable())
            return;
        const qint64 t0 = qp::nowNs();
        static const QString kId = QStringLiteral("id"), kX = QStringLiteral("x"), kY = QStringLiteral("y"),
                             kHeading = QStringLiteral("heading"), kSpeed = QStringLiteral("speed"),
                             kLength = QStringLiteral("length"), kWidth = QStringLiteral("width"),
                             kCls = QStringLiteral("cls");
        QJSValue list = m_engine->newArray(uint(tracks->tracks.size()));
        for (size_t i = 0; i < tracks->tracks.size(); ++i) {
            const qp::Track &t = tracks->tracks[i];
            QJSValue o = m_engine->newObject();
            o.setProperty(kId, t.id);
            o.setProperty(kX, t.x);
            o.setProperty(kY, t.y);
            o.setProperty(kHeading, t.heading);
            o.setProperty(kSpeed, t.speed);
            o.setProperty(kLength, t.length);
            o.setProperty(kWidth, t.width);
            o.setProperty(kCls, t.cls);
            list.setProperty(quint32(i), o);
        }
        QJSValue input = m_engine->newObject();
        input.setProperty(QStringLiteral("t"), double(tracks->captureNs) / 1e6);
        input.setProperty(QStringLiteral("selectedId"), selected);
        input.setProperty(QStringLiteral("tracks"), list);
        m_draw->reset();
        const qint64 t1 = qp::nowNs();
        m_watched->busySinceNs = t1;
        const QJSValue r = m_update.callWithInstance(m_self, {input, m_drawValue});
        m_watched->busySinceNs = 0;
        const qint64 t2 = qp::nowNs();
        const bool interrupted = m_watched->fired.exchange(false);
        if (interrupted) {
            m_engine->setInterrupted(false);
            std::lock_guard lk(m_input->mutex);
            m_input->halted = true;
        }
        QString error;
        if (r.isError())
            error = r.property(QStringLiteral("message")).toString() + QStringLiteral(" (line ")
                    + r.property(QStringLiteral("lineNumber")).toString() + u')';
        const quint32 overLimit = m_draw->overLimit();
        OverlayFramePtr frame = m_draw->take(tracks->captureNs);
        const double updateMs = double(t2 - t1) / 1e6, marshalMs = double(t1 - t0) / 1e6;
        auto &stats = qp::StageStats::instance();
        stats.add(QStringLiteral("layer_%1_update_ms").arg(m_manifest.id), updateMs);
        stats.add(QStringLiteral("layer_%1_marshal_ms").arg(m_manifest.id), marshalMs);
        if (interrupted)
            stats.add(QStringLiteral("layer_%1_interrupted_after_ms").arg(m_manifest.id), updateMs);
        post([=](ScriptLayer *l) { l->finished(frame, updateMs, marshalMs, error, interrupted, overLimit); });
    }

private:
    template <typename F> void post(F f)
    {
        if (m_onGui) {
            f(m_layer);
            return;
        }
        QPointer<ScriptLayer> layer = m_layer;
        const quint64 generation = m_generation;
        QMetaObject::invokeMethod(
            m_layer,
            [layer, generation, f] {
                if (layer && layer->m_generation == generation)
                    f(layer.data());
            },
            Qt::QueuedConnection);
    }

    PluginManifest m_manifest;
    std::shared_ptr<PluginHost::Watched> m_watched;
    std::shared_ptr<Input> m_input;
    ScriptLayer *m_layer;
    quint64 m_generation;
    bool m_onGui;
    QJSEngine *m_engine = nullptr;
    QObject *m_object = nullptr;
    QJSValue m_self, m_update, m_drawValue;
    OverlayBuilder *m_draw = nullptr;
};

ScriptLayer::ScriptLayer(const PluginManifest &m, PluginHost *host) : Layer(m, host) {}

ScriptLayer::~ScriptLayer()
{
    stop();
}

void ScriptLayer::start()
{
    stop();
    ++m_generation;
    m_watched = std::make_shared<PluginHost::Watched>();
    m_input = std::make_shared<Input>();
    m_host->watch(m_watched);
    m_overBudget = 0;
    m_lastUpdateMs = m_lastMarshalMs = 0;
    m_skippedSeen = m_fedSeen = 0;
    m_windowStart = qp::nowNs();
    m_skipShare = 0;
    const bool onGui = m_host->options().scriptOnGui;
    setStatus(Status::Starting);
    m_runner = new Runner(m_manifest, m_watched, m_input, this, m_generation, onGui);
    if (onGui) {
        m_runner->load();
        return;
    }
    m_thread = std::make_unique<QThread>();
    m_thread->setObjectName(QStringLiteral("layer ") + m_manifest.id);
    m_runner->moveToThread(m_thread.get());
    m_thread->start();
    QMetaObject::invokeMethod(m_runner, [r = m_runner] { r->load(); }, Qt::QueuedConnection);
}

void ScriptLayer::stop()
{
    ++m_generation;
    if (m_runner) {
        if (m_thread) {
            // Blocks until a running update() returns; the watchdog bounds that.
            QMetaObject::invokeMethod(m_runner, [r = m_runner] { delete r; }, Qt::BlockingQueuedConnection);
            m_thread->quit();
            m_thread->wait();
            m_thread.reset();
        } else {
            delete m_runner;
        }
        m_runner = nullptr;
    }
    if (m_status != Status::Off)
        setStatus(Status::Off);
}

void ScriptLayer::loaded(const QString &error, const QString &where)
{
    m_where = where;
    if (!error.isEmpty())
        setStatus(Status::Error, error);
    else
        setStatus(Status::Ok, where);
}

void ScriptLayer::feed(const qp::TrackSetPtr &tracks, int selectedId, const QByteArray &)
{
    if (!m_runner || m_status == Status::Stopped || m_status == Status::Suspended || m_status == Status::Starting
        || (m_status == Status::Error && m_lastUpdateMs == 0))
        return;
    bool schedule = false;
    {
        std::lock_guard lk(m_input->mutex);
        ++m_input->fed;
        if (m_input->tracks)
            ++m_input->skipped; // latest wins: the one waiting is replaced
        m_input->tracks = tracks;
        m_input->selectedId = selectedId;
        schedule = !m_input->scheduled;
        m_input->scheduled = true;
    }
    if (m_host->options().scriptOnGui)
        m_runner->run();
    else if (schedule)
        QMetaObject::invokeMethod(m_runner, [r = m_runner] { r->run(); }, Qt::QueuedConnection);
}

void ScriptLayer::finished(const OverlayFramePtr &frame, double updateMs, double marshalMs, const QString &error,
                           bool interrupted, quint32 overLimit)
{
    m_lastUpdateMs = updateMs;
    m_lastMarshalMs = marshalMs;
    if (interrupted) {
        setStatus(Status::Stopped, QStringLiteral("update() ran for %1 ms; interrupted").arg(updateMs, 0, 'f', 0));
        return;
    }
    if (!error.isEmpty()) {
        setStatus(Status::Error, error);
        return;
    }
    const bool onGui = m_host->options().scriptOnGui;
    if (onGui) {
        if (updateMs + marshalMs > m_host->options().budgetMs) {
            if (++m_overBudget >= 3) {
                setStatus(Status::Suspended, QStringLiteral("%1 ms per update on the GUI thread (budget %2 ms)")
                                                 .arg(updateMs + marshalMs, 0, 'f', 1)
                                                 .arg(m_host->options().budgetMs));
                return;
            }
        } else {
            m_overBudget = 0;
        }
    }
    const qint64 now = qp::nowNs();
    if (now - m_windowStart > 1'000'000'000) {
        std::lock_guard lk(m_input->mutex);
        const quint64 fed = m_input->fed - m_fedSeen, skipped = m_input->skipped - m_skippedSeen;
        m_fedSeen = m_input->fed;
        m_skippedSeen = m_input->skipped;
        m_skipShare = fed ? double(skipped) / double(fed) : 0;
        m_windowStart = now;
    }
    QString detail = overLimit ? QStringLiteral("%1 drawing calls over the limit").arg(overLimit) : QString();
    if (m_skipShare > 0.05)
        setStatus(Status::Slow, QStringLiteral("draws %1% of updates").arg(int(100 * (1 - m_skipShare))));
    else
        setStatus(Status::Ok, detail.isEmpty() ? m_where : detail);
    m_host->deliver(this, frame);
}

QString ScriptLayer::cost() const
{
    return QStringLiteral("%1 ms per update + %2 ms input").arg(m_lastUpdateMs, 0, 'f', 2).arg(m_lastMarshalMs, 0, 'f', 2);
}
