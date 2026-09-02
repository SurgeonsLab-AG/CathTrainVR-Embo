#include "NavigationController.h"

#include <algorithm>
#include <cmath>

namespace cn {

void NavigationController::startFrom(int32_t accessNode) {
    m_route.clear();
    m_committed.clear();
    m_options.clear();
    m_pendingJunction = -1;
    m_deadEnd = false;
    m_depth = 0.f;
    m_path = CenterlinePath{};

    if (!m_graph || accessNode < 0 ||
        accessNode >= static_cast<int32_t>(m_graph->nodeCount()))
        return;

    m_route.push_back(accessNode);
    extendRoute();
    rebuildPath();
    computeOptions();
}

void NavigationController::startFrom(int32_t accessNode, int32_t forwardNeighbour) {
    m_route.clear();
    m_committed.clear();
    m_options.clear();
    m_pendingJunction = -1;
    m_deadEnd = false;
    m_depth = 0.f;
    m_path = CenterlinePath{};

    if (!m_graph || accessNode < 0 || forwardNeighbour < 0 ||
        accessNode >= static_cast<int32_t>(m_graph->nodeCount()) ||
        forwardNeighbour >= static_cast<int32_t>(m_graph->nodeCount()))
        return;

    m_route.push_back(accessNode);
    m_route.push_back(forwardNeighbour);
    extendRoute();
    rebuildPath();
    computeOptions();
}

void NavigationController::setViewFrame(const Vec3 &screenRight, const Vec3 &screenUp) {
    m_screenRight = screenRight.normalized();
    m_screenUp = screenUp.normalized();
    computeOptions();   // ordering depends on the camera, so re-sort in place
}

void NavigationController::extendRoute() {
    m_pendingJunction = -1;
    m_deadEnd = false;
    if (!m_graph || m_route.empty()) return;

    // Walk forward while the vessel is a simple chain. `visited` guards against
    // the extractor's occasional small cycle where two shells were stitched at
    // both ends - without it this loop would never terminate.
    std::vector<char> visited(m_graph->nodeCount(), 0);
    for (int32_t n : m_route) visited[n] = 1;

    while (true) {
        const int32_t last = m_route.back();
        const int32_t prev = m_route.size() > 1 ? m_route[m_route.size() - 2] : -1;

        std::vector<int32_t> next;
        for (int32_t nb : m_graph->neighbours(last)) {
            if (nb == prev || visited[nb]) continue;
            next.push_back(nb);
        }

        if (next.empty()) { m_deadEnd = true; return; }
        if (next.size() > 1) { m_pendingJunction = last; return; }

        m_route.push_back(next.front());
        visited[next.front()] = 1;
    }
}

void NavigationController::rebuildPath() {
    if (!m_graph) { m_path = CenterlinePath{}; return; }
    // Built straight from graph nodes, deliberately not resampled: node spacing
    // is already near-uniform from extraction, and keeping the original samples
    // means the arc length of everything behind the tip is *exactly* unchanged
    // when the route is extended. Resampling would nudge it, and insertion
    // depth would drift every time a branch was taken.
    m_path = m_graph->makePath(m_route);
    m_depth = std::clamp(m_depth, 0.f, m_path.totalLength());
}

void NavigationController::computeOptions() {
    m_options.clear();
    if (!m_graph || m_pendingJunction < 0 || m_route.empty()) return;

    const int32_t J = m_pendingJunction;
    const Vec3 jp = m_graph->nodes()[J].position;

    // Direction of travel arriving at the junction.
    Vec3 fwd{0.f, 0.f, 1.f};
    if (m_route.size() > 1) {
        const Vec3 prev = m_graph->nodes()[m_route[m_route.size() - 2]].position;
        fwd = (jp - prev).normalized();
    }

    const int32_t cameFrom = m_route.size() > 1 ? m_route[m_route.size() - 2] : -1;
    std::vector<char> onRoute(m_graph->nodeCount(), 0);
    for (int32_t n : m_route) onRoute[n] = 1;

    for (int32_t nb : m_graph->neighbours(J)) {
        if (nb == cameFrom || onRoute[nb]) continue;
        BranchOption o;
        o.node = nb;
        o.direction = (m_graph->nodes()[nb].position - jp).normalized();
        o.radius = m_graph->nodes()[nb].radius;

        const float cosA = std::clamp(o.direction.dot(fwd), -1.f, 1.f);
        o.takeoffAngleDeg = std::acos(cosA) * 57.2957795f;

        // Project the *lateral* part of the takeoff - the component along the
        // current travel direction says nothing about which side it is on.
        const Vec3 lat = o.direction - fwd * o.direction.dot(fwd);
        o.lateralScore = lat.dot(m_screenRight);
        o.verticalScore = lat.dot(m_screenUp);
        m_options.push_back(o);
    }

    // Left-to-right in screen space. When two branches are nearly edge-on the
    // lateral scores collapse, so fall back to screen-y to keep the ordering
    // deterministic rather than letting it flicker with the camera.
    std::sort(m_options.begin(), m_options.end(),
              [](const BranchOption &a, const BranchOption &b) {
                  if (std::fabs(a.lateralScore - b.lateralScore) > 1e-4f)
                      return a.lateralScore < b.lateralScore;
                  return a.verticalScore > b.verticalScore;
              });
}

float NavigationController::distanceToDecision() const {
    if (m_pendingJunction < 0) return -1.f;
    return std::max(0.f, m_path.totalLength() - m_params.junctionStandoff - m_depth);
}

float NavigationController::advance(float ds) {
    if (m_path.empty()) return 0.f;

    // Forward travel stops at the end of the committed route. That end is
    // either an un-chosen junction or a true dead end; either way the device
    // has nowhere legal to go.
    const float ceiling = std::max(0.f, m_path.totalLength() -
                                            (decisionPending() ? m_params.junctionStandoff : 0.f));
    const float before = m_depth;
    m_depth = std::clamp(m_depth + ds, 0.f, ceiling);

    if (ds < 0.f) maybeReopenJunction();
    return m_depth - before;
}

void NavigationController::maybeReopenJunction() {
    // Pulling back behind the last committed junction releases that choice, so
    // the operator can re-select. Hysteresis stops the decision prompt from
    // flickering when the tip sits right on the carina.
    while (!m_committed.empty()) {
        const Commit &c = m_committed.back();
        if (m_depth >= c.arcLength - m_params.reopenHysteresis) break;

        m_route.resize(c.routeIndex + 1);
        m_committed.pop_back();
        extendRoute();
        rebuildPath();
        computeOptions();
    }
}

bool NavigationController::chooseOption(int index) {
    if (m_pendingJunction < 0) return false;
    if (index < 0 || index >= static_cast<int>(m_options.size())) return false;

    Commit c;
    c.routeIndex = m_route.size() - 1;   // the junction is the current tail
    c.node = m_pendingJunction;
    c.arcLength = m_path.totalLength();  // path currently ends at the junction
    m_committed.push_back(c);

    m_route.push_back(m_options[index].node);
    extendRoute();
    rebuildPath();
    computeOptions();
    return true;
}

bool NavigationController::chooseLeft() {
    return chooseOption(0);
}

bool NavigationController::chooseRight() {
    return chooseOption(static_cast<int>(m_options.size()) - 1);
}

int32_t NavigationController::currentNode() const {
    return m_path.empty() ? (m_route.empty() ? -1 : m_route.front()) : m_path.nodeAt(m_depth);
}

} // namespace cn