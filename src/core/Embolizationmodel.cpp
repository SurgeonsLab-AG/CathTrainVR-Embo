#include "EmbolizationModel.h"
#include <algorithm>
#include <limits>
#include <queue>
#include <utility>

namespace cn {

namespace {

// Reference calibre: vessels at or above this radius are treated as "wide"
// for resistance purposes — a real viscous-agent injection preferentially
// fills the main trunk/parent artery before backing into anything smaller,
// so resistance approaches 1 (no penalty) once a link reaches this calibre.
constexpr float kReferenceRadius = 0.0015f;   // 1.5 mm at mmPerUnit = 1000
constexpr float kMaxResistance   = 8.0f;      // clamp so a near-zero radius cannot blow up the metric

float resistance(float radius, float viscosity) {
    const float r = std::max(radius, 1e-6f);
    const float narrowness = std::max(0.f, kReferenceRadius / r - 1.f);
    return 1.f + std::min(viscosity * narrowness, kMaxResistance - 1.f);
}

// Order matches EmbolicAgent's declaration order exactly (Onyx, Squid, Phil,
// Glue) — embolicAgentSpec() indexes straight into this array.
const EmbolicAgentSpec kAgentSpecs[] = {
    { "Onyx",
     "Ethylene-vinyl alcohol copolymer in DMSO. Highly cohesive and "
     "radiopaque via suspended TANTALUM powder; injected slowly, fills the "
     "path of least resistance and is the most controllable of the four.",
     { 0.16f, 0.15f, 0.17f }, 0.92f, 0.90f, 0.85f, 0.009f },

    { "SQUID",
     "Also an EVOH/DMSO copolymer, radiopaque via suspended "
     "TANTALUM/TUNGSTEN powder — a newer alternative to Onyx with similar "
     "handling, slightly less viscous in practice, so it advances a touch "
     "faster for the same injection technique.",
     { 0.34f, 0.16f, 0.46f }, 0.90f, 0.85f, 0.70f, 0.011f },

    { "PHIL",
     "Precipitating Hydrophobic Injectable Liquid — also EVOH-based, but "
     "radiopaque via dissolved IODINE rather than a suspended metal powder, "
     "which is why it reads visibly less radiopaque on fluoroscopy than "
     "Onyx/SQUID despite similar handling. A cream/off-white liquid, not "
     "Onyx's black \"lava\"; less viscous, so it advances a little further "
     "per injection and backs into a narrow branch a little sooner.",
     { 0.82f, 0.78f, 0.68f }, 0.88f, 0.55f, 0.55f, 0.013f },

    { "Glue",
     "n-butyl cyanoacrylate (n-BCA), radiopacified by mixing with an "
     "IODINE-based oil (Lipiodol) rather than a suspended metal powder — "
     "moderately radiopaque, not in Onyx/SQUID's class. Thin and "
     "fast-polymerizing: it spreads into small branches almost as readily "
     "as the main channel, which is exactly what makes it the hardest of "
     "the four to control.",
     { 0.86f, 0.81f, 0.56f }, 0.80f, 0.58f, 0.25f, 0.020f },
    };

} // namespace

const EmbolicAgentSpec &embolicAgentSpec(EmbolicAgent a) {
    return kAgentSpecs[static_cast<int>(a)];
}

void EmbolizationModel::begin(int32_t node, int32_t excludeNeighbor) {
    m_state = State{};
    m_excludedNeighbor = excludeNeighbor;
    if (!m_graph || node < 0 || node >= static_cast<int32_t>(m_graph->nodeCount())) return;
    m_state.active = true;
    m_state.injectionNode = node;
    recomputeWeightedDistances();
}

void EmbolizationModel::recomputeWeightedDistances() {
    const size_t n = m_graph->nodeCount();
    const float inf = std::numeric_limits<float>::max();
    m_weightedDist.assign(n, inf);
    if (m_state.injectionNode < 0) return;

    const EmbolicAgentSpec &spec = embolicAgentSpec(m_agent);
    using Item = std::pair<float, int32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    m_weightedDist[static_cast<size_t>(m_state.injectionNode)] = 0.f;
    pq.push({0.f, m_state.injectionNode});

    while (!pq.empty()) {
        const auto [d, v] = pq.top();
        pq.pop();
        if (d > m_weightedDist[static_cast<size_t>(v)]) continue;   // stale heap entry
        for (int32_t w : m_graph->neighbours(v)) {
            // The catheter's own body occupies this one specific segment -
            // see begin()'s own comment for why this is the actual fix for
            // the cast preferentially refluxing backward into the (usually
            // wider) parent vessel instead of advancing into the (usually
            // narrower) target branches around the tip. Only the DIRECT
            // edge out of the injection node is blocked, not every route
            // that happens to lead toward the access site in general - a
            // real cast reaching the same proximal territory via a
            // different bifurcation further back is legitimate collateral
            // filling, not something this should prevent.
            if (v == m_state.injectionNode && w == m_excludedNeighbor) continue;
            // The controlling calibre for a link is its narrower end — a
            // real cast is limited by the tightest point along the path,
            // not the average.
            const float radius = std::min(m_graph->nodes()[static_cast<size_t>(v)].radius,
                                          m_graph->nodes()[static_cast<size_t>(w)].radius);
            const float length = cn::distance(m_graph->nodes()[static_cast<size_t>(v)].position,
                                              m_graph->nodes()[static_cast<size_t>(w)].position);
            const float weighted = length * resistance(radius, spec.viscosity);
            const float nd = d + weighted;
            if (nd < m_weightedDist[static_cast<size_t>(w)]) {
                m_weightedDist[static_cast<size_t>(w)] = nd;
                pq.push({nd, w});
            }
        }
    }
}

void EmbolizationModel::inject(float dVolume) {
    if (!m_state.active || dVolume <= 0.f || !m_graph) return;
    const EmbolicAgentSpec &spec = embolicAgentSpec(m_agent);
    m_state.volumeInjected  += dVolume;
    m_state.filledArcLength += dVolume * spec.spreadRate;

    // Rebuilt from scratch each call: cheap for a graph this size (the same
    // reasoning CenterlineGraph's own Dijkstra already relies on), and,
    // unlike an incrementally-patched list, this cannot drift from what
    // m_weightedDist + filledArcLength actually implies.
    m_state.links.clear();
    const float inf = std::numeric_limits<float>::max();

    for (const auto &link : m_graph->links()) {
        if (link.from < 0 || link.to < 0) continue;
        if (static_cast<size_t>(link.from) >= m_weightedDist.size() ||
            static_cast<size_t>(link.to)   >= m_weightedDist.size())
            continue;
        // Same exclusion as recomputeWeightedDistances(), and independently
        // necessary here: without this check, this exact edge would still
        // render starting from dNear=0 (the injection node's own distance
        // is always 0, regardless of what the Dijkstra pass found for the
        // excluded neighbor via some OTHER path) - completely bypassing the
        // distance-level exclusion above at the rendering stage.
        if ((link.from == m_state.injectionNode && link.to == m_excludedNeighbor) ||
            (link.to == m_state.injectionNode && link.from == m_excludedNeighbor))
            continue;

        const float dFrom = m_weightedDist[static_cast<size_t>(link.from)];
        const float dTo   = m_weightedDist[static_cast<size_t>(link.to)];
        if (dFrom >= inf && dTo >= inf) continue;   // unreachable from the injection node

        const float dNear = std::min(dFrom, dTo);
        if (dNear >= m_state.filledArcLength) continue;   // fill front has not reached this link yet

        const float radius = std::min(m_graph->nodes()[static_cast<size_t>(link.from)].radius,
                                      m_graph->nodes()[static_cast<size_t>(link.to)].radius);
        const float weightedLen = std::max(
            cn::distance(m_graph->nodes()[static_cast<size_t>(link.from)].position,
                         m_graph->nodes()[static_cast<size_t>(link.to)].position) *
                resistance(radius, spec.viscosity),
            1e-6f);

        CastLink cl;
        cl.from = link.from;
        cl.to   = link.to;
        cl.weightedLength = weightedLen;
        cl.fillFraction = std::clamp((m_state.filledArcLength - dNear) / weightedLen, 0.f, 1.f);
        // Orient from the nearer node to the farther one, so the renderer's
        // "fill fraction sweeps from `from` to `to`" convention always draws
        // the leading edge advancing away from the injection point, never
        // backwards.
        if (dTo < dFrom) std::swap(cl.from, cl.to);
        m_state.links.push_back(cl);
    }
}

void EmbolizationModel::reset() {
    m_state = State{};
    m_weightedDist.clear();
}

} // namespace cn