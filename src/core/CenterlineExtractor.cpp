#include "CenterlineExtractor.h"

#include <algorithm>
#include <limits>
#include <queue>
#include <unordered_map>
#include <cmath>

namespace cn {

namespace {

/// Uniform spatial hash for the free-end stitching step. The graph is small
/// (hundreds of nodes) but this keeps the join O(n) instead of O(n^2) when the
/// extractor is pointed at a full-resolution vascular tree.
class SpatialHash {
public:
    SpatialHash(const std::vector<Vec3> &pts, float cell) : m_pts(pts), m_cell(std::max(cell, 1e-6f)) {
        for (size_t i = 0; i < pts.size(); ++i) m_buckets[key(pts[i])].push_back(static_cast<int32_t>(i));
    }

    void query(const Vec3 &p, float radius, std::vector<int32_t> &out) const {
        out.clear();
        const int r = std::max(1, static_cast<int>(std::ceil(radius / m_cell)));
        const int64_t cx = cell(p.x), cy = cell(p.y), cz = cell(p.z);
        const float r2 = radius * radius;
        for (int dx = -r; dx <= r; ++dx)
            for (int dy = -r; dy <= r; ++dy)
                for (int dz = -r; dz <= r; ++dz) {
                    auto it = m_buckets.find(hash(cx + dx, cy + dy, cz + dz));
                    if (it == m_buckets.end()) continue;
                    for (int32_t id : it->second)
                        if ((m_pts[id] - p).lengthSquared() <= r2) out.push_back(id);
                }
    }

private:
    int64_t cell(float v) const { return static_cast<int64_t>(std::floor(v / m_cell)); }
    static int64_t hash(int64_t x, int64_t y, int64_t z) {
        return (x * 73856093ll) ^ (y * 19349663ll) ^ (z * 83492791ll);
    }
    int64_t key(const Vec3 &p) const { return hash(cell(p.x), cell(p.y), cell(p.z)); }

    const std::vector<Vec3> &m_pts;
    float m_cell;
    std::unordered_map<int64_t, std::vector<int32_t>> m_buckets;
};

float medianEdgeLength(const StlMesh &mesh) {
    std::vector<float> lens;
    lens.reserve(mesh.edges().size());
    for (const auto &e : mesh.edges())
        lens.push_back(distance(mesh.vertices()[e.a], mesh.vertices()[e.b]));
    if (lens.empty()) return 0.f;
    const size_t mid = lens.size() / 2;
    std::nth_element(lens.begin(), lens.begin() + mid, lens.end());
    return lens[mid];
}

} // namespace

void CenterlineExtractor::geodesicFromSeeds(const StlMesh &mesh,
                                            const std::vector<uint32_t> &shellVertices,
                                            const std::vector<uint32_t> &seeds,
                                            std::vector<float> &distOut) {
    const auto &V = mesh.vertices();
    const auto &adj = mesh.adjacency();
    const auto &off = mesh.adjacencyOffset();

    const float inf = std::numeric_limits<float>::max();
    // Global-size array indexed by vertex id; only shell entries are touched.
    distOut.assign(V.size(), inf);

    using Item = std::pair<float, uint32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    for (uint32_t s : seeds) { distOut[s] = 0.f; pq.push({0.f, s}); }

    while (!pq.empty()) {
        const auto [d, v] = pq.top();
        pq.pop();
        if (d > distOut[v]) continue;
        for (uint32_t k = off[v]; k < off[v + 1]; ++k) {
            const uint32_t w = adj[k];
            const float nd = d + distance(V[v], V[w]);
            if (nd < distOut[w]) { distOut[w] = nd; pq.push({nd, w}); }
        }
    }
    (void)shellVertices;
}

uint32_t CenterlineExtractor::farthestVertex(const StlMesh &mesh,
                                             const std::vector<uint32_t> &shellVertices,
                                             uint32_t from) {
    std::vector<float> d;
    geodesicFromSeeds(mesh, shellVertices, {from}, d);
    uint32_t best = from;
    float bestD = -1.f;
    for (uint32_t v : shellVertices)
        if (d[v] < std::numeric_limits<float>::max() && d[v] > bestD) { bestD = d[v]; best = v; }
    return best;
}

bool CenterlineExtractor::extract(const StlMesh &mesh, CenterlineGraph &out, Report *report) {
    out.clear();
    Report rep;

    if (mesh.vertices().empty() || mesh.triangles().empty()) {
        if (report) *report = rep;
        return false;
    }

    float step = m_params.sampleStep;
    if (m_params.autoSampleStep) {
        const float med = medianEdgeLength(mesh);
        if (med > 0.f) step = med * m_params.autoStepFactor;
    }
    rep.effectiveSampleStep = step;

    // Group vertices per shell.
    const auto &comp = mesh.componentOfVertex();
    std::vector<std::vector<uint32_t>> shells(mesh.componentCount());
    for (uint32_t v = 0; v < comp.size(); ++v) shells[comp[v]].push_back(v);

    // vertex -> centerline node, for the link-building pass.
    std::vector<int32_t> vertexNode(mesh.vertices().size(), -1);

    std::vector<float> dist;
    std::vector<uint32_t> bin;

    for (int c = 0; c < mesh.componentCount(); ++c) {
        const std::vector<uint32_t> &shell = shells[c];
        if (static_cast<int>(shell.size()) < m_params.minComponentVertices) {
            ++rep.shellsSkipped;
            continue;
        }

        // --- 1. seed the geodesic sweep -------------------------------------
        std::vector<uint32_t> seeds;
        const std::vector<int> &loops = mesh.loopsOfComponent(c);
        if (!loops.empty()) {
            // Open tube: start from one rim, so iso-contours are cross-sections.
            const auto &lv = mesh.boundaryLoopVertices(loops.front());
            seeds.assign(lv.begin(), lv.end());
        } else {
            // Closed shell: use the far end of a double farthest-point sweep,
            // which approximates the tube's tip.
            const uint32_t a = farthestVertex(mesh, shell, shell.front());
            seeds.push_back(farthestVertex(mesh, shell, a));
        }
        geodesicFromSeeds(mesh, shell, seeds, dist);

        float maxD = 0.f;
        for (uint32_t v : shell)
            if (dist[v] < std::numeric_limits<float>::max()) maxD = std::max(maxD, dist[v]);
        if (maxD <= 1e-9f) { ++rep.shellsSkipped; continue; }

        // --- 2. bin by geodesic distance ------------------------------------
        const int binCount = std::max(2, static_cast<int>(std::round(maxD / step)));
        const float delta = maxD / static_cast<float>(binCount);

        bin.assign(mesh.vertices().size(), 0);
        std::vector<std::vector<uint32_t>> buckets(binCount + 1);
        for (uint32_t v : shell) {
            const float d = (dist[v] < std::numeric_limits<float>::max()) ? dist[v] : 0.f;
            int b = static_cast<int>(d / delta);
            b = std::clamp(b, 0, binCount);
            bin[v] = static_cast<uint32_t>(b);
            buckets[b].push_back(v);
        }

        // --- 3. one node per connected cluster inside a bin ------------------
        const auto &adj = mesh.adjacency();
        const auto &off = mesh.adjacencyOffset();
        std::vector<char> visited(mesh.vertices().size(), 0);
        std::vector<uint32_t> stack, cluster;

        for (int b = 0; b <= binCount; ++b) {
            for (uint32_t seed : buckets[b]) {
                if (visited[seed]) continue;
                cluster.clear();
                stack.clear();
                stack.push_back(seed);
                visited[seed] = 1;
                while (!stack.empty()) {
                    const uint32_t v = stack.back();
                    stack.pop_back();
                    cluster.push_back(v);
                    for (uint32_t k = off[v]; k < off[v + 1]; ++k) {
                        const uint32_t w = adj[k];
                        // stay inside the same bin AND the same shell
                        if (!visited[w] && bin[w] == bin[v] && comp[w] == c) {
                            visited[w] = 1;
                            stack.push_back(w);
                        }
                    }
                }

                Vec3 centroid{};
                for (uint32_t v : cluster) centroid += mesh.vertices()[v];
                centroid = centroid / static_cast<float>(cluster.size());

                float radius = 0.f;
                for (uint32_t v : cluster) radius += distance(mesh.vertices()[v], centroid);
                radius /= static_cast<float>(cluster.size());

                const int32_t node = out.addNode(centroid, radius, c);
                for (uint32_t v : cluster) vertexNode[v] = node;
            }
        }
        ++rep.shellsProcessed;
    }

    // --- 4. link nodes whose clusters touch across a surface edge -------------
    // Deduplicate with a hash set of packed (min,max) node pairs.
    std::unordered_map<uint64_t, char> seen;
    seen.reserve(mesh.edges().size());
    for (const auto &e : mesh.edges()) {
        const int32_t a = vertexNode[e.a], b = vertexNode[e.b];
        if (a < 0 || b < 0 || a == b) continue;
        const uint64_t key = (static_cast<uint64_t>(std::min(a, b)) << 32) |
                             static_cast<uint32_t>(std::max(a, b));
        if (seen.emplace(key, 1).second) out.addLink(a, b);
    }
    out.buildAdjacency();

    // --- 5. repair, stitch, smooth -------------------------------------------
    repairRadii(out);
    rep.stitchedJoints = stitchFreeEnds(out);
    out.buildAdjacency();
    out.smooth(m_params.smoothIterations, m_params.smoothLambda);

    rep.nodes = static_cast<int>(out.nodeCount());
    rep.links = static_cast<int>(out.links().size());
    rep.bifurcations = static_cast<int>(out.bifurcations().size());
    rep.terminals = static_cast<int>(out.terminals().size());
    for (const auto &l : out.links()) rep.totalCenterlineLength += l.length;

    rep.minRadius = std::numeric_limits<float>::max();
    rep.maxRadius = 0.f;
    double sum = 0.0;
    for (const auto &nd : out.nodes()) {
        rep.minRadius = std::min(rep.minRadius, nd.radius);
        rep.maxRadius = std::max(rep.maxRadius, nd.radius);
        sum += nd.radius;
    }
    if (!out.nodes().empty()) rep.meanRadius = static_cast<float>(sum / out.nodeCount());
    if (out.nodes().empty()) rep.minRadius = 0.f;

    if (report) *report = rep;
    return out.nodeCount() > 1;
}

void CenterlineExtractor::repairRadii(CenterlineGraph &g) const {
    // The first and last geodesic bins often hold a partial ring (or a single
    // vertex), whose "radius" collapses toward zero. Replace any node whose
    // radius is below its neighbours' median with that median: a lumen does
    // not pinch shut at an open rim, that is a sampling artefact.
    for (int pass = 0; pass < m_params.radiusRepairPasses; ++pass) {
        std::vector<float> updated(g.nodeCount());
        for (size_t i = 0; i < g.nodeCount(); ++i) {
            const auto nb = g.neighbours(static_cast<int32_t>(i));
            updated[i] = g.nodes()[i].radius;
            if (nb.empty()) continue;
            std::vector<float> r;
            r.reserve(nb.size());
            for (int32_t j : nb) r.push_back(g.nodes()[j].radius);
            std::sort(r.begin(), r.end());
            const float med = r[r.size() / 2];
            updated[i] = std::max(updated[i], med);
        }
        for (size_t i = 0; i < g.nodeCount(); ++i) g.mutableNodes()[i].radius = updated[i];
    }
}

int CenterlineExtractor::stitchFreeEnds(CenterlineGraph &g) const {
    std::vector<Vec3> pts;
    pts.reserve(g.nodeCount());
    for (const auto &nd : g.nodes()) pts.push_back(nd.position);

    float maxTol = m_params.minJoinTolerance;
    for (const auto &nd : g.nodes())
        maxTol = std::max(maxTol, m_params.joinRadiusFactor * nd.radius);
    SpatialHash hash(pts, maxTol);

    std::vector<std::pair<int32_t, int32_t>> newLinks;
    std::vector<int32_t> hits;

    for (int32_t i = 0; i < static_cast<int32_t>(g.nodeCount()); ++i) {
        if (g.degree(i) > 1) continue;   // only free ends get stitched
        const float tol = std::max(m_params.minJoinTolerance,
                                   m_params.joinRadiusFactor * g.nodes()[i].radius);
        hash.query(pts[i], tol, hits);

        // Nearest candidate on a *different* shell. Restricting to a different
        // mesh component is what stops the stitcher from folding a vessel back
        // onto itself where two branches run close together.
        int32_t best = -1;
        float bestD = std::numeric_limits<float>::max();
        for (int32_t j : hits) {
            if (j == i) continue;
            if (g.nodes()[j].meshComponent == g.nodes()[i].meshComponent) continue;
            const float d = (pts[j] - pts[i]).lengthSquared();
            if (d < bestD) { bestD = d; best = j; }
        }
        if (best >= 0) newLinks.emplace_back(std::min(i, best), std::max(i, best));
    }

    std::sort(newLinks.begin(), newLinks.end());
    newLinks.erase(std::unique(newLinks.begin(), newLinks.end()), newLinks.end());
    for (const auto &l : newLinks) g.addLink(l.first, l.second);
    return static_cast<int>(newLinks.size());
}

} // namespace cn
