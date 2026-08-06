#pragma once
#include "CenterlineGraph.h"
#include <string>
#include <vector>

namespace cn {

/// The arterial access sites supported.
/// Carotid is navigated TO from femoral/radial — not a separate puncture site.
enum class AccessSite {
    RightFemoral,
    LeftFemoral,
    RightRadial,
    LeftRadial
};

const char *accessSiteName(AccessSite s);

struct AccessPoint {
    AccessSite  site;
    int32_t     node           = -1;
    bool        valid          = false;
    float       vesselDiameter = 0.f;  ///< model units
    float       pathLengthMm   = 0.f;  ///< reach to the cranial-most terminal, mm
    std::string note;
};

/// Resolves the four puncture sites from the centerline graph's terminals.
///
/// The rules are anatomical rather than positional-only, because a vessel tree
/// has many terminals in the same region and only one of them is the vessel you
/// would actually cannulate:
///
///   Femoral — the leg terminals occupy the bottom of the model. Several sit in
///     that band; the one to puncture is the widest, because the distal twigs
///     sharing the band are sub-millimetre. Selecting by "lowest Z" instead
///     lands on a 0.4 mm capillary stub every time.
///
///   Radial — the arm is the most *lateral* structure in the body, so the wrist
///     is the terminal farthest from the midline on each side. This needs no Z
///     band, which matters because arm position varies between models: arms
///     down at the sides puts the wrist near hip height, not near the shoulder.
///
/// Both rules are restricted to the largest connected component, so a terminal
/// stranded on an unreachable island is never offered as a puncture site.
class AccessSiteResolver {
public:
    struct Params {
        /// Femoral search band, as a fraction of the model's Z span measured
        /// from the bottom. The leg terminals of a whole-body tree sit inside
        /// the bottom ~12%.
        float femoralZLoFraction = 0.00f;
        float femoralZHiFraction = 0.12f;

        /// A femoral candidate must be at least this wide (model units,
        /// diameter). Rejects the sub-millimetre distal stubs.
        float femoralMinDiameter = 0.0015f;   // 1.5 mm at mmPerUnit = 1000

        /// A radial candidate must lie at least this far from the midline, as a
        /// fraction of the model's half-width. Guards against picking an iliac
        /// terminal on a model that has no arms.
        float radialLateralFraction = 0.55f;

        /// A radial candidate must be at least this wide (model units).
        float radialMinDiameter = 0.0010f;    // 1.0 mm at mmPerUnit = 1000
    };

    /// `vtpLeafNodes` overrides the terminal set when non-empty. Pass the
    /// loader's Report::leafNodeIds; when the graph came from STL extraction,
    /// leave it empty and graph.terminals() is used.
    ///
    /// Two overloads rather than one with `const Params & = Params{}`: a default
    /// argument may not name a brace-init of a type whose own member
    /// initialisers are still being parsed, so that form does not compile here.
    static std::vector<AccessPoint> resolve(const CenterlineGraph &graph,
                                            float mmPerUnit,
                                            bool patientLeftIsPlusX,
                                            const std::vector<int32_t> &vtpLeafNodes,
                                            const Params &params);

    static std::vector<AccessPoint> resolve(const CenterlineGraph &graph,
                                            float mmPerUnit = 1000.f,
                                            bool patientLeftIsPlusX = true,
                                            const std::vector<int32_t> &vtpLeafNodes = {}) {
        return resolve(graph, mmPerUnit, patientLeftIsPlusX, vtpLeafNodes, Params{});
    }

    static int32_t nodeFor(const std::vector<AccessPoint> &points, AccessSite site);

    /// True when the site is a groin puncture rather than a wrist puncture.
    static bool isFemoral(AccessSite s) {
        return s == AccessSite::RightFemoral || s == AccessSite::LeftFemoral;
    }
};

} // namespace cn