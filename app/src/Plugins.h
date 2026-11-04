#pragma once

#include "Overlay.h"
#include "qp/SimTypes.h"

#include <QAbstractItemModel>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class FrameStore;
class PluginHost;
class QJSEngine;
class Selection;

// plugin.json in a plugin's directory.
struct PluginManifest
{
    QString dir;
    QString id, name, description;
    QString kind; // item | script | process
    int apiMajor = 0, apiMinor = 0;
    QString entry;     // item, script: a QML file in dir
    QStringList exec;  // process: program and arguments, relative to dir
    QVariantMap properties; // script: initial properties of the plugin object
    QString error;     // non-empty: unusable

    static PluginManifest load(const QString &dir);
    QString api() const { return QStringLiteral("%1.%2").arg(apiMajor).arg(apiMinor); }
};

// One overlay layer from a plugin. Lives on the GUI thread; subclasses may run the plugin elsewhere.
class Layer : public QObject
{
    Q_OBJECT

public:
    enum class Status { Off, Starting, Ok, Slow, Suspended, Stopped, Error, Crashed, NotResponding, Failed, Refused };
    static QString statusName(Status s);

    Layer(const PluginManifest &m, PluginHost *host);

    const PluginManifest &manifest() const { return m_manifest; }
    Status status() const { return m_status; }
    QString detail() const { return m_detail; }
    virtual QString cost() const { return {}; }
    bool drawable() const { return m_status == Status::Ok || m_status == Status::Slow; }

    virtual void start() = 0;
    virtual void stop() = 0;
    // GUI thread, for every presented track set. tracksRecord: the set on the wire, encoded once.
    virtual void feed(const qp::TrackSetPtr &, int /*selectedId*/, const QByteArray & /*tracksRecord*/) {}
    virtual void selectionChanged(int) {}
    virtual void setPxPerMetre(double) {}

protected:
    void setStatus(Status s, const QString &detail = {});

    PluginManifest m_manifest;
    PluginHost *m_host;
    Status m_status = Status::Off;
    QString m_detail;
};

// The viewer's side of every plugin: finds them, checks them, feeds them, collects what they draw.
class PluginHost : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Plugins)
    QML_SINGLETON
    QML_UNCREATABLE("Provided by main() via QQmlEngine::setExternalSingletonInstance")

    Q_PROPERTY(QVariantList layers READ layers NOTIFY layersChanged FINAL)
    Q_PROPERTY(QString api READ api CONSTANT FINAL)
    Q_PROPERTY(QQuickItem *itemContainer READ itemContainer WRITE setItemContainer NOTIFY itemContainerChanged FINAL)
    Q_PROPERTY(double pxPerMetre READ pxPerMetre WRITE setPxPerMetre NOTIFY pxPerMetreChanged FINAL)

public:
    static constexpr int kApiMajor = 1;
    static constexpr int kApiMinor = 1;

    struct Options
    {
        QStringList dirs;
        QStringList enabled;          // layer ids on at start
        bool scriptOnGui = false;     // script plugins on the GUI thread instead of their own
        double budgetMs = 4;          // GUI-thread scripts: suspended after 3 updates over this
        double hardMs = 100;          // scripts: interrupted after this long in one call
        int silenceMs = 2000;         // processes: killed after this long without output
        bool sandbox = false;         // processes: run inside bwrap (no network, no home)
        QString logPath;              // every status change, one JSON object per line
        bool postEvery = false;       // processes: hand every overlay to the GUI thread, not only the newest
        int answerSlack = 50;         // processes: answers beyond inputs + this are a flood (0: no limit)
    };
    using EventSink = std::function<void(const QString &source, const QString &text, int severity)>;

    PluginHost(FrameStore *store, Selection *selection, QAbstractItemModel *tracksModel, Options options,
               EventSink events, QObject *parent = nullptr);
    ~PluginHost() override;

    void start(); // after the QML scene exists

    QVariantList layers() const { return m_snapshot; }
    QString api() const { return QStringLiteral("%1.%2").arg(kApiMajor).arg(kApiMinor); }
    QQuickItem *itemContainer() const { return m_itemContainer; }
    void setItemContainer(QQuickItem *item);
    double pxPerMetre() const { return m_pxPerMetre; }
    void setPxPerMetre(double v);
    Q_INVOKABLE void setEnabled(const QString &id, bool on);
    Layer *layer(const QString &id) const;

    // For LayerOverlay (GUI thread or render thread with the GUI blocked).
    struct Drawn
    {
        const Layer *layer;
        OverlayFramePtr frame;
    };
    std::vector<Drawn> drawn() const;

    // From layers, GUI thread.
    void deliver(Layer *layer, const OverlayFramePtr &frame);
    void layerChanged() { m_dirty = true; }
    void logStatus(const Layer *layer);
    void message(const Layer *layer, const QString &text, int severity);
    const Options &options() const { return m_options; }
    QAbstractItemModel *tracksModel() const { return m_tracksModel; }

    // Script engines the watchdog may interrupt. Any thread.
    struct Watched
    {
        std::mutex mutex;
        QJSEngine *engine = nullptr;
        std::atomic<qint64> busySinceNs{0};
        std::atomic<bool> fired{false};
    };
    void watch(const std::shared_ptr<Watched> &w);

signals:
    void layersChanged();
    void overlaysChanged();
    void itemContainerChanged();
    void pxPerMetreChanged();

private:
    void scan();
    void onTracks();
    void refresh();

    FrameStore *m_store;
    Selection *m_selection;
    QAbstractItemModel *m_tracksModel;
    Options m_options;
    EventSink m_events;
    std::vector<std::unique_ptr<Layer>> m_layers;
    std::map<const Layer *, OverlayFramePtr> m_frames;
    std::map<const Layer *, bool> m_enabled;
    QVariantList m_snapshot;
    bool m_dirty = true;
    QPointer<QQuickItem> m_itemContainer;
    double m_pxPerMetre = 7;
    // t -> when those tracks were presented, for the latency of each overlay.
    std::deque<std::pair<qint64, qint64>> m_presented;
    qint64 m_messageWindowNs = 0;
    qint64 m_startNs = 0;
    std::unique_ptr<QFile> m_log;
    int m_messagesInWindow = 0;

    std::thread m_watchdog;
    std::atomic<bool> m_quit{false};
    std::mutex m_watchMutex;
    std::vector<std::weak_ptr<Watched>> m_watched;
};

// The first implementation: a QML item in the viewer's own engine, over the top-down view. It gets
// the tracks model and the scale, and draws however it likes.
class ItemLayer : public Layer
{
    Q_OBJECT

public:
    using Layer::Layer;
    ~ItemLayer() override;
    void start() override;
    void stop() override;
    void setPxPerMetre(double v) override;
    QString cost() const override { return QStringLiteral("not measurable per layer"); }

private:
    QPointer<QObject> m_object;
};
