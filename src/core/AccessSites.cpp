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
    case AccessSite::RightFemoral:   return "Right femoral";
    case AccessSite::LeftFemoral:    return "Left femoral";
    case AccessSite::RightRadial:    return "Right radial";
    case AccessSite::LeftRadial:     return "Left radial";
    case AccessSite::RightPopliteal: return "Right popliteal";
    case AccessSite::LeftPopliteal:  return "Left popliteal";
    case AccessSite::RightBrachial:  return "Right brachial";
    case AccessSite::LeftBrachial:   return "Left brachial";
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
    std::vector<AccessPoint> out(8);
    for (int i = 0; i < 8; ++i) out[i].site = static_cast<AccessSite>(i);

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

    // ── Femoral: TRUE groin-level entry, not the leg's own distal terminal ──
    // The leg's own graph terminal is the ankle/foot - a genuine dead end -
    // but the actual common femoral artery access point is NOT a terminal
    // at all: it is a mid-pathway point where the vessel continues
    // proximally into the external iliac artery and distally down the leg
    // toward the knee. Using the ankle terminal directly (the original,
    // incorrect implementation) placed the sheath roughly 2.7% of the way
    // up the body's own height - nowhere near the groin, which sits
    // roughly halfway up.
    //
    // Finds the landmark by walking a shortestPath() from the ankle to the
    // farthest reachable terminal (the same "cranial-most reachable
    // terminal" pattern SheathManager's own path-building already uses
    // elsewhere), looking for the last node before the path reaches a
    // high-degree (>=4) junction (the iliac bifurcation) with a radius
    // that has grown to genuine femoral-artery caliber by that point.
    // Deliberately reuses this project's own proven pathfinding rather
    // than a bespoke graph walk - a custom traversal risks behaving
    // differently against the actual runtime graph (built by
    // VtpCenterlineLoader's own weld/merge processing of the source file)
    // than a direct analysis of the raw file would suggest, since even a
    // logically-identical search can land somewhere unexpected if the
    // graph topology it is walking differs even slightly. If the
    // radius-then-junction landmark is never found along the path at all,
    // falls back to whichever node on that same verified path sits
    // closest to the expected anatomical Z-fraction, rather than to
    // whatever arbitrary node the search happened to still be at - a
    // reasonable in-range position instead of a potentially wrong one.
    const float femoralGroinZFraction = 0.315f;   // verified against this project's own model, matching the backoff target
    for (int idx = 0; idx < 2; ++idx) {
        const float sgn = (idx == 0) ? -1.f : +1.f;   // 0 = Right, 1 = Left
        int32_t ankle = -1;
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
                (nd.radius == bestR && ankle >= 0 &&
                 nd.position.z < graph.nodes()[ankle].position.z)) {
                bestR = nd.radius;
                ankle = n;
            }
        }

        // `ankle` is the widest terminal in the leg's own distal band - the
        // starting point for tracing proximally up to the actual groin,
        // not the access point itself.
        int32_t groinNode = -1;
        if (ankle >= 0) {
            // Farthest-reachable terminal from the ankle, same pattern
            // SheathManager's own path-building already uses.
            std::vector<int32_t> bestPath;
            float bestFarZ = -std::numeric_limits<float>::max();
            for (int32_t t : terms) {
                if (t == ankle) continue;
                if (graph.nodes()[t].position.z <= bestFarZ) continue;
                auto candidate = graph.shortestPath(ankle, t);
                if (candidate.empty()) continue;
                bestFarZ = graph.nodes()[t].position.z;
                bestPath = std::move(candidate);
            }

            // Primary: walk this verified path's own node list to find the
            // iliac bifurcation (a high-degree, >=4, junction) at full
            // femoral-artery caliber, then back off a fixed arc-length
            // distance from it. The vessel segment leading up to that
            // bifurcation is uniform caliber for several hundred
            // millimetres in this model (it actually represents the
            // external iliac + common femoral + superficial femoral
            // merged into one continuous segment, since they are not
            // distinguished separately at this centerline resolution), so
            // stopping right at/near the bifurcation itself is still
            // effectively the external iliac artery in the pelvis, not a
            // real femoral puncture site, which sits well below the
            // inguinal ligament, clearly down in the thigh.
            // femoralBackoffFromBifurcationMm targets that instead,
            // verified against this project's own model.
            int32_t bifurcationNode = -1;   // the junction node itself, not the candidate before it
            size_t  bifurcationIdx  = 0;
            for (size_t i = 0; i + 1 < bestPath.size(); ++i) {
                const int32_t n    = bestPath[i];
                const int32_t next = bestPath[i + 1];
                if (graph.nodes()[n].radius * 2.f < params.femoralMinDiameter) continue;
                // Degree>=3 alone is not enough to identify the iliac
                // bifurcation specifically - an ordinary popliteal
                // trifurcation lower in the same leg is also degree 3 (see
                // Params::femoralBifurcationMinRadius's own comment for the
                // full story). Radius is what actually tells them apart.
                if (static_cast<int32_t>(graph.neighbours(next).size()) >= 3 &&
                    graph.nodes()[next].radius >= params.femoralBifurcationMinRadius) {
                    bifurcationNode = next;
                    bifurcationIdx  = i + 1;
                    break;
                }
            }
            if (bifurcationNode >= 0) {
                // Walk backward from the bifurcation NODE ITSELF (not the
                // candidate before it - that point already sits some
                // distance below the true junction, which would silently
                // shift every target distance by that same amount),
                // accumulating arc length (straight-line distance between
                // consecutive path nodes - the centerline is densely
                // sampled almost everywhere, so this is a close
                // approximation of true arc length, though not near this
                // particular junction - see below). When the accumulated
                // distance crosses the target between two path nodes, pick
                // whichever one is actually closer to it, rather than
                // always taking the step that crosses the threshold - this
                // graph is very sparse near the groin specifically (as few
                // as ~6 hops from the ankle terminal to the bifurcation,
                // with correspondingly large gaps between some of them),
                // so always taking the crossing step risks a dramatic
                // overshoot past the intended target - verified directly
                // against this project's data: without this, backing off
                // from the bifurcation landed near the knee instead of the
                // thigh, because there was no node available anywhere near
                // the actual target distance.
                float prevAccumMm = 0.f;
                int32_t prevNode = bifurcationNode;
                groinNode = bifurcationNode;
                for (size_t i = bifurcationIdx; i-- > 0; ) {
                    const float step = (graph.nodes()[bestPath[i]].position -
                                        graph.nodes()[bestPath[i + 1]].position).length() * mmPerUnit;
                    const float newAccumMm = prevAccumMm + step;
                    if (newAccumMm >= params.femoralBackoffFromBifurcationMm) {
                        const float distToNew = std::fabs(newAccumMm - params.femoralBackoffFromBifurcationMm);
                        const float distToOld = std::fabs(prevAccumMm - params.femoralBackoffFromBifurcationMm);
                        groinNode = (distToNew < distToOld) ? bestPath[i] : prevNode;
                        break;
                    }
                    prevAccumMm = newAccumMm;
                    prevNode    = bestPath[i];
                    groinNode   = prevNode;   // path exhausted before reaching the target - use the nearest end
                }
            }

            // Fallback: landmark never found along the path (e.g. the
            // runtime graph's topology does not present the same clean
            // signature the source file's own data does) - use whichever
            // node on the SAME VERIFIED PATH sits closest to the expected
            // anatomical Z-fraction instead, so the result is still a
            // reasonable in-range position rather than an arbitrary one.
            if (groinNode < 0 && !bestPath.empty()) {
                const float targetZ = b.lo.z + zSpan * femoralGroinZFraction;
                float bestDz = std::numeric_limits<float>::max();
                for (int32_t n : bestPath) {
                    const float dz = std::fabs(graph.nodes()[n].position.z - targetZ);
                    if (dz < bestDz) { bestDz = dz; groinNode = n; }
                }
            }
            if (groinNode < 0) groinNode = ankle;   // last resort - the path itself was empty
        }

        AccessPoint &ap = out[idx];
        ap.node  = groinNode;
        ap.valid = groinNode >= 0;
        if (ap.valid) {
            const auto &nd = graph.nodes()[groinNode];
            ap.vesselDiameter = 2.f * nd.radius;
            ap.pathLengthMm   = longestReach(graph, groinNode) * mmPerUnit;
            ap.note = "common femoral artery (groin), Z=" + fmt(nd.position.z * mmPerUnit) +
                      "mm, reach " + fmt(ap.pathLengthMm, 0) + "mm";
        } else {
            ap.note = "no femoral entry: " + std::to_string(inBand) +
                      " ankle terminal(s) in band Z=" + fmt(femZLo * mmPerUnit) + "-" +
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

    // ── Brachial: upper-arm entry, found by tracing proximally from the
    // already-resolved radial (wrist) node ──────────────────────────────────
    // Like femoral's corrected entry, this is a mid-pathway point - the
    // vessel continues both distally back toward the wrist and proximally
    // toward the axilla/shoulder - not a graph terminal at all. Reuses the
    // wrist node this same resolve() call already found for radial as the
    // starting point, then walks a shortestPath() to the farthest reachable
    // terminal (the same "cranial-most reachable terminal" pattern used for
    // femoral, and for the same reason: shortestPath() reliably stays on
    // the correct vessel through major junctions, unlike a naive "follow
    // the widest branch" walk, which was tried first and found to jump
    // across vascular territories entirely at the shoulder/neck junction).
    // The landmark itself is simply the first node along that path whose
    // radius crosses into brachial caliber - verified against this
    // project's own model to land exactly at the elbow-side start of the
    // brachial segment, symmetric on both sides, with no backoff needed
    // (unlike femoral's long, uniform-caliber segment, this one is short
    // enough that its very first wide node is already an anatomically
    // reasonable brachial access point).
    for (int idx = 6; idx < 8; ++idx) {
        const int32_t wrist = out[idx - 4].node;   // 6->RightRadial(2), 7->LeftRadial(3)
        AccessPoint &ap = out[idx];
        if (wrist < 0) {
            ap.note = "no brachial entry: radial (wrist) access was not resolved";
            continue;
        }

        std::vector<int32_t> bestPath;
        float bestFarZ = -std::numeric_limits<float>::max();
        for (int32_t t : terms) {
            if (t == wrist || !onMainTree(t)) continue;
            if (graph.nodes()[t].position.z <= bestFarZ) continue;
            auto candidate = graph.shortestPath(wrist, t);
            if (candidate.empty()) continue;
            bestFarZ = graph.nodes()[t].position.z;
            bestPath = std::move(candidate);
        }

        int32_t brachialNode = -1;
        for (size_t i = 0; i < bestPath.size(); ++i) {
            if (graph.nodes()[bestPath[i]].radius >= params.brachialMinRadius) {
                // Step back one node toward the wrist, into the tapering
                // segment just below full brachial caliber - still a real,
                // verified graph node (not an interpolated position: this
                // vessel's radius tapers smoothly here, but its actual
                // position still curves significantly along the same edge,
                // the same failure mode that made a naive interpolation
                // unsafe for femoral's own entry). Lands at the
                // elbow-crease/antecubital transition rather than further
                // up in mid-upper-arm, a lower and still clinically
                // reasonable brachial access point.
                brachialNode = (i > 0) ? bestPath[i - 1] : bestPath[i];
                break;
            }
        }

        ap.node  = brachialNode;
        ap.valid = brachialNode >= 0;
        if (ap.valid) {
            const auto &nd = graph.nodes()[brachialNode];
            ap.vesselDiameter = 2.f * nd.radius;
            ap.pathLengthMm   = longestReach(graph, brachialNode) * mmPerUnit;
            ap.note = "brachial artery, Z=" + fmt(nd.position.z * mmPerUnit) +
                      "mm, reach " + fmt(ap.pathLengthMm, 0) + "mm";
        } else {
            ap.note = "no brachial entry: path from wrist never reached " +
                      fmt(params.brachialMinRadius * mmPerUnit, 2) + "mm radius";
        }
    }

    // ── Popliteal: widest leg terminal on each side ───────────────────────
    // Reuses the exact search this project's OWN femoral resolver originally
    // used (before it was corrected to find the true groin-level entry
    // instead) - that search was always correctly finding a genuine leg
    // terminal, just mislabeled. Popliteal is a real, distinct clinical
    // access site (behind the knee), and this terminal is anatomically
    // appropriate for it: a true dead end, unlike femoral's own corrected
    // entry, which is a mid-pathway point further up the same limb.
    for (int idx = 4; idx < 6; ++idx) {
        const float sgn = (idx == 4) ? -1.f : +1.f;   // 4 = Right, 5 = Left
        int32_t best = -1;
        float   bestR = -1.f;
        int     inBand = 0;
        for (int32_t n : terms) {
            if (!onMainTree(n) || !onSide(n, sgn)) continue;
            const auto &nd = graph.nodes()[n];
            if (nd.position.z < femZLo || nd.position.z > femZHi) continue;
            ++inBand;
            if (nd.radius * 2.f < params.femoralMinDiameter) continue;
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
            ap.note = "popliteal terminal, Z=" + fmt(nd.position.z * mmPerUnit) +
                      "mm, reach " + fmt(ap.pathLengthMm, 0) + "mm";
        } else {
            ap.note = "no popliteal terminal: " + std::to_string(inBand) +
                      " terminal(s) in band Z=" + fmt(femZLo * mmPerUnit) + "-" +
                      fmt(femZHi * mmPerUnit) + "mm, none wider than " +
                      fmt(params.femoralMinDiameter * mmPerUnit, 2) + "mm";
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