#include "ContrastInjectionModel.h"
#include <algorithm>
#include <limits>
#include <queue>
#include <utility>

namespace cn {

namespace {

// Same reference-calibre/resistance formula EmbolizationModel.cpp uses,
// duplicated here rather than shared: contrast's viscosity is a single fixed
// constant (a thin diagnostic liquid, not a per-agent property), so there is
// no per-agent table to look values up from the way the embolic side has.
constexpr float kReferenceRadius  = 0.0015f;   // 1.5 mm at mmPerUnit = 1000
constexpr float kMaxResistance    = 8.0f;
constexpr float kContrastViscosity = 0.12f;    // thin liquid - barely prefers the wide channel

// A link at or above this cast fraction is treated as impassable to
// contrast - not "very slow", genuinely blocked, which is what a real
// coil/liquid-embolic occlusion does to a vessel's own flow.
constexpr float kOcclusionThreshold = 0.6f;

// How fast the fill front recedes, in model units (arc length) per second.
// Flush is roughly 5x faster than natural blood-flow washout — a real
// saline/DMSO flush clears the catheter and local field in a couple of
// seconds; natural washout of a contrast bolus left alone takes several.
constexpr float kNaturalDecayPerSec = 0.030f;
constexpr float kFlushDecayPerSec   = 0.150f;

float resistance(float radius, float viscosity) {
    const float r = std::max(radius, 1e-6f);
    const float narrowness = std::max(0.f, kReferenceRadius / r - 1.f);
    return 1.f + std::min(viscosity * narrowness, kMaxResistance - 1.f);
}

} // namespace

bool ContrastInjectionModel::linkOccluded(int32_t a, int32_t b) const {
    if (!m_emboli) return false;   // nothing embolized yet - e.g. pre-embolization angiogram
    for (const auto &cl : m_emboli->state().links) {
        if (cl.fillFraction < kOcclusionThreshold) continue;
        if ((cl.from == a && cl.to == b) || (cl.from == b && cl.to == a)) return true;
    }
    return false;
}

void ContrastInjectionModel::beginPhase(ContrastPhase phase, int32_t node) {
    if (phase == ContrastPhase::Idle) { reset(); return; }

    if (phase == ContrastPhase::Flush) {
        // Keep whatever contrast currently exists — Flush accelerates its
        // removal via tick(), it does not instantly clear it (a real flush
        // still takes a couple of seconds to fully clear the field).
        m_state.phase = ContrastPhase::Flush;
        return;
    }

    // A fresh angiogram phase: reset and regrow from the tip, re-reading
    // whatever the EmbolizationModel currently reports as occluded at this
    // exact moment - so a post-embolization angiogram genuinely sees the
    // cast that exists NOW, not a snapshot from whenever setEmbolization()
    // was first called.
    m_state = State{};
    if (!m_graph || node < 0 || node >= static_cast<int32_t>(m_graph->nodeCount())) return;
    m_state.phase = phase;
    m_state.injectionNode = node;
    recomputeWeightedDistances();
}

void ContrastInjectionModel::recomputeWeightedDistances() {
    const size_t n = m_graph->nodeCount();
    const float inf = std::numeric_limits<float>::max();
    m_weightedDist.assign(n, inf);
    if (m_state.injectionNode < 0) return;

    using Item = std::pair<float, int32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    m_weightedDist[static_cast<size_t>(m_state.injectionNode)] = 0.f;
    pq.push({0.f, m_state.injectionNode});

    while (!pq.empty()) {
        const auto [d, v] = pq.top();
        pq.pop();
        if (d > m_weightedDist[static_cast<size_t>(v)]) continue;
        for (int32_t w : m_graph->neighbours(v)) {
            // The edge simply does not exist for this flood fill if the
            // corresponding embolic link is occluded - contrast cannot flow
            // past it, full stop, not "flows past it slowly".
            if (linkOccluded(v, w)) continue;

            const float radius = std::min(m_graph->nodes()[static_cast<size_t>(v)].radius,
                                          m_graph->nodes()[static_cast<size_t>(w)].radius);
            const float length = cn::distance(m_graph->nodes()[static_cast<size_t>(v)].position,
                                              m_graph->nodes()[static_cast<size_t>(w)].position);
            const float weighted = length * resistance(radius, kContrastViscosity);
            const float nd = d + weighted;
            if (nd < m_weightedDist[static_cast<size_t>(w)]) {
                m_weightedDist[static_cast<size_t>(w)] = nd;
                pq.push({nd, w});
            }
        }
    }
}

void ContrastInjectionModel::rebuildLinks() {
    m_state.links.clear();
    if (!m_graph) return;
    const float inf = std::numeric_limits<float>::max();

    for (const auto &link : m_graph->links()) {
        if (link.from < 0 || link.to < 0) continue;
        if (static_cast<size_t>(link.from) >= m_weightedDist.size() ||
            static_cast<size_t>(link.to)   >= m_weightedDist.size())
            continue;
        if (linkOccluded(link.from, link.to)) continue;   // never draw contrast past a blocked link

        const float dFrom = m_weightedDist[static_cast<size_t>(link.from)];
        const float dTo   = m_weightedDist[static_cast<size_t>(link.to)];
        if (dFrom >= inf && dTo >= inf) continue;

        const float dNear = std::min(dFrom, dTo);
        if (dNear >= m_state.filledArcLength) continue;

        const float radius = std::min(m_graph->nodes()[static_cast<size_t>(link.from)].radius,
                                      m_graph->nodes()[static_cast<size_t>(link.to)].radius);
        const float weightedLen = std::max(
            cn::distance(m_graph->nodes()[static_cast<size_t>(link.from)].position,
                         m_graph->nodes()[static_cast<size_t>(link.to)].position) *
                resistance(radius, kContrastViscosity),
            1e-6f);

        ContrastLink cl;
        cl.from = link.from;
        cl.to   = link.to;
        cl.weightedLength = weightedLen;
        cl.level = std::clamp((m_state.filledArcLength - dNear) / weightedLen, 0.f, 1.f);
        if (dTo < dFrom) std::swap(cl.from, cl.to);
        m_state.links.push_back(cl);
    }
}

void ContrastInjectionModel::inject(float dVolume) {
    if (dVolume <= 0.f || !m_graph) return;
    // Only meaningful during an actual angiogram phase — a real operator
    // does not inject contrast while idle or mid-flush.
    if (m_state.phase != ContrastPhase::PreEmbolizationAngiogram &&
        m_state.phase != ContrastPhase::PostEmbolizationAngiogram)
        return;

    // Same spread-rate order of magnitude as the embolic model's own thin
    // agents (Glue: 0.020) - contrast is thinner still, so it advances
    // fastest of anything modelled here, matching how quickly a real
    // contrast bolus actually races down a vessel compared to a viscous
    // liquid embolic.
    constexpr float kContrastSpreadRate = 0.028f;
    m_state.volumeInjected  += dVolume;
    m_state.filledArcLength += dVolume * kContrastSpreadRate;
    rebuildLinks();
}

void ContrastInjectionModel::tick(float dtSeconds) {
    if (dtSeconds <= 0.f || m_state.phase == ContrastPhase::Idle) return;
    const float rate = (m_state.phase == ContrastPhase::Flush) ? kFlushDecayPerSec
                                                               : kNaturalDecayPerSec;
    m_state.filledArcLength = std::max(0.f, m_state.filledArcLength - dtSeconds * rate);
    rebuildLinks();
}

void ContrastInjectionModel::reset() {
    m_state = State{};
    m_weightedDist.clear();
}

} // namespace cn