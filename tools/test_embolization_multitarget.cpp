// Proves EmbolizationModel's multi-target isolation:
//   1. A fresh model behaves exactly as a single-target model always did -
//      addTarget()/setActiveTarget() are never required for existing usage.
//   2. Two independent targets, injected with DIFFERENT agents, keep
//      completely separate cast state - injecting into target B does not
//      change target A's own fill/volume/links at all.
//   3. reset() clears only the ACTIVE target; the other survives untouched.
//   4. resetAll() genuinely starts over: back to exactly one fresh target.
#include "EmbolizationModel.h"
#include "CenterlineGraph.h"
#include <cstdio>

using namespace cn;

int main() {
    CenterlineGraph g;
    const int32_t trunk    = g.addNode({0.f, 0.f, 0.f}, 0.002f, 0);
    const int32_t branchA1 = g.addNode({0.01f, 0.f, 0.f}, 0.001f, 0);
    const int32_t branchA2 = g.addNode({0.02f, 0.f, 0.f}, 0.001f, 0);
    const int32_t branchB1 = g.addNode({-0.01f, 0.f, 0.f}, 0.001f, 0);
    const int32_t branchB2 = g.addNode({-0.02f, 0.f, 0.f}, 0.001f, 0);
    g.addLink(trunk, branchA1);
    g.addLink(branchA1, branchA2);
    g.addLink(trunk, branchB1);
    g.addLink(branchB1, branchB2);
    g.buildAdjacency();

    EmbolizationModel model;
    model.setGraph(&g);
    model.setAgent(EmbolicAgent::Onyx);
    model.begin(branchA1, trunk);
    model.inject(5.f);
    const bool singleTargetWorks = model.active() && model.state().filledArcLength > 0.f;
    const bool onlyOneTargetSoFar = model.targetCount() == 1;
    std::printf("[1a] single-target usage still works with no addTarget() call: %s\n",
                singleTargetWorks ? "yes" : "NO");
    std::printf("[1b] targetCount() is 1 before any addTarget(): %s\n",
                onlyOneTargetSoFar ? "yes" : "NO");

    const float targetAFillBefore = model.state().filledArcLength;
    const float targetAVolBefore  = model.state().volumeInjected;

    const int idxB = model.addTarget("Posterior division");
    model.setAgent(EmbolicAgent::Glue);
    model.begin(branchB1, trunk);
    model.inject(5.f);

    const bool targetAUnchanged =
        model.stateAt(0).filledArcLength == targetAFillBefore &&
        model.stateAt(0).volumeInjected == targetAVolBefore;
    const bool targetBHasOwnCast = model.stateAt(idxB).filledArcLength > 0.f;
    const bool agentsIndependent =
        model.agentAt(0) == EmbolicAgent::Onyx && model.agentAt(idxB) == EmbolicAgent::Glue;
    const bool nameStored = model.targetName(idxB) == "Posterior division";

    std::printf("[2a] target A completely unchanged after injecting into target B: %s\n",
                targetAUnchanged ? "yes" : "NO");
    std::printf("[2b] target B has its own real cast: %s\n", targetBHasOwnCast ? "yes" : "NO");
    std::printf("[2c] each target kept its OWN agent independently: %s\n",
                agentsIndependent ? "yes" : "NO");
    std::printf("[2d] target name stored correctly: %s\n", nameStored ? "yes" : "NO");

    model.reset();
    const bool targetBCleared = !model.stateAt(idxB).active;
    const bool targetAStillIntact =
        model.stateAt(0).filledArcLength == targetAFillBefore && model.stateAt(0).active;
    std::printf("[3a] reset() cleared the active target (B): %s\n", targetBCleared ? "yes" : "NO");
    std::printf("[3b] reset() left target A completely untouched: %s\n",
                targetAStillIntact ? "yes" : "NO");

    model.resetAll();
    const bool backToOneTarget = model.targetCount() == 1;
    const bool freshTargetInactive = !model.active();
    std::printf("[4a] resetAll() collapses back to exactly one target: %s\n",
                backToOneTarget ? "yes" : "NO");
    std::printf("[4b] that one target is fresh/inactive: %s\n",
                freshTargetInactive ? "yes" : "NO");

    const bool pass = singleTargetWorks && onlyOneTargetSoFar && targetAUnchanged &&
                      targetBHasOwnCast && agentsIndependent && nameStored &&
                      targetBCleared && targetAStillIntact && backToOneTarget && freshTargetInactive;
    std::printf("\n%s\n", pass ? "ALL CHECKS PASSED" : "CHECK FAILED");
    return pass ? 0 : 1;
}
