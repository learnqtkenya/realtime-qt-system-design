#include "qp/StageStats.h"

#include "qp/SimTypes.h"

namespace qp {

StageStats &StageStats::instance()
{
    static StageStats s;
    return s;
}

void StageStats::add(const QString &name, double value)
{
    std::lock_guard lock(m_mutex);
    m_samples[name].add(value);
}

void StageStats::reset()
{
    std::lock_guard lock(m_mutex);
    m_samples.clear();
}

QJsonObject StageStats::toJson() const
{
    std::lock_guard lock(m_mutex);
    QJsonObject o;
    for (const auto &[name, samples] : m_samples)
        o[name] = samples.toJson();
    return o;
}

qint64 Stages::nowNs() const
{
    return qp::nowNs();
}

void Stages::recordSince(const QString &name, qint64 startNs) const
{
    StageStats::instance().add(name, double(qp::nowNs() - startNs) / 1e6);
}

void Stages::gauge(const QString &name, double value) const
{
    StageStats::instance().add(name, value);
}

} // namespace qp
