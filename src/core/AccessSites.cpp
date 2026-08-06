#include "AccessSites.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <sstream>
#include <string>

namespace cn {

const char *accessSiteName(AccessSite s) {
    switch (s) {
    case AccessSite::RightFemoral: return "Right femoral";
    case AccessSite::LeftFemoral:  return "Left femoral";
    case AccessSite::RightRadial:  return "Right radial";
    case AccessSite::LeftRadial:   return "Left radial";
    }
    return "?";
}

namespace {

/// Component label per node, plus the size of each component. One pass, so
/// candidate filtering is O(1) per terminal instead of a BFS each time.
struct Components {
    std::vector<int32_t> label;
    std::vector<int32_t> size;
    int32_t largest = -1;

    static Components build(const CenterlineGraph &g) {
        Components c;
        const size_t n = g.nodeCount();
        c.label.assign(n, -1);
        std::vector<int32_t> stack;
        for (size_t s = 0; s < n; ++s) {
            if (c.label[s] != -1) continue;
            const int32_t id = static_cast<int32_t>(c.size.size());
            c.size.push_back(0);
            c.label[s] = id;
            stack.push_back(static_cast<int32_t>(s));
            while (!stack.empty()) {
                const int32_t v = stack.back();
                stack.pop_back();
                ++c.size[id];
                for (int32_t w : g.neighbours(v))
                    if (c.label[w] == -1) { c.label[w] = id; stack.push_back(w); }
            }
        }
        if (!c.size.empty())
            c.largest = static_cast<int32_t>(
                std::max_element(c.size.begin(), c.size.end()) - c.size.begin());
        return c;
    }
};

std::string fmt(float v, int decimals = 1) {
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(decimals);
    os << v;
    return os.str();
}

/// Longest geodesic reach from `start`, in model units. Used only for the
/// pathLengthMm read-out, so a plain Dijkstra over the component is fine.
float longestReach(const CenterlineGraph &g, int32_t start) {
    const size_t n = g.nodeCount();
    std::vector<float> dist(n, std::numeric_limits<float>::max());
    using Item = std::pair<float, int32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    dist[start] = 0.f;
    pq.push({0.f, start});
    float best = 0.f;
    while (!pq.empty()) {
        const auto [d, v] = pq.top();
        pq.pop();
        if (d > dist[v]) continue;
        best = std::max(best, d);
        for (int32_t w : g.neighbours(v)) {
            const float nd = d + distance(g.nodes()[v].position, g.nodes()[w].position);
            if (nd < dist[w]) { dist[w] = nd; pq.push({nd, w}); }
        }
    }
    return best;
}

} // namespace

std::vector<AccessPoint> AccessSiteResolver::resolve(const CenterlineGraph &graph,
                                                     float mmPerUnit,
                                                     bool  patientLeftIsPlusX,
                                                     const std::vector<int32_t> &vtpLeafNodes,
                                                     const Params &params) {
    std::vector<AccessPoint> out(4);
    for (int i = 0; i < 4; ++i) out[i].site = static_cast<AccessSite>(i);

    if (graph.nodeCount() == 0) {
        for (auto &p : out) p.note = "empty graph";
        return out;
    }

    const Box3  b       = graph.bounds();
    const float midX    = b.center().x;
    const float leftSgn = patientLeftIsPlusX ? 1.f : -1.f;
    const float zSpan   = std::max(b.hi.z - b.lo.z, 1e-9f);
    const float halfW   = std::max((b.hi.x - b.lo.x) * 0.5f, 1e-9f);

    const std::vector<int32_t> terms =
        vtpLeafNodes.empty() ? graph.terminals() : vtpLeafNodes;

    const Components comp = Components::build(graph);

    // Only terminals on the main tree are cannulable: a puncture into an
    // isolated fragment cannot reach anything.
    auto onMainTree = [&](int32_t n) {
        return comp.largest >= 0 && comp.label[n] == comp.largest;
    };
    // `sgn` is -1 for the patient's right, +1 for the patient's left, expressed
    // in patient axes; leftSgn maps that onto the model's X direction.
    auto onSide = [&](int32_t n, float sgn) {
        return (graph.nodes()[n].position.x - midX) * sgn * leftSgn > 0.f;
    };

    const float femZLo = b.lo.z + zSpan * params.femoralZLoFraction;
    const float femZHi = b.lo.z + zSpan * params.femoralZHiFraction;
    const float radXMin = halfW * params.radialLateralFraction;

    // ── Femoral: widest leg terminal on each side ────────────────────────────
    for (int idx = 0; idx < 2; ++idx) {
        const float sgn = (idx == 0) ? -1.f : +1.f;   // 0 = Right, 1 = Left
        int32_t best = -1;
        float   bestR = -1.f;
        int     inBand = 0;
        for (int32_t n : terms) {
            if (!onMainTree(n) || !onSide(n, sgn)) continue;
            const auto &nd = graph.nodes()[n];
            if (nd.position.z < femZLo || nd.position.z > femZHi) continue;
            ++inBand;
            if (nd.radius * 2.f < params.femoralMinDiameter) continue;
            // Widest wins; on a tie take the more distal one, which is the
            // longer and therefore more forgiving introduction.
            if (nd.radius > bestR ||
                (nd.radius == bestR && best >= 0 &&
                 nd.position.z < graph.nodes()[best].position.z)) {
                bestR = nd.radius;
                best  = n;
            }
        }
        AccessPoint &ap = out[idx];
        ap.node  = best;
        ap.valid = best >= 0;
        if (ap.valid) {
            const auto &nd = graph.nodes()[best];
            ap.vesselDiameter = 2.f * nd.radius;
            ap.pathLengthMm   = longestReach(graph, best) * mmPerUnit;
            ap.note = "groin terminal, Z=" + fmt(nd.position.z * mmPerUnit) +
                      "mm, reach " + fmt(ap.pathLengthMm, 0) + "mm";
        } else {
            ap.note = "no femoral terminal: " + std::to_string(inBand) +
                      " terminal(s) in band Z=" + fmt(femZLo * mmPerUnit) + "-" +
                      fmt(femZHi * mmPerUnit) + "mm, none wider than " +
                      fmt(params.femoralMinDiameter * mmPerUnit, 2) + "mm";
        }
    }

    // ── Radial: most lateral terminal on each side ───────────────────────────
    for (int idx = 2; idx < 4; ++idx) {
        const float sgn = (idx == 2) ? -1.f : +1.f;   // 2 = Right, 3 = Left
        int32_t best = -1;
        float   bestLat = -1.f;
        int     lateral = 0;
        for (int32_t n : terms) {
            if (!onMainTree(n) || !onSide(n, sgn)) continue;
            const auto &nd = graph.nodes()[n];
            const float lat = std::fabs(nd.position.x - midX);
            if (lat < radXMin) continue;
            ++lateral;
            if (nd.radius * 2.f < params.radialMinDiameter) continue;
            if (lat > bestLat) { bestLat = lat; best = n; }
        }
        AccessPoint &ap = out[idx];
        ap.node  = best;
        ap.valid = best >= 0;
        if (ap.valid) {
            const auto &nd = graph.nodes()[best];
            ap.vesselDiameter = 2.f * nd.radius;
            ap.pathLengthMm   = longestReach(graph, best) * mmPerUnit;
            ap.note = "wrist terminal, |X|=" + fmt(bestLat * mmPerUnit) +
                      "mm, Z=" + fmt(nd.position.z * mmPerUnit) +
                      "mm, reach " + fmt(ap.pathLengthMm, 0) + "mm";
        } else {
            ap.note = "no radial terminal: " + std::to_string(lateral) +
                      " terminal(s) beyond |X|=" + fmt(radXMin * mmPerUnit) +
                      "mm, none wider than " +
                      fmt(params.radialMinDiameter * mmPerUnit, 2) + "mm";
        }
    }

    return out;
}

int32_t AccessSiteResolver::nodeFor(const std::vector<AccessPoint> &points,
                                    AccessSite site) {
    for (const auto &p : points)
        if (p.site == site && p.valid) return p.node;
    return -1;
}

} // namespace cn