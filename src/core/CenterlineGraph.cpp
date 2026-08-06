#include "CenterlineGraph.h"

#include <algorithm>
#include <limits>
#include <queue>
#include <sstream>
#include <iomanip>

namespace cn {

// ---------------------------------------------------------------- CenterlinePath

size_t CenterlinePath::locate(float s, float &t) const {
    const float total = totalLength();
    if (s <= 0.f || points.size() < 2) { t = 0.f; return 0; }
    if (s >= total) { t = 1.f; return points.size() - 2; }

    // arcLength is monotonically increasing, so a binary search is exact.
    const auto it = std::upper_bound(arcLength.begin(), arcLength.end(), s);
    size_t i = static_cast<size_t>(it - arcLength.begin());
    if (i == 0) i = 1;
    if (i >= points.size()) i = points.size() - 1;
    const size_t seg = i - 1;
    const float span = arcLength[seg + 1] - arcLength[seg];
    t = span > 1e-12f ? (s - arcLength[seg]) / span : 0.f;
    return seg;
}

Vec3 CenterlinePath::pointAt(float s) const {
    if (points.empty()) return {};
    if (points.size() == 1) return points[0];
    float t;
    const size_t i = locate(s, t);
    return lerp(points[i], points[i + 1], t);
}

float CenterlinePath::radiusAt(float s) const {
    if (radii.empty()) return 0.f;
    if (radii.size() == 1) return radii[0];
    float t;
    const size_t i = locate(s, t);
    return radii[i] + (radii[i + 1] - radii[i]) * t;
}

Vec3 CenterlinePath::tangentAt(float s) const {
    if (points.size() < 2) return {0.f, 0.f, 1.f};
    float t;
    const size_t i = locate(s, t);
    return (points[i + 1] - points[i]).normalized();
}

int32_t CenterlinePath::nodeAt(float s) const {
    if (nodeId.empty()) return -1;
    float t;
    const size_t i = locate(s, t);
    const size_t pick = (t < 0.5f) ? i : std::min(i + 1, nodeId.size() - 1);
    return nodeId[pick];
}

CenterlinePath CenterlinePath::resampled(float spacing) const {
    CenterlinePath out;
    if (points.size() < 2 || spacing <= 0.f) return *this;

    const float total = totalLength();
    const int steps = std::max(1, static_cast<int>(std::round(total / spacing)));
    const float step = total / static_cast<float>(steps);

    out.points.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const float s = step * static_cast<float>(i);
        out.points.push_back(pointAt(s));
        out.radii.push_back(radiusAt(s));
        out.arcLength.push_back(s);
        out.nodeId.push_back(nodeAt(s));
    }
    return out;
}

// --------------------------------------------------------------- CenterlineGraph

void CenterlineGraph::clear() {
    m_nodes.clear();
    m_links.clear();
    m_adjacency.clear();
    m_adjOffset.clear();
    m_adjWeight.clear();
}

int32_t CenterlineGraph::addNode(const Vec3 &p, float radius, int32_t meshComponent) {
    m_nodes.push_back({p, radius, meshComponent, -1});
    return static_cast<int32_t>(m_nodes.size()) - 1;
}

void CenterlineGraph::addLink(int32_t a, int32_t b) {
    if (a == b || a < 0 || b < 0) return;
    m_links.push_back({a, b, distance(m_nodes[a].position, m_nodes[b].position)});
}

void CenterlineGraph::buildAdjacency() {
    const size_t n = m_nodes.size();
    m_adjOffset.assign(n + 1, 0);
    for (const Link &l : m_links) {
        ++m_adjOffset[l.from + 1];
        ++m_adjOffset[l.to + 1];
    }
    for (size_t i = 0; i < n; ++i) m_adjOffset[i + 1] += m_adjOffset[i];

    m_adjacency.assign(m_adjOffset[n], 0);
    m_adjWeight.assign(m_adjOffset[n], 0.f);
    std::vector<uint32_t> cursor(m_adjOffset.begin(), m_adjOffset.end() - 1);
    for (const Link &l : m_links) {
        m_adjacency[cursor[l.from]] = static_cast<uint32_t>(l.to);
        m_adjWeight[cursor[l.from]++] = l.length;
        m_adjacency[cursor[l.to]] = static_cast<uint32_t>(l.from);
        m_adjWeight[cursor[l.to]++] = l.length;
    }
}

std::vector<int32_t> CenterlineGraph::neighbours(int32_t n) const {
    std::vector<int32_t> out;
    for (uint32_t k = m_adjOffset[n]; k < m_adjOffset[n + 1]; ++k)
        out.push_back(static_cast<int32_t>(m_adjacency[k]));
    return out;
}

std::vector<int32_t> CenterlineGraph::terminals() const {
    std::vector<int32_t> out;
    for (int32_t i = 0; i < static_cast<int32_t>(m_nodes.size()); ++i)
        if (degree(i) == 1) out.push_back(i);
    return out;
}

std::vector<int32_t> CenterlineGraph::bifurcations() const {
    std::vector<int32_t> out;
    for (int32_t i = 0; i < static_cast<int32_t>(m_nodes.size()); ++i)
        if (degree(i) >= 3) out.push_back(i);
    return out;
}

int32_t CenterlineGraph::suggestAccessNode() const {
    // Label components, keep the biggest, take its lowest terminal. On an
    // aorto-iliac model that lands on an iliac stump - i.e. femoral access.
    const size_t n = m_nodes.size();
    std::vector<int32_t> comp(n, -1);
    int32_t nc = 0;
    std::vector<int32_t> stack;
    for (size_t s = 0; s < n; ++s) {
        if (comp[s] != -1) continue;
        const int32_t id = nc++;
        comp[s] = id;
        stack.push_back(static_cast<int32_t>(s));
        while (!stack.empty()) {
            const int32_t v = stack.back();
            stack.pop_back();
            for (uint32_t k = m_adjOffset[v]; k < m_adjOffset[v + 1]; ++k) {
                const int32_t w = static_cast<int32_t>(m_adjacency[k]);
                if (comp[w] == -1) { comp[w] = id; stack.push_back(w); }
            }
        }
    }
    std::vector<int32_t> sizes(nc, 0);
    for (int32_t c : comp) ++sizes[c];
    const int32_t big = static_cast<int32_t>(
        std::max_element(sizes.begin(), sizes.end()) - sizes.begin());

    int32_t best = -1;
    float bestZ = std::numeric_limits<float>::max();
    for (int32_t i = 0; i < static_cast<int32_t>(n); ++i) {
        if (comp[i] != big || degree(i) != 1) continue;
        if (m_nodes[i].position.z < bestZ) { bestZ = m_nodes[i].position.z; best = i; }
    }
    return best;
}

int32_t CenterlineGraph::nearestNode(const Vec3 &p) const {
    int32_t best = -1;
    float bestD = std::numeric_limits<float>::max();
    for (int32_t i = 0; i < static_cast<int32_t>(m_nodes.size()); ++i) {
        const float d = (m_nodes[i].position - p).lengthSquared();
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

std::vector<int32_t> CenterlineGraph::shortestPath(int32_t start, int32_t goal) const {
    const size_t n = m_nodes.size();
    if (start < 0 || goal < 0 || start >= static_cast<int32_t>(n) ||
        goal >= static_cast<int32_t>(n))
        return {};

    const float inf = std::numeric_limits<float>::max();
    std::vector<float> dist(n, inf);
    std::vector<int32_t> prev(n, -1);
    using Item = std::pair<float, int32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;

    dist[start] = 0.f;
    pq.push({0.f, start});
    while (!pq.empty()) {
        const auto [d, v] = pq.top();
        pq.pop();
        if (d > dist[v]) continue;          // stale heap entry
        if (v == goal) break;
        for (uint32_t k = m_adjOffset[v]; k < m_adjOffset[v + 1]; ++k) {
            const int32_t w = static_cast<int32_t>(m_adjacency[k]);
            const float nd = d + m_adjWeight[k];
            if (nd < dist[w]) { dist[w] = nd; prev[w] = v; pq.push({nd, w}); }
        }
    }
    if (dist[goal] == inf) return {};

    std::vector<int32_t> path;
    for (int32_t v = goal; v != -1; v = prev[v]) path.push_back(v);
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<float> CenterlineGraph::distancesFrom(int32_t start) const {
    const size_t n = m_nodes.size();
    const float inf = std::numeric_limits<float>::max();
    std::vector<float> dist(n, inf);
    if (start < 0 || start >= static_cast<int32_t>(n) || m_adjOffset.empty()) return dist;

    using Item = std::pair<float, int32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    dist[start] = 0.f;
    pq.push({0.f, start});
    while (!pq.empty()) {
        const auto [d, v] = pq.top();
        pq.pop();
        if (d > dist[v]) continue;              // stale heap entry
        for (uint32_t k = m_adjOffset[v]; k < m_adjOffset[v + 1]; ++k) {
            const int32_t w = static_cast<int32_t>(m_adjacency[k]);
            const float nd = d + m_adjWeight[k];
            if (nd < dist[w]) { dist[w] = nd; pq.push({nd, w}); }
        }
    }
    return dist;
}

CenterlinePath CenterlineGraph::makePath(const std::vector<int32_t> &nodeIds) const {
    CenterlinePath p;
    if (nodeIds.empty()) return p;
    float acc = 0.f;
    for (size_t i = 0; i < nodeIds.size(); ++i) {
        const Node &nd = m_nodes[nodeIds[i]];
        if (i > 0) acc += distance(m_nodes[nodeIds[i - 1]].position, nd.position);
        p.points.push_back(nd.position);
        p.radii.push_back(nd.radius);
        p.arcLength.push_back(acc);
        p.nodeId.push_back(nodeIds[i]);
    }
    return p;
}

void CenterlineGraph::labelBranches(int32_t root) {
    for (Node &nd : m_nodes) nd.branchId = -1;
    if (root < 0 || m_nodes.empty()) return;

    int32_t nextBranch = 0;
    // BFS outward. A node inherits its parent's branch id while the vessel
    // stays a simple chain, and a fresh id is minted on each child of a
    // bifurcation - the same convention angio reports use.
    std::queue<std::pair<int32_t, int32_t>> q;   // (node, branch id)
    std::vector<bool> seen(m_nodes.size(), false);
    seen[root] = true;
    m_nodes[root].branchId = nextBranch++;
    q.push({root, m_nodes[root].branchId});

    while (!q.empty()) {
        const auto [v, branch] = q.front();
        q.pop();
        std::vector<int32_t> kids;
        for (uint32_t k = m_adjOffset[v]; k < m_adjOffset[v + 1]; ++k) {
            const int32_t w = static_cast<int32_t>(m_adjacency[k]);
            if (!seen[w]) kids.push_back(w);
        }
        const bool splits = kids.size() > 1;
        for (int32_t w : kids) {
            seen[w] = true;
            const int32_t b = splits ? nextBranch++ : branch;
            m_nodes[w].branchId = b;
            q.push({w, b});
        }
    }
}

void CenterlineGraph::smooth(int iterations, float lambda) {
    if (m_adjOffset.empty()) return;
    const size_t n = m_nodes.size();
    for (int it = 0; it < iterations; ++it) {
        std::vector<Vec3> np(n);
        std::vector<float> nr(n);
        for (size_t i = 0; i < n; ++i) {
            np[i] = m_nodes[i].position;
            nr[i] = m_nodes[i].radius;
            if (degree(static_cast<int32_t>(i)) != 2) continue;  // pin junctions & tips
            Vec3 avg{};
            float ravg = 0.f;
            int c = 0;
            for (uint32_t k = m_adjOffset[i]; k < m_adjOffset[i + 1]; ++k) {
                avg += m_nodes[m_adjacency[k]].position;
                ravg += m_nodes[m_adjacency[k]].radius;
                ++c;
            }
            if (c == 0) continue;
            avg = avg / static_cast<float>(c);
            ravg /= static_cast<float>(c);
            np[i] = lerp(m_nodes[i].position, avg, lambda);
            nr[i] = m_nodes[i].radius + (ravg - m_nodes[i].radius) * lambda;
        }
        for (size_t i = 0; i < n; ++i) { m_nodes[i].position = np[i]; m_nodes[i].radius = nr[i]; }
    }
    for (Link &l : m_links)
        l.length = distance(m_nodes[l.from].position, m_nodes[l.to].position);
    buildAdjacency();
}

Box3 CenterlineGraph::bounds() const {
    Box3 b;
    for (const Node &nd : m_nodes) b.extend(nd.position);
    return b;
}

std::string CenterlineGraph::describeNode(int32_t n, float mmPerUnit) const {
    if (n < 0 || n >= static_cast<int32_t>(m_nodes.size())) return "-";
    const Node &nd = m_nodes[n];
    std::ostringstream os;
    os << std::fixed << std::setprecision(2);
    os << "node " << n << "  (r=" << nd.radius * mmPerUnit << " mm";
    if (nd.branchId >= 0) os << ", branch " << nd.branchId;
    os << ")";
    return os.str();
}

} // namespace cn