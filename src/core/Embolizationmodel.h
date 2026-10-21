#pragma once
#include "CenterlineGraph.h"
#include <vector>
#include <cstdint>
#include <string>

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
///
/// REFLUX. `excludeNeighbor` (see begin()) permanently walls off the
/// catheter's own proximal segment from the forward flood fill - correct
/// for a well-positioned injection, but it also made reflux structurally
/// impossible no matter how a trainee injected, which is backwards: reflux
/// past the tip into the wider parent vessel from overinjecting - pushing
/// material in faster or longer than the target territory can actually
/// absorb - is one of the headline real complications of embolization, and
/// arguably the single most important failure mode to train against.
///
/// So the excluded segment is not actually walled off forever - it is
/// walled off from the FORWARD flood fill (the target territory should
/// never be reached "the wrong way round"), while a SEPARATE weighted
/// Dijkstra (m_refluxWeightedDist) is run from that same excluded neighbor
/// outward into the rest of the proximal tree, mirroring the forward one
/// exactly (same resistance() formula, same per-agent viscosity) but
/// excluding the edge back to the injection node so it cannot just fold
/// back into the same forward territory. Injected volume splits between
/// the two fronts based on how full the forward target already is: while
/// there is still real room forward, all of it advances the forward cast
/// as before; once the forward territory approaches its own capacity (the
/// farthest weighted distance actually reachable there), a growing share
/// of any further volume instead advances the reflux front - see inject()'s
/// own comment for the exact split and why the onset point itself is
/// derived from the agent's own viscosity rather than a single fixed
/// number for all four agents.
///
/// MULTI-TARGET. A real MMA case sometimes needs two independent target
/// branches (e.g. anterior + posterior division) treated separately in the
/// same procedure - previously impossible without reset() destroying the
/// first target's progress. This is now a small VECTOR of independent
/// targets (m_targets), each with its own agent, own weighted-distance
/// fields, own State (including its own reflux) - completely isolated from
/// every other target, the same way two separate EmbolizationModel
/// instances would be, just sharing one object and one graph pointer.
/// Exactly ONE target is "active" at a time (m_activeIdx); begin()/
/// inject()/reset()/state()/agent()/refluxing()/forwardCapacity() all
/// operate on THAT one, completely unchanged in behaviour from before this
/// feature existed - a caller that never calls addTarget()/
/// setActiveTarget() sees no difference at all, since a fresh model starts
/// with exactly one target already active (index 0). Multi-target access
/// (stateAt(), targetName(), targetCount()) is purely ADDITIVE on top,
/// needed so a renderer can draw every target's own cast simultaneously
/// rather than only whichever one is currently active.
class EmbolizationModel {
public:
    /// One graph link that the cast has reached at least partially.
    struct CastLink {
        int32_t from = -1, to = -1;
        float weightedLength = 0.f;   ///< agent-resistance-scaled length used to grow this link
        float fillFraction   = 0.f;   ///< 0..1, how much of this link is currently cast
        /// True when this link was filled by REFLUX (backward, past the
        /// tip, into the excluded proximal segment and beyond) rather than
        /// by the forward target-territory cast. Same CastLink shape
        /// either way - VesselView's existing buildEmboliInto() renders
        /// both through the one shared tube-builder with no changes needed
        /// - this flag exists so a caller that wants to warn "reflux
        /// detected" or tint it differently later can tell the two apart.
        bool isReflux = false;
    };

    struct State {
        bool    active          = false;
        int32_t injectionNode   = -1;
        float   volumeInjected  = 0.f;   ///< cumulative, arbitrary unit — read-out only
        float   filledArcLength = 0.f;   ///< current forward flood-fill radius, model units
        /// Current reflux flood-fill radius, model units - zero until
        /// overinjection actually pushes material backward. Permanent, the
        /// same as filledArcLength: refluxed material does not un-reflux
        /// once deposited, any more than forward cast does.
        float   refluxArcLength = 0.f;
        std::vector<CastLink> links;     ///< every link touched so far, forward AND reflux
    };

    void setGraph(const CenterlineGraph *g) { m_graph = g; }
    const CenterlineGraph *graph() const { return m_graph; }

    void setAgent(EmbolicAgent a) { m_targets[m_activeIdx].agent = a; }
    EmbolicAgent agent() const { return m_targets[m_activeIdx].agent; }

    /// Starts a new cast at `node` — normally the catheter tip's current
    /// graph node (CatheterModel::State::currentNodeId). Clears any previous
    /// cast ON THE CURRENTLY ACTIVE TARGET ONLY (every other target this
    /// model may hold is completely untouched) - "one embolization site
    /// per target at a time", the same "start a fresh step" convention the
    /// rest of the console uses (e.g. NavigationController::startFrom()).
    /// To work on a genuinely separate target instead of restarting this
    /// one, call addTarget()/setActiveTarget() first.
    ///
    /// `excludeNeighbor`, if >= 0, is treated as unreachable directly from
    /// `node` FOR THE FORWARD CAST — this is what represents the catheter's
    /// own body physically occupying and blocking the one vessel segment
    /// immediately PROXIMAL to its tip (the segment it just travelled
    /// through to get here). Without this, the weighted flood-fill (see
    /// resistance() in the .cpp) actively prefers WIDER channels for a
    /// viscous agent, and the vessel the catheter arrived through is very
    /// often wider than the narrow target branches the tip is actually
    /// positioned at — meaning the cast would preferentially reflux
    /// backward into the parent vessel instead of advancing into the
    /// target on the very first drop of agent, which is backwards from how
    /// a real, well-positioned embolization actually behaves. The caller
    /// (normally MainWindow) determines which neighbor is proximal via
    /// CenterlineGraph::distancesFrom() from the access site.
    ///
    /// This does NOT make reflux impossible, only conditional — see the
    /// class comment above and inject()'s own comment for when and how
    /// much of it can still happen once the forward target territory
    /// starts running out of room.
    void begin(int32_t node, int32_t excludeNeighbor = -1);

    /// Injects `dVolume` (>= 0) more material into the CURRENTLY ACTIVE
    /// target. While the forward target territory still has real room
    /// left, this grows the forward cast exactly as before. Once that
    /// territory approaches its own capacity, a growing share of any
    /// further volume instead grows a SEPARATE reflux cast backward through
    /// the excluded proximal segment (see the class comment). No-op before
    /// begin() has been called on this target.
    void inject(float dVolume);

    /// Clears the CURRENTLY ACTIVE target's cast entirely (forward AND
    /// reflux); its active() becomes false. Every OTHER target this model
    /// may hold is completely untouched - this is deliberately NOT "clear
    /// everything", the same way retracting one coil never touched a
    /// different one already detached elsewhere. See resetAll() for
    /// "start completely over with a single fresh target".
    void reset();

    /// Discards every target and starts over with exactly one fresh,
    /// inactive target at index 0 - the genuine "clear everything" a New
    /// Patient/new-case path wants, as opposed to reset()'s "clear just
    /// the one target I'm currently working on".
    void resetAll();

    bool active() const { return m_targets[m_activeIdx].state.active; }
    const State &state() const { return m_targets[m_activeIdx].state; }

    /// True if ANY target this model holds is active - not only the
    /// currently selected one. What a renderer needs for "is there real
    /// embolic material/workflow anywhere in this scene", as opposed to
    /// active()'s own "is the currently active target itself in progress" -
    /// checking only active() here would wrongly report false whenever the
    /// active target happens to be empty/reset while a DIFFERENT target
    /// still holds real material.
    bool anyActive() const {
        for (const auto &t : m_targets) if (t.state.active) return true;
        return false;
    }

    /// True once any reflux has actually occurred on the ACTIVE target
    /// (state().refluxArcLength > 0) - the one-line "has this gone wrong"
    /// check a caller wants without reasoning about arc lengths directly.
    bool refluxing() const { return m_targets[m_activeIdx].state.refluxArcLength > 1e-6f; }

    /// Total weighted arc length reachable in the ACTIVE target's forward
    /// territory (the farthest finite distance in the forward Dijkstra) -
    /// "how much room is there forward before it is genuinely full".
    /// Exposed so a caller can show a real "how close to full is the
    /// target" read-out, e.g. state().filledArcLength / forwardCapacity().
    float forwardCapacity() const { return m_targets[m_activeIdx].forwardCapacity; }

    /// True once fillFraction on this link crosses the occlusion threshold
    /// IN ANY TARGET this model holds, not only the currently active one -
    /// occlusion is a physical fact about the vessel, and a link the
    /// operator packed full while working on target A must still read as
    /// occluded while target B happens to be selected in the UI. Mirrors
    /// CoilEmbolizationModel::linkOccluded()'s own "owns its threshold,
    /// checks every committed link regardless of which target" pattern -
    /// see ContrastInjectionModel::linkOccluded(), which checks both this
    /// and the coil model.
    bool linkOccluded(int32_t a, int32_t b) const;

    // ── Multi-target access ──────────────────────────────────────────────
    /// Creates a new, independent embolization target and makes it active -
    /// e.g. a second MMA division treated separately from the first.
    /// Returns the new target's index. `name` is a caller-supplied label
    /// (e.g. "Anterior division") purely for display in the UI/report;
    /// empty auto-generates "Target N". The PREVIOUSLY active target's own
    /// state (cast, reflux, agent, everything) is completely untouched -
    /// switching which target is active never resets or discards anything.
    int addTarget(const std::string &name = std::string());

    /// Switches which target subsequent begin()/inject()/reset()/state()/
    /// agent()/refluxing()/forwardCapacity() calls affect. Every other
    /// target's own state is completely unaffected by this - it only
    /// changes which ONE the next call reaches. No-op if idx is out of range.
    void setActiveTarget(int idx);
    int activeTarget() const { return m_activeIdx; }
    int targetCount() const { return static_cast<int>(m_targets.size()); }
    const std::string &targetName(int idx) const;

    /// Read-only access to a SPECIFIC target's own state, regardless of
    /// which one is currently active - what a renderer needs to draw every
    /// target's own cast simultaneously rather than only the active one.
    /// Returns a static empty State if idx is out of range.
    const State &stateAt(int idx) const;
    EmbolicAgent agentAt(int idx) const;
    bool refluxingAt(int idx) const;

private:
    /// One independent embolization target - see the class comment's
    /// "MULTI-TARGET" section. Everything that used to be a bare member
    /// (m_state, m_excludedNeighbor, m_weightedDist, ...) now lives in one
    /// of these, one per target, so two targets can never see or affect
    /// each other's state.
    struct Target {
        std::string name;
        EmbolicAgent agent = EmbolicAgent::Onyx;
        State state;
        /// See begin()'s own comment - the proximal neighbor node the
        /// catheter's own body blocks (for the forward cast only), or -1
        /// if none was given.
        int32_t excludedNeighbor = -1;
        std::vector<float> weightedDist;        ///< forward, size == graph node count after begin()
        std::vector<float> refluxWeightedDist;  ///< backward from excludedNeighbor; empty if none given
        /// See forwardCapacity(). Computed once in recomputeWeightedDistances().
        float forwardCapacity = 0.f;
    };

    /// Agent-resistance-weighted Dijkstra from `seed`, excluding the single
    /// direct edge (seed, excludeFromSeed) if excludeFromSeed >= 0. Used
    /// twice: once forward from the injection node (excluding the
    /// catheter's own proximal segment), once backward from that same
    /// excluded segment (excluding the edge back to the injection node) -
    /// same formula both times, mirrored seed/exclusion, which is what
    /// makes reflux behave with exactly the same per-agent viscosity/
    /// width preference the forward cast already has, not a separately
    /// tuned "reflux physics".
    void weightedDijkstra(int32_t seed, int32_t excludeFromSeed,
                          std::vector<float> &distOut, EmbolicAgent agent) const;

    /// Recomputes t.weightedDist (forward) and t.refluxWeightedDist
    /// (backward, only if t.excludedNeighbor >= 0) plus t.forwardCapacity -
    /// once per begin(), not per inject(): neither the graph nor the
    /// injection node nor the agent change between injections within the
    /// same cast.
    void recomputeWeightedDistances(Target &t);

    /// Rebuilds t.state.links from scratch from the CURRENT filledArcLength
    /// and refluxArcLength - the same "cheap enough to redo every call,
    /// cannot drift" reasoning inject() already relied on before reflux
    /// existed, now walking both weighted-distance fields instead of one.
    void rebuildLinks(Target &t);

    const CenterlineGraph *m_graph = nullptr;
    /// Always at least one entry - a fresh model starts with exactly one
    /// target already active at index 0, so single-target use (the whole
    /// of this class's behaviour before multi-target existed) needs no
    /// special-casing anywhere.
    std::vector<Target> m_targets{Target{}};
    int m_activeIdx = 0;
};

} // namespace cn
