#include "TrackIndex.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

bool pointInBox(const TrackIndex::Box &b, float x, float y)
{
    const float dx = x - b.cx;
    const float dy = y - b.cy;
    const float lx = dx * b.cosH + dy * b.sinH; // into the box frame
    const float ly = -dx * b.sinH + dy * b.cosH;
    return std::abs(lx) <= b.halfLength && std::abs(ly) <= b.halfWidth;
}

} // namespace

float TrackIndex::heightFor(quint8 cls)
{
    switch (cls) {
    case 0: return 1.6f; // car
    case 1: return 3.2f; // truck
    case 2: return 1.7f; // cyclist
    default: return 1.8f; // pedestrian
    }
}

TrackIndex::TrackIndex(float cellSize, float halfExtent)
    : m_cell(cellSize), m_half(halfExtent), m_cells(int(std::ceil(2 * halfExtent / cellSize)))
{
    m_grid.resize(size_t(m_cells * m_cells));
}

int TrackIndex::cellOf(float v) const
{
    return std::clamp(int(std::floor((v + m_half) / m_cell)), 0, m_cells - 1);
}

void TrackIndex::rebuild(const std::vector<qp::Track> &tracks)
{
    m_boxes.clear();
    m_boxes.reserve(tracks.size());
    for (auto &cell : m_grid)
        cell.clear();
    for (const qp::Track &t : tracks) {
        Box b{t.id, t.x, t.y, kGroundZ, kGroundZ + heightFor(t.cls), t.length / 2, t.width / 2,
              std::cos(t.heading), std::sin(t.heading)};
        const int index = int(m_boxes.size());
        m_boxes.push_back(b);
        const float r = std::hypot(b.halfLength, b.halfWidth);
        for (int cy = cellOf(b.cy - r); cy <= cellOf(b.cy + r); ++cy)
            for (int cx = cellOf(b.cx - r); cx <= cellOf(b.cx + r); ++cx)
                m_grid[size_t(cellIndex(cx, cy))].push_back(index);
    }
    m_seen.assign(m_boxes.size(), 0);
    m_stamp = 0;
}

int TrackIndex::pickPoint(float x, float y) const
{
    if (std::abs(x) > m_half || std::abs(y) > m_half)
        return pickPointBrute(x, y); // outside the grid; rare, and still correct
    int best = -1;
    float bestD = kInf;
    for (int i : m_grid[size_t(cellIndex(cellOf(x), cellOf(y)))]) {
        const Box &b = m_boxes[size_t(i)];
        if (!pointInBox(b, x, y))
            continue;
        const float d = std::hypot(x - b.cx, y - b.cy);
        if (d < bestD) {
            bestD = d;
            best = int(b.id);
        }
    }
    return best;
}

int TrackIndex::pickPointBrute(float x, float y) const
{
    int best = -1;
    float bestD = kInf;
    for (const Box &b : m_boxes) {
        if (!pointInBox(b, x, y))
            continue;
        const float d = std::hypot(x - b.cx, y - b.cy);
        if (d < bestD) {
            bestD = d;
            best = int(b.id);
        }
    }
    return best;
}

// Slab test in the box's own frame.
bool TrackIndex::rayHitsBox(const Box &b, const QVector3D &o, const QVector3D &d, float *tEnter)
{
    const float ox = o.x() - b.cx, oy = o.y() - b.cy;
    const float lo[3] = {ox * b.cosH + oy * b.sinH, -ox * b.sinH + oy * b.cosH, o.z()};
    const float ld[3] = {d.x() * b.cosH + d.y() * b.sinH, -d.x() * b.sinH + d.y() * b.cosH, d.z()};
    const float mins[3] = {-b.halfLength, -b.halfWidth, b.z0};
    const float maxs[3] = {b.halfLength, b.halfWidth, b.z1};
    float t0 = 0.0f, t1 = kInf;
    for (int a = 0; a < 3; ++a) {
        if (std::abs(ld[a]) < 1e-12f) {
            if (lo[a] < mins[a] || lo[a] > maxs[a])
                return false;
            continue;
        }
        float ta = (mins[a] - lo[a]) / ld[a];
        float tb = (maxs[a] - lo[a]) / ld[a];
        if (ta > tb)
            std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1)
            return false;
    }
    *tEnter = t0;
    return true;
}

int TrackIndex::pickRayBrute(const QVector3D &o, const QVector3D &d, float *tOut) const
{
    int best = -1;
    float bestT = kInf;
    for (const Box &b : m_boxes) {
        float t;
        if (rayHitsBox(b, o, d, &t) && (t < bestT || (t == bestT && int(b.id) < best))) {
            bestT = t;
            best = int(b.id);
        }
    }
    if (tOut)
        *tOut = bestT;
    return best;
}

int TrackIndex::pickRay(const QVector3D &o, const QVector3D &d, float *tOut) const
{
    if (m_boxes.empty())
        return -1;

    // Only the part of the ray inside the grid's footprint and the boxes' height band matters.
    float t0 = 0.0f, t1 = kInf;
    const float zLo = kGroundZ, zHi = kGroundZ + 3.2f; // tallest class
    const float mins[3] = {-m_half, -m_half, zLo};
    const float maxs[3] = {m_half, m_half, zHi};
    const float os[3] = {o.x(), o.y(), o.z()};
    const float ds[3] = {d.x(), d.y(), d.z()};
    for (int a = 0; a < 3; ++a) {
        if (std::abs(ds[a]) < 1e-12f) {
            if (os[a] < mins[a] || os[a] > maxs[a])
                return -1;
            continue;
        }
        float ta = (mins[a] - os[a]) / ds[a];
        float tb = (maxs[a] - os[a]) / ds[a];
        if (ta > tb)
            std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
    }
    if (t0 > t1)
        return -1;

    // 2D DDA over the cells the segment crosses, nearest first.
    const float sx = o.x() + d.x() * t0, sy = o.y() + d.y() * t0;
    int cx = cellOf(sx), cy = cellOf(sy);
    const int stepX = d.x() > 0 ? 1 : -1, stepY = d.y() > 0 ? 1 : -1;
    auto boundaryT = [&](float pos, float dir, int cell, int step) {
        if (std::abs(dir) < 1e-12f)
            return kInf;
        const float edge = -m_half + float(cell + (step > 0 ? 1 : 0)) * m_cell;
        return (edge - pos) / dir;
    };
    float nextX = t0 + boundaryT(sx, d.x(), cx, stepX);
    float nextY = t0 + boundaryT(sy, d.y(), cy, stepY);
    const float dtX = std::abs(d.x()) < 1e-12f ? kInf : m_cell / std::abs(d.x());
    const float dtY = std::abs(d.y()) < 1e-12f ? kInf : m_cell / std::abs(d.y());

    if (++m_stamp == 0) { // wrapped: reset the stamps
        std::fill(m_seen.begin(), m_seen.end(), 0);
        m_stamp = 1;
    }
    int best = -1;
    float bestT = kInf;
    float cellExit = std::min(nextX, nextY);
    for (;;) {
        for (int i : m_grid[size_t(cellIndex(cx, cy))]) {
            if (m_seen[size_t(i)] == m_stamp)
                continue;
            m_seen[size_t(i)] = m_stamp;
            const Box &b = m_boxes[size_t(i)];
            float t;
            if (rayHitsBox(b, o, d, &t) && (t < bestT || (t == bestT && int(b.id) < best))) {
                bestT = t;
                best = int(b.id);
            }
        }
        // A hit before this cell's exit can't be beaten by boxes registered only in later cells.
        if (best >= 0 && bestT <= cellExit)
            break;
        if (cellExit > t1)
            break;
        if (nextX < nextY) {
            cx += stepX;
            nextX += dtX;
        } else {
            cy += stepY;
            nextY += dtY;
        }
        if (cx < 0 || cy < 0 || cx >= m_cells || cy >= m_cells)
            break;
        cellExit = std::min(nextX, nextY);
    }
    if (tOut)
        *tOut = bestT;
    return best;
}
