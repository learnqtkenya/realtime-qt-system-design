#pragma once

#include "qp/SimTypes.h"

#include <QObject>
#include <QQmlEngine>
#include <QStringList>

namespace qp {

class AppOptions : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool bench READ bench CONSTANT FINAL)
    Q_PROPERTY(double warmupSec READ warmupSec CONSTANT FINAL)
    Q_PROPERTY(double durationSec READ durationSec CONSTANT FINAL)
    Q_PROPERTY(QString outPath READ outPath CONSTANT FINAL)
    Q_PROPERTY(QString label READ label CONSTANT FINAL)
    Q_PROPERTY(bool noSim READ noSim CONSTANT FINAL)
    Q_PROPERTY(int sensors READ sensors CONSTANT FINAL)
    Q_PROPERTY(int pointsPerFrame READ pointsPerFrame CONSTANT FINAL)
    Q_PROPERTY(double lidarRateHz READ lidarRateHz CONSTANT FINAL)
    Q_PROPERTY(int objects READ objects CONSTANT FINAL)
    Q_PROPERTY(int eventsPerSecond READ eventsPerSecond CONSTANT FINAL)
    Q_PROPERTY(QString graphicsApi READ graphicsApi CONSTANT FINAL)
    Q_PROPERTY(QString policy READ policy CONSTANT FINAL)
    Q_PROPERTY(int queueDepth READ queueDepth CONSTANT FINAL)
    Q_PROPERTY(int workers READ workers CONSTANT FINAL)
    Q_PROPERTY(QString variant READ variant CONSTANT FINAL)

public:
    static AppOptions *instance();
    static AppOptions *create(QQmlEngine *, QJSEngine *);

    bool parse(const QStringList &arguments);

    // Call before any window is created.
    void applyGraphicsApi() const;

    const Workload &workload() const { return m_workload; }
    bool bench() const { return m_bench; }
    double warmupSec() const { return m_warmupSec; }
    double durationSec() const { return m_durationSec; }
    QString outPath() const { return m_outPath; }
    QString label() const { return m_label; }
    bool noSim() const { return m_noSim; }
    int sensors() const { return m_workload.sensors; }
    int pointsPerFrame() const { return m_workload.pointsPerFrame; }
    double lidarRateHz() const { return m_workload.lidarRateHz; }
    int objects() const { return m_workload.objects; }
    int eventsPerSecond() const { return m_workload.eventsPerSecond; }
    QString graphicsApi() const { return m_graphicsApi; }
    QString policy() const { return m_policy; }
    int queueDepth() const { return m_queueDepth; }
    int workers() const { return m_workers; }
    QString variant() const { return m_variant; }
    QString grabPath() const { return m_grabPath; }
    double grabAfterSec() const { return m_grabAfterSec; }
    // --set key=value (repeatable): app-specific settings.
    Q_INVOKABLE QVariant extra(const QString &key, const QVariant &fallback = {}) const;
    // Values are strings; in QML "0" would be true. This converts the way C++ does ("0", "false": false).
    Q_INVOKABLE bool flag(const QString &key, bool fallback) const { return extra(key, fallback).toBool(); }

    Q_INVOKABLE QVariantMap workloadMap() const;

private:
    AppOptions() = default;

    Workload m_workload;
    bool m_bench = false;
    double m_warmupSec = 3.0;
    double m_durationSec = 20.0;
    QString m_outPath = QStringLiteral("report.json");
    QString m_label;
    bool m_noSim = false;
    QString m_graphicsApi = QStringLiteral("default");
    QString m_policy = QStringLiteral("drop-oldest");
    int m_queueDepth = 1;
    int m_workers = 4;
    QString m_variant;
    QString m_grabPath;
    double m_grabAfterSec = 3.0;
    QVariantMap m_extra;
};

} // namespace qp
