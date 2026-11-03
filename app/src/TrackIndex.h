#pragma once

#include "qp/SimTypes.h"

#include <QVector3D>

#include <vector>

// Domain-side picking: a uniform grid over the tracks' footprints, world coordinates
// (metres, z up). Independent of any renderer, so every view picks the same way.
class TrackIndex
{
public:
    static constexpr float kGroundZ = -1.8f; // the lidar sits 1.8 m above the ground
    static float heightFor(quint8 cls);

    struct Box
    {
        quint32 id;
        float cx, cy, z0, z1;
        float halfLength, halfWidth;
        float cosH, sinH;
    };

    explicit TrackIndex(float cellSize = 4.0f, float halfExtent = 64.0f);

    void rebuild(const std::vector<qp::Track> &tracks);
    size_t size() const { return m_boxes.size(); }
    const std::vector<Box> &boxes() const { return m_boxes; }

    // Top-down: the box containing (x, y); if several, the one whose centre is nearest. -1 if none.
    int pickPoint(float x, float y) const;
    // 3D: the first box the ray enters (t >= 0). -1 if none. tOut receives the entry distance.
    int pickRay(const QVector3D &origin, const QVector3D &direction, float *tOut = nullptr) const;

    // Same answers without the grid; for tests and for checking the grid.
    int pickPointBrute(float x, float y) const;
    int pickRayBrute(const QVector3D &origin, const QVector3D &direction, float *tOut = nullptr) const;

    static bool rayHitsBox(const Box &b, const QVector3D &o, const QVector3D &d, float *tEnter);

private:
    int cellIndex(int cx, int cy) const { return cy * m_cells + cx; }
    int cellOf(float v) const;

    float m_cell;
    float m_half;
    int m_cells;
    std::vector<Box> m_boxes;
    std::vector<std::vector<int>> m_grid;
    mutable std::vector<quint32> m_seen; // per-box stamp so a box spanning cells is tested once
    mutable quint32 m_stamp = 0;
};
