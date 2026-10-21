// Proves undoLastCoil() - the "undo the coil I just committed" reversal a
// simple one-click Advance/Retract workflow needs (as opposed to
// retractCoil(), which only undoes something not yet committed):
//   1. Undoing the only coil placed removes it completely - occlusion
//      clears, history empties, exactly as if it never happened.
//   2. Undoing the LAST of several nested coils at the same site correctly
//      subtracts only that one's contribution - the earlier coils' own
//      packing survives untouched.
//   3. undoLastCoil() on an empty history is a safe no-op (returns false).
#include "CoilEmbolizationModel.h"
#include "CenterlineGraph.h"
#include <cstdio>

using namespace cn;

int main() {
    CenterlineGraph g;
    const int32_t p1 = g.addNode({-0.01f, 0.f, 0.f}, 0.0015f, 0);
    const int32_t n0 = g.addNode({ 0.00f, 0.f, 0.f}, 0.0015f, 0);
    const int32_t n1 = g.addNode({ 0.01f, 0.f, 0.f}, 0.0015f, 0);
    g.addLink(p1, n0);
    g.addLink(n0, n1);
    g.buildAdjacency();

    size_t coilCount = 0;
    const CoilSpec *cat = coilCatalog(coilCount);
    const CoilSpec &small = cat[0];

    CoilEmbolizationModel model;
    model.setGraph(&g);

    // --- Test 1: single coil, undo removes it completely ---
    model.deployWholeCoil(small, n0, p1, 1000.f, Vec3{0.f, 0.f, 0.f});
    const bool occludesBefore = model.linkOccluded(n0, n1);   // likely not fully occluded by one coil, just checking it exists
    const bool hasLinksBefore = !model.state().links.empty();
    const bool undoOk1 = model.undoLastCoil();
    const bool linksEmptyAfter = model.state().links.empty();
    const bool historyEmptyAfter = model.state().coils.empty();
    std::printf("[1a] has links after one coil: %s  (expect YES)\n", hasLinksBefore ? "yes" : "NO");
    std::printf("[1b] undoLastCoil() succeeded: %s  (expect YES)\n", undoOk1 ? "yes" : "NO");
    std::printf("[1c] links empty after undo: %s  (expect YES)\n", linksEmptyAfter ? "yes" : "NO");
    std::printf("[1d] history empty after undo: %s  (expect YES)\n", historyEmptyAfter ? "yes" : "NO");
    (void)occludesBefore;

    // --- Test 2: three nested coils, undo the LAST one only - the first
    // two's packing should survive intact ---
    model.deployWholeCoil(small, n0, p1, 1000.f, Vec3{0.f, 0.f, 0.f});
    model.deployWholeCoil(small, n0, p1, 1000.f, Vec3{0.f, 0.f, 0.f});
    float packingAfterTwo = 0.f;
    for (const auto &l : model.state().links)
        if ((l.from == n0 && l.to == n1) || (l.from == n1 && l.to == n0))
            packingAfterTwo = l.packingLength;

    model.deployWholeCoil(small, n0, p1, 1000.f, Vec3{0.f, 0.f, 0.f});   // third coil
    const size_t historyAfterThree = model.state().coils.size();

    model.undoLastCoil();   // undo the THIRD one only
    float packingAfterUndo = 0.f;
    for (const auto &l : model.state().links)
        if ((l.from == n0 && l.to == n1) || (l.from == n1 && l.to == n0))
            packingAfterUndo = l.packingLength;
    const size_t historyAfterUndo = model.state().coils.size();

    const bool packingMatchesFirstTwo = std::abs(packingAfterUndo - packingAfterTwo) < 1e-4f;
    const bool historyDroppedByOne = historyAfterUndo == historyAfterThree - 1;
    std::printf("[2a] history had 3 coils before undo: %s  (expect YES)\n",
                historyAfterThree == 3 ? "yes" : "NO");
    std::printf("[2b] packing after undoing 3rd matches packing after only 2: %s  (expect YES, %.4f vs %.4f)\n",
                packingMatchesFirstTwo ? "yes" : "NO", packingAfterUndo, packingAfterTwo);
    std::printf("[2c] history dropped by exactly one: %s  (expect YES, %zu -> %zu)\n",
                historyDroppedByOne ? "yes" : "NO", historyAfterThree, historyAfterUndo);

    // --- Test 3: undo on empty history is a safe no-op ---
    model.reset();
    const bool undoEmptyFails = !model.undoLastCoil();
    std::printf("[3] undoLastCoil() on empty history returns false: %s  (expect YES)\n",
                undoEmptyFails ? "yes" : "NO");

    const bool pass = hasLinksBefore && undoOk1 && linksEmptyAfter && historyEmptyAfter &&
                      historyAfterThree == 3 && packingMatchesFirstTwo && historyDroppedByOne &&
                      undoEmptyFails;
    std::printf("\n%s\n", pass ? "ALL CHECKS PASSED" : "CHECK FAILED");
    return pass ? 0 : 1;
}
