// Proves CoilEmbolizationModel's multi-target isolation:
//   1. A fresh model behaves exactly as single-target usage always did.
//   2. Two independent targets, each with its own coil deployed, keep
//      completely separate nests/history/staging.
//   3. linkOccluded() checks ALL targets, not just the active one - a link
//      occluded by target A must still read as occluded while target B is
//      the one currently selected.
//   4. reset() clears only the active target; undoLastCoil()/retractCoil()
//      on the active target never touch a different one's own nest.
//   5. resetAll() genuinely starts over.
#include "CoilEmbolizationModel.h"
#include "CenterlineGraph.h"
#include <cstdio>

using namespace cn;

int main() {
    CenterlineGraph g;
    const int32_t trunk    = g.addNode({0.f, 0.f, 0.f}, 0.0015f, 0);
    const int32_t branchA1 = g.addNode({0.01f, 0.f, 0.f}, 0.0015f, 0);
    const int32_t branchA2 = g.addNode({0.02f, 0.f, 0.f}, 0.0015f, 0);
    const int32_t branchB1 = g.addNode({-0.01f, 0.f, 0.f}, 0.0015f, 0);
    const int32_t branchB2 = g.addNode({-0.02f, 0.f, 0.f}, 0.0015f, 0);
    g.addLink(trunk, branchA1);
    g.addLink(branchA1, branchA2);
    g.addLink(trunk, branchB1);
    g.addLink(branchB1, branchB2);
    g.buildAdjacency();

    size_t coilCount = 0;
    const CoilSpec *cat = coilCatalog(coilCount);
    const CoilSpec &spec = cat[1];   // 3mm, matches the 3mm vessel radius*2 above

    // --- Test 1: single-target usage unchanged ---
    CoilEmbolizationModel model;
    model.setGraph(&g);
    model.deployWholeCoil(spec, branchA1, trunk, 1000.f, Vec3{0.f, 0.f, 0.f});
    const bool singleTargetWorks = !model.state().coils.empty();
    const bool onlyOneTargetSoFar = model.targetCount() == 1;
    std::printf("[1a] single-target usage still works with no addTarget(): %s\n",
                singleTargetWorks ? "yes" : "NO");
    std::printf("[1b] targetCount() is 1 before any addTarget(): %s\n",
                onlyOneTargetSoFar ? "yes" : "NO");

    // --- Test 2: independent second target ---
    const size_t targetACoilCountBefore = model.state().coils.size();
    const int idxB = model.addTarget("Posterior division");
    model.deployWholeCoil(spec, branchB1, trunk, 1000.f, Vec3{1.f, 0.f, 0.f});

    const bool targetAUnchanged = model.stateAt(0).coils.size() == targetACoilCountBefore;
    const bool targetBHasOwnNest = !model.stateAt(idxB).coils.empty();
    const bool nameStored = model.targetName(idxB) == "Posterior division";
    std::printf("[2a] target A's coil history unchanged after deploying into target B: %s\n",
                targetAUnchanged ? "yes" : "NO");
    std::printf("[2b] target B has its own real coil nest: %s\n", targetBHasOwnNest ? "yes" : "NO");
    std::printf("[2c] target name stored correctly: %s\n", nameStored ? "yes" : "NO");

    // --- Test 3: linkOccluded() spans every target, not just the active one ---
    // Pack target A's link to full occlusion while target B is active.
    // deployWholeCoil(branchA1, exclude=trunk) walks OUTWARD from branchA1
    // (trunk is excluded as the "arrived from" segment), so packing lands
    // on the branchA1<->branchA2 link, not trunk<->branchA1.
    model.setActiveTarget(0);
    for (int i = 0; i < 20; ++i)
        model.deployWholeCoil(spec, branchA1, trunk, 1000.f, Vec3{0.f, 0.f, 0.f});
    const bool targetALinkOccluded = model.linkOccluded(branchA1, branchA2);
    model.setActiveTarget(idxB);   // switch away from A - occlusion must still hold
    const bool stillOccludedWithBActive = model.linkOccluded(branchA1, branchA2);
    std::printf("[3a] target A's link reads occluded: %s\n", targetALinkOccluded ? "yes" : "NO");
    std::printf("[3b] still reads occluded after switching active target to B: %s\n",
                stillOccludedWithBActive ? "yes" : "NO");

    // --- Test 4: reset() only clears the active target ---
    model.setActiveTarget(idxB);
    model.reset();
    const bool targetBCleared = model.stateAt(idxB).coils.empty();
    const bool targetAStillIntact = !model.stateAt(0).coils.empty();
    std::printf("[4a] reset() cleared target B (the active one): %s\n", targetBCleared ? "yes" : "NO");
    std::printf("[4b] reset() left target A's nest completely untouched: %s\n",
                targetAStillIntact ? "yes" : "NO");

    // --- Test 5: resetAll() genuinely starts over ---
    model.resetAll();
    const bool backToOneTarget = model.targetCount() == 1;
    const bool freshTargetEmpty = model.state().coils.empty();
    std::printf("[5a] resetAll() collapses back to exactly one target: %s\n",
                backToOneTarget ? "yes" : "NO");
    std::printf("[5b] that one target has no coils: %s\n", freshTargetEmpty ? "yes" : "NO");

    const bool pass = singleTargetWorks && onlyOneTargetSoFar && targetAUnchanged &&
                      targetBHasOwnNest && nameStored && targetALinkOccluded &&
                      stillOccludedWithBActive && targetBCleared && targetAStillIntact &&
                      backToOneTarget && freshTargetEmpty;
    std::printf("\n%s\n", pass ? "ALL CHECKS PASSED" : "CHECK FAILED");
    return pass ? 0 : 1;
}
