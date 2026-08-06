#pragma once
#include "Vec3.h"
#include <vector>
#include <cstdint>
#include <string>

namespace cn {

/// A path through the vessel tree, resampled at uniform arc length.
/// This is the object the catheter actually rides on.
struct CenterlinePath {
    std::vector<Vec3> points;
    std::vector<float> radii;      ///< lumen radius at each point
    std::vector<float> arcLength;  ///< cumulative length, arcLength[0] == 0
    std::vector<int32_t> nodeId;   ///< originating graph node, for branch read-out

    bool empty() const { return points.size() < 2; }
    float totalLength() const { return arcLength.empty() ? 0.f : arcLength.back(); }

    /// Position at arc length s, clamped to [0, totalLength()].
    Vec3 pointAt(float s) const;
    /// Lumen radius at arc length s.
    float radiusAt(float s) const;
    /// Unit tangent (direction of advancement) at arc length s.
    Vec3 tangentAt(float s) const;
    /// Graph node nearest to arc length s.
    int32_t nodeAt(float s) const;

    /// Resample to a fixed spacing; used after planning so that catheter
    /// segment length maps cleanly onto path samples.
    CenterlinePath resampled(float spacing) const;

private:
    /// Returns segment index i such that arcLength[i] <= s <= arcLength[i+1],
    /// and writes the local parameter into `t`.
    size_t locate(float s, float &t) const;
};

/// Undirected graph of centerline nodes: positions, lumen radii, and the
/// connectivity recovered from the surface mesh.
class CenterlineGraph {
public:
    struct Node {
        Vec3 position;
        float radius = 0.f;
        int32_t meshComponent = -1;  ///< which STL shell this came from
        int32_t branchId = -1;       ///< filled by labelBranches()
    };

    struct Link {
        int32_t from = -1, to = -1;
        float length = 0.f;
    };

    void clear();
    int32_t addNode(const Vec3 &p, float radius, int32_t meshComponent);
    void addLink(int32_t a, int32_t b);
    void buildAdjacency();

    const std::vector<Node> &nodes() const { return m_nodes; }
    std::vector<Node> &mutableNodes() { return m_nodes; }
    const std::vector<Link> &links() const { return m_links; }

    size_t nodeCount() const { return m_nodes.size(); }
    int32_t degree(int32_t n) const { return static_cast<int32_t>(m_adjOffset[n + 1] - m_adjOffset[n]); }
    /// Neighbour node ids of `n`.
    std::vector<int32_t> neighbours(int32_t n) const;

    /// Degree-1 nodes: vessel ostia and distal terminations.
    std::vector<int32_t> terminals() const;
    /// Degree>=3 nodes: bifurcations / trifurcations.
    std::vector<int32_t> bifurcations() const;

    /// Terminal node with the smallest z inside the largest connected
    /// component - a sensible default femoral access point for this model.
    int32_t suggestAccessNode() const;

    /// Node whose position is closest to `p` (linear scan; the graph is small).
    int32_t nearestNode(const Vec3 &p) const;

    /// Dijkstra over link lengths. Returns node ids from `start` to `goal`,
    /// or an empty vector when unreachable.
    std::vector<int32_t> shortestPath(int32_t start, int32_t goal) const;

    /// Arc length from `start` to every node, FLT_MAX where unreachable.
    /// Used to place a travelling pulse wave along the tree: a point 1 m from
    /// the aortic root distends about 170 ms after the root does.
    std::vector<float> distancesFrom(int32_t start) const;

    /// Turn a node sequence into a path with radii and arc length.
    CenterlinePath makePath(const std::vector<int32_t> &nodeIds) const;

    /// Assign a branch id to every node: ids increment at each bifurcation
    /// when walking outward from `root`. Gives the UI human-readable targets.
    void labelBranches(int32_t root);

    /// Laplacian smoothing of node positions and radii. Junction nodes
    /// (degree != 2) are pinned so bifurcations do not migrate.
    void smooth(int iterations, float lambda);

    Box3 bounds() const;

    /// Descriptive label for a terminal, e.g. "Terminal 4  (r=1.7 mm, branch 12)".
    std::string describeNode(int32_t n, float mmPerUnit) const;

private:
    std::vector<Node> m_nodes;
    std::vector<Link> m_links;
    std::vector<uint32_t> m_adjacency;   ///< CSR neighbour ids
    std::vector<uint32_t> m_adjOffset;
    std::vector<float> m_adjWeight;
};

} // namespace cn