#pragma once
#include "StlMesh.h"
#include "Vec3.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace cn {

/// Exact point-to-triangle distance (Ericson, *Real-Time Collision Detection*).
/// Branches over the seven Voronoi regions of a triangle: three vertices,
/// three edges, and the face interior.
inline float pointTriangleDistance(const Vec3 &p, const Vec3 &a, const Vec3 &b, const Vec3 &c) {
    const Vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = ab.dot(ap), d2 = ac.dot(ap);
    if (d1 <= 0.f && d2 <= 0.f) return (p - a).length();

    const Vec3 bp = p - b;
    const float d3 = ab.dot(bp), d4 = ac.dot(bp);
    if (d3 >= 0.f && d4 <= d3) return (p - b).length();

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f)
        return (p - (a + ab * (d1 / (d1 - d3)))).length();

    const Vec3 cp = p - c;
    const float d5 = ab.dot(cp), d6 = ac.dot(cp);
    if (d6 >= 0.f && d5 <= d6) return (p - c).length();

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f)
        return (p - (a + ac * (d2 / (d2 - d6)))).length();

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f)
        return (p - (b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6))))).length();

    const float denom = 1.f / (va + vb + vc);
    return (p - (a + ab * (vb * denom) + ac * (vc * denom))).length();
}

/// Uniform-grid index over a mesh's triangles, for nearest-surface queries.
///
/// Used to measure the *inscribed* radius at each centerline node: the largest
/// sphere centred there that still fits inside the vessel. That is the only
/// radius definition a containment constraint can safely use - a cross-section
/// mean over-states the room available wherever the node sits off the medial
/// axis, and the device then tracks straight through the wall.
class MeshDistanceField {
public:
    void build(const StlMesh &mesh, float cellSize) {
        m_mesh = &mesh;
        m_cell = std::max(cellSize, 1e-6f);
        m_buckets.clear();
        for (size_t t = 0; t < mesh.triangles().size(); ++t) {
            const auto &tri = mesh.triangles()[t];
            Box3 bb;
            for (int k = 0; k < 3; ++k) bb.extend(mesh.vertices()[tri.v[k]]);
            for (long x = cell(bb.lo.x); x <= cell(bb.hi.x); ++x)
                for (long y = cell(bb.lo.y); y <= cell(bb.hi.y); ++y)
                    for (long z = cell(bb.lo.z); z <= cell(bb.hi.z); ++z)
                        m_buckets[key(x, y, z)].push_back(static_cast<uint32_t>(t));
        }
    }

    bool valid() const { return m_mesh != nullptr; }

    /// Distance from `p` to the nearest point on the surface. Searches
    /// outward in shells and stops once the ring being examined cannot
    /// contain anything closer than the best found so far.
    float distance(const Vec3 &p, int maxRings = 6) const {
        if (!m_mesh) return 0.f;
        float best = std::numeric_limits<float>::max();
        const long cx = cell(p.x), cy = cell(p.y), cz = cell(p.z);

        for (int r = 0; r <= maxRings; ++r) {
            // Anything in ring r is at least (r-1)*cell away, so once best is
            // inside that bound no further ring can improve it.
            if (best < static_cast<float>(r - 1) * m_cell) break;
            for (long x = cx - r; x <= cx + r; ++x)
                for (long y = cy - r; y <= cy + r; ++y)
                    for (long z = cz - r; z <= cz + r; ++z) {
                        // shell only - skip the interior already visited
                        if (r > 0 && std::labs(x - cx) != r && std::labs(y - cy) != r &&
                            std::labs(z - cz) != r)
                            continue;
                        auto it = m_buckets.find(key(x, y, z));
                        if (it == m_buckets.end()) continue;
                        for (uint32_t t : it->second) {
                            const auto &tri = m_mesh->triangles()[t];
                            best = std::min(best, pointTriangleDistance(
                                                      p, m_mesh->vertices()[tri.v[0]],
                                                      m_mesh->vertices()[tri.v[1]],
                                                      m_mesh->vertices()[tri.v[2]]));
                        }
                    }
        }
        return best == std::numeric_limits<float>::max() ? 0.f : best;
    }

private:
    long cell(float v) const { return static_cast<long>(std::floor(v / m_cell)); }
    static long long key(long x, long y, long z) {
        return (static_cast<long long>(x) * 73856093LL) ^
               (static_cast<long long>(y) * 19349663LL) ^
               (static_cast<long long>(z) * 83492791LL);
    }

    const StlMesh *m_mesh = nullptr;
    float m_cell = 1.f;
    std::unordered_map<long long, std::vector<uint32_t>> m_buckets;
};

} // namespace cn
