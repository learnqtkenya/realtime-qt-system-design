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
    auto [it, inserted] = m_samples.try_emplace(name);
    if (inserted)
        it->second.setCapacity(m_capacity);
    it->second.add(value);
}

void StageStats::setCapacity(size_t capacity)
{
    std::lock_guard lock(m_mutex);
    m_capacity = capacity;
    for (auto &[name, s] : m_samples)
        s.setCapacity(capacity);
}

size_t StageStats::storedValues() const
{
    std::lock_guard lock(m_mutex);
    size_t n = 0;
    for (const auto &[name, s] : m_samples)
        n += s.count();
    return n;
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
    for (const auto &[name, samples] : m_samples) {
        QJsonObject j = samples.toJson();
        // Trend check: a drifting quantity has different first and last quarters.
        const std::vector<double> &v = samples.values();
        if (v.size() >= 8) {
            const size_t q = v.size() / 4;
            double first = 0, last = 0;
            for (size_t i = 0; i < q; ++i) {
                first += v[i];
                last += v[v.size() - q + i];
            }
            j["firstQuarterMean"] = first / double(q);
            j["lastQuarterMean"] = last / double(q);
        }
        o[name] = j;
    }
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
