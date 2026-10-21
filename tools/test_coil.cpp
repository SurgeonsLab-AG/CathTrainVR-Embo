// Quick correctness check, same spirit as tools/centerline_cli.cpp:
// build a tiny synthetic graph by hand (a straight run into a bifurcation)
// and verify the coil model's three defining behaviors:
//   1. A single coil does NOT fully occlude a normal segment (needs a nest).
//   2. Stacking a second/third coil at the same site DOES cross fillFraction 1.0.
//   3. The coil walk stops dead at a bifurcation rather than fanning out.
#include "CoilEmbolizationModel.h"
#include "CenterlineGraph.h"
#include <cstdio>

using namespace cn;

int main() {
    CenterlineGraph g;
    // 0 -- 1 -- 2 -- 3 (straight run, 10mm segments in a 1000 mmPerUnit model)
    //           |
    //           4  (branch off node 2)
    const int32_t n0 = g.addNode({0.f, 0.f, 0.f}, 0.0015f, 0);
    const int32_t n1 = g.addNode({0.01f, 0.f, 0.f}, 0.0015f, 0);
    const int32_t n2 = g.addNode({0.02f, 0.f, 0.f}, 0.0015f, 0);
    const int32_t n3 = g.addNode({0.03f, 0.f, 0.f}, 0.0015f, 0);
    const int32_t n4 = g.addNode({0.02f, 0.01f, 0.f}, 0.0015f, 0);
    g.addLink(n0, n1);
    g.addLink(n1, n2);
    g.addLink(n2, n3);
    g.addLink(n2, n4);
    g.buildAdjacency();

    const float mmPerUnit = 1000.f;   // matches VtpCenterlineLoader's own convention

    size_t coilCount = 0;
    const CoilSpec *cat = coilCatalog(coilCount);
    const CoilSpec &small = cat[0];   // Target Nano 2mm x 4cm

    CoilEmbolizationModel coils;
    coils.setGraph(&g);

    // --- Test 1: one coil released at n1, excluding n0 (catheter's own
    // shaft), should NOT fully occlude the n1-n2 link on its own. ---
    coils.deployWholeCoil(small, n1, /*excludeNeighbor=*/n0, mmPerUnit);
    const bool oneCoilOccludes = coils.linkOccluded(n1, n2);
    std::printf("[1] single coil occludes n1-n2 link: %s  (expect NO)\n",
                oneCoilOccludes ? "YES" : "no");

    // --- Test 2: add two more coils at the SAME site - nest should now
    // cross full occlusion. ---
    coils.deployWholeCoil(small, n1, n0, mmPerUnit);
    coils.deployWholeCoil(small, n1, n0, mmPerUnit);
    const bool nestOccludes = coils.linkOccluded(n1, n2);
    std::printf("[2] three-coil nest occludes n1-n2 link: %s  (expect YES)\n",
                nestOccludes ? "yes" : "NO");
    std::printf("    deployed coils in history: %zu\n", coils.state().coils.size());

    // --- Test 3: release AT the bifurcation node n2 - walk must stop
    // immediately (two branches, n3 and n4) rather than picking one. ---
    CoilEmbolizationModel atJunction;
    atJunction.setGraph(&g);
    atJunction.deployWholeCoil(small, n2, n1, mmPerUnit);
    bool touchedN3 = false, touchedN4 = false;
    for (const auto &l : atJunction.state().links) {
        if ((l.from == n2 && l.to == n3) || (l.from == n3 && l.to == n2)) touchedN3 = true;
        if ((l.from == n2 && l.to == n4) || (l.from == n4 && l.to == n2)) touchedN4 = true;
    }
    std::printf("[3] coil released at bifurcation touches any link: %s  (expect NO - stops immediately)\n",
                (touchedN3 || touchedN4) ? "YES" : "no");

    const bool pass = !oneCoilOccludes && nestOccludes && !touchedN3 && !touchedN4;
    std::printf("\n%s\n", pass ? "ALL CHECKS PASSED" : "CHECK FAILED");
    return pass ? 0 : 1;
}
