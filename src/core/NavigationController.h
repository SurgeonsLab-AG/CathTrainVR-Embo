#pragma once
#include "CenterlineGraph.h"
#include "Vec3.h"
#include <vector>

namespace cn {

/// Live steering, as opposed to following a route planned up front.
///
/// The route is built *as the operator advances*. From the access point the
/// controller walks the centerline while the vessel stays a simple chain, and
/// stops dead at the first bifurcation: the device cannot advance past a
/// junction until a branch is selected, exactly as it cannot in the lab.
/// Retracting back behind a junction un-commits that choice, so a wrong turn is
/// recoverable by pulling back and re-selecting.
///
/// Branch options are ordered by where they appear *on screen*, because "take
/// the left one" is a statement about the image the operator is looking at, not
/// about patient anatomy. The view frame therefore has to be pushed in from the
/// renderer; if the camera rotates 180 degrees, left and right swap, which is
/// the behaviour you want.
class NavigationController {
public:
    struct BranchOption {
        int32_t node = -1;         ///< first node down this branch
        Vec3 direction;            ///< unit takeoff direction at the junction
        float lateralScore = 0.f;  ///< signed screen-x; negative is left
        float verticalScore = 0.f; ///< signed screen-y, used to break ties
        float radius = 0.f;        ///< lumen radius entering the branch
        float takeoffAngleDeg = 0.f; ///< angle off the current travel direction
    };

    struct Params {
        /// Retracting this far behind a junction releases the committed choice.
        float reopenHysteresis = 0.004f;
        /// Stop this far short of the junction node so the tip sits at the
        /// carina rather than inside it.
        float junctionStandoff = 0.f;
    };

    void setGraph(const CenterlineGraph *g) { m_graph = g; }
    void setParams(const Params &p) { m_params = p; }
    const Params &params() const { return m_params; }

    /// Reset the route to start at `accessNode` with zero insertion.
    void startFrom(int32_t accessNode);

    /// Camera basis, used only to order branch options left-to-right.
    void setViewFrame(const Vec3 &screenRight, const Vec3 &screenUp);

    /// Page Up / Page Down. `ds` is in model units and may be negative.
    /// Returns the distance actually travelled - short of `ds` when the device
    /// runs up against an un-chosen junction or the access point.
    float advance(float ds);
    float retract(float ds) { return -advance(-ds); }

    /// Left / Right arrow. Returns false when no decision is pending.
    bool chooseLeft();
    bool chooseRight();
    /// For trifurcations: pick the i-th option in screen order.
    bool chooseOption(int index);

    bool decisionPending() const { return m_pendingJunction >= 0; }
    int32_t pendingJunctionNode() const { return m_pendingJunction; }
    const std::vector<BranchOption> &options() const { return m_options; }

    /// Distance from the tip to the pending junction (0 when parked on it).
    float distanceToDecision() const;

    bool atDeadEnd() const { return m_deadEnd; }

    const CenterlinePath &path() const { return m_path; }
    const std::vector<int32_t> &route() const { return m_route; }
    float depth() const { return m_depth; }
    float navigableLength() const { return m_path.totalLength(); }

    /// Number of junctions committed so far - the "turns taken" read-out.
    size_t turnsTaken() const { return m_committed.size(); }
    /// Nodes reachable from the current tip without retracting, for the UI.
    int32_t currentNode() const;

private:
    void extendRoute();
    void rebuildPath();
    void computeOptions();
    void maybeReopenJunction();

    struct Commit {
        size_t routeIndex = 0;   ///< index of the junction node in m_route
        float arcLength = 0.f;   ///< its arc length along the path
        int32_t node = -1;
    };

    const CenterlineGraph *m_graph = nullptr;
    Params m_params;

    std::vector<int32_t> m_route;
    CenterlinePath m_path;
    float m_depth = 0.f;

    int32_t m_pendingJunction = -1;
    std::vector<BranchOption> m_options;
    std::vector<Commit> m_committed;
    bool m_deadEnd = false;

    Vec3 m_screenRight{1.f, 0.f, 0.f};
    Vec3 m_screenUp{0.f, 1.f, 0.f};
};

} // namespace cn
