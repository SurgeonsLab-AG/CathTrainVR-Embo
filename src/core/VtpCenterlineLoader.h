#pragma once
#include "CenterlineGraph.h"
#include <string>

namespace cn {

// ─────────────────────────────────────────────────────────────────────────────
// VtpCenterlineLoader
//
// Loads a pre-computed VMTK centerline VTP file directly into a CenterlineGraph,
// bypassing the geodesic extraction algorithm (which is designed for STL inputs).
//
// Verified against FullVasculature_Centerlines.vtp:
//   • 192,192 points, 1,333 lines (branches), 442 bifurcations
//   • Coordinate system: Z-up, units = METRES  → mmPerUnit = 1000
//   • Radius array: MaximumInscribedSphereRadius (Float64, metres)
//   • Branch connectivity: ParentBranchId cell array
//
// The VTP branch/connectivity is mapped to CenterlineGraph nodes+links:
//   - Each VTP line cell = one centerline branch
//   - Each point on the line = one graph Node (position + radius)
//   - Adjacent points on the same line = graph Links
//   - Branch-to-parent connections from ParentBranchId create bifurcation links
//
// Access sites (confirmed from data, all Z in mm):
//   Right femoral: branch 295, Z≈172-200mm,  X>0, Ø≈4.0mm
//   Left femoral:  branch 292, Z≈172-200mm,  X<0, Ø≈4.0mm
//   Right radial:  branch 991, Z≈1409-1414mm, X>0, Ø≈6.2mm
//   Left radial:   branch 1142, Z≈1409-1413mm, X<0, Ø≈6.6mm
// ─────────────────────────────────────────────────────────────────────────────
class VtpCenterlineLoader {
public:
    struct Params {
        // Scale factor: VTP units → model units used by CenterlineGraph.
        // VTP files are in metres; EmboNav expects the same units as the STL.
        // Vessel_Reconstructed.stl spans Z=[0.0117, 1.7006] → also metres.
        // So mmPerUnit is 1000 (1 model unit = 1 metre = 1000 mm),
        // but no scale conversion is needed when loading into the graph since
        // both STL and VTP share the same coordinate frame.
        float vtpToModelScale = 1.0f;   // 1.0 = no conversion (both in metres)

        // Smoothing after load (optional — VMTK data is already clean)
        int smoothIterations = 2;
        float smoothLambda   = 0.15f;

        // Minimum radius to accept a point (filters near-zero artefacts at junctions)
        float minRadiusM = 1e-5f;   // 0.01mm in metres

        // Maximum number of branches to load (0 = all)
        int maxBranches = 0;
    };

    struct Report {
        int branchesLoaded   = 0;
        int pointsLoaded     = 0;
        int linksCreated     = 0;
        int bifurcationLinks = 0;
        int terminals        = 0;
        int bifurcations     = 0;
        float minRadiusMm    = 0.f;
        float maxRadiusMm    = 0.f;
        float meanRadiusMm   = 0.f;
        std::string error;
        /// Graph node IDs that are true VTP leaf terminals (identified before
        /// inter-branch linking). Use these for access site detection instead
        /// of graph.terminals() which may be wrong after inter-branch links.
        std::vector<int32_t> leafNodeIds;
    };

    VtpCenterlineLoader() = default;
    explicit VtpCenterlineLoader(const Params &p) : m_params(p) {}

    /// Load VTP centerline file into graph. Returns false on failure.
    bool load(const std::string &vtpPath, CenterlineGraph &out, Report *report = nullptr);

    const Params &params() const { return m_params; }
    void setParams(const Params &p) { m_params = p; }

private:
    Params m_params;
};

} // namespace cn