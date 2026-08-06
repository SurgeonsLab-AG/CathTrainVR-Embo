#pragma once
#include "CenterlineGraph.h"
#include "Vec3.h"
#include <vector>

namespace cn {

/// Kinematic model of a catheter / guidewire riding a planned centerline path.
///
/// This is a *navigation* model, not an FEM: the device is a chain of
/// inextensible segments whose lead node is driven along the path by insertion
/// depth ("follow-the-leader", the standard reduced model for endovascular
/// navigation), with an articulating tip and a hard constraint that keeps
/// every node inside the lumen. It is cheap enough to run at display rate and
/// gives the quantities an operator actually watches: insertion depth, lumen
/// calibre at the tip, wall clearance, and which branch the tip is in.
///
/// What it deliberately does not model: friction, torque wind-up along the
/// shaft, vessel compliance, or buckling. Swap in a Cosserat-rod solver behind
/// the same interface if you need those.
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
        float restingTipCurve = 0.f;    ///< radians; 0 keeps the tip on the axis
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
        float rollAngle = 0.f;
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

    CatheterModel() = default;
    explicit CatheterModel(const Params &p) : m_params(p) {}

    void setParams(const Params &p) { m_params = p; }
    const Params &params() const { return m_params; }

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
    void applyTipArticulation();
    void constrainToLumen();

    /// Arc length of the centerline point closest to `p`, searched in a window
    /// around `guess`. Articulating the tip moves a node away from the sample
    /// it was assigned, so clamping it against that stale sample lets it escape
    /// the wall on a bend - the clamp has to be taken about the point the node
    /// is actually nearest to.
    float nearestArc(const Vec3 &p, float guess) const;

    Params m_params;
    const CenterlineGraph *m_graph = nullptr;
    CenterlinePath m_path;
    State m_state;
    std::vector<float> m_nodeArc;   ///< arc length assigned to each chain node
};

} // namespace cn
