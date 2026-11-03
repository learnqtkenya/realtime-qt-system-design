#pragma once

#include "qp/RunningSamples.h"

#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QString>

#include <map>
#include <mutex>

namespace qp {

// Thread-safe. Names carry their unit, e.g. "convert_ms".
class StageStats
{
public:
    static StageStats &instance();

    void add(const QString &name, double value);
    void reset();
    QJsonObject toJson() const;

private:
    mutable std::mutex m_mutex;
    std::map<QString, RunningSamples> m_samples;
};

class Stages : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    // Exact as a JS number for ~104 days of uptime.
    Q_INVOKABLE qint64 nowNs() const;
    Q_INVOKABLE void recordSince(const QString &name, qint64 startNs) const;
    Q_INVOKABLE void gauge(const QString &name, double value) const;
};

} // namespace qp
