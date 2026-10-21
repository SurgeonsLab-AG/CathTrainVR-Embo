// Proves the staged-vs-committed deployment model:
//   1. advanceCoil() alone (no finishCoil) does NOT occlude anything and
//      does NOT appear in state().links - it's staged, not committed.
//   2. retractCoil() discards it completely - hasPendingCoil() clears, and
//      a subsequent commit history stays exactly as it was before the
//      retracted attempt (as if it had never happened).
//   3. finishCoil() after advanceCoil() DOES commit into state().links and
//      state().coils, exactly as before.
//   4. beginAt() on a fresh coil implicitly abandons a forgotten pending
//      one from before, without disturbing already-committed coils.
#include "CoilEmbolizationModel.h"
#include "CenterlineGraph.h"
#include <cstdio>

using namespace cn;

int main() {
    CenterlineGraph g;
    const int32_t p1 = g.addNode({-0.01f, 0.f, 0.f}, 0.002f, 0);
    const int32_t n0 = g.addNode({ 0.00f, 0.f, 0.f}, 0.001f, 0);
    const int32_t n1 = g.addNode({ 0.01f, 0.f, 0.f}, 0.001f, 0);
    g.addLink(p1, n0);
    g.addLink(n0, n1);
    g.buildAdjacency();

    size_t coilCount = 0;
    const CoilSpec *cat = coilCatalog(coilCount);
    const CoilSpec &small = cat[0];

    CoilEmbolizationModel model;
    model.setGraph(&g);

    // --- Test 1: advanceCoil() alone is staged only ---
    model.beginAt(n0, p1, Vec3{0.f, 0.f, 0.f});
    model.advanceCoil(small, small.lengthMm, 1000.f);
    const bool pendingAfterAdvance = model.hasPendingCoil();
    const bool committedEmptyAfterAdvance = model.state().links.empty();
    const bool stagingNonEmptyAfterAdvance = !model.stagingLinks().empty();
    const bool occludesBeforeCommit = model.linkOccluded(n0, n1);
    std::printf("[1a] hasPendingCoil() after advance only: %s  (expect YES)\n",
                pendingAfterAdvance ? "yes" : "NO");
    std::printf("[1b] state().links empty (nothing committed yet): %s  (expect YES)\n",
                committedEmptyAfterAdvance ? "yes" : "NO");
    std::printf("[1c] stagingLinks() non-empty (something IS staged): %s  (expect YES)\n",
                stagingNonEmptyAfterAdvance ? "yes" : "NO");
    std::printf("[1d] linkOccluded() before commit: %s  (expect NO - staged material cannot occlude)\n",
                occludesBeforeCommit ? "YES" : "no");

    // --- Test 2: retractCoil() discards it completely ---
    model.retractCoil();
    const bool pendingAfterRetract = model.hasPendingCoil();
    const bool stagingClearedAfterRetract = model.stagingLinks().empty();
    const bool historyStillEmpty = model.state().coils.empty();
    std::printf("[2a] hasPendingCoil() after retract: %s  (expect NO)\n",
                pendingAfterRetract ? "YES" : "no");
    std::printf("[2b] stagingLinks() cleared after retract: %s  (expect YES)\n",
                stagingClearedAfterRetract ? "yes" : "NO");
    std::printf("[2c] deployment history still empty (retract left no trace): %s  (expect YES)\n",
                historyStillEmpty ? "yes" : "NO");

    // --- Test 3: advance then finish DOES commit ---
    model.beginAt(n0, p1, Vec3{0.f, 0.f, 0.f});
    model.advanceCoil(small, small.lengthMm, 1000.f);
    model.finishCoil();
    const bool committedAfterFinish = !model.state().links.empty();
    const bool historyOneAfterFinish = model.state().coils.size() == 1;
    const bool pendingClearedAfterFinish = !model.hasPendingCoil();
    std::printf("[3a] state().links non-empty after finish: %s  (expect YES)\n",
                committedAfterFinish ? "yes" : "NO");
    std::printf("[3b] deployment history has exactly 1 coil: %s  (expect YES)\n",
                historyOneAfterFinish ? "yes" : "NO");
    std::printf("[3c] hasPendingCoil() cleared after finish: %s  (expect YES)\n",
                pendingClearedAfterFinish ? "yes" : "NO");

    // --- Test 4: beginAt() abandons a forgotten pending coil without
    // touching already-committed history ---
    model.beginAt(n0, p1, Vec3{0.f, 0.f, 0.f});
    model.advanceCoil(small, small.lengthMm, 1000.f);   // staged, never finished or retracted
    model.beginAt(n0, p1, Vec3{0.f, 0.f, 0.f});          // fresh begin - should abandon the above
    const bool abandonedStaging = model.stagingLinks().empty();
    const bool historyStillOne = model.state().coils.size() == 1;   // unchanged from test 3
    std::printf("[4a] forgotten pending coil abandoned by fresh beginAt(): %s  (expect YES)\n",
                abandonedStaging ? "yes" : "NO");
    std::printf("[4b] previously committed history untouched (still 1): %s  (expect YES)\n",
                historyStillOne ? "yes" : "NO");

    const bool pass = pendingAfterAdvance && committedEmptyAfterAdvance && stagingNonEmptyAfterAdvance &&
                      !occludesBeforeCommit && !pendingAfterRetract && stagingClearedAfterRetract &&
                      historyStillEmpty && committedAfterFinish && historyOneAfterFinish &&
                      pendingClearedAfterFinish && abandonedStaging && historyStillOne;
    std::printf("\n%s\n", pass ? "ALL CHECKS PASSED" : "CHECK FAILED");
    return pass ? 0 : 1;
}
