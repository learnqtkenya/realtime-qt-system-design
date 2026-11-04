#include "Plugins.h"

#include "FrameStore.h"
#include "ProcessLayer.h"
#include "ScriptLayer.h"
#include "Selection.h"
#include "Wire.h"
#include "qp/StageStats.h"

#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QRegularExpression>
#include <QTimer>

PluginManifest PluginManifest::load(const QString &dir)
{
    PluginManifest m;
    m.dir = QDir::cleanPath(QDir(dir).absolutePath());
    m.id = QDir(dir).dirName();
    m.name = m.id;
    QFile f(QDir(dir).filePath(QStringLiteral("plugin.json")));
    if (!f.open(QIODevice::ReadOnly)) {
        m.error = QStringLiteral("no plugin.json");
        return m;
    }
    QJsonParseError pe;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll(), &pe).object();
    if (pe.error != QJsonParseError::NoError) {
        m.error = QStringLiteral("plugin.json: ") + pe.errorString();
        return m;
    }
    m.id = o.value(u"id").toString(m.id);
    m.name = o.value(u"name").toString(m.id);
    m.description = o.value(u"description").toString();
    m.kind = o.value(u"kind").toString();
    m.entry = o.value(u"entry").toString();
    for (const QJsonValue &v : o.value(u"exec").toArray())
        m.exec << v.toString();
    m.properties = o.value(u"properties").toObject().toVariantMap();
    static const QRegularExpression idPattern(QStringLiteral("^[a-z0-9][a-z0-9-]{0,40}$"));
    static const QRegularExpression apiPattern(QStringLiteral("^(\\d+)\\.(\\d+)$"));
    const auto api = apiPattern.match(o.value(u"api").toString());
    if (!idPattern.match(m.id).hasMatch())
        m.error = QStringLiteral("id must be lower-case letters, digits and dashes");
    else if (!api.hasMatch())
        m.error = QStringLiteral("api must be \"major.minor\"");
    else if (m.kind != u"item" && m.kind != u"script" && m.kind != u"process")
        m.error = QStringLiteral("kind must be item, script or process");
    else if (m.kind != u"process" && (m.entry.isEmpty() || m.entry.contains(u"..") || QDir::isAbsolutePath(m.entry)
                                      || !QFile::exists(QDir(m.dir).filePath(m.entry))))
        m.error = QStringLiteral("entry must name a file in the plugin's directory");
    else if (m.kind == u"script" && !m.entry.endsWith(u".mjs") && !m.entry.endsWith(u".qml"))
        m.error = QStringLiteral("a script's entry is a JavaScript module (.mjs) or a QML file");
    else if (m.kind == u"item" && !m.entry.endsWith(u".qml"))
        m.error = QStringLiteral("an item's entry is a QML file");
    else if (m.kind == u"process" && m.exec.isEmpty())
        m.error = QStringLiteral("exec is empty");
    if (api.hasMatch()) {
        m.apiMajor = api.captured(1).toInt();
        m.apiMinor = api.captured(2).toInt();
    }
    return m;
}

QString Layer::statusName(Status s)
{
    switch (s) {
    case Status::Off: return QStringLiteral("off");
    case Status::Starting: return QStringLiteral("starting");
    case Status::Ok: return QStringLiteral("ok");
    case Status::Slow: return QStringLiteral("slow");
    case Status::Suspended: return QStringLiteral("suspended");
    case Status::Stopped: return QStringLiteral("stopped");
    case Status::Error: return QStringLiteral("error");
    case Status::Crashed: return QStringLiteral("crashed");
    case Status::NotResponding: return QStringLiteral("not responding");
    case Status::Failed: return QStringLiteral("failed");
    case Status::Refused: return QStringLiteral("refused");
    }
    return {};
}

Layer::Layer(const PluginManifest &m, PluginHost *host) : m_manifest(m), m_host(host) {}

void Layer::setStatus(Status s, const QString &detail)
{
    if (s == m_status && detail == m_detail)
        return;
    const Status from = m_status;
    m_status = s;
    m_detail = detail;
    m_host->layerChanged();
    m_host->logStatus(this);
    if (s != from && s != Status::Starting && s != Status::Ok && s != Status::Off)
        m_host->message(this, statusName(s) + (detail.isEmpty() ? QString() : QStringLiteral(": ") + detail),
                        s == Status::Slow ? 1 : 2);
    if (s != from)
        qp::StageStats::instance().add(QStringLiteral("layer_%1_%2").arg(m_manifest.id, statusName(s).replace(u' ', u'_')),
                                       0);
}

namespace {

class RefusedLayer : public Layer
{
public:
    RefusedLayer(const PluginManifest &m, PluginHost *host, const QString &why) : Layer(m, host) { m_why = why; }
    void start() override { setStatus(Status::Refused, m_why); }
    void stop() override { setStatus(Status::Refused, m_why); }

private:
    QString m_why;
};

} // namespace

PluginHost::PluginHost(FrameStore *store, Selection *selection, QAbstractItemModel *tracksModel, Options options,
                       EventSink events, QObject *parent)
    : QObject(parent), m_store(store), m_selection(selection), m_tracksModel(tracksModel),
      m_options(std::move(options)), m_events(std::move(events))
{
    m_startNs = qp::nowNs();
    if (!m_options.logPath.isEmpty()) {
        m_log = std::make_unique<QFile>(m_options.logPath);
        if (!m_log->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            m_log.reset();
    }
    m_watchdog = std::thread([this] {
        while (!m_quit.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            const qint64 now = qp::nowNs();
            const auto hard = qint64(m_options.hardMs * 1e6);
            std::lock_guard lk(m_watchMutex);
            std::erase_if(m_watched, [](const auto &w) { return w.expired(); });
            for (const auto &weak : m_watched) {
                const auto w = weak.lock();
                const qint64 since = w ? w->busySinceNs.load() : 0;
                if (since == 0 || now - since < hard || w->fired.load())
                    continue;
                std::lock_guard engineLock(w->mutex);
                if (w->engine) {
                    w->engine->setInterrupted(true);
                    w->fired = true;
                }
            }
        }
    });
}

PluginHost::~PluginHost()
{
    for (auto &l : m_layers)
        l->stop();
    m_layers.clear();
    m_quit = true;
    m_watchdog.join();
}

void PluginHost::logStatus(const Layer *layer)
{
    if (!m_log)
        return;
    const QJsonObject o{{QStringLiteral("t"), double(qp::nowNs() - m_startNs) / 1e9},
                        {QStringLiteral("layer"), layer->manifest().id},
                        {QStringLiteral("status"), Layer::statusName(layer->status())},
                        {QStringLiteral("detail"), layer->detail()}};
    m_log->write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
    m_log->flush();
}

void PluginHost::watch(const std::shared_ptr<Watched> &w)
{
    std::lock_guard lk(m_watchMutex);
    m_watched.push_back(w);
}

void PluginHost::start()
{
    scan();
    connect(m_store, &FrameStore::tracksChanged, this, &PluginHost::onTracks);
    if (m_selection)
        connect(m_selection, &Selection::trackIdChanged, this, [this] {
            for (auto &l : m_layers)
                if (m_enabled[l.get()])
                    l->selectionChanged(m_selection->trackId());
        });
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &PluginHost::refresh);
    timer->start(500);
    for (const QString &id : m_options.enabled)
        setEnabled(id, true);
    refresh();
}

void PluginHost::scan()
{
    QSet<QString> seen;
    for (const QString &root : m_options.dirs) {
        const QDir d(root);
        for (const QString &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (!QFile::exists(d.filePath(sub + QStringLiteral("/plugin.json"))))
                continue;
            PluginManifest m = PluginManifest::load(d.filePath(sub));
            QString why = m.error;
            if (why.isEmpty() && seen.contains(m.id))
                why = QStringLiteral("another plugin has the id %1").arg(m.id);
            if (why.isEmpty() && (m.apiMajor != kApiMajor || m.apiMinor > kApiMinor))
                why = QStringLiteral("needs API %1; this viewer offers %2").arg(m.api(), api());
            seen.insert(m.id);
            std::unique_ptr<Layer> layer;
            if (!why.isEmpty())
                layer = std::make_unique<RefusedLayer>(m, this, why);
            else if (m.kind == u"item")
                layer = std::make_unique<ItemLayer>(m, this);
            else if (m.kind == u"script")
                layer = std::make_unique<ScriptLayer>(m, this);
            else
                layer = std::make_unique<ProcessLayer>(m, this);
            if (!why.isEmpty())
                layer->start();
            m_enabled[layer.get()] = false;
            m_layers.push_back(std::move(layer));
        }
    }
    m_dirty = true;
}

Layer *PluginHost::layer(const QString &id) const
{
    for (const auto &l : m_layers)
        if (l->manifest().id == id)
            return l.get();
    return nullptr;
}

void PluginHost::setEnabled(const QString &id, bool on)
{
    Layer *l = layer(id);
    if (!l || l->status() == Layer::Status::Refused || m_enabled[l] == on)
        return;
    m_enabled[l] = on;
    m_frames.erase(l);
    if (on) {
        l->setPxPerMetre(m_pxPerMetre);
        l->start();
        if (const qp::TrackSetPtr set = m_store->latestTracks()) {
            const QByteArray record = l->manifest().kind == u"process" ? wire::tracksRecord(set) : QByteArray();
            l->feed(set, m_selection ? m_selection->trackId() : -1, record);
        }
    } else {
        l->stop();
    }
    m_dirty = true;
    refresh();
    emit overlaysChanged();
}

void PluginHost::setItemContainer(QQuickItem *item)
{
    if (item == m_itemContainer)
        return;
    m_itemContainer = item;
    emit itemContainerChanged();
}

void PluginHost::setPxPerMetre(double v)
{
    if (qFuzzyCompare(v, m_pxPerMetre))
        return;
    m_pxPerMetre = v;
    for (auto &l : m_layers)
        if (m_enabled[l.get()])
            l->setPxPerMetre(v);
    emit pxPerMetreChanged();
}

void PluginHost::onTracks()
{
    const qp::TrackSetPtr set = m_store->latestTracks();
    if (!set)
        return;
    m_presented.emplace_back(set->captureNs, qp::nowNs());
    while (m_presented.size() > 64)
        m_presented.pop_front();
    QByteArray record;
    const int selected = m_selection ? m_selection->trackId() : -1;
    for (auto &l : m_layers) {
        if (!m_enabled[l.get()])
            continue;
        if (record.isEmpty() && l->manifest().kind == u"process")
            record = wire::tracksRecord(set);
        l->feed(set, selected, record);
    }
}

void PluginHost::deliver(Layer *layer, const OverlayFramePtr &frame)
{
    if (!m_enabled[layer] || !frame)
        return;
    const qint64 now = qp::nowNs();
    for (auto it = m_presented.rbegin(); it != m_presented.rend(); ++it) {
        if (it->first == frame->t) {
            qp::StageStats::instance().add(QStringLiteral("layer_%1_latency_ms").arg(layer->manifest().id),
                                           double(now - it->second) / 1e6);
            break;
        }
    }
    m_frames[layer] = frame;
    emit overlaysChanged();
}

void PluginHost::message(const Layer *layer, const QString &text, int severity)
{
    const qint64 now = qp::nowNs();
    if (now - m_messageWindowNs > 1'000'000'000) {
        m_messageWindowNs = now;
        m_messagesInWindow = 0;
    }
    if (++m_messagesInWindow > 20)
        return;
    if (m_log) {
        const QJsonObject o{{QStringLiteral("t"), double(now - m_startNs) / 1e9},
                            {QStringLiteral("layer"), layer->manifest().id},
                            {QStringLiteral("message"), text}};
        m_log->write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
        m_log->flush();
    }
    if (m_events)
        m_events(QStringLiteral("layer ") + layer->manifest().id, text, severity);
}

std::vector<PluginHost::Drawn> PluginHost::drawn() const
{
    std::vector<Drawn> out;
    const qint64 newest = m_presented.empty() ? 0 : m_presented.back().first;
    for (const auto &l : m_layers) {
        const auto it = m_frames.find(l.get());
        if (it == m_frames.end() || !l->drawable())
            continue;
        // An overlay for tracks more than a second older than the ones shown is not drawn.
        if (newest - it->second->t > 1'000'000'000)
            continue;
        out.push_back({l.get(), it->second});
    }
    return out;
}

void PluginHost::refresh()
{
    QVariantList list;
    for (const auto &l : m_layers) {
        const PluginManifest &m = l->manifest();
        list.append(QVariantMap{{QStringLiteral("id"), m.id},
                                {QStringLiteral("name"), m.name},
                                {QStringLiteral("kind"), m.kind},
                                {QStringLiteral("api"), m.api()},
                                {QStringLiteral("enabled"), m_enabled.at(l.get())},
                                {QStringLiteral("status"), Layer::statusName(l->status())},
                                {QStringLiteral("detail"), l->detail()},
                                {QStringLiteral("cost"), m_enabled.at(l.get()) ? l->cost() : QString()}});
    }
    if (list != m_snapshot) {
        m_snapshot = list;
        emit layersChanged();
    }
    m_dirty = false;
}

ItemLayer::~ItemLayer()
{
    delete m_object;
}

void ItemLayer::start()
{
    QQuickItem *container = m_host->itemContainer();
    QQmlEngine *engine = container ? qmlEngine(container) : nullptr;
    if (!engine) {
        setStatus(Status::Error, QStringLiteral("no view to draw on"));
        return;
    }
    QQmlComponent component(engine, QUrl::fromLocalFile(QDir(m_manifest.dir).filePath(m_manifest.entry)));
    QObject *o = component.isReady() ? component.beginCreate(qmlContext(container)) : nullptr;
    auto *item = qobject_cast<QQuickItem *>(o);
    if (!item) {
        if (o)
            component.completeCreate();
        delete o;
        setStatus(Status::Error, component.isError() ? component.errors().value(0).toString()
                                                     : QStringLiteral("entry is not an Item"));
        return;
    }
    // Parented before its bindings run, so "anchors.fill: parent" works.
    item->setParentItem(container);
    component.setInitialProperties(item, {{QStringLiteral("tracks"), QVariant::fromValue(m_host->tracksModel())},
                                          {QStringLiteral("pxPerMetre"), m_host->pxPerMetre()}});
    component.completeCreate();
    m_object = item;
    setStatus(Status::Ok, QStringLiteral("in the viewer's engine"));
}

void ItemLayer::stop()
{
    delete m_object;
    setStatus(Status::Off);
}

void ItemLayer::setPxPerMetre(double v)
{
    if (m_object)
        m_object->setProperty("pxPerMetre", v);
}
