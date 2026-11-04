#pragma once

#include "qp/SimTypes.h"

#include <cmath>

// Ingest checks: whatever fails is rejected before views, history or recordings see it.
namespace validate {

constexpr float kMaxRangeM = 1000;

// The whole frame or nothing: a frame with bad points says the sensor is in trouble.
inline bool lidar(const qp::SensorFrame &f)
{
    if (!f.cloud || f.cloud->xyz.size() % 3 != 0)
        return false;
    // Counted, without an early exit: GCC vectorises an int sum, not a bool reduction (step 4).
    // |v| <= max is false for NaN; infinities fail the range.
    int bad = 0;
    for (float v : f.cloud->xyz)
        bad += !(std::abs(v) <= kMaxRangeM);
    return bad == 0;
}

// Bad tracks are removed, the rest kept. Returns how many were removed; out is null if none were.
inline int tracks(const qp::TrackSetPtr &in, qp::TrackSetPtr *out)
{
    *out = nullptr;
    if (!in)
        return 0;
    auto good = [](const qp::Track &t) {
        return std::isfinite(t.x) && std::isfinite(t.y) && std::isfinite(t.heading) && std::isfinite(t.speed)
               && std::abs(t.x) <= kMaxRangeM && std::abs(t.y) <= kMaxRangeM;
    };
    int bad = 0;
    for (const qp::Track &t : in->tracks)
        bad += !good(t);
    if (bad == 0)
        return 0;
    auto copy = std::make_shared<qp::TrackSet>(*in);
    std::erase_if(copy->tracks, [&](const qp::Track &t) { return !good(t); });
    *out = std::move(copy);
    return bad;
}

} // namespace validate
