#pragma once

#include "LidarHub.h"
#include "qp/SimTypes.h"

#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>

namespace qp {
class SensorSimulator;
}

// The lab panel's model: which renderer is live and how heavy the world is.
class Lab : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    QML_UNCREATABLE("Provided by main() via QQmlEngine::setExternalSingletonInstance")

    Q_PROPERTY(QString renderer READ renderer WRITE setRenderer NOTIFY rendererChanged FINAL)
    Q_PROPERTY(QVariantList renderers READ renderers CONSTANT FINAL)
    Q_PROPERTY(int maxPoints READ maxPoints NOTIFY rendererChanged FINAL)
    Q_PROPERTY(int pointsPerSensor READ pointsPerSensor WRITE setPointsPerSensor NOTIFY workloadChanged FINAL)
    Q_PROPERTY(double rateHz READ rateHz WRITE setRateHz NOTIFY workloadChanged FINAL)
    Q_PROPERTY(int objects READ objects WRITE setObjects NOTIFY workloadChanged FINAL)
    Q_PROPERTY(int eventsPerSecond READ eventsPerSecond WRITE setEventsPerSecond NOTIFY workloadChanged FINAL)
    Q_PROPERTY(int sensors READ sensors CONSTANT FINAL)
    Q_PROPERTY(bool chartsEnabled READ chartsEnabled CONSTANT FINAL)

public:
    Lab(LidarHub *hub, const qp::Workload &workload, const QString &renderer, bool chartsEnabled,
        QObject *parent = nullptr);

    // The workload is clamped to the renderer's cap before any simulator exists.
    const qp::Workload &workload() const { return m_workload; }
    void setSimulator(qp::SensorSimulator *sim) { m_sim = sim; }

    static QStringList rendererIds();

    QString renderer() const { return m_renderer; }
    void setRenderer(const QString &id);
    QVariantList renderers() const;
    int maxPoints() const;

    int pointsPerSensor() const { return m_workload.pointsPerFrame; }
    void setPointsPerSensor(int n);
    double rateHz() const { return m_workload.lidarRateHz; }
    void setRateHz(double hz);
    int objects() const { return m_workload.objects; }
    void setObjects(int n);
    int eventsPerSecond() const { return m_workload.eventsPerSecond; }
    void setEventsPerSecond(int n);
    int sensors() const { return m_workload.sensors; }
    bool chartsEnabled() const { return m_chartsEnabled; }

    Q_INVOKABLE void resetToNominal();
    // Cycles through every renderer, one every intervalSec; used by the switch soak.
    Q_INVOKABLE void startCycle(double intervalSec, const QStringList &ids = {});
    // Alternates points per sensor between 20k and the renderer's cap; used by the load-change test.
    Q_INVOKABLE void startLoadCycle(double intervalSec);
    // The frame monitor saw new content on screen.
    Q_INVOKABLE void noteContentShown();

signals:
    void rendererChanged();
    void workloadChanged();

private:
    void workloadEdited();
    void enforceCap();

    qp::SensorSimulator *m_sim = nullptr;
    LidarHub *m_hub;
    qp::Workload m_workload;
    QString m_renderer;
    bool m_chartsEnabled;
    QTimer m_apply; // debounce: sliders emit many values per second
    QTimer m_cycle;
    QStringList m_cycleIds;
    QTimer m_loadCycle;
    int64_t m_switchedAtNs = 0;
};
