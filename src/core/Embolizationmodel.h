#pragma once
#include "CenterlineGraph.h"
#include <vector>
#include <cstdint>

namespace cn {

enum class EmbolicAgent { Onyx, Squid, Phil, Glue };

/// Fixed properties of one embolic agent. Values are reasoned from each
/// agent's real clinical handling and radiopacity characteristics:
///
///   Onyx, SQUID   — ethylene-vinyl alcohol copolymers, radiopaque via
///                   suspended TANTALUM (Onyx) / TANTALUM+TUNGSTEN (SQUID)
///                   powder. Viscous, cohesive, slow, highly radiopaque.
///   PHIL          — also an EVOH copolymer, but radiopaque via dissolved
///                   IODINE (triiodophenol) rather than a suspended metal
///                   powder — visibly less radiopaque than Onyx/SQUID on
///                   fluoroscopy, and physically a cream/off-white liquid
///                   rather than Onyx's black "lava".
///   Glue (n-BCA)  — radiopaque via mixed ethiodized oil (Lipiodol), an
///                   IODINE-based contrast oil, not a suspended metal —
///                   thin, fast-polymerizing, least controllable.
///
/// Not a claim of exact clinical dosing or flow numbers — the same spirit
/// CatheterCatalog already applies to its own device numbers.
struct EmbolicAgentSpec {
    const char *name;
    const char *note;
    float colour[3];    ///< base RGB, 0..1
    float opacity;      ///< cast render opacity, 0..1
    float radiopacity;  ///< brightness contribution in the Fluoro/X-ray view, 0..1

    /// How strongly the agent prefers the widest available channel over a
    /// narrow side branch, 0..1. High viscosity (Onyx/SQUID) fills the main
    /// column first and only backs into small branches once it does; low
    /// viscosity (Glue) spreads into everything nearby almost equally,
    /// which is exactly why glue is the hardest of the four to control.
    float viscosity;

    /// Arc length filled per unit of injected volume, model units per unit.
    /// Glue polymerizes and advances fast; Onyx/SQUID are injected slowly
    /// and under continuous fluoroscopic control, so they advance less per
    /// unit for the same "one injection step".
    float spreadRate;
};

const EmbolicAgentSpec &embolicAgentSpec(EmbolicAgent a);

/// Embolic-cast growth model: deliberately a REDUCED model, not a fluid
/// simulation - the same precedent CatheterModel's own class comment
/// establishes for this codebase. The cast grows outward from the injection
/// node as a graph-distance flood fill: every link's effective length is
/// scaled by a per-agent, per-radius "resistance" (narrow relative to a
/// hard-coded reference calibre costs more for a viscous agent, costs
/// almost nothing for a thin one), and the fill front is one scalar -
/// `filledArcLength` - that advances as volume is injected. This is cheap
/// enough to update every frame and, for the same sequence of inject() calls,
/// always produces the same graph of filled links: the same determinism
/// guarantee CatheterModel relies on so that every view, reading the one
/// shared instance, can never compute a different cast from it.
class EmbolizationModel {
public:
    /// One graph link that the cast has reached at least partially.
    struct CastLink {
        int32_t from = -1, to = -1;
        float weightedLength = 0.f;   ///< agent-resistance-scaled length used to grow this link
        float fillFraction   = 0.f;   ///< 0..1, how much of this link is currently cast
    };

    struct State {
        bool    active          = false;
        int32_t injectionNode   = -1;
        float   volumeInjected  = 0.f;   ///< cumulative, arbitrary unit — read-out only
        float   filledArcLength = 0.f;   ///< current flood-fill radius, model units
        std::vector<CastLink> links;     ///< every link touched so far
    };

    void setGraph(const CenterlineGraph *g) { m_graph = g; }
    const CenterlineGraph *graph() const { return m_graph; }

    void setAgent(EmbolicAgent a) { m_agent = a; }
    EmbolicAgent agent() const { return m_agent; }

    /// Starts a new cast at `node` — normally the catheter tip's current
    /// graph node (CatheterModel::State::currentNodeId). Clears any previous
    /// cast: one embolization target at a time, the same "start a fresh step"
    /// convention the rest of the console uses (e.g. NavigationController::
    /// startFrom()).
    ///
    /// `excludeNeighbor`, if >= 0, is treated as unreachable directly from
    /// `node` — this is what represents the catheter's own body physically
    /// occupying and blocking the one vessel segment immediately PROXIMAL to
    /// its tip (the segment it just travelled through to get here). Without
    /// this, the weighted flood-fill (see resistance() in the .cpp) actively
    /// prefers WIDER channels for a viscous agent, and the vessel the
    /// catheter arrived through is very often wider than the narrow target
    /// branches the tip is actually positioned at — meaning the cast would
    /// preferentially reflux backward into the parent vessel instead of
    /// advancing into the target, which is backwards from how a real,
    /// well-positioned embolization actually behaves. The caller (normally
    /// MainWindow) determines which neighbor is proximal via
    /// CenterlineGraph::distancesFrom() from the access site.
    void begin(int32_t node, int32_t excludeNeighbor = -1);

    /// Injects `dVolume` (>= 0) more material, growing the cast outward.
    /// No-op before begin() has been called, or once the graph reports no
    /// further reachable links (the whole reachable subtree is fully cast).
    void inject(float dVolume);

    /// Clears the cast entirely; active() becomes false.
    void reset();

    bool active() const { return m_state.active; }
    const State &state() const { return m_state; }

private:
    /// Recomputes m_weightedDist (agent-resistance-weighted Dijkstra
    /// distance from the injection node to every node) — once per begin(),
    /// not per inject(): neither the graph nor the injection node nor the
    /// agent change between injections within the same cast.
    void recomputeWeightedDistances();

    const CenterlineGraph *m_graph = nullptr;
    EmbolicAgent m_agent = EmbolicAgent::Onyx;
    State m_state;
    /// See begin()'s own comment - the proximal neighbor node the catheter's
    /// own body blocks, or -1 if none was given.
    int32_t m_excludedNeighbor = -1;
    std::vector<float> m_weightedDist;   ///< size == graph node count after begin()
};

} // namespace cn
