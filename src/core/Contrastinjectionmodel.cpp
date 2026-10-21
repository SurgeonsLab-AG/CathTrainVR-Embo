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

// The 0.6 occlusion threshold this used to define locally now lives in
// EmbolizationModel::linkOccluded() itself (see this file's own
// linkOccluded(), which just asks that method) - the same "the owning
// model owns its own occlusion definition" pattern
// CoilEmbolizationModel::linkOccluded() already established for its own
// 1.0 threshold.

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
    // Both delegate to the owning model's OWN linkOccluded() now, rather
    // than this function iterating state().links directly - with multi-
    // target support, state() only ever returns the currently ACTIVE
    // target, and reading it directly here would miss occlusive material
    // sitting in a different target that simply is not selected in the UI
    // right now. Each model's own linkOccluded() already checks every
    // target it holds, not just the active one.
    if (m_emboli && m_emboli->linkOccluded(a, b)) return true;
    // Coils use their own fillFraction >= 1.0 "fully packed" rule rather
    // than the liquid model's softer 0.6 - a coil nest that is 60%
    // packed by length is nowhere near occluded the way a viscous liquid
    // cast at 60% fill visually reads as "getting there". The coil model
    // owns that definition; this just asks it rather than duplicating the
    // threshold here. Independent of m_emboli - a case can be coiled,
    // liquid-embolized, or both, and either alone blocks the link.
    if (m_coils && m_coils->linkOccluded(a, b)) return true;
    return false;
}

void ContrastInjectionModel::beginPhase(ContrastPhase phase, int32_t node, int32_t excludeNeighbor) {
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
    m_excludedNeighbor = excludeNeighbor;
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
            // The catheter's own body occupies this one specific segment -
            // same fix as EmbolizationModel::begin(), see beginPhase()'s
            // own comment. Only the DIRECT edge out of the injection node
            // is blocked, not every route that happens to lead toward the
            // access site in general.
            if (v == m_state.injectionNode && w == m_excludedNeighbor) continue;

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
        // Same exclusion as recomputeWeightedDistances(), independently
        // necessary here for the identical reason the embolic fix needed
        // it in both places: this exact edge would otherwise still render
        // starting from dNear=0 (the injection node's own distance is
        // always 0, regardless of what the Dijkstra pass found for the
        // excluded neighbor via some other path).
        if ((link.from == m_state.injectionNode && link.to == m_excludedNeighbor) ||
            (link.to == m_state.injectionNode && link.from == m_excludedNeighbor))
            continue;

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