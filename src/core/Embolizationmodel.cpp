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

// ─────────────────────────────────────────────────────────────────────────────
// Active-target API - every one of these is a thin wrapper that resolves
// m_targets[m_activeIdx] and forwards to the Target-parameterized
// implementation below. This is the ENTIRE surface a caller that never uses
// multi-target sees, and its behaviour is byte-for-byte what it was before
// Target existed, since a fresh model starts with exactly one active target.
// ─────────────────────────────────────────────────────────────────────────────
void EmbolizationModel::begin(int32_t node, int32_t excludeNeighbor) {
    Target &t = m_targets[static_cast<size_t>(m_activeIdx)];
    t.state = State{};
    t.excludedNeighbor = excludeNeighbor;
    if (!m_graph || node < 0 || node >= static_cast<int32_t>(m_graph->nodeCount())) return;
    t.state.active = true;
    t.state.injectionNode = node;
    recomputeWeightedDistances(t);
}

void EmbolizationModel::inject(float dVolume) {
    Target &t = m_targets[static_cast<size_t>(m_activeIdx)];
    if (!t.state.active || dVolume <= 0.f || !m_graph) return;
    const EmbolicAgentSpec &spec = embolicAgentSpec(t.agent);
    t.state.volumeInjected += dVolume;

    const float requestedAdvance = dVolume * spec.spreadRate;

    const float fillRatio = (t.forwardCapacity > 1e-6f)
                                ? std::clamp(t.state.filledArcLength / t.forwardCapacity, 0.f, 1.f)
                                : 1.f;

    const float onsetFraction = 0.5f + 0.4f * spec.viscosity;
    const float refluxSusceptibility =
        std::clamp((fillRatio - onsetFraction) / std::max(1.f - onsetFraction, 0.05f), 0.f, 1.f);

    const float toForward = requestedAdvance * (1.f - refluxSusceptibility);
    const float toReflux  = requestedAdvance * refluxSusceptibility;

    if (t.forwardCapacity > 1e-6f)
        t.state.filledArcLength = std::min(t.state.filledArcLength + toForward, t.forwardCapacity);
    else
        t.state.filledArcLength += toForward;   // toForward is ~0 here regardless - see fillRatio above

    if (t.excludedNeighbor >= 0)
        t.state.refluxArcLength += toReflux;

    rebuildLinks(t);
}

void EmbolizationModel::reset() {
    m_targets[static_cast<size_t>(m_activeIdx)] = Target{};
}

void EmbolizationModel::resetAll() {
    m_targets.assign(1, Target{});
    m_activeIdx = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-target access
// ─────────────────────────────────────────────────────────────────────────────
int EmbolizationModel::addTarget(const std::string &name) {
    Target t;
    t.name = name.empty() ? ("Target " + std::to_string(m_targets.size() + 1)) : name;
    m_targets.push_back(std::move(t));
    m_activeIdx = static_cast<int>(m_targets.size()) - 1;
    return m_activeIdx;
}

void EmbolizationModel::setActiveTarget(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_targets.size())) return;
    m_activeIdx = idx;
}

const std::string &EmbolizationModel::targetName(int idx) const {
    static const std::string kEmpty;
    if (idx < 0 || idx >= static_cast<int>(m_targets.size())) return kEmpty;
    return m_targets[static_cast<size_t>(idx)].name;
}

const EmbolizationModel::State &EmbolizationModel::stateAt(int idx) const {
    static const State kEmptyState{};
    if (idx < 0 || idx >= static_cast<int>(m_targets.size())) return kEmptyState;
    return m_targets[static_cast<size_t>(idx)].state;
}

EmbolicAgent EmbolizationModel::agentAt(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(m_targets.size())) return EmbolicAgent::Onyx;
    return m_targets[static_cast<size_t>(idx)].agent;
}

bool EmbolizationModel::refluxingAt(int idx) const {
    return stateAt(idx).refluxArcLength > 1e-6f;
}

bool EmbolizationModel::linkOccluded(int32_t a, int32_t b) const {
    // Same 0.6 threshold ContrastInjectionModel's own kOcclusionThreshold
    // used before this method existed - a viscous liquid cast reads as
    // genuinely occlusive well before it reaches 100% fill, unlike a coil
    // nest (CoilEmbolizationModel's own linkOccluded() uses >= 1.0, "fully
    // packed by length" - the two devices occlude differently and each
    // model owns its own definition rather than a shared constant that
    // would blur that difference).
    constexpr float kOcclusionThreshold = 0.6f;
    for (const auto &t : m_targets) {
        for (const auto &cl : t.state.links) {
            if (cl.fillFraction < kOcclusionThreshold) continue;
            if ((cl.from == a && cl.to == b) || (cl.from == b && cl.to == a)) return true;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Target-parameterized implementation - identical logic to what used to
// read/write bare members, just threaded through `t` explicitly so it works
// on ANY target, not only whichever one happened to be active.
// ─────────────────────────────────────────────────────────────────────────────
void EmbolizationModel::weightedDijkstra(int32_t seed, int32_t excludeFromSeed,
                                         std::vector<float> &distOut, EmbolicAgent agent) const {
    const size_t n = m_graph->nodeCount();
    const float inf = std::numeric_limits<float>::max();
    distOut.assign(n, inf);
    if (seed < 0 || static_cast<size_t>(seed) >= n) return;

    const EmbolicAgentSpec &spec = embolicAgentSpec(agent);
    using Item = std::pair<float, int32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    distOut[static_cast<size_t>(seed)] = 0.f;
    pq.push({0.f, seed});

    while (!pq.empty()) {
        const auto [d, v] = pq.top();
        pq.pop();
        if (d > distOut[static_cast<size_t>(v)]) continue;   // stale heap entry
        for (int32_t w : m_graph->neighbours(v)) {
            if (v == seed && w == excludeFromSeed) continue;
            const float radius = std::min(m_graph->nodes()[static_cast<size_t>(v)].radius,
                                          m_graph->nodes()[static_cast<size_t>(w)].radius);
            const float length = cn::distance(m_graph->nodes()[static_cast<size_t>(v)].position,
                                              m_graph->nodes()[static_cast<size_t>(w)].position);
            const float weighted = length * resistance(radius, spec.viscosity);
            const float nd = d + weighted;
            if (nd < distOut[static_cast<size_t>(w)]) {
                distOut[static_cast<size_t>(w)] = nd;
                pq.push({nd, w});
            }
        }
    }
}

void EmbolizationModel::recomputeWeightedDistances(Target &t) {
    if (!m_graph || t.state.injectionNode < 0) {
        t.weightedDist.clear();
        t.refluxWeightedDist.clear();
        t.forwardCapacity = 0.f;
        return;
    }

    weightedDijkstra(t.state.injectionNode, t.excludedNeighbor, t.weightedDist, t.agent);

    if (t.excludedNeighbor >= 0)
        weightedDijkstra(t.excludedNeighbor, t.state.injectionNode, t.refluxWeightedDist, t.agent);
    else
        t.refluxWeightedDist.clear();

    t.forwardCapacity = 0.f;
    const float inf = std::numeric_limits<float>::max();
    for (float d : t.weightedDist)
        if (d < inf) t.forwardCapacity = std::max(t.forwardCapacity, d);
}

void EmbolizationModel::rebuildLinks(Target &t) {
    t.state.links.clear();
    if (!m_graph) return;
    const float inf = std::numeric_limits<float>::max();
    const EmbolicAgentSpec &spec = embolicAgentSpec(t.agent);

    for (const auto &link : m_graph->links()) {
        if (link.from < 0 || link.to < 0) continue;
        if (static_cast<size_t>(link.from) >= t.weightedDist.size() ||
            static_cast<size_t>(link.to)   >= t.weightedDist.size())
            continue;

        const float radius = std::min(m_graph->nodes()[static_cast<size_t>(link.from)].radius,
                                      m_graph->nodes()[static_cast<size_t>(link.to)].radius);
        const float weightedLen = std::max(
            cn::distance(m_graph->nodes()[static_cast<size_t>(link.from)].position,
                         m_graph->nodes()[static_cast<size_t>(link.to)].position) *
                resistance(radius, spec.viscosity),
            1e-6f);

        const bool isExcludedEdge =
            (link.from == t.state.injectionNode && link.to == t.excludedNeighbor) ||
            (link.to == t.state.injectionNode && link.from == t.excludedNeighbor);

        if (!isExcludedEdge) {
            const float dFrom = t.weightedDist[static_cast<size_t>(link.from)];
            const float dTo   = t.weightedDist[static_cast<size_t>(link.to)];
            if (!(dFrom >= inf && dTo >= inf)) {
                const float dNear = std::min(dFrom, dTo);
                if (dNear < t.state.filledArcLength) {
                    CastLink cl;
                    cl.from = link.from;
                    cl.to   = link.to;
                    cl.weightedLength = weightedLen;
                    cl.fillFraction = std::clamp((t.state.filledArcLength - dNear) / weightedLen, 0.f, 1.f);
                    if (dTo < dFrom) std::swap(cl.from, cl.to);
                    cl.isReflux = false;
                    t.state.links.push_back(cl);
                }
            }
        }

        if (t.excludedNeighbor >= 0 && t.state.refluxArcLength > 0.f &&
            static_cast<size_t>(link.from) < t.refluxWeightedDist.size() &&
            static_cast<size_t>(link.to)   < t.refluxWeightedDist.size()) {
            const float rFrom = t.refluxWeightedDist[static_cast<size_t>(link.from)];
            const float rTo   = t.refluxWeightedDist[static_cast<size_t>(link.to)];
            if (!(rFrom >= inf && rTo >= inf)) {
                const float rNear = std::min(rFrom, rTo);
                if (rNear < t.state.refluxArcLength) {
                    CastLink cl;
                    cl.from = link.from;
                    cl.to   = link.to;
                    cl.weightedLength = weightedLen;
                    cl.fillFraction = std::clamp((t.state.refluxArcLength - rNear) / weightedLen, 0.f, 1.f);
                    if (rTo < rFrom) std::swap(cl.from, cl.to);
                    cl.isReflux = true;
                    t.state.links.push_back(cl);
                }
            }
        }
    }
}

} // namespace cn
