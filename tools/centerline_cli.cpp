// Head-less harness for the geometry + navigation core. Builds without Qt:
//   ./build.sh --core-only
//   ./centerline_cli data/simplified_branches_.stl
//
// Exercises the same code path the GUI uses: extract -> resolve access sites ->
// start from one -> advance -> answer bifurcations -> retract past a junction to
// confirm the choice re-opens.

#include "AccessSites.h"
#include "CatheterModel.h"
#include "CenterlineExtractor.h"
#include "NavigationController.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

using namespace cn;

namespace {

void printOptions(const NavigationController &nav, double mmPerUnit) {
    const auto &opts = nav.options();
    for (size_t i = 0; i < opts.size(); ++i) {
        const char *key = (i == 0) ? "LEFT " : (i + 1 == opts.size() ? "RIGHT" : "  -  ");
        std::printf("      [%s] node %-4d  takeoff %5.1f deg  lumen %.2f mm  screenX %+.3f\n",
                    key, opts[i].node, opts[i].takeoffAngleDeg,
                    2.0 * opts[i].radius * mmPerUnit, opts[i].lateralScore);
    }
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <mesh.stl> [--dump prefix]\n", argv[0]);
        return 1;
    }
    const double mmPerUnit = 100.0;

    StlMesh mesh;
    std::string err;
    if (!mesh.load(argv[1], &err)) {
        std::fprintf(stderr, "load failed: %s\n", err.c_str());
        return 2;
    }

    const Box3 b = mesh.bounds();
    std::printf("MESH\n");
    std::printf("  triangles %zu   welded vertices %zu   shells %d   boundary loops %d\n",
                mesh.triangles().size(), mesh.vertices().size(),
                mesh.componentCount(), mesh.boundaryLoopCount());
    std::printf("  bbox [%.4f %.4f %.4f] .. [%.4f %.4f %.4f]\n",
                b.lo.x, b.lo.y, b.lo.z, b.hi.x, b.hi.y, b.hi.z);

    CenterlineExtractor extractor;
    CenterlineGraph graph;
    CenterlineExtractor::Report rep;
    if (!extractor.extract(mesh, graph, &rep)) {
        std::fprintf(stderr, "extraction failed\n");
        return 3;
    }

    std::printf("\nCENTERLINE\n");
    std::printf("  step %.5f   nodes %d   links %d   stitched %d\n",
                rep.effectiveSampleStep, rep.nodes, rep.links, rep.stitchedJoints);
    std::printf("  bifurcations %d   terminals %d   total length %.1f mm\n",
                rep.bifurcations, rep.terminals, rep.totalCenterlineLength * mmPerUnit);
    std::printf("  lumen diameter %.2f - %.2f mm\n",
                2.0 * rep.minRadius * mmPerUnit, 2.0 * rep.maxRadius * mmPerUnit);

    std::map<int, int> degHist;
    for (int32_t i = 0; i < static_cast<int32_t>(graph.nodeCount()); ++i)
        ++degHist[graph.degree(i)];
    std::printf("  degree histogram ");
    for (auto [d, n] : degHist) std::printf(" %d:%d", d, n);
    std::printf("\n");

    // ---------------------------------------------------------------- access
    const auto sites = AccessSiteResolver::resolve(graph, /*patientLeftIsPlusX=*/true);
    std::printf("\nACCESS SITES\n");
    for (const auto &s : sites) {
        if (s.valid) {
            const auto &n = graph.nodes()[s.node];
            std::printf("  %-14s node %-4d  (%.3f %.3f %.3f)  vessel %.2f mm  [%s]\n",
                        accessSiteName(s.site), s.node, n.position.x, n.position.y,
                        n.position.z, s.vesselDiameter * mmPerUnit, s.note.c_str());
        } else {
            std::printf("  %-14s UNAVAILABLE  [%s]\n", accessSiteName(s.site), s.note.c_str());
        }
    }

    // ------------------------------------------------------------ navigation
    const int32_t start = AccessSiteResolver::nodeFor(sites, AccessSite::RightFemoral);
    if (start < 0) { std::printf("\nno right femoral access on this model\n"); return 0; }

    graph.labelBranches(start);

    NavigationController nav;
    nav.setGraph(&graph);
    // Camera looking at the front of the patient: screen right = +X, up = +Z.
    nav.setViewFrame(Vec3(1, 0, 0), Vec3(0, 0, 1));
    nav.startFrom(start);

    CatheterModel::Params cp;
    cp.deviceRadius = static_cast<float>(0.5 * 1.667 / mmPerUnit);   // 5F
    cp.segmentLength = static_cast<float>(1.5 / mmPerUnit);
    cp.segmentCount = 45;
    cp.tipFlexSegments = 6;
    CatheterModel cath(cp);
    cath.setGraph(&graph);
    cath.setPath(nav.path());

    const float stepMm = 10.f;
    const float step = static_cast<float>(stepMm / mmPerUnit);

    std::printf("\nSTEERED RUN from %s  (Page Up = %.0f mm per press)\n",
                accessSiteName(AccessSite::RightFemoral), stepMm);

    int presses = 0, turns = 0;
    bool goLeft = true;
    for (int guard = 0; guard < 400; ++guard) {
        const float moved = nav.advance(step);
        ++presses;

        if (moved < step * 0.5f) {
            if (nav.decisionPending()) {
                std::printf("  [%3d presses, %6.1f mm]  BIFURCATION at node %d, %zu options:\n",
                            presses, nav.depth() * mmPerUnit, nav.pendingJunctionNode(),
                            nav.options().size());
                printOptions(nav, mmPerUnit);
                // Alternate turns so the test walks a non-trivial route.
                const bool ok = goLeft ? nav.chooseLeft() : nav.chooseRight();
                std::printf("      -> pressed %s  (route now %.1f mm)\n",
                            goLeft ? "LEFT arrow" : "RIGHT arrow",
                            nav.navigableLength() * mmPerUnit);
                goLeft = !goLeft;
                ++turns;
                if (!ok) break;
                cath.setPath(nav.path(), /*keepDepth=*/true);
                continue;
            }
            if (nav.atDeadEnd()) {
                std::printf("  [%3d presses, %6.1f mm]  DEAD END - distal vessel reached\n",
                            presses, nav.depth() * mmPerUnit);
                break;
            }
        }
        cath.setPath(nav.path(), true);
        cath.setInsertionDepth(nav.depth());
        cath.update();
    }

    const auto &st = cath.state();
    std::printf("\n  final: depth %.1f mm, %d turns, branch %d, lumen at tip %.2f mm, "
                "clearance %+.3f mm, %s\n",
                st.insertionDepth * mmPerUnit, turns, st.currentBranchId,
                2.0 * st.lumenRadiusAtTip * mmPerUnit, st.minWallClearance * mmPerUnit,
                st.wallContact ? "WALL CONTACT" : "tracking");

    // -------------------------------------------------- retract past a turn
    std::printf("\nRETRACT TEST (Page Down)\n");
    const size_t turnsBefore = nav.turnsTaken();
    std::printf("  turns committed before: %zu\n", turnsBefore);
    for (int i = 0; i < 400 && nav.turnsTaken() == turnsBefore; ++i) nav.retract(step);
    std::printf("  after pulling back to %.1f mm: turns committed %zu, decision pending %s\n",
                nav.depth() * mmPerUnit, nav.turnsTaken(),
                nav.decisionPending() ? "YES (re-selectable)" : "no");
    if (nav.decisionPending()) printOptions(nav, mmPerUnit);

    // ------------------------------------------------- view-relative L/R check
    std::printf("\nVIEW-RELATIVE ORDERING\n");
    if (nav.decisionPending() && nav.options().size() >= 2) {
        const int32_t leftFromFront = nav.options().front().node;
        nav.setViewFrame(Vec3(-1, 0, 0), Vec3(0, 0, 1));   // camera swung 180 deg
        const int32_t leftFromBack = nav.options().front().node;
        std::printf("  LEFT arrow from front view -> node %d\n", leftFromFront);
        std::printf("  LEFT arrow from rear view  -> node %d  %s\n", leftFromBack,
                    leftFromBack != leftFromFront ? "(swapped, as expected)"
                                                  : "(unchanged - branches are edge-on)");
        nav.setViewFrame(Vec3(1, 0, 0), Vec3(0, 0, 1));
    }

    if (argc >= 4 && std::strcmp(argv[2], "--dump") == 0) {
        const std::string prefix = argv[3];
        std::ofstream csv(prefix + "_centerline.csv");
        csv << "node,x,y,z,radius,degree,branch,shell\n";
        for (int32_t i = 0; i < static_cast<int32_t>(graph.nodeCount()); ++i) {
            const auto &n = graph.nodes()[i];
            csv << i << ',' << n.position.x << ',' << n.position.y << ',' << n.position.z
                << ',' << n.radius << ',' << graph.degree(i) << ',' << n.branchId << ','
                << n.meshComponent << '\n';
        }
        std::ofstream obj(prefix + "_centerline.obj");
        for (const auto &n : graph.nodes())
            obj << "v " << n.position.x << ' ' << n.position.y << ' ' << n.position.z << '\n';
        for (const auto &l : graph.links()) obj << "l " << l.from + 1 << ' ' << l.to + 1 << '\n';
        std::printf("\nwrote %s_centerline.csv / .obj\n", prefix.c_str());
    }
    return 0;
}
