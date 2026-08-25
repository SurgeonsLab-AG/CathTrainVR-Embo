#include "CatheterModel.h"

#include <algorithm>
#include <limits>

namespace cn {

void CatheterModel::setPath(const CenterlinePath &path, bool keepDepth) {
    const float depth = keepDepth ? m_state.insertionDepth : 0.f;
    const float roll = keepDepth ? m_state.rollAngle : 0.f;
<<<<<<< HEAD
    const float rollRealized = keepDepth ? m_state.rollAngleRealized : 0.f;
    const float deflect = keepDepth ? m_state.tipDeflection : 0.f;

    // Carry the shaft's actual rendered shape across a route EXTENSION (a
    // branch just chosen, a click-refined entry) instead of discarding it -
    // the already-travelled proximal portion of the route does not change,
    // so there is no reason for the shaft to visibly reset to the raw
    // centerline the instant the path object is replaced. Only meaningful
    // when this model already had a real relaxed shape to carry (m_seeded);
    // a setPath(..., true) on a model that has never actually update()'d yet
    // has nothing worth keeping.
    const bool carryForward = keepDepth && m_seeded;
    std::vector<Vec3> keptNodes;
    if (carryForward) keptNodes = std::move(m_state.nodes);

    m_path = path;
    m_state = State{};
    m_state.rollAngle = roll;
    m_state.rollAngleRealized = rollRealized;
    m_state.tipDeflection = deflect;
    m_state.nodes.assign(std::max(2, m_params.segmentCount), Vec3());
    m_nodeArc.assign(m_state.nodes.size(), 0.f);

    if (carryForward) {
        // Index i means "i segments behind the tip" regardless of which path
        // object the chain is riding, so this correspondence is still valid
        // after the route changes; only copy as much as overlaps in case the
        // node count differs.
        const size_t m = std::min(keptNodes.size(), m_state.nodes.size());
        std::copy(keptNodes.begin(), keptNodes.begin() + m, m_state.nodes.begin());
    }
    m_seeded = carryForward;   // false forces a clean seed on the next update()
=======
    const float deflect = keepDepth ? m_state.tipDeflection : 0.f;

    m_path = path;
    m_state = State{};
    m_state.rollAngle = roll;
    m_state.tipDeflection = deflect;
    m_state.nodes.assign(std::max(2, m_params.segmentCount), Vec3());
    m_nodeArc.assign(m_state.nodes.size(), 0.f);
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7
    setInsertionDepth(depth);
}

void CatheterModel::setInsertionDepth(float s) {
    if (m_path.empty()) { m_state.insertionDepth = 0.f; return; }
    m_state.insertionDepth = std::clamp(s, 0.f, m_path.totalLength());
}

void CatheterModel::setTipDeflection(float radians) {
    m_state.tipDeflection = std::clamp(radians, -m_params.maxTipDeflection,
                                       m_params.maxTipDeflection);
}

void CatheterModel::update() {
    if (m_path.empty()) return;

<<<<<<< HEAD
    const size_t prevSize = m_state.nodes.size();

=======
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7
    // The shaft must reach back to the puncture: a fixed-length chain leaves
    // the catheter floating in mid-vessel with no visible connection to the
    // access point, which reads as an artefact rather than as a device.
    if (m_params.autoLength) {
        const float seg = std::max(m_params.segmentLength, 1e-6f);
        const int need = std::clamp(
            static_cast<int>(std::ceil(m_state.insertionDepth / seg)) + 2,
            std::max(8, m_params.tipFlexSegments + 2), m_params.maxSegments);
        if (static_cast<int>(m_state.nodes.size()) != need) {
<<<<<<< HEAD
            // resize(), not assign(): keeps whatever the existing nodes
            // already are - carried forward from the previous frame, or from
            // setPath()'s own carry-forward - instead of wiping the whole
            // chain to zero every time it needs to grow or shrink.
            m_state.nodes.resize(static_cast<size_t>(need), Vec3());
=======
            m_state.nodes.assign(static_cast<size_t>(need), Vec3());
            m_nodeArc.assign(static_cast<size_t>(need), 0.f);
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7
        }
    }

    const size_t n = m_state.nodes.size();
    if (m_nodeArc.size() != n) m_nodeArc.assign(n, 0.f);
    if (m_state.nodeRadius.size() != n) m_state.nodeRadius.assign(n, m_params.deviceRadius);

<<<<<<< HEAD
    // --- 1. Target samples along the path -------------------------------------
    // What a perfectly compliant device would occupy: the tip at the insertion
    // depth, each trailing node one segment length further back *along the
    // path*, not along a straight line. This is the REST state relaxChain()
    // pulls the actual chain toward, not the final answer - a device stiffer
    // than the vessel is tight only approximates it.
    const float depth = m_state.insertionDepth;
    std::vector<Vec3> target(n);
    for (size_t i = 0; i < n; ++i) {
        const float s = depth - static_cast<float>(i) * m_params.segmentLength;
        m_nodeArc[i] = std::max(0.f, s);
        target[i] = m_path.pointAt(m_nodeArc[i]);
    }

    // --- 2. Roll wind-up -------------------------------------------------------
    // The tip's realised roll eases toward the operator's commanded roll
    // rather than snapping to it - a fixed fraction per update() call, the
    // same "ease by a constant fraction every fixed tick" idiom the vessel
    // camera already uses (VesselView::m_camEase), not a true torque
    // simulation.
    {
        const float resp = std::clamp(m_params.rollResponsiveness, 0.f, 1.f);
        float d = m_state.rollAngle - m_state.rollAngleRealized;
        // Shortest angular path, so a roll command near +/-pi does not wind
        // the long way around.
        while (d > 3.14159265f)  d -= 6.28318531f;
        while (d < -3.14159265f) d += 6.28318531f;
        m_state.rollAngleRealized += d * resp;
    }

    // --- 3. Tip shape / steering ------------------------------------------
    // Bends the leading tip-flex-zone TARGET samples into the tip's resting
    // shape plus operator deflection, about the realised (lagged) roll. This
    // happens to the target array, before relaxation, so the tip is folded
    // into the very same solve as the rest of the shaft below rather than
    // being pinned afterward - which is what keeps the shaft smoothly,
    // tangent-continuously joined to it instead of kinking at the pivot.
    applyTipArticulation(target);

    // --- 4. Seed / carry forward, then relax ----------------------------
    // The rendered chain is NOT reset to `target` every call - it is carried
    // forward from whatever it already was (the previous frame's relaxed
    // shape, or whatever setPath() just carried over) and only nudged toward
    // the new target by relaxChain()'s fixed iteration count. That is what
    // turns advancing, steering, and crossing a bifurcation into continuous,
    // spring-like motion instead of the shaft popping to a freshly-solved
    // shape every frame. Only genuinely new nodes have no previous shape to
    // carry and start at their own target sample: the chain just grew
    // (autoLength added trailing entries), or this is the very first update
    // after a plain setPath() with nothing seeded yet at all.
    if (!m_seeded) {
        m_state.nodes = target;
        m_seeded = true;
    } else if (n > prevSize) {
        for (size_t i = prevSize; i < n; ++i) m_state.nodes[i] = target[i];
    }
    relaxChain(target);

=======
    // --- follow-the-leader ---------------------------------------------------
    // The tip sits at the insertion depth; every trailing node sits one segment
    // length further back *along the path*, not along a straight line. This is
    // what makes the shaft hug the vessel through a bend instead of chording
    // across it.
    const float depth = m_state.insertionDepth;
    for (size_t i = 0; i < n; ++i) {
        const float s = depth - static_cast<float>(i) * m_params.segmentLength;
        m_nodeArc[i] = std::max(0.f, s);
        m_state.nodes[i] = m_path.pointAt(m_nodeArc[i]);
    }

    applyTipArticulation();
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7
    if (m_params.clampToLumen) constrainToLumen();

    // --- telemetry -----------------------------------------------------------
    m_state.tipPosition = m_state.nodes[0];
    m_state.tipDirection = (n > 1) ? (m_state.nodes[0] - m_state.nodes[1]).normalized()
                                   : m_path.tangentAt(depth);
    m_state.lumenRadiusAtTip = m_path.radiusAt(depth);
    m_state.remainingToTarget = m_path.totalLength() - depth;
    m_state.currentNodeId = m_path.nodeAt(depth);
    m_state.currentBranchId =
        (m_graph && m_state.currentNodeId >= 0 &&
         m_state.currentNodeId < static_cast<int32_t>(m_graph->nodeCount()))
            ? m_graph->nodes()[m_state.currentNodeId].branchId
            : -1;

    m_state.minWallClearance = std::numeric_limits<float>::max();
    m_state.contactNode = -1;
    for (size_t i = 0; i < n; ++i) {
        const Vec3 axis = m_path.pointAt(m_nodeArc[i]);
        const Vec3 tangent = m_path.tangentAt(m_nodeArc[i]);
        const float lumen = m_path.radiusAt(m_nodeArc[i]);

        // Only the component *across* the vessel counts. A curved tip is also
        // displaced along the axis, and measuring the raw distance from the
        // centerline sample counted that as penetration - which reported wall
        // contact on a device that was comfortably inside the lumen, and is
        // also the metric the lumen constraint itself uses.
        const Vec3 d = m_state.nodes[i] - axis;
        const float off = (d - tangent * d.dot(tangent)).length();
        const float clearance = lumen - off - m_params.deviceRadius;
        if (clearance < m_state.minWallClearance) {
            m_state.minWallClearance = clearance;
            m_state.contactNode = static_cast<int>(i);
            m_state.contactPoint = m_state.nodes[i];
        }

        // Drawn radius is fitted to the lumen so no part of the device is ever
        // rendered outside the vessel. The true OD is kept for the clearance
        // figure above, so shrinking the drawing never hides an interference.
        float r = m_params.deviceRadius;
        if (m_params.fitToLumen) r = std::min(r, lumen * m_params.lumenFitFraction);
        m_state.nodeRadius[i] = std::max(r, m_params.deviceRadius * 0.12f);
    }
    // --- balloon -------------------------------------------------------------
    if (m_params.hasBalloon) {
        const float sB = std::max(0.f, depth - m_params.balloonOffset);
        m_state.balloonCentre = m_path.pointAt(sB);
        m_state.balloonAxis = m_path.tangentAt(sB);
        m_state.balloonLumen = m_path.radiusAt(sB);

        // A compliant balloon conforms to the vessel: it inflates to its rated
        // size or to the wall, whichever comes first. That is the whole point
        // of the device, so wall apposition here is a *goal*, not a collision,
        // and it is deliberately excluded from the contact alarm above.
        const float want = m_params.balloonMaxRadius * m_params.balloonInflation;
        const float floorR = m_params.deviceRadius * 1.05f;   // deflated ~= shaft
        m_state.balloonRadius = std::max(floorR, std::min(want, m_state.balloonLumen * 0.98f));
        m_state.balloonApposition =
            m_state.balloonLumen > 1e-9f
                ? std::clamp(m_state.balloonRadius / m_state.balloonLumen, 0.f, 1.f)
                : 0.f;
        m_state.balloonInflated = m_params.balloonInflation > 0.01f;
    } else {
        m_state.balloonRadius = 0.f;
        m_state.balloonApposition = 0.f;
        m_state.balloonInflated = false;
    }

    m_state.wallContact = m_state.minWallClearance <= 0.f;
    m_state.contactSeverity =
        std::clamp(-m_state.minWallClearance / std::max(m_params.deviceRadius, 1e-9f), 0.f, 1.f);
    m_state.oversized = m_params.deviceRadius >= m_state.lumenRadiusAtTip;
}

<<<<<<< HEAD
namespace {

/// Cumulative bend angle at normalised distance `d` (0 at the pivot, 1 at the
/// tip) along a compound resting shape. Walks the segment list summing each
/// one's full curve before it, then linearly interpolates through whichever
/// segment `d` falls inside - the same taper-to-a-constant-curvature-arc
/// approximation the single-curve case always used, just repeated once per
/// segment instead of once for the whole tip. With exactly one segment of
/// lengthFraction 1 this reduces to `curveRadians * d`, i.e. the plain
/// single-arc formula it replaces.
float compoundShapeAngle(const std::vector<CatheterModel::Params::TipShapeSegment> &shape,
                         float d) {
    float base = 0.f;      // angle accumulated by segments fully behind d
    float pos  = 0.f;      // cumulative lengthFraction consumed so far
    for (const auto &seg : shape) {
        const float segLen = std::max(seg.lengthFraction, 1e-6f);
        const float segEnd = pos + segLen;
        if (d <= segEnd || &seg == &shape.back()) {
            const float t = std::clamp((d - pos) / segLen, 0.f, 1.f);
            return base + seg.curveRadians * t;
        }
        base += seg.curveRadians;
        pos = segEnd;
    }
    return base;   // empty shape
}

} // namespace

void CatheterModel::applyTipArticulation(std::vector<Vec3> &nodes) {
    const int k = std::min(m_params.tipFlexSegments,
                           static_cast<int>(nodes.size()) - 1);
    const bool compound = !m_params.tipShape.empty();
    // Single-arc fallback keeps its own early-out so a straight device with
    // no operator input costs nothing beyond this check, exactly as before.
    if (!compound) {
        const float curve = m_state.tipDeflection + m_params.restingTipCurve;
        if (k < 2 || std::fabs(curve) < 1e-5f) return;
    } else if (k < 2) {
        return;
    }

    // Bend the leading k nodes about a pivot, in the plane selected by the
    // REALISED (lagged) roll angle, not the raw commanded one - see State::
    // rollAngleRealized. A single curve tapers linearly to zero at the pivot,
    // approximating a constant-curvature arc; a compound shape does the same
    // per segment (see compoundShapeAngle) so the whole tip stays planar -
    // true to how a real shaped diagnostic catheter is heat-set, one flat
    // curve at a time.
    const Vec3 pivot = nodes[k];
    const Vec3 tangent = (nodes[0] - nodes[k]).normalized();
    if (tangent.lengthSquared() < 1e-12f) return;

    const Vec3 ref = anyPerpendicular(tangent);
    const Vec3 up = rotateAbout(ref, tangent, m_state.rollAngleRealized);
    const Vec3 bendAxis = tangent.cross(up).normalized();

    for (int i = 0; i < k; ++i) {
        const float d = static_cast<float>(k - i) / static_cast<float>(k);
        // Operator steering is added on top of the resting shape, tapered the
        // same way the resting curve is, so a steered compound tip still
        // flexes as one continuous piece rather than kinking at the pivot.
        const float angle = compound ? compoundShapeAngle(m_params.tipShape, d)
                                           + m_state.tipDeflection * d
                                     : (m_state.tipDeflection + m_params.restingTipCurve) * d;
        const Vec3 rel = nodes[i] - pivot;
        nodes[i] = pivot + rotateAbout(rel, bendAxis, angle);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// relaxChain — position-based (Gauss-Seidel) relaxation.
//
// Three passes per iteration, in this order:
//
//   1. Distance: push/pull each adjacent pair symmetrically back to exactly
//      segmentLength apart. The chain does not stretch; this is the one
//      constraint that always holds exactly regardless of stiffness.
//
//   2. Bending: pull each interior node toward the midpoint of its two
//      neighbours - the straightest the chain can be without breaking the
//      distance constraint above - scaled by Params::shaftStiffness. This is
//      what makes a stiff device cut a corner instead of hugging it, and it
//      is the ONLY thing that determines how far the final resting shape
//      deviates from the centerline.
//
//   3. Centerline attraction: pull every node back toward its own target
//      sample. This pull has a FLOOR (kPullFloor below) that does not go to
//      zero even at full stiffness - it exists purely to make each frame
//      converge to its equilibrium quickly, not to limit how far a stiff
//      chain is allowed to deviate (the bending pass already owns that). A
//      pull weight that faded to ~0 at high stiffness (the previous
//      formulation) made a STIFF device converge slower toward a changed
//      target than a soft one - backwards from a real catheter, where more
//      rigidity means a quicker, more decisive response, not a floppier,
//      slower-settling one. That mismatch is what read as "too flexible":
//      with the chain now warm-started across frames (see update()) instead
//      of fully re-solved every call, a weak pull meant a stiff device could
//      take many frames to catch up to a moving target, showing as lag and
//      wobble along the way rather than a taut, immediate response.
//
// A handful of Gauss-Seidel sweeps is standard for a short position-based
// chain like this (see e.g. cloth/rope solvers): each pass only looks at
// immediate neighbours, so the whole thing stays O(n) per iteration, and a
// small FIXED iteration count keeps update() reproducible call to call -
// which matters here because every view renders this one CatheterModel
// instance and must never compute a different answer from it.
// ─────────────────────────────────────────────────────────────────────────────
void CatheterModel::relaxChain(const std::vector<Vec3> &target) {
    const size_t n = m_state.nodes.size();
    if (n < 3 || target.size() != n) return;

    const float stiff  = std::clamp(m_params.shaftStiffness, 0.f, 1.f);
    const float segLen = std::max(m_params.segmentLength, 1e-6f);
    const int   iters   = std::max(1, m_params.relaxIterations);

    // Floor so a stiff device still converges fast frame-to-frame - see the
    // class comment above. 0 stiffness -> 0.85 (dominant, hugs the
    // centerline almost exactly); full stiffness -> 0.35 (still a decisive
    // per-iteration correction, just weak enough that the bending pass below
    // can hold the chain off-axis through an actual bend).
    constexpr float kPullFloor = 0.35f;
    constexpr float kPullSpan  = 0.50f;
    const float pull = kPullFloor + (1.f - stiff) * kPullSpan;

    for (int pass = 0; pass < iters; ++pass) {
        for (size_t i = 0; i + 1 < n; ++i) {
            Vec3 &a = m_state.nodes[i];
            Vec3 &b = m_state.nodes[i + 1];
            const Vec3 d = b - a;
            const float len = d.length();
            if (len < 1e-9f) continue;
            const float err = (len - segLen) * 0.5f;
            const Vec3 corr = d * (err / len);
            a += corr;
            b -= corr;
        }

        if (stiff > 1e-4f) {
            for (size_t i = 1; i + 1 < n; ++i) {
                const Vec3 straight = (m_state.nodes[i - 1] + m_state.nodes[i + 1]) * 0.5f;
                m_state.nodes[i] += (straight - m_state.nodes[i]) * (stiff * 0.65f);
            }
        }

        if (pull > 1e-4f)
            for (size_t i = 0; i < n; ++i)
                m_state.nodes[i] += (target[i] - m_state.nodes[i]) * pull;
=======
void CatheterModel::applyTipArticulation() {
    const int k = std::min(m_params.tipFlexSegments,
                           static_cast<int>(m_state.nodes.size()) - 1);
    // Total curve = the catheter's own pre-shaped primary curve plus whatever
    // the operator is adding.
    const float curve = m_state.tipDeflection + m_params.restingTipCurve;
    if (k < 2 || std::fabs(curve) < 1e-5f) return;

    // Bend the leading k nodes about a pivot, in the plane selected by the roll
    // angle. Deflection tapers linearly to zero at the pivot, which approximates
    // the constant-curvature arc of a steerable tip.
    const Vec3 pivot = m_state.nodes[k];
    const Vec3 tangent = (m_state.nodes[0] - m_state.nodes[k]).normalized();
    if (tangent.lengthSquared() < 1e-12f) return;

    const Vec3 ref = anyPerpendicular(tangent);
    const Vec3 up = rotateAbout(ref, tangent, m_state.rollAngle);
    const Vec3 bendAxis = tangent.cross(up).normalized();

    for (int i = 0; i < k; ++i) {
        const float w = static_cast<float>(k - i) / static_cast<float>(k);
        const Vec3 rel = m_state.nodes[i] - pivot;
        m_state.nodes[i] = pivot + rotateAbout(rel, bendAxis, curve * w);
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7
    }
}

float CatheterModel::nearestArc(const Vec3 &p, float guess) const {
    if (m_path.empty()) return guess;
    const float total = m_path.totalLength();
<<<<<<< HEAD
    // Widened from 3x to 4x segmentLength: the relaxed shaft can now sit
    // further from its own arc-length target than a purely kinematic snap
    // ever did (that is the point of shaftStiffness), so the search has to
    // reach a little further to still find the true nearest cross-section
    // rather than settling for the nearest point inside too narrow a window.
    const float window = std::max(m_params.segmentLength * 4.f, total * 0.01f);
=======
    // A window of a few segment lengths is plenty: a node never strays further
    // than the articulation can throw it, and a global search would cost O(n)
    // per node per frame for no benefit.
    const float window = std::max(m_params.segmentLength * 3.f, total * 0.01f);
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7
    const float lo = std::clamp(guess - window, 0.f, total);
    const float hi = std::clamp(guess + window, 0.f, total);
    const int steps = 24;

    float best = guess, bestD = std::numeric_limits<float>::max();
    for (int i = 0; i <= steps; ++i) {
        const float s = lo + (hi - lo) * static_cast<float>(i) / static_cast<float>(steps);
        const float d = (m_path.pointAt(s) - p).lengthSquared();
        if (d < bestD) { bestD = d; best = s; }
    }
    // One golden-ish refinement pass around the winner.
    const float span = (hi - lo) / static_cast<float>(steps);
    for (int i = -3; i <= 3; ++i) {
        const float s = std::clamp(best + span * static_cast<float>(i) / 3.f, 0.f, total);
        const float d = (m_path.pointAt(s) - p).lengthSquared();
        if (d < bestD) { bestD = d; best = s; }
    }
    return best;
}

void CatheterModel::constrainToLumen() {
    // Hard projection: a node may not sit further from the centerline than the
    // usable lumen radius minus the device radius.
    //
<<<<<<< HEAD
    // The cross-section to clamp against is found by nearestArc(), not by
    // using the node's nominal arc position: both tip articulation and the
    // relaxation pass move a node away from the arc sample it was assigned,
    // so clamping it against that original cross-section measured the wrong
    // distance in the wrong direction - which let an articulated or
    // stiffness-deviated tip swing bodily outside the vessel while the
    // constraint reported it as satisfied.
    for (size_t i = 0; i < m_state.nodes.size(); ++i) {
        const Vec3 &node = m_state.nodes[i];
        const float bestS = nearestArc(node, m_nodeArc[i]);
=======
    // The cross-section to clamp against is found by a local search rather than
    // by using the node's nominal arc position. Tip articulation *moves* a node
    // away from the arc it was placed at, so clamping it against that original
    // cross-section measured the wrong distance in the wrong direction - which
    // let an articulated tip swing bodily outside the vessel while the
    // constraint reported it as satisfied. This is the fix for the curved tip
    // escaping the wall.
    const float total = m_path.totalLength();
    const float window = m_params.segmentLength * 3.f;
    const float step = std::max(m_params.segmentLength * 0.25f, 1e-6f);

    for (size_t i = 0; i < m_state.nodes.size(); ++i) {
        const Vec3 &node = m_state.nodes[i];

        float bestS = m_nodeArc[i];
        float bestD = std::numeric_limits<float>::max();
        const float lo = std::max(0.f, m_nodeArc[i] - window);
        const float hi = std::min(total, m_nodeArc[i] + window);
        for (float s = lo; s <= hi; s += step) {
            const float d = (node - m_path.pointAt(s)).lengthSquared();
            if (d < bestD) { bestD = d; bestS = s; }
        }
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7

        const Vec3 axis = m_path.pointAt(bestS);
        const Vec3 tangent = m_path.tangentAt(bestS);
        const float lumen = m_path.radiusAt(bestS);

        Vec3 offset = node - axis;
        offset -= tangent * offset.dot(tangent);   // keep only the cross-vessel part

        const float allowed = std::max(0.f, lumen * m_params.wallSafety - m_params.deviceRadius);
        const float len = offset.length();
        if (len > allowed && len > 1e-9f) {
            const Vec3 along = tangent * (node - axis).dot(tangent);
            m_state.nodes[i] = axis + along + offset * (allowed / len);
        }
        // Remember where this node actually ended up, so the clearance read-out
        // and the drawn radius are computed against the same cross-section.
        m_nodeArc[i] = bestS;
    }
}

float CatheterModel::narrowestAhead() const {
    if (m_path.empty()) return 0.f;
    float minR = std::numeric_limits<float>::max();
    const float total = m_path.totalLength();
    const float step = std::max(1e-4f, m_params.segmentLength * 0.25f);
    for (float s = m_state.insertionDepth; s <= total; s += step)
        minR = std::min(minR, m_path.radiusAt(s));
    return minR == std::numeric_limits<float>::max() ? 0.f : minR;
}

float CatheterModel::tipTaper(int i) const {
    // Real catheter tips are moulded down to a soft, slightly narrower nose so
    // they do not dissect the intima. Four segments of ramp is enough to read
    // as a taper at any sane zoom.
    constexpr int kRamp = 4;
    if (i >= kRamp) return 1.f;
    return 0.55f + 0.45f * static_cast<float>(i) / static_cast<float>(kRamp);
}

void CatheterModel::buildTubeMesh(int sides,
                                  std::vector<float> &interleavedOut,
                                  std::vector<uint32_t> &indicesOut,
                                  int firstNode,
                                  int lastNode) const {
    interleavedOut.clear();
    indicesOut.clear();
    const auto &nodes = m_state.nodes;
    if (nodes.size() < 2 || sides < 3) return;

    const int n = static_cast<int>(nodes.size());
    const int lo = std::clamp(firstNode, 0, n - 1);
    const int hi = (lastNode < 0) ? n - 1 : std::clamp(lastNode, 0, n - 1);
    if (hi - lo < 1) return;

    auto tangentAt = [&](int i) {
        Vec3 t;
        if (i == 0) t = nodes[1] - nodes[0];
        else if (i + 1 == n) t = nodes[i] - nodes[i - 1];
        else t = nodes[i + 1] - nodes[i - 1];
        return t.normalized();
    };

    // Parallel-transport the frame from the very start of the chain, not from
    // `lo`: spans built independently would otherwise have mismatched frames
    // and the marker band would visibly twist against the shaft.
    Vec3 prevTangent = tangentAt(0);
    Vec3 normal = anyPerpendicular(prevTangent);
    for (int i = 1; i <= lo; ++i) {
        const Vec3 t = tangentAt(i);
        const Vec3 axis = prevTangent.cross(t);
        const float sinA = axis.length();
        if (sinA > 1e-6f) normal = rotateAbout(normal, axis / sinA, std::asin(std::min(1.f, sinA)));
        normal = (normal - t * normal.dot(t)).normalized();
        prevTangent = t;
    }

    const int rings = hi - lo + 1;
    for (int i = lo; i <= hi; ++i) {
        Vec3 tangent = tangentAt(i);
        if (tangent.lengthSquared() < 1e-12f) tangent = prevTangent;

        const Vec3 axis = prevTangent.cross(tangent);
        const float sinA = axis.length();
        if (sinA > 1e-6f)
            normal = rotateAbout(normal, axis / sinA, std::asin(std::min(1.f, sinA)));
        normal = (normal - tangent * normal.dot(tangent)).normalized();
        const Vec3 binormal = tangent.cross(normal);
        prevTangent = tangent;

        const float base = (i < static_cast<int>(m_state.nodeRadius.size()))
                               ? m_state.nodeRadius[i]
                               : m_params.deviceRadius;
        const float r = base * tipTaper(i);
        for (int j = 0; j < sides; ++j) {
            const float a = 6.2831853f * static_cast<float>(j) / static_cast<float>(sides);
            const Vec3 dir = normal * std::cos(a) + binormal * std::sin(a);
            const Vec3 p = nodes[i] + dir * r;
            interleavedOut.insert(interleavedOut.end(), {p.x, p.y, p.z, dir.x, dir.y, dir.z});
        }
    }

    for (int i = 0; i + 1 < rings; ++i) {
        for (int j = 0; j < sides; ++j) {
            const uint32_t a = static_cast<uint32_t>(i * sides + j);
            const uint32_t b = static_cast<uint32_t>(i * sides + (j + 1) % sides);
            const uint32_t c = a + static_cast<uint32_t>(sides);
            const uint32_t d = b + static_cast<uint32_t>(sides);
            indicesOut.insert(indicesOut.end(), {a, c, b, b, c, d});
        }
    }

    // Rounded nose on the leading span only, so the device does not read as a
    // cut-off pipe when the camera is close behind the tip.
    if (lo == 0) {
        const Vec3 t0 = tangentAt(0);
        const float noseR = m_state.nodeRadius.empty() ? m_params.deviceRadius
<<<<<<< HEAD
                                                       : m_state.nodeRadius[0];
=======
                                                      : m_state.nodeRadius[0];
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7
        const Vec3 apex = nodes[0] + t0 * (noseR * 0.7f);
        const uint32_t apexId = static_cast<uint32_t>(interleavedOut.size() / 6);
        interleavedOut.insert(interleavedOut.end(),
                              {apex.x, apex.y, apex.z, t0.x, t0.y, t0.z});
        for (int j = 0; j < sides; ++j) {
            const uint32_t a = static_cast<uint32_t>(j);
            const uint32_t b = static_cast<uint32_t>((j + 1) % sides);
            indicesOut.insert(indicesOut.end(), {apexId, b, a});
        }
    }
}

void CatheterModel::setBalloonInflation(float f) {
    m_params.balloonInflation = std::clamp(f, 0.f, 1.f);
}

void CatheterModel::buildBalloonMesh(int rings, int sides,
                                     std::vector<float> &interleavedOut,
                                     std::vector<uint32_t> &indicesOut) const {
    interleavedOut.clear();
    indicesOut.clear();
    if (!m_params.hasBalloon || m_state.balloonRadius <= 0.f || rings < 3 || sides < 3) return;

    const Vec3 axis = m_state.balloonAxis.normalized();
    const Vec3 u = anyPerpendicular(axis);
    const Vec3 v = axis.cross(u);

    // Prolate spheroid: semi-axis `a` along the shaft, `b` across it. The
    // deflated balloon has b ~= the shaft radius, so the same mesh covers both
    // states and the transition is continuous.
    const float a = std::max(m_params.balloonLength * 0.5f, m_state.balloonRadius * 0.6f);
    const float b = m_state.balloonRadius;

    for (int i = 0; i <= rings; ++i) {
        const float theta = 3.14159265f * static_cast<float>(i) / static_cast<float>(rings);
        const float ca = std::cos(theta), sa = std::sin(theta);
        for (int j = 0; j < sides; ++j) {
            const float phi = 6.2831853f * static_cast<float>(j) / static_cast<float>(sides);
            const Vec3 radial = u * std::cos(phi) + v * std::sin(phi);
            const Vec3 p = m_state.balloonCentre + axis * (a * ca) + radial * (b * sa);
            // Ellipsoid normal is (x/a^2, y/b^2, z/b^2) in the local frame, not
            // the position vector - using the position gives a visibly wrong
            // highlight on anything but a sphere.
            const Vec3 nrm = (axis * (ca / (a * a)) + radial * (sa / (b * b))).normalized();
            interleavedOut.insert(interleavedOut.end(),
                                  {p.x, p.y, p.z, nrm.x, nrm.y, nrm.z});
        }
    }

    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < sides; ++j) {
            const uint32_t p0 = static_cast<uint32_t>(i * sides + j);
            const uint32_t p1 = static_cast<uint32_t>(i * sides + (j + 1) % sides);
            const uint32_t p2 = p0 + static_cast<uint32_t>(sides);
            const uint32_t p3 = p1 + static_cast<uint32_t>(sides);
            indicesOut.insert(indicesOut.end(), {p0, p2, p1, p1, p2, p3});
        }
    }
}

<<<<<<< HEAD
} // namespace cn
=======
} // namespace cn
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7
