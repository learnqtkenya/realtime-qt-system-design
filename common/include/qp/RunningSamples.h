#pragma once

#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <vector>

namespace qp {

class RunningSamples
{
public:
    void add(double v)
    {
        ++m_added;
        if (m_capacity > 0 && m_values.size() == m_capacity) {
            // A window of the newest values: the oldest is replaced.
            m_sum += v - m_values[m_next];
            m_values[m_next] = v;
            m_next = (m_next + 1) % m_capacity;
            return;
        }
        m_values.push_back(v);
        m_sum += v;
    }

    void clear()
    {
        m_values.clear();
        m_sum = 0;
        m_next = 0;
        m_added = 0;
    }

    // 0: keep every value (benchmarks: exact percentiles over a bounded run).
    void setCapacity(size_t capacity)
    {
        m_capacity = capacity;
        if (capacity > 0 && m_values.size() > capacity) {
            m_values.erase(m_values.begin(), m_values.end() - qsizetype(capacity));
            m_sum = 0;
            for (double v : m_values)
                m_sum += v;
            m_next = 0;
        }
    }
    size_t added() const { return m_added; }

    size_t count() const { return m_values.size(); }
    bool empty() const { return m_values.empty(); }
    double mean() const { return empty() ? 0.0 : m_sum / double(m_values.size()); }
    double min() const { return empty() ? 0.0 : *std::min_element(m_values.begin(), m_values.end()); }
    double max() const { return empty() ? 0.0 : *std::max_element(m_values.begin(), m_values.end()); }
    const std::vector<double> &values() const { return m_values; }

    // p in [0, 100]
    double percentile(double p) const
    {
        if (empty())
            return 0.0;
        std::vector<double> sorted = m_values;
        std::sort(sorted.begin(), sorted.end());
        p = std::clamp(p, 0.0, 100.0);
        const double rank = p / 100.0 * double(sorted.size() - 1);
        const size_t lo = size_t(std::floor(rank));
        const size_t hi = size_t(std::ceil(rank));
        const double frac = rank - double(lo);
        return sorted[lo] + (sorted[hi] - sorted[lo]) * frac;
    }

    size_t countAbove(double threshold) const
    {
        return size_t(std::count_if(m_values.begin(), m_values.end(),
                                    [threshold](double v) { return v > threshold; }));
    }

    QJsonObject toJson() const
    {
        QJsonObject o;
        o["n"] = double(count());
        o["mean"] = mean();
        o["min"] = min();
        o["p50"] = percentile(50);
        o["p95"] = percentile(95);
        o["p99"] = percentile(99);
        o["max"] = max();
        return o;
    }

private:
    std::vector<double> m_values;
    double m_sum = 0;
    size_t m_capacity = 0, m_next = 0, m_added = 0;
};

} // namespace qp
