#pragma once
#include "CenterlineGraph.h"
#include <cstdint>
#include <vector>
#include <string>

namespace cn {

/// One catalogue size of a platinum embolization coil. A handful of real
/// device families sized for the calibre range MMA embolization actually
/// uses (0.014"/0.018" microcatheter-delivered coils, 2-4 mm helix
/// diameter) — not a claim of exact manufacturer specs, the same spirit
/// CatheterCatalog and EmbolicAgentSpec already apply to their own numbers.
struct CoilSpec {
    const char *name = "";
    const char *note = "";

    /// Helix (primary) diameter, mm — what the coil is sized against the
    /// target vessel's own diameter for. Genuinely consequential: see
    /// CoilSizeFit and classifyCoilFit() below - an undersized or oversized
    /// coil for the vessel it is deployed into packs measurably less
    /// effectively than a well-matched one, the same "reports rather than
    /// silently resizes" spirit SheathManager's own oversized flag uses,
    /// just with a real mechanical consequence instead of only a warning.
    float primaryDiameterMm = 0.f;

    /// Total stretched length of wire available to deploy, mm. A coil is a
    /// fixed length of platinum — advanceCoil() cannot push out more than
    /// this regardless of how much is asked for.
    float lengthMm = 0.f;

    float colour[3] = {0.85f, 0.75f, 0.15f};  ///< platinum-gold, typical fluoro tint
    float radiopacity = 0.95f;                ///< platinum is highly radiopaque

    /// How much of a link's own physical length one millimetre of THIS
    /// coil, packed there, counts toward occluding — i.e. the inverse of
    /// how much wire it takes to fill a given length of vessel. Softer/
    /// smaller coils (finer nano platforms) pack denser per mm than a
    /// stiffer, larger-helix coil, which is why this is a per-spec
    /// property rather than one constant. Calibrated so a normal-calibre
    /// MMA-territory segment needs a small nest (two to four coils), not
    /// one coil alone, to cross fillFraction 1.0 — matching real MMA
    /// coil-embolization technique, where a single coil is essentially
    /// never sufficient on its own. This is the WELL-MATCHED-SIZE number;
    /// classifyCoilFit() scales it down further for an undersized or
    /// oversized coil - see that function's own comment.
    float packingYieldPerMm = 0.02f;
};

/// How a coil's own primaryDiameterMm compares to the vessel it is
/// actually being deployed into.
enum class CoilSizeFit { Undersized, Good, Oversized };

/// Classifies fit from the coil's helix diameter and the local vessel
/// diameter, both in mm. A real undersized coil does not anchor against
/// the vessel wall and packs loosely rather than building an effective
/// nest (it can even migrate or herniate back toward the parent vessel);
/// a real oversized coil cannot conform to a narrower vessel and kinks/
/// buckles rather than packing in a controlled way. Both are modelled here
/// as the SAME kind of consequence — reduced packing efficiency — rather
/// than two different mechanisms, which keeps the effect legible: a
/// trainee sees "this coil is packing far less effectively than it
/// should," not two unrelated failure modes to learn separately.
CoilSizeFit classifyCoilFit(float coilDiameterMm, float vesselDiameterMm);

/// Short display string for `fit` - "well sized" / "undersized for this
/// vessel" / "oversized for this vessel" - for the status readout and
/// case-review report.
const char *coilSizeFitName(CoilSizeFit fit);

/// Full coil catalogue. `count` receives the number of entries.
const CoilSpec *coilCatalog(size_t &count);

/// Look up by the exact display name used in the selection dialogs, e.g.
/// "Target 360 Nano 2mm x 4cm". Returns nullptr when there is no match.
const CoilSpec *findCoil(const char *displayName);

/// Discrete coil embolization.
///
/// Deliberately a SEPARATE mechanism from EmbolizationModel's liquid-agent
/// flood fill, not a variant of it — the two devices behave nothing alike
/// physically:
///
///   A liquid embolic advances as a continuous front that can reach any
///   reachable branch by the path of least resistance, biased toward wide
///   channels for a viscous agent (see EmbolizationModel's own class
///   comment). It "chooses" where it goes.
///
///   A coil is a physical wire pushed out of a microcatheter tip. It does
///   not choose a channel — it occupies whatever single vessel it was
///   pushed into, and it takes several overlapping coils stacked at the
///   same site (a "nest") to build up to a full occlusion. One 2 mm coil
///   does not occlude a 2 mm vessel by itself the way one unit of injected
///   Onyx effectively can once the flood-fill front reaches it.
///
/// Deployment therefore walks a single non-branching path outward from the
/// release node — the same simple-chain walk NavigationController::
/// extendRoute() already uses elsewhere in this codebase — and STOPS at the
/// first bifurcation, rather than a graph-distance flood fill that can
/// spread through every reachable branch. Packing is cumulative and
/// additive per link: deploying a second or third coil at the same site
/// keeps raising that link's packingLength, which is what lets a trainee
/// genuinely see "one coil was not enough" and add another — the actual
/// clinical judgement call coil-embolization technique is built around,
/// and something a single click-to-place gesture cannot teach on its own.
///
/// STAGED vs COMMITTED. A real coil is not permanent the instant it is
/// pushed out — it stays attached to the pusher wire, fully visible and
/// fully retrievable, until the operator deliberately detaches it, and a
/// real operator routinely pushes a coil, checks its shape and position on
/// fluoro, and pulls it back to try again if it looks wrong before ever
/// committing to detachment. So advanceCoil() only ever touches a SEPARATE
/// staging area (stagingLinks()) — never state().links directly. Exactly
/// one of two things can happen to that staging area next:
///
///   finishCoil()   merges it into state().links permanently (the "Detach"
///                  action) and records a DeployedCoil in the history.
///
///   retractCoil()  discards it entirely (the "Retract" action) as if this
///                  coil had never been pushed — state().links and
///                  state().coils, i.e. every PREVIOUSLY detached coil at
///                  this or any other site, are completely unaffected.
///
/// linkOccluded() and every occlusion-relevant read only ever consult
/// state().links (committed material) — a staged, undetached coil cannot
/// block contrast flow, because physically it has not actually stayed in
/// the vessel yet. A renderer that wants to show the staged coil too
/// (so the operator has something to actually look at before deciding)
/// reads stagingLinks() separately and combines it with state().links
/// purely for display.
///
/// undoLastCoil() is a DIFFERENT kind of reversal from retractCoil() above
/// — it removes the most recently COMMITTED coil (already merged into
/// state().links, already in state().coils) rather than a still-staged
/// one. This is what backs the simplified two-action UI (Advance, which
/// pushes AND commits a coil in one call via deployWholeCoil(), and
/// Retract, which now means "undo the last one I just placed" rather than
/// "take back what I haven't committed yet"). Both reversal mechanisms
/// exist because they solve different problems: retractCoil() is cheap
/// (nothing to unwind, the staging area is simply discarded) and exists
/// for a review-before-commit workflow; undoLastCoil() has to unwind an
/// actual merge (see DeployedCoil::contributionLinks) and exists for a
/// simpler workflow where every Advance is already permanent unless
/// immediately undone.
///
/// MULTI-TARGET. A real MMA case sometimes needs two independent coiling
/// sites (e.g. anterior + posterior division) tracked separately in the
/// same procedure - previously impossible without reset() destroying the
/// first site's own nest. This is now a small VECTOR of independent
/// targets (m_targets), each with its OWN release site, own committed
/// nest, own staging area, own deployment history - completely isolated
/// from every other target. Exactly ONE target is "active" at a time
/// (m_activeIdx); beginAt()/advanceCoil()/finishCoil()/retractCoil()/
/// undoLastCoil()/state()/hasPendingCoil()/etc. all operate on THAT one,
/// completely unchanged in behaviour from before this feature existed - a
/// caller that never calls addTarget()/setActiveTarget() sees no
/// difference at all, since a fresh model starts with exactly one target
/// already active (index 0). Multi-target access (stateAt(), targetName(),
/// targetCount()) is purely ADDITIVE, needed so a renderer can draw every
/// target's own coil nest simultaneously rather than only the active one.
/// linkOccluded() is the one exception to "operates on the active target
/// only" - occlusion is a physical fact about the vessel, so it checks
/// every target's own committed links, not just the active one's - see
/// that method's own comment.
///
/// Multiple separate coils can be in progress AT ONE TARGET in the sense
/// that beginAt()/advanceCoil()/finishCoil() can be called again at the
/// same or a different node without clearing earlier coils — only
/// reset() clears that target's own whole cast, the same "begin doesn't
/// wipe history, reset does" distinction EmbolizationModel does NOT need
/// (a liquid cast is one continuous growth, a coil cast is inherently a
/// sequence of discrete objects). beginAt() also clears any not-yet-
/// committed staging left over from a previous coil that was neither
/// detached nor explicitly retracted — starting a new coil implicitly
/// abandons an old, forgotten one exactly the way retractCoil() would.
class CoilEmbolizationModel {
public:
    /// One graph link that any deployed coil has reached at least partially.
    struct CoiledLink {
        int32_t from = -1, to = -1;
        float packingLength = 0.f;   ///< cumulative effective packing, mm
        float fillFraction  = 0.f;   ///< clamped 0..1, packingLength vs. this link's own length
    };

    /// One coil actually cut and committed — the deployment history, most
    /// recent last, so a trainee (or the case-review report) can see how
    /// many coils it took to occlude the target.
    struct DeployedCoil {
        const CoilSpec *spec = nullptr;
        float deployedLengthMm = 0.f;  ///< how much of this coil's own length was actually paid out
        int32_t releaseNode = -1;
        /// Exact continuous world position of the microcatheter tip AT THE
        /// MOMENT this coil was deployed — not derived from releaseNode's
        /// own graph-node position. A graph node is a discrete sample point
        /// on the centerline, potentially some real distance from wherever
        /// the tip's own continuous position (CatheterModel::State::
        /// tipPosition) actually was between two nodes; snapping to the
        /// nearest node could leave the rendered coil visibly detached from
        /// the catheter it supposedly came out of. This is a PERMANENT
        /// snapshot, taken once at deployment - if the catheter is advanced
        /// further afterward, this coil correctly stays exactly where it
        /// was released rather than following the tip around.
        Vec3 releasePosition;
        /// How well this coil's own primaryDiameterMm matched the vessel
        /// it was actually deployed into - see classifyCoilFit()'s own
        /// comment. Recorded at commit time so a trainee (or the case
        /// review) can see, after the fact, "that coil was undersized for
        /// where it went."
        CoilSizeFit fit = CoilSizeFit::Good;
        /// Local vessel diameter, mm, that `fit` was assessed against.
        float vesselDiameterMm = 0.f;
        /// Exactly what THIS coil itself added to state().links at commit
        /// time — a snapshot of the staging area right before it was
        /// merged, i.e. before it was combined with whatever previously-
        /// committed coils had already contributed to the same links. This
        /// is what undoLastCoil() subtracts back out, which is the only
        /// way to unwind one specific coil's contribution from a nest where
        /// several coils have already been additively merged together onto
        /// the same links.
        std::vector<CoiledLink> contributionLinks;
    };

    struct State {
        bool active = false;
        int32_t releaseNode = -1;
        std::vector<CoiledLink>   links;   ///< every link touched by any COMMITTED (detached) coil
        std::vector<DeployedCoil> coils;   ///< deployment history - committed coils only
    };

    void setGraph(const CenterlineGraph *g) { m_graph = g; }
    const CenterlineGraph *graph() const { return m_graph; }

    /// Parks the delivery site — normally the microcatheter tip's current
    /// graph node (CatheterModel::State::currentNodeId), exactly the value
    /// MainWindow::onBeginEmbolization() already reads for the liquid-agent
    /// path. `excludeNeighbor`, if >= 0, is the segment the microcatheter's
    /// own shaft occupies (same convention and same caller-side helper,
    /// MainWindow::findProximalNeighbor(), as EmbolizationModel::begin())
    /// so a coil is never credited with occupying the vessel the delivery
    /// device itself is sitting in. `tipWorldPosition` is the microcatheter
    /// tip's own continuous position (CatheterModel::State::tipPosition) at
    /// this exact moment - carried through to whichever DeployedCoil
    /// finishCoil() eventually commits, so rendering can anchor to the
    /// real tip position rather than the nearest graph node (see
    /// DeployedCoil::releasePosition's own comment).
    ///
    /// Does NOT clear previously deployed (committed) coils — see the class
    /// comment for why coiling a site is normally several begin/advance/
    /// finish cycles at the same node, not a fresh start each time. Call
    /// reset() to clear the whole cast. DOES discard any not-yet-committed
    /// staging left over from a previous advanceCoil() that was never
    /// finished or retracted - the same abandonment retractCoil() would do,
    /// applied implicitly so a forgotten pending coil cannot silently leak
    /// into the next one.
    void beginAt(int32_t node, int32_t excludeNeighbor = -1,
                 const Vec3 &tipWorldPosition = Vec3{});

    /// Pushes `dLengthMm` more of the CURRENT coil out of the microcatheter,
    /// walking the single non-branching path from the release node and
    /// accumulating packing onto each link it crosses, nearest first. This
    /// only ever touches the STAGING area (stagingLinks()), never
    /// state().links directly — see the class comment's "STAGED vs
    /// COMMITTED" section. `mmPerUnit` converts the graph's own model-unit
    /// link lengths into mm so `dLengthMm`/`spec.lengthMm`/
    /// `packingYieldPerMm` can all be expressed in real millimetres rather
    /// than model units, the same convention CatheterCatalog::
    /// catheterParams() already uses for its own mm-denominated device
    /// dimensions.
    ///
    /// No-op before beginAt(). No-op past the coil's own `spec.lengthMm` —
    /// a coil is a fixed length of wire, you cannot push out more than the
    /// device actually has. Stops at the first bifurcation reached: a coil
    /// does not decide which branch to take the way a liquid agent's
    /// weighted flood fill does.
    void advanceCoil(const CoilSpec &spec, float dLengthMm, float mmPerUnit = 1000.f);

    /// "Cuts" the coil at its current staged length, merging the staging
    /// area permanently into state().links, committing a DeployedCoil to
    /// history, and clearing the staging area. Mirrors the manual's
    /// click-to-cut gesture: deployment is continuous (advanceCoil), commit
    /// is discrete (finishCoil). No-op if no coil is currently staged.
    void finishCoil();

    /// Discards the currently staged coil entirely, as if it had never
    /// been pushed — the "Retract" action for a review-before-commit
    /// workflow. Clears stagingLinks() and the in-progress spec/length;
    /// state().links and state().coils (every PREVIOUSLY committed coil)
    /// are completely untouched. No-op if no coil is currently staged. See
    /// undoLastCoil() below for the different "undo something already
    /// committed" reversal a simpler one-click-deploy workflow needs
    /// instead.
    void retractCoil();

    /// Removes the MOST RECENTLY COMMITTED coil entirely, as if it had
    /// never been detached: subtracts exactly what that coil contributed
    /// (DeployedCoil::contributionLinks) back out of state().links,
    /// dropping any link that returns to zero packing, and pops it off
    /// state().coils. Every OTHER previously committed coil at this or any
    /// other site is unaffected, even ones that packed onto the very same
    /// links — only this specific coil's own contribution is reversed.
    /// Returns false (no-op) if there is no committed coil to undo.
    ///
    /// This is a genuinely different reversal from retractCoil() above -
    /// see the class comment's own note on why both exist. It is what
    /// backs a simple "Advance commits immediately, Retract undoes
    /// whatever I just placed" workflow with no separate review/commit
    /// step in between.
    bool undoLastCoil();

    /// True while a coil has been pushed (advanceCoil() called at least
    /// once since the last beginAt()) but not yet committed (finishCoil())
    /// or discarded (retractCoil()) — the "review before you decide" state
    /// a real coil sits in between being pushed out and being detached.
    /// A UI uses this to enable/disable Detach and Retract, and to disable
    /// starting a new coil until the current one is resolved one way or
    /// the other.
    bool hasPendingCoil() const { return m_targets[m_activeIdx].currentCoilLengthMm > 0.f; }

    /// Links touched by the CURRENTLY STAGED coil only - separate from
    /// state().links (committed coils). A renderer combines the two for
    /// display (so there is something to actually look at before deciding
    /// whether to detach or retract), but nothing occlusion-relevant
    /// (linkOccluded(), contrast blocking) ever reads this - see the class
    /// comment's "STAGED vs COMMITTED" section for why.
    const std::vector<CoiledLink> &stagingLinks() const { return m_targets[m_activeIdx].stagingLinks; }

    /// The release position the CURRENTLY STAGED coil is using - only
    /// meaningful while hasPendingCoil() is true. Lets a renderer anchor a
    /// staged-but-not-yet-committed coil correctly even before any coil at
    /// this site has ever been detached (state().coils could still be
    /// empty at that point).
    Vec3 pendingReleasePosition() const { return m_targets[m_activeIdx].currentReleasePosition; }

    /// How well the CURRENTLY STAGED coil's size matches the vessel it was
    /// pushed into - only meaningful while hasPendingCoil() is true. This
    /// is deliberately available BEFORE commit, not just after: the whole
    /// point of a review checkpoint (see the class's own "STAGED vs
    /// COMMITTED" section) is to let a trainee catch a bad choice — here,
    /// the wrong coil SIZE for this vessel — and Retract rather than
    /// Detach, exactly the same way reviewing its position/shape already
    /// lets them catch a bad LOCATION.
    CoilSizeFit pendingFit() const { return m_targets[m_activeIdx].currentFit; }
    /// Local vessel diameter, mm, that pendingFit() was assessed against.
    float pendingVesselDiameterMm() const { return m_targets[m_activeIdx].currentVesselDiameterMm; }

    /// Convenience for tests and any workflow that genuinely wants the old
    /// one-shot behaviour: pays out the coil's full length (advanceCoil,
    /// staged) and immediately commits it (finishCoil) in one call, with no
    /// review step in between. MainWindow's real coil UI no longer calls
    /// this directly for the main workflow — see onAdvanceCoil()/
    /// onDetachCoil()/onRetractCoil() — but it remains the simplest way to
    /// deploy a coil programmatically when a review checkpoint is not
    /// wanted. `tipWorldPosition` - see beginAt()'s own comment. Operates
    /// on the CURRENTLY ACTIVE target, same as every other method here.
    void deployWholeCoil(const CoilSpec &spec, int32_t node, int32_t excludeNeighbor = -1,
                         float mmPerUnit = 1000.f, const Vec3 &tipWorldPosition = Vec3{});

    /// True once a link's cumulative packing has crossed its own physical
    /// length — the same fillFraction >= 1.0 "impassable" threshold
    /// EmbolizationModel/ContrastInjectionModel already use, so a coil nest
    /// blocks contrast flow through it exactly the same way a liquid cast
    /// does. Checks EVERY target this model holds, not only the active one
    /// - occlusion is a physical fact about the vessel, and a link packed
    /// full while working on one target must still read as occluded once a
    /// different target is selected in the UI. Reads state().links
    /// (COMMITTED coils) only - a staged, undetached coil never occludes
    /// anything, see the class comment. See ContrastInjectionModel::
    /// linkOccluded(), which checks both this and the liquid-agent model.
    bool linkOccluded(int32_t a, int32_t b) const;

    /// Clears the CURRENTLY ACTIVE target's deployed (committed) coils AND
    /// any pending staged one. Every OTHER target this model may hold is
    /// completely untouched. Unlike beginAt(), this genuinely starts over
    /// on this one target — the "reset" button, not a new coil in the
    /// same nest. See resetAll() for "start completely over with a single
    /// fresh target".
    void reset();

    /// Discards every target and starts over with exactly one fresh,
    /// inactive target at index 0 - the genuine "clear everything" a New
    /// Patient/new-case path wants, as opposed to reset()'s "clear just
    /// the one target I'm currently working on".
    void resetAll();

    bool active() const { return m_targets[m_activeIdx].state.active; }
    const State &state() const { return m_targets[m_activeIdx].state; }

    // ── Multi-target access ──────────────────────────────────────────────
    /// Creates a new, independent coiling target and makes it active - e.g.
    /// a second MMA division treated separately from the first. Returns the
    /// new target's index. `name` is a caller-supplied label (e.g.
    /// "Anterior division") purely for display; empty auto-generates
    /// "Target N". The PREVIOUSLY active target's own nest/history/staging
    /// is completely untouched - switching which target is active never
    /// resets or discards anything.
    int addTarget(const std::string &name = std::string());

    /// Switches which target subsequent beginAt()/advanceCoil()/
    /// finishCoil()/retractCoil()/undoLastCoil()/reset()/state()/etc. calls
    /// affect. Every other target's own state is completely unaffected by
    /// this - it only changes which ONE the next call reaches. No-op if
    /// idx is out of range.
    void setActiveTarget(int idx);
    int activeTarget() const { return m_activeIdx; }
    int targetCount() const { return static_cast<int>(m_targets.size()); }
    const std::string &targetName(int idx) const;

    /// Read-only access to a SPECIFIC target's own state, regardless of
    /// which one is currently active - what a renderer needs to draw every
    /// target's own coil nest simultaneously rather than only the active
    /// one. Returns a static empty State if idx is out of range.
    const State &stateAt(int idx) const;
    bool hasPendingCoilAt(int idx) const;

private:
    static CoiledLink *findOrAddLink(std::vector<CoiledLink> &links, int32_t a, int32_t b);

    /// One independent coiling target - see the class comment's
    /// "MULTI-TARGET" section. Everything that used to be a bare member
    /// (m_state, m_excludedNeighbor, m_currentSpec, ...) now lives in one
    /// of these, one per target, so two targets can never see or affect
    /// each other's nest.
    struct Target {
        std::string name;
        State state;

        /// See beginAt()'s own comment - the proximal neighbor node the
        /// microcatheter's own body blocks, or -1 if none was given.
        int32_t excludedNeighbor = -1;

        /// The coil currently being staged, if any - cleared by
        /// finishCoil() (committed) or retractCoil() (discarded). Not yet
        /// reflected in state().links/state().coils either way until
        /// finishCoil() runs.
        const CoilSpec *currentSpec = nullptr;
        float currentCoilLengthMm = 0.f;
        /// Set by beginAt(), copied into DeployedCoil::releasePosition by
        /// finishCoil() when the coil is actually committed.
        Vec3 currentReleasePosition;
        /// Fit/vessel-diameter for the coil CURRENTLY being staged.
        CoilSizeFit currentFit = CoilSizeFit::Good;
        float currentVesselDiameterMm = 0.f;
        /// mmPerUnit passed to the most recent advanceCoil() call, cached
        /// so finishCoil() can recompute each merged link's fillFraction
        /// using the same conversion factor.
        float lastMmPerUnit = 1000.f;
        /// Links touched by the coil currently being staged.
        std::vector<CoiledLink> stagingLinks;
    };

    const CenterlineGraph *m_graph = nullptr;
    /// Always at least one entry - a fresh model starts with exactly one
    /// target already active at index 0, so single-target use (the whole
    /// of this class's behaviour before multi-target existed) needs no
    /// special-casing anywhere.
    std::vector<Target> m_targets{Target{}};
    int m_activeIdx = 0;
};

} // namespace cn