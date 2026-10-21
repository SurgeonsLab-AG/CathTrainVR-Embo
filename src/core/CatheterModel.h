#pragma once
#include "CenterlineGraph.h"
#include "Vec3.h"
#include <string>
#include <vector>

namespace cn {

/// Kinematic model of a catheter / guidewire riding a planned centerline path.
///
/// This is a *reduced* model, not an FEM: the device is a chain of
/// inextensible segments whose lead node is driven along the path by
/// insertion depth ("follow-the-leader", the standard reduced model for
/// endovascular navigation). It is cheap enough to run at display rate and
/// gives the quantities an operator actually watches: insertion depth, lumen
/// calibre at the tip, wall clearance, and which branch the tip is in.
///
/// The chain is NOT snapped onto the centerline, and it is NOT re-solved from
/// scratch every call either. Each node has a target sample on the path
/// (from its insertion-depth-derived arc length), but update() carries the
/// chain's actual rendered shape forward from the previous call and only
/// relaxes it (see relaxChain()) a fixed number of steps toward the new
/// target - the same "distance constraint + bending resistance + centerline
/// pull" solve as before, just warm-started from where the shaft already
/// was instead of reset to the centerline every frame. That is what turns
/// advancing, steering, and crossing a bifurcation into continuous motion:
/// the shaft eases toward its new equilibrium rather than popping to a fresh
/// one each call. Bending resistance is set by Params::shaftStiffness, and
/// the result is always re-clamped inside the lumen (constrainToLumen() runs
/// last). The tip's own resting/steered shape (applyTipArticulation()) feeds
/// into the SAME relaxation as the rest of the chain, which is what keeps the
/// shaft smoothly, tangent-continuously joined to the bent tip rather than
/// kinking where the flexible zone ends.
///
/// What it deliberately does not model: friction, true torsional dynamics
/// (rollResponsiveness below is a first-order approximation of wind-up, not
/// a torque simulation), vessel compliance, or buckling. Swap in a Cosserat-
/// rod solver behind the same interface if you need those.
///
/// Exactly one CatheterModel exists per device in the running console
/// (MainWindow owns m_catheter and each parked host device); every VesselView
/// only ever reads that same instance's state() to build its own render
/// batches. So the position, orientation, bend and roll computed here are, by
/// construction, identical in every view - there is nothing here that could
/// diverge per view, because no view ever computes any of it itself.
class CatheterModel {
public:
    struct Params {
        float deviceRadius = 0.0012f;   ///< outer radius, model units
        float segmentLength = 0.010f;   ///< distance between chain nodes
        int   segmentCount = 40;        ///< nodes in the trailing chain
        int   tipFlexSegments = 6;      ///< how many leading nodes articulate
        float maxTipDeflection = 1.2f;  ///< radians
        /// Pre-shaped primary curve, applied even at zero operator input. This
        /// is what makes a JR4 behave differently from a straight-tip wire.
        ///
        /// This is the SINGLE-ARC case only: one constant-curvature bend from
        /// the pivot to the tip. Kept rather than folded away because it is
        /// still the cheapest way to describe a device whose shape really is
        /// one arc (a plain angled or curved-tip wire). A shape with more than
        /// one curve — a Simmons loop-back, a Newton double-kink — cannot be
        /// expressed by a single angle; see `tipShape` below, which subsumes
        /// this field whenever it is non-empty.
        float restingTipCurve = 0.f;    ///< radians; 0 keeps the tip on the axis

        /// One arc of a compound resting shape: turns through `curveRadians`
        /// (signed — opposite sign reverses the direction of the bend) over
        /// `lengthFraction` of the tip flex zone. Segments are listed PROXIMAL
        /// TO DISTAL (pivot end first, tip end last); their fractions should
        /// sum to 1, but this is not enforced so a shape can be authored to
        /// leave the very tip straight - an over- or under-filled list simply
        /// stops short of, or overshoots past, the tip.
        struct TipShapeSegment {
            float lengthFraction = 1.f;
            float curveRadians   = 0.f;
        };

        /// Compound resting shape, pivot to tip. Empty means "use
        /// restingTipCurve as one segment" - the plain single-arc case above.
        /// A non-empty list takes over entirely (restingTipCurve is then
        /// ignored): a device's shape is one thing or the other, never both.
        ///
        /// This is what lets the tip trace a real diagnostic-catheter curve -
        /// a Simmons' primary loop followed by its own reverse secondary bend,
        /// a Newton's tight double-kink right at the tip - rather than only
        /// ever the single hooks a plain angle can draw. See
        /// CatheterCatalog::TipShapeDef for the named presets (Simmons 1/2/3,
        /// Newton, Cobra, Headhunter, vertebral/angled-taper).
        std::vector<TipShapeSegment> tipShape;
        float wallSafety = 0.90f;       ///< fraction of lumen radius usable
        bool  clampToLumen = true;      ///< project nodes back inside the wall

        /// The chain grows to reach back to the access point, so the shaft
        /// spans the whole inserted length instead of stopping short of it.
        bool  autoLength = true;
        int   maxSegments = 1400;

        /// Clamp the *drawn* radius to the local lumen so the device can never
        /// be rendered outside the vessel. Physics and the clearance read-out
        /// still use the true outer diameter, so an oversized device is
        /// reported as wall contact rather than being silently made to fit.
        bool  fitToLumen = true;
        float lumenFitFraction = 0.82f;

        /// How strongly the shaft resists bending, 0..1. At 0 the chain
        /// traces the vessel centerline exactly - the old, purely-kinematic
        /// behaviour, useful for a very soft microcatheter. At 1 it behaves
        /// like a stiff rod: it straightens through a bend and leans on the
        /// OUTER wall of a curve rather than hugging the medial axis, which
        /// is what a real braided guide catheter actually does - devices
        /// already differ enough in stiffness (see CatheterCatalog) that
        /// this is exposed per device rather than fixed for every one.
        float shaftStiffness = 0.5f;

        /// Position-based relaxation passes per update(). A short, FIXED
        /// count rather than an error-tolerance loop, so update() costs the
        /// same every call and - critically - is exactly reproducible for
        /// the same inputs: every view renders the one shared CatheterModel
        /// instance and must never show a different shape from it. Raised
        /// from 8: with the chain now warm-started across frames rather than
        /// fully re-solved each call (see update()), 8 passes left a visible
        /// multi-frame settling wobble whenever the target moved by more
        /// than a small step; 16 converges close enough to equilibrium in a
        /// single frame that the lag is no longer visible at any normal
        /// advance/steer speed, while still costing nothing extra a real
        /// frame budget would notice for a chain this short.
        int relaxIterations = 16;

        /// How quickly the tip's realised roll catches up to the operator's
        /// commanded roll (State::rollAngle), per update() call. 1 =
        /// instantaneous (a short/stiff device); lower values model the
        /// wind-up of a long compliant shaft, which does not transmit torque
        /// to the tip the instant the operator turns it.
        float rollResponsiveness = 0.35f;

        /// Fogarty-style embolectomy balloon: a compliant balloon set back a
        /// short distance from a small rounded atraumatic tip. Advanced
        /// deflated past the clot, inflated, then withdrawn to drag it out -
        /// so inflation is an operator control, not a fixed property.
        bool  hasBalloon = true;
        float balloonOffset = 0.006f;      ///< tip to balloon centre, arc length
        float balloonMaxRadius = 0.0045f;  ///< fully inflated radius
        float balloonLength = 0.009f;      ///< full length along the axis
        float balloonInflation = 0.f;      ///< 0 = deflated, 1 = rated volume
    };

    struct State {
        std::vector<Vec3> nodes;     ///< nodes[0] is the tip
        std::vector<float> nodeRadius; ///< drawn radius per node, lumen-fitted
        Vec3 tipPosition;
        Vec3 tipDirection{0.f, 0.f, 1.f};

        float insertionDepth = 0.f;      ///< arc length of the tip along the path
        float remainingToTarget = 0.f;
        float lumenRadiusAtTip = 0.f;
        float minWallClearance = 0.f;    ///< negative means the wall is engaged
        float rollAngle = 0.f;           ///< operator-commanded roll, radians
        /// Roll the tip has actually reached, easing toward rollAngle each
        /// update() at Params::rollResponsiveness - what applyTipArticulation
        /// actually bends the tip about, not the raw commanded value. Models
        /// the wind-up of a long compliant shaft without a full torsional
        /// simulation.
        float rollAngleRealized = 0.f;
        float tipDeflection = 0.f;

        int32_t currentNodeId = -1;
        int32_t currentBranchId = -1;
        bool wallContact = false;
        bool oversized = false;          ///< device will not fit the lumen ahead
        /// 0 = clear, 1 = fully engaged. Drives the operator feedback.
        float contactSeverity = 0.f;
        Vec3 contactPoint;               ///< where the wall is being pressed
        int  contactNode = -1;

        // --- balloon ---------------------------------------------------------
        Vec3  balloonCentre;
        Vec3  balloonAxis{0.f, 0.f, 1.f};
        float balloonRadius = 0.f;       ///< actual, limited by the lumen
        float balloonLumen = 0.f;        ///< lumen radius at the balloon
        float balloonApposition = 0.f;   ///< 0..1 fraction of the lumen filled
        bool  balloonInflated = false;
    };

    /// A coaxial neighbour that mechanically reinforces this device over the
    /// portion of its shaft it shares with it - a stiff guide catheter
    /// straightening whatever tracks inside it, or a guidewire threaded
    /// through a microcatheter's own lumen stiffening that length of the
    /// microcatheter. `depthLimit` is the neighbour's own tip insertion
    /// depth (arc length): the shared/reinforced span runs from the access
    /// site out to there, since that is as far as the neighbour physically
    /// reaches. A softer neighbour (lower `stiffness`) never loosens this
    /// device - see effectiveStiffnessAt() - it just does not add anything.
    struct Support {
        float depthLimit = 0.f;
        float stiffness  = 0.f;
    };

    /// Derived, read-only mechanical picture of the device against the
    /// vessel - the stable core-level feedback API a training/visualization
    /// layer reads (regime, contact, force, the three resistances, support
    /// and exposure). Nothing here is a measured or invented force
    /// constant: every field is a normalised (0..1, or a unit direction)
    /// combination of values update() already computes for its own
    /// containment/response purposes (lumen clearance, per-node effective
    /// stiffness, local centerline curvature, roll wind-up) - re-exposed as
    /// its own read-out rather than left folded silently into rendering or
    /// motion. Force/torque are dimensionless model units, not physical
    /// Newtons/N*m: this is a kinematic reduced model with no mass or
    /// friction coefficients to derive real units from (see the class
    /// comment above). Field NAMES and MEANINGS are meant to stay stable
    /// across implementation changes underneath - a future training UI, or
    /// MechanicalEventDetector (MechanicalEvents.h), reads this struct and
    /// nothing else.
    struct MechanicalState {
        enum class Regime {
            Free,                 ///< no meaningful contact or curvature engagement
            MildContact,          ///< touching the wall, well inside tolerance
            SignificantContact,   ///< heavy wall engagement
            TightCurvature,       ///< resisted by bending stiffness through a bend, not necessarily at the wall
            ExcessiveAdvancement, ///< contact severity saturated - further push only grinds against the wall
            Unsupported,          ///< long exposed run past any coaxial Support, riding on bare stiffness alone
        };

        // --- contact -------------------------------------------------------
        float wallContactAmount = 0.f;   ///< == State::contactSeverity
        Vec3  contactLocation;           ///< == State::contactPoint
        int   contactNode = -1;          ///< == State::contactNode

        // --- resistances (0 = free, 1 = maximally resisted) ----------------
        float advancementResistance = 0.f;
        float bendingResistance     = 0.f;
        float torsionalResistance   = 0.f;

        // --- support ---------------------------------------------------------
        /// 0..1: how much of shaftStiffness's own headroom up to 1.0 is
        /// currently filled by a coaxial Support at the tip's own depth.
        float effectiveSupport = 0.f;
        /// 0..1: the device's exposure/support relationship - how much of
        /// its OWN insertion depth lies beyond the furthest coaxial
        /// Support's reach (0 = fully sleeved its whole length, 1 = no
        /// Support at all reaches any of it). Distinct from
        /// effectiveSupport, which is a stiffness reading at the tip alone:
        /// this is the proximal-to-distal picture a training overlay needs
        /// to explain WHY a device is behaving unsupported (a long exposed
        /// run) rather than just THAT it currently is (low tip stiffness).
        float unsupportedExposureFraction = 0.f;

        // --- force / torque --------------------------------------------------
        float forceMagnitude    = 0.f;
        Vec3  forceDirection;             ///< unit vector, centerline axis -> wall at the contact point
        float torsionMagnitude  = 0.f;    ///< radians of commanded roll not yet realised at the tip

        Regime regime = Regime::Free;
    };

    CatheterModel() = default;
    explicit CatheterModel(const Params &p) : m_params(p) {}

    void setParams(const Params &p) { m_params = p; }
    const Params &params() const { return m_params; }

    /// Coaxial neighbours currently reinforcing this device - see Support.
    /// Recomputed by the caller (MainWindow, from the parked host stack)
    /// whenever it changes; empty means "riding unsupported on its own
    /// stiffness alone", the previous, only behaviour.
    void setSupports(std::vector<Support> supports) { m_supports = std::move(supports); }
    const std::vector<Support> &supports() const { return m_supports; }

    /// Install the route. `keepDepth` preserves insertion depth, which is what
    /// you want when the route is *extended* mid-procedure after a branch
    /// choice - resetting to zero there would yank the device back to the
    /// access sheath.
    void setPath(const CenterlinePath &path, bool keepDepth = false);
    const CenterlinePath &path() const { return m_path; }

    /// Optional: lets the model resolve the tip's branch id for read-out.
    /// The graph must outlive the model.
    void setGraph(const CenterlineGraph *g) { m_graph = g; }

    void setInsertionDepth(float s);
    void advance(float ds) { setInsertionDepth(m_state.insertionDepth + ds); }
    void setRollAngle(float radians) { m_state.rollAngle = radians; }
    void setTipDeflection(float radians);

    /// Recompute node positions, constraints and telemetry.
    void update();

    const State &state() const { return m_state; }

    /// Force/contact/torsion read-out, recomputed at the end of every
    /// update() - see MechanicalState. Always current with state(); there
    /// is no separate call needed to refresh it.
    const MechanicalState &mechanics() const { return m_mech; }

    /// One-line diagnostic dump of mechanics() plus enough of state() to
    /// locate it (depth, contact node) - for logging at a call site that
    /// wants to validate contact/resistance/force/torsion/support numbers
    /// without hand-formatting every field itself.
    std::string debugSummary() const;

    /// The narrowest lumen between the tip and the target - what tells you a
    /// device will not pass before you push it there.
    float narrowestAhead() const;

    /// Triangulated tube around the device chain, for rendering.
    /// Writes interleaved (pos, normal) floats and indices.
    ///
    /// `firstNode`/`lastNode` select a span of the chain so the renderer can
    /// draw a real catheter rather than a uniform pipe: a tapered soft tip, a
    /// radiopaque marker band behind it, then the shaft - each in its own
    /// material. `lastNode < 0` means "to the end of the chain".
    ///
    /// The tip span is tapered and capped; interior spans are left open because
    /// the neighbouring span covers the seam.
    void buildTubeMesh(int sides,
                       std::vector<float> &interleavedOut,
                       std::vector<uint32_t> &indicesOut,
                       int firstNode = 0,
                       int lastNode = -1) const;

    /// Radius multiplier applied at chain index `i` to shape the soft tip.
    float tipTaper(int i) const;

    /// Triangulated balloon (a spheroid on the shaft axis). Empty when the
    /// device has no balloon.
    void buildBalloonMesh(int rings, int sides,
                          std::vector<float> &interleavedOut,
                          std::vector<uint32_t> &indicesOut) const;

    void setBalloonInflation(float f);
    float balloonInflation() const { return m_params.balloonInflation; }

private:
    /// Bends the leading tip-flex-zone entries of `nodes` about a pivot into
    /// the tip's resting/steered shape - the same math as before, just
    /// writing into the caller's target array instead of m_state.nodes
    /// directly, so relaxChain() can fold the tip into the same solve as the
    /// rest of the shaft rather than treating it as a separate, later step.
    void applyTipArticulation(std::vector<Vec3> &nodes);

    /// Position-based relaxation: pulls m_state.nodes (seeded at `target`)
    /// toward mutual segmentLength spacing and toward straightness, blended
    /// by Params::shaftStiffness, in Params::relaxIterations fixed passes.
    /// This is what replaces "snap onto the centerline" - see the class
    /// comment above for the reasoning.
    void relaxChain(const std::vector<Vec3> &target);

    /// Blends Params::shaftStiffness with whichever active Support reaches
    /// this far out (see Support), tapered smoothly across the support's own
    /// tip rather than cut off there, so the shaft does not kink at the exact
    /// point a reinforcing neighbour ends.
    float effectiveStiffnessAt(float arc) const;

    void constrainToLumen();

    /// Fills m_mech from state()/params()/m_supports as they stand at the
    /// end of update() - see MechanicalState's own comment for exactly how
    /// each field is derived. Called last, once the lumen constraint and
    /// all telemetry above it are final for this call.
    void updateMechanicalState();

    /// Arc length of the centerline point closest to `p`, searched in a window
    /// around `guess`. Nodes move away from the sample they were assigned -
    /// by tip articulation, and now routinely by the relaxation pass too -
    /// so clamping against that stale sample would let a node escape the
    /// wall on a bend; the clamp has to be taken about the point the node is
    /// actually nearest to.
    float nearestArc(const Vec3 &p, float guess) const;

    Params m_params;
    const CenterlineGraph *m_graph = nullptr;
    CenterlinePath m_path;
    State m_state;
    std::vector<float> m_nodeArc;   ///< arc length assigned to each chain node
    std::vector<Support> m_supports;
    MechanicalState m_mech;

    /// True once the chain has a real relaxed shape to carry forward. False
    /// right after construction or a plain setPath(path) (keepDepth=false —
    /// there is nothing to carry, so the next update() seeds cleanly from the
    /// centerline target); true otherwise, including across a keepDepth=true
    /// setPath() (a route extension), so the shaft's actual rendered shape
    /// persists across that call instead of resetting. See update()'s own
    /// comment on why the chain is not reseeded to target every frame.
    bool m_seeded = false;
};

/// Display name for a MechanicalState::Regime value, e.g. for logging or a
/// future training-UI read-out.
const char *mechanicalRegimeName(CatheterModel::MechanicalState::Regime r);

} // namespace cn
