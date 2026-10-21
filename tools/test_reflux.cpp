// Proves the three defining behaviors of reflux modeling:
//   1. Early injection (well below target capacity) produces ZERO reflux.
//   2. Continued injection past the viscosity-derived onset fraction
//      produces growing, permanent reflux into the excluded proximal segment.
//   3. Without an excluded neighbor at all, excess volume is simply capped
//      at capacity rather than fabricating a reflux direction - no crash,
//      no silent wrong behavior.
#include "EmbolizationModel.h"
#include "CenterlineGraph.h"
#include <cstdio>

using namespace cn;

int main() {
    // p2 -- p1 -- n0 -- n1 -- n2
    //  (wide proximal tree)  (narrow target territory)
    // n0 is the injection node (catheter tip); p1 is the catheter's own
    // proximal segment (excludeNeighbor); p2 is further back, giving the
    // reflux Dijkstra somewhere real to spread into beyond p1 itself.
    CenterlineGraph g;
    const int32_t p2 = g.addNode({-0.02f, 0.f, 0.f}, 0.003f, 0);
    const int32_t p1 = g.addNode({-0.01f, 0.f, 0.f}, 0.003f, 0);
    const int32_t n0 = g.addNode({ 0.00f, 0.f, 0.f}, 0.001f, 0);
    const int32_t n1 = g.addNode({ 0.01f, 0.f, 0.f}, 0.001f, 0);
    const int32_t n2 = g.addNode({ 0.02f, 0.f, 0.f}, 0.001f, 0);
    g.addLink(p2, p1);
    g.addLink(p1, n0);
    g.addLink(n0, n1);
    g.addLink(n1, n2);
    g.buildAdjacency();

    EmbolizationModel model;
    model.setGraph(&g);
    model.setAgent(EmbolicAgent::Onyx);
    model.begin(n0, /*excludeNeighbor=*/p1);

    const float capacity = model.forwardCapacity();
    std::printf("forward capacity: %.5f\n", capacity);

    // --- Phase 1: small injections, well under capacity - expect NO reflux ---
    bool refluxedEarly = false;
    for (int i = 0; i < 5; ++i) {
        model.inject(0.05f);
        if (model.refluxing()) refluxedEarly = true;
    }
    std::printf("[1] reflux after early small injections: %s  (expect NO)\n",
                refluxedEarly ? "YES" : "no");
    std::printf("    filled=%.5f / capacity=%.5f\n", model.state().filledArcLength, capacity);

    // --- Phase 2: keep injecting well past capacity - expect reflux to grow ---
    for (int i = 0; i < 60; ++i) model.inject(0.5f);
    const bool refluxedLate = model.refluxing();
    std::printf("[2] reflux after sustained overinjection: %s  (expect YES)\n",
                refluxedLate ? "yes" : "NO");
    std::printf("    filled=%.5f / capacity=%.5f   refluxArcLength=%.5f\n",
                model.state().filledArcLength, capacity, model.state().refluxArcLength);

    // Forward fill must never exceed its own real capacity, no matter how
    // much excess volume was pushed in.
    const bool forwardCapped = model.state().filledArcLength <= capacity + 1e-4f;
    std::printf("    forward fill respects capacity cap: %s\n", forwardCapped ? "yes" : "NO - BUG");

    // --- Phase 3: the excluded segment (n0-p1) should be the first, and by
    // now clearly-touched, reflux link. ---
    bool foundExcludedEdgeAsReflux = false;
    for (const auto &l : model.state().links) {
        if (!l.isReflux) continue;
        if ((l.from == n0 && l.to == p1) || (l.from == p1 && l.to == n0))
            foundExcludedEdgeAsReflux = true;
    }
    std::printf("[3] excluded segment (n0-p1) now claimed as reflux: %s  (expect YES)\n",
                foundExcludedEdgeAsReflux ? "yes" : "NO");

    // --- Phase 4: no excludeNeighbor given at all - excess should just cap,
    // never crash, never silently invent a reflux direction. ---
    EmbolizationModel noExclude;
    noExclude.setGraph(&g);
    noExclude.setAgent(EmbolicAgent::Onyx);
    noExclude.begin(n0);   // no excludeNeighbor
    for (int i = 0; i < 60; ++i) noExclude.inject(0.5f);
    const bool noExcludeSafe = !noExclude.refluxing() &&
                               noExclude.state().filledArcLength <= noExclude.forwardCapacity() + 1e-4f;
    std::printf("[4] no excludeNeighbor -> capped, no reflux, no crash: %s  (expect YES)\n",
                noExcludeSafe ? "yes" : "NO");

    const bool pass = !refluxedEarly && refluxedLate && forwardCapped &&
                      foundExcludedEdgeAsReflux && noExcludeSafe;
    std::printf("\n%s\n", pass ? "ALL CHECKS PASSED" : "CHECK FAILED");
    return pass ? 0 : 1;
}
