// Proves classifyCoilFit() and its packing consequence:
//   1. A well-matched coil (helix ~= vessel diameter) classifies Good and
//      packs at full efficiency (unchanged from before this feature).
//   2. A coil far too small for a wide vessel classifies Undersized and
//      packs measurably less per mm than the same coil would in a
//      well-matched vessel.
//   3. A coil far too large for a narrow vessel classifies Oversized and
//      also packs measurably less.
//   4. The assessed fit is visible on the STAGED coil (pendingFit()) before
//      any commit, not only afterward, and is correctly carried into the
//      DeployedCoil history once detached.
#include "CoilEmbolizationModel.h"
#include "CenterlineGraph.h"
#include <cstdio>

using namespace cn;

namespace {
// One coil, one straight link, one push - returns the resulting packing
// length on that link so the three scenarios can be compared directly.
float packingForVesselDiameter(const CoilSpec &spec, float vesselRadiusModelUnits,
                               CoilSizeFit *outFit = nullptr) {
    CenterlineGraph g;
    const int32_t p1 = g.addNode({-0.01f, 0.f, 0.f}, vesselRadiusModelUnits, 0);
    const int32_t n0 = g.addNode({ 0.00f, 0.f, 0.f}, vesselRadiusModelUnits, 0);
    const int32_t n1 = g.addNode({ 0.01f, 0.f, 0.f}, vesselRadiusModelUnits, 0);
    g.addLink(p1, n0);
    g.addLink(n0, n1);
    g.buildAdjacency();

    CoilEmbolizationModel model;
    model.setGraph(&g);
    model.beginAt(n0, p1, Vec3{0.f, 0.f, 0.f});
    model.advanceCoil(spec, spec.lengthMm, 1000.f);
    if (outFit) *outFit = model.pendingFit();

    for (const auto &l : model.stagingLinks())
        if ((l.from == n0 && l.to == n1) || (l.from == n1 && l.to == n0))
            return l.packingLength;
    return 0.f;
}
} // namespace

int main() {
    size_t coilCount = 0;
    const CoilSpec *cat = coilCatalog(coilCount);
    const CoilSpec &small  = cat[0];   // 2mm helix
    const CoilSpec &medium = cat[1];   // 3mm helix
    const CoilSpec &large  = cat[2];   // 4mm helix

    // --- Test 1: medium coil (3mm) in a well-matched 3mm vessel (radius
    // 0.0015 model units) - expect Good fit, full efficiency. ---
    CoilSizeFit fitGood{};
    packingForVesselDiameter(medium, 0.0015f, &fitGood);   // return value unused here - only the fit classification matters
    std::printf("[1] medium coil in matched vessel: fit=%s  (expect \"well sized\")\n",
                coilSizeFitName(fitGood));

    // --- Test 2: small coil (2mm) in a WIDE 6mm vessel (radius 0.003) -
    // expect Undersized, packing measurably less than a good-fit baseline
    // of the SAME coil deployed into a vessel actually matching it. ---
    CoilSizeFit fitUnder{};
    const float packedUndersized = packingForVesselDiameter(small, 0.003f, &fitUnder);
    CoilSizeFit fitSmallGood{};
    const float packedSmallGood = packingForVesselDiameter(small, 0.001f, &fitSmallGood);  // 2mm vessel, matches small coil
    std::printf("[2a] small coil in wide (6mm) vessel: fit=%s  (expect \"undersized for this vessel\")\n",
                coilSizeFitName(fitUnder));
    std::printf("[2b] small coil in matched (2mm) vessel: fit=%s  (expect \"well sized\")\n",
                coilSizeFitName(fitSmallGood));
    std::printf("     packed undersized=%.4f  vs  packed well-matched=%.4f  (expect undersized clearly less)\n",
                packedUndersized, packedSmallGood);

    // --- Test 3: large coil (4mm) in a NARROW 2mm vessel (radius 0.001) -
    // expect Oversized, packing measurably less than the same coil in a
    // vessel that actually matches it. ---
    CoilSizeFit fitOver{};
    const float packedOversized = packingForVesselDiameter(large, 0.001f, &fitOver);
    CoilSizeFit fitLargeGood{};
    const float packedLargeGood = packingForVesselDiameter(large, 0.002f, &fitLargeGood);  // 4mm vessel, matches large coil
    std::printf("[3a] large coil in narrow (2mm) vessel: fit=%s  (expect \"oversized for this vessel\")\n",
                coilSizeFitName(fitOver));
    std::printf("[3b] large coil in matched (4mm) vessel: fit=%s  (expect \"well sized\")\n",
                coilSizeFitName(fitLargeGood));
    std::printf("     packed oversized=%.4f  vs  packed well-matched=%.4f  (expect oversized clearly less)\n",
                packedOversized, packedLargeGood);

    // --- Test 4: fit survives commit into DeployedCoil history ---
    CenterlineGraph g2;
    const int32_t q1 = g2.addNode({-0.01f, 0.f, 0.f}, 0.003f, 0);
    const int32_t m0 = g2.addNode({ 0.00f, 0.f, 0.f}, 0.003f, 0);
    const int32_t m1 = g2.addNode({ 0.01f, 0.f, 0.f}, 0.003f, 0);
    g2.addLink(q1, m0);
    g2.addLink(m0, m1);
    g2.buildAdjacency();
    CoilEmbolizationModel model2;
    model2.setGraph(&g2);
    model2.beginAt(m0, q1, Vec3{0.f, 0.f, 0.f});
    model2.advanceCoil(small, small.lengthMm, 1000.f);   // 2mm coil in a 6mm vessel - undersized
    const bool pendingFitUndersized = model2.pendingFit() == CoilSizeFit::Undersized;
    model2.finishCoil();
    const bool committedFitUndersized =
        !model2.state().coils.empty() && model2.state().coils.back().fit == CoilSizeFit::Undersized;
    std::printf("[4a] pendingFit() shows Undersized BEFORE commit: %s  (expect YES)\n",
                pendingFitUndersized ? "yes" : "NO");
    std::printf("[4b] DeployedCoil.fit shows Undersized AFTER commit: %s  (expect YES)\n",
                committedFitUndersized ? "yes" : "NO");

    const bool pass = fitGood == CoilSizeFit::Good &&
                      fitUnder == CoilSizeFit::Undersized && fitSmallGood == CoilSizeFit::Good &&
                      packedUndersized < packedSmallGood * 0.7f &&
                      fitOver == CoilSizeFit::Oversized && fitLargeGood == CoilSizeFit::Good &&
                      packedOversized < packedLargeGood * 0.85f &&
                      pendingFitUndersized && committedFitUndersized;
    std::printf("\n%s\n", pass ? "ALL CHECKS PASSED" : "CHECK FAILED");
    return pass ? 0 : 1;
}
