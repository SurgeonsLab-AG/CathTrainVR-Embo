#include "CoilEmbolizationModel.h"
#include <algorithm>
#include <cstring>

namespace cn {

namespace {

// A handful of real platinum embolization coils sized for the calibre range
// MMA embolization actually uses (0.014"/0.018" microcatheter-delivered
// coils, 2-4 mm helix diameter) - not a claim of exact manufacturer specs,
// the same spirit CatheterCatalog and EmbolizationModel's own agent table
// already apply to their own numbers. Ordered smallest to largest helix so
// a selection dropdown reads as a natural size progression.
const CoilSpec kCoilSpecs[] = {
    { "Target Nano 2mm x 4cm",
     "Fibered platinum detachable coil, softest/finest platform in the "
     "family - sized for the distal MMA branches themselves.",
     2.0f, 40.f, {0.86f, 0.76f, 0.20f}, 0.95f, 0.090f },

    { "Target Nano 3mm x 6cm",
     "Fibered platinum detachable coil, one step up in helix diameter - "
     "the usual size for the proximal MMA trunk just past the takeoff.",
     3.0f, 60.f, {0.86f, 0.76f, 0.20f}, 0.95f, 0.070f },

    { "Axium Prime 4mm x 8cm",
     "Platinum coil with a stiffer, larger-helix finish - sized for a "
     "wider feeder or as a proximal protection coil placed before the "
     "liquid-agent injection.",
     4.0f, 80.f, {0.84f, 0.74f, 0.22f}, 0.92f, 0.050f },
    };

} // namespace

const CoilSpec *coilCatalog(size_t &count) {
    count = sizeof(kCoilSpecs) / sizeof(kCoilSpecs[0]);
    return kCoilSpecs;
}

const CoilSpec *findCoil(const char *displayName) {
    size_t n = 0;
    const CoilSpec *cat = coilCatalog(n);
    for (size_t i = 0; i < n; ++i)
        if (std::strcmp(cat[i].name, displayName) == 0) return &cat[i];
    return nullptr;
}

namespace {
constexpr float kUndersizedRatio = 0.65f;
constexpr float kOversizedRatio = 1.60f;
constexpr float kUndersizedEfficiency = 0.40f;
constexpr float kOversizedEfficiency  = 0.60f;
} // namespace

CoilSizeFit classifyCoilFit(float coilDiameterMm, float vesselDiameterMm) {
    if (vesselDiameterMm <= 1e-6f) return CoilSizeFit::Good;   // no vessel data - assume fine rather than guess
    const float ratio = coilDiameterMm / vesselDiameterMm;
    if (ratio < kUndersizedRatio) return CoilSizeFit::Undersized;
    if (ratio > kOversizedRatio)  return CoilSizeFit::Oversized;
    return CoilSizeFit::Good;
}

const char *coilSizeFitName(CoilSizeFit fit) {
    switch (fit) {
    case CoilSizeFit::Undersized: return "undersized for this vessel";
    case CoilSizeFit::Oversized:  return "oversized for this vessel";
    default:                      return "well sized";
    }
}

namespace {
float packingEfficiencyFor(CoilSizeFit fit) {
    switch (fit) {
    case CoilSizeFit::Undersized: return kUndersizedEfficiency;
    case CoilSizeFit::Oversized:  return kOversizedEfficiency;
    default:                      return 1.0f;
    }
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Active-target API - each resolves m_targets[m_activeIdx] and operates on
// it exactly as this class always operated on its one bare State/staging
// area before Target existed. A caller that never calls addTarget()/
// setActiveTarget() sees byte-for-byte the same behaviour as before.
// ─────────────────────────────────────────────────────────────────────────────
void CoilEmbolizationModel::beginAt(int32_t node, int32_t excludeNeighbor,
                                    const Vec3 &tipWorldPosition) {
    if (!m_graph || node < 0 || node >= static_cast<int32_t>(m_graph->nodeCount())) return;
    Target &t = m_targets[static_cast<size_t>(m_activeIdx)];
    t.state.active = true;
    t.state.releaseNode = node;
    t.excludedNeighbor = excludeNeighbor;
    t.currentSpec = nullptr;
    t.currentCoilLengthMm = 0.f;
    t.currentReleasePosition = tipWorldPosition;
    t.currentFit = CoilSizeFit::Good;
    t.currentVesselDiameterMm = 0.f;
    // Abandon any not-yet-committed staging left over from a previous coil
    // - see the class comment's own note on this. A fresh beginAt() means a
    // fresh coil; a forgotten pending one from before should not silently
    // keep accumulating alongside it.
    t.stagingLinks.clear();
}

CoilEmbolizationModel::CoiledLink *CoilEmbolizationModel::findOrAddLink(
    std::vector<CoiledLink> &links, int32_t a, int32_t b) {
    for (auto &l : links)
        if ((l.from == a && l.to == b) || (l.from == b && l.to == a)) return &l;
    links.push_back(CoiledLink{a, b, 0.f, 0.f});
    return &links.back();
}

void CoilEmbolizationModel::advanceCoil(const CoilSpec &spec, float dLengthMm, float mmPerUnit) {
    Target &t = m_targets[static_cast<size_t>(m_activeIdx)];
    if (!m_graph || !t.state.active || dLengthMm <= 0.f) return;
    t.currentSpec = &spec;
    t.lastMmPerUnit = mmPerUnit;

    // Assessed fresh every call (cheap: one node lookup) rather than only
    // once at beginAt() - the release node/vessel do not change mid-coil,
    // so this is idempotent, but recomputing costs nothing and avoids a
    // second place that could fall out of sync. Exposed live via
    // pendingFit()/pendingVesselDiameterMm() so a trainee can see a size
    // mismatch warning WHILE the coil is still staged, not only after
    // committing - see those accessors' own comment.
    if (static_cast<size_t>(t.state.releaseNode) < m_graph->nodeCount())
        t.currentVesselDiameterMm = m_graph->nodes()[static_cast<size_t>(t.state.releaseNode)].radius *
                                    2.f * mmPerUnit;
    t.currentFit = classifyCoilFit(spec.primaryDiameterMm, t.currentVesselDiameterMm);
    const float efficiency = packingEfficiencyFor(t.currentFit);

    const float room = spec.lengthMm - t.currentCoilLengthMm;
    const float push = std::min(dLengthMm, std::max(0.f, room));
    if (push <= 0.f) return;   // coil already fully paid out

    // Walk the single non-branching path outward from the release node -
    // the same convention NavigationController::extendRoute() uses
    // elsewhere in this codebase: a coil does not fan out through every
    // reachable branch the way a liquid agent's weighted flood fill does,
    // it follows the one vessel it was physically pushed into and stops
    // dead at the first bifurcation. `visited` guards the same small-cycle
    // case extendRoute() itself guards against.
    //
    // Every link touched here goes into t.stagingLinks, NEVER
    // t.state.links directly - this coil is not real (committed, occlusion-
    // relevant) material until finishCoil() says so. See the class
    // comment's "STAGED vs COMMITTED" section.
    std::vector<char> visited(m_graph->nodeCount(), 0);
    visited[static_cast<size_t>(t.state.releaseNode)] = 1;
    int32_t cur = t.state.releaseNode;
    int32_t prev = t.excludedNeighbor;
    CoiledLink *lastLink = nullptr;   ///< most recent link actually crossed, for overflow below

    float remainingMm = push;
    while (remainingMm > 1e-6f) {
        std::vector<int32_t> next;
        for (int32_t nb : m_graph->neighbours(cur))
            if (nb != prev && !visited[static_cast<size_t>(nb)]) next.push_back(nb);

        if (next.empty()) break;    // ran off the end of the vessel - no more room to advance into
        if (next.size() > 1) break; // stopped at a bifurcation - a coil does not choose a branch

        const int32_t nb = next.front();
        const float linkLenMm = distance(m_graph->nodes()[static_cast<size_t>(cur)].position,
                                         m_graph->nodes()[static_cast<size_t>(nb)].position) *
                                mmPerUnit;
        const float take = std::min(remainingMm, std::max(linkLenMm, 1e-6f));

        lastLink = findOrAddLink(t.stagingLinks, cur, nb);
        lastLink->packingLength += take * spec.packingYieldPerMm * efficiency;
        lastLink->fillFraction = std::clamp(lastLink->packingLength / std::max(linkLenMm, 1e-6f), 0.f, 1.f);

        remainingMm -= take;
        visited[static_cast<size_t>(nb)] = 1;
        prev = cur;
        cur = nb;
    }

    // Whatever is left once the walk can no longer advance (dead end or
    // bifurcation) is not lost - a real coil that runs out of straight
    // vessel to travel into does not vanish, it loops back on itself and
    // nests right where it stopped.
    if (remainingMm > 1e-6f && lastLink) {
        lastLink->packingLength += remainingMm * spec.packingYieldPerMm * efficiency;
        const float linkLenMm = distance(m_graph->nodes()[static_cast<size_t>(lastLink->from)].position,
                                         m_graph->nodes()[static_cast<size_t>(lastLink->to)].position) *
                                mmPerUnit;
        lastLink->fillFraction = std::clamp(lastLink->packingLength / std::max(linkLenMm, 1e-6f), 0.f, 1.f);
        remainingMm = 0.f;
    }

    t.currentCoilLengthMm += (push - remainingMm);
}

void CoilEmbolizationModel::finishCoil() {
    Target &t = m_targets[static_cast<size_t>(m_activeIdx)];
    if (!t.state.active || !t.currentSpec || t.currentCoilLengthMm <= 0.f) {
        t.currentSpec = nullptr;
        t.currentCoilLengthMm = 0.f;
        t.stagingLinks.clear();
        return;
    }

    // Snapshot BEFORE merging - exactly what this one coil itself
    // contributed, so undoLastCoil() can later subtract precisely this
    // back out even though the merge below combines it additively with
    // whatever earlier coils already packed onto the same links.
    const std::vector<CoiledLink> contribution = t.stagingLinks;

    for (const auto &staged : t.stagingLinks) {
        CoiledLink *committed = findOrAddLink(t.state.links, staged.from, staged.to);
        committed->packingLength += staged.packingLength;
        float linkLenMm = 0.f;
        if (m_graph &&
            static_cast<size_t>(staged.from) < m_graph->nodeCount() &&
            static_cast<size_t>(staged.to)   < m_graph->nodeCount()) {
            linkLenMm = distance(m_graph->nodes()[static_cast<size_t>(staged.from)].position,
                                 m_graph->nodes()[static_cast<size_t>(staged.to)].position) *
                        t.lastMmPerUnit;
        }
        committed->fillFraction = std::clamp(committed->packingLength / std::max(linkLenMm, 1e-6f), 0.f, 1.f);
    }
    t.stagingLinks.clear();

    DeployedCoil dc;
    dc.spec = t.currentSpec;
    dc.deployedLengthMm = t.currentCoilLengthMm;
    dc.releaseNode = t.state.releaseNode;
    dc.releasePosition = t.currentReleasePosition;
    dc.fit = t.currentFit;
    dc.vesselDiameterMm = t.currentVesselDiameterMm;
    dc.contributionLinks = contribution;
    t.state.coils.push_back(dc);

    t.currentSpec = nullptr;
    t.currentCoilLengthMm = 0.f;
}

bool CoilEmbolizationModel::undoLastCoil() {
    Target &t = m_targets[static_cast<size_t>(m_activeIdx)];
    if (t.state.coils.empty()) return false;
    const DeployedCoil last = t.state.coils.back();
    t.state.coils.pop_back();

    for (const auto &contrib : last.contributionLinks) {
        for (auto it = t.state.links.begin(); it != t.state.links.end(); ++it) {
            const bool sameEdge = (it->from == contrib.from && it->to == contrib.to) ||
                                  (it->from == contrib.to && it->to == contrib.from);
            if (!sameEdge) continue;

            it->packingLength -= contrib.packingLength;
            if (it->packingLength <= 1e-6f) {
                it = t.state.links.erase(it);
            } else {
                float linkLenMm = 0.f;
                if (m_graph &&
                    static_cast<size_t>(it->from) < m_graph->nodeCount() &&
                    static_cast<size_t>(it->to)   < m_graph->nodeCount()) {
                    linkLenMm = distance(m_graph->nodes()[static_cast<size_t>(it->from)].position,
                                         m_graph->nodes()[static_cast<size_t>(it->to)].position) *
                                t.lastMmPerUnit;
                }
                it->fillFraction = std::clamp(it->packingLength / std::max(linkLenMm, 1e-6f), 0.f, 1.f);
            }
            break;
        }
    }
    return true;
}

void CoilEmbolizationModel::retractCoil() {
    Target &t = m_targets[static_cast<size_t>(m_activeIdx)];
    t.stagingLinks.clear();
    t.currentSpec = nullptr;
    t.currentCoilLengthMm = 0.f;
    t.currentFit = CoilSizeFit::Good;
    t.currentVesselDiameterMm = 0.f;
}

void CoilEmbolizationModel::deployWholeCoil(const CoilSpec &spec, int32_t node,
                                            int32_t excludeNeighbor, float mmPerUnit,
                                            const Vec3 &tipWorldPosition) {
    beginAt(node, excludeNeighbor, tipWorldPosition);
    advanceCoil(spec, spec.lengthMm, mmPerUnit);
    finishCoil();
}

bool CoilEmbolizationModel::linkOccluded(int32_t a, int32_t b) const {
    // Checks EVERY target, not only the active one - see this method's own
    // header comment for why occlusion cannot be scoped to "whichever
    // target the UI happens to have selected right now".
    for (const auto &t : m_targets) {
        for (const auto &l : t.state.links)
            if (l.fillFraction >= 1.f &&
                ((l.from == a && l.to == b) || (l.from == b && l.to == a)))
                return true;
    }
    return false;
}

void CoilEmbolizationModel::reset() {
    m_targets[static_cast<size_t>(m_activeIdx)] = Target{};
}

void CoilEmbolizationModel::resetAll() {
    m_targets.assign(1, Target{});
    m_activeIdx = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-target access
// ─────────────────────────────────────────────────────────────────────────────
int CoilEmbolizationModel::addTarget(const std::string &name) {
    Target t;
    t.name = name.empty() ? ("Target " + std::to_string(m_targets.size() + 1)) : name;
    m_targets.push_back(std::move(t));
    m_activeIdx = static_cast<int>(m_targets.size()) - 1;
    return m_activeIdx;
}

void CoilEmbolizationModel::setActiveTarget(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_targets.size())) return;
    m_activeIdx = idx;
}

const std::string &CoilEmbolizationModel::targetName(int idx) const {
    static const std::string kEmpty;
    if (idx < 0 || idx >= static_cast<int>(m_targets.size())) return kEmpty;
    return m_targets[static_cast<size_t>(idx)].name;
}

const CoilEmbolizationModel::State &CoilEmbolizationModel::stateAt(int idx) const {
    static const State kEmptyState{};
    if (idx < 0 || idx >= static_cast<int>(m_targets.size())) return kEmptyState;
    return m_targets[static_cast<size_t>(idx)].state;
}

bool CoilEmbolizationModel::hasPendingCoilAt(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(m_targets.size())) return false;
    return m_targets[static_cast<size_t>(idx)].currentCoilLengthMm > 0.f;
}

} // namespace cn