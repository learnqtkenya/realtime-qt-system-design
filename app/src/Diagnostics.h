#pragma once

#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QThread>
#include <QVariantMap>

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

// What someone needs to understand a running viewer, or a problem report about one: build
// identity, resources, the state of every part, and recent log lines. Sampled once a second on
// the GUI thread; served as text on a localhost port (its own thread); saved as one JSON file.
class Diagnostics : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    QML_UNCREATABLE("Provided by main() via QQmlEngine::setExternalSingletonInstance")

    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed FINAL)
    Q_PROPERTY(QStringList recentLog READ recentLog NOTIFY changed FINAL)
    Q_PROPERTY(QString logPath READ logPath CONSTANT FINAL)
    Q_PROPERTY(QString metricsUrl READ metricsUrl NOTIFY metricsUrlChanged FINAL)

public:
    explicit Diagnostics(QObject *parent = nullptr);
    ~Diagnostics() override;

    // Every qDebug/qWarning from now on also goes to <dir>/viewer.log, as JSON lines, rotated at
    // maxBytes (keeping `keep` old files).
    void installLog(const QString &dir, qint64 maxBytes = 5'000'000, int keep = 3);
    // State changes worth keeping (health, plugins, connections): to the log, at info level.
    void note(const QString &source, const QString &text);

    void setBuild(const QJsonObject &build) { m_build = build; }
    void setStartup(const QString &key, double ms);
    void addSection(const QString &name, std::function<QJsonObject()> sample);
    void start();
    bool startMetrics(quint16 port, QString *error);

    QVariantMap snapshot() const;
    QStringList recentLog() const;
    QString logPath() const { return m_logPath; }
    QString metricsUrl() const { return m_metricsUrl; }

    QJsonObject bundle() const;
    // Returns the path written, or an empty string.
    Q_INVOKABLE QString saveBundle(const QString &path = {});
    void setBundleDir(const QString &dir) { m_bundleDir = dir; }
    void setExtraBundle(const QString &key, std::function<QJsonValue()> f) { m_extraBundle[key] = std::move(f); }

    // Text for the metrics endpoint: "qp_<section>_<key> <value>" per number (any thread).
    QByteArray metricsText() const;

    static double msSinceProcessStart();

signals:
    void changed();
    void metricsUrlChanged();

private:
    void sample();
    void sampleProcess();
    void writeLog(const QByteArray &line);

    QJsonObject m_build;
    QJsonObject m_startup;
    std::map<QString, std::function<QJsonObject()>> m_sections;
    std::map<QString, std::function<QJsonValue()>> m_extraBundle;
    mutable std::mutex m_mutex; // m_current, m_recent, the log file
    QJsonObject m_current;
    QStringList m_recent;
    QString m_logPath, m_logDir, m_bundleDir;
    qint64 m_logMax = 0;
    int m_logKeep = 0;
    class QFile *m_log = nullptr;
    QString m_metricsUrl;
    QThread m_metricsThread;
    QObject *m_metricsServer = nullptr;
    QJsonObject m_process; // from the sampler thread, under m_mutex
    std::thread m_sampler;
    std::mutex m_samplerMutex;
    std::condition_variable m_samplerWake;
    bool m_samplerStop = false;
};
