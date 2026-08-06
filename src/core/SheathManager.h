#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SheathManager — verified access-point registry and sheath geometry builder.
//
// Layer 1: pre-verified paths. Each AccessSite maps to a CenterlinePath
//   built from the graph once access sites are resolved. setAccessPoint()
//   simply looks up that path and returns its first point — no search.
//
// Layer 2: click-to-refine. setAccessPointFromWorldPoint() runs a nearest-
//   node search on the active path and re-slices it from that index onward.
//   The entry index and the "current path" are always the same data.
//
// Layer 3: sheath geometry. buildSheathParams() returns everything a renderer
//   needs to draw a correctly positioned and scaled sheath cylinder.
// ─────────────────────────────────────────────────────────────────────────────
#include "CenterlineGraph.h"
#include "AccessSites.h"
#include <optional>
#include <vector>
#include <string>
#include <unordered_map>

namespace cn {

// ─────────────────────────────────────────────────────────────────────────────
// Introducer sheath, as actually used in the cath lab.
//
// A percutaneous introducer (Terumo Pinnacle / Radifocus, Cordis Avanti, Merit
// Prelude) is not a cylinder. It is five parts:
//
//   1. Cannula      thin-walled PTFE tube with a tapered distal tip, the only
//                   part inside the patient. Sized by French: ID mm = Fr / 3,
//                   so a 6 Fr sheath passes a 6 Fr (2.0 mm) catheter and its
//                   own OD is ~0.75 mm larger. Working length 11 cm femoral,
//                   7 cm radial.
//   2. Hub          moulded body at the skin, COLOUR-CODED BY FRENCH SIZE. The
//                   colour code below is the industry convention and is how an
//                   operator identifies a sheath at a glance.
//   3. Valve        haemostasis cap on the back of the hub; the catheter is
//                   introduced through its cross-slit.
//   4. Side arm     flexible flush/contrast line leaving the hub at ~45 deg.
//   5. Stopcock     three-way tap at the end of the side arm.
//
// Sizing off the vessel radius (the previous behaviour: length = 4 x radius,
// radius = 1.5 x radius) produces a device that changes size depending on where
// it is placed and can never be oversized, which is exactly the failure mode a
// simulator should be able to show.
// ─────────────────────────────────────────────────────────────────────────────

/// Catalogue entry. All lengths in MILLIMETRES; converted to model units when
/// the geometry is built.
struct SheathSpec {
    int   french          = 6;
    float innerDiaMm      = 2.00f;   ///< French / 3
    float outerDiaMm      = 2.75f;   ///< ID + wall
    float workingLenMm    = 110.f;   ///< inserted cannula length
    float exposedLenMm    = 12.f;    ///< strain relief between skin and hub
    float hubLenMm        = 28.f;
    float hubDiaMm        = 8.5f;
    float valveLenMm      = 9.f;
    float valveDiaMm      = 10.5f;
    float sideArmDiaMm    = 2.4f;
    float sideArmLenMm    = 95.f;
    float sideArmAngleDeg = 45.f;
    float stopcockLenMm   = 14.f;
    float stopcockDiaMm   = 7.0f;
    float hubColour[3]    = {0.11f, 0.55f, 0.27f};   ///< 6 Fr green
    bool  slender         = false;   ///< thin-wall (transradial) construction
};

/// Standard introducer for a French size.
///   `radial`  selects the shorter 7 cm working length used at the wrist.
///   `slender` selects a thin-wall design (Terumo Glidesheath Slender and
///             equivalents): 0.40 mm wall instead of 0.75 mm, so a 6 Fr lumen
///             fits through a 5 Fr arteriotomy. This is now the default device
///             for transradial access and is what makes 6 Fr work in a wrist.
/// Hub colour code: 4 red, 5 grey, 6 green, 7 orange, 8 blue, 9 black,
/// 10 purple, 11 yellow.
SheathSpec sheathSpecForFrench(int french, bool radial = false, bool slender = false);

/// Everything a renderer needs, in model units. No VTK or Qt types, so the core
/// library stays render-agnostic.
struct SheathGeometry {
    bool  valid = false;

    /// Centreline of the cannula, ordered DISTAL TIP FIRST and running back out
    /// to the base of the hub. The inserted portion follows the vessel
    /// centreline rather than running straight, so a long sheath in a curved
    /// iliac stays inside the lumen instead of spearing the wall.
    std::vector<Vec3> cannulaPath;
    float cannulaOuterRadius = 0.f;
    float cannulaInnerRadius = 0.f;
    float tipTaperLength     = 0.f;  ///< distal taper, model units

    Vec3  skinPoint;                 ///< where the cannula leaves the vessel
    Vec3  axis;                      ///< unit, pointing OUT of the patient

    Vec3  hubBase, hubTop;
    float hubRadius   = 0.f;
    Vec3  valveTop;
    float valveRadius = 0.f;

    Vec3  sideArmOrigin, sideArmEnd, sideArmDir;
    float sideArmRadius = 0.f;
    Vec3  stopcockCentre;
    float stopcockRadius = 0.f, stopcockLength = 0.f;

    float hubColour[3] = {0.11f, 0.55f, 0.27f};
    int   french       = 6;

    float vesselRadius = 0.f;        ///< lumen radius at the entry
    /// Cannula OD exceeds the lumen. Reported rather than silently resized: an
    /// 6 Fr sheath does not fit a 2.2 mm radial artery, and that is a finding.
    bool  oversized    = false;
};

class SheathManager {
public:
    SheathManager() = default;

    // ── Wiring ──────────────────────────────────────────────────────────────
    /// Must be called once after graph + access sites are resolved.
    void init(const CenterlineGraph *graph,
              const std::vector<AccessPoint> &sites,
              float mmPerUnit);

    // ── Layer 1: default entry point ─────────────────────────────────────
    /// Set access point for the given site.
    /// Returns true and sets m_currentPath / m_entryIndex.
    /// Returns false (with message) when:
    ///   – the site is invalid / not found
    ///   – the path is empty
    ///   – a sanity check fails (entry point outside expected anatomical Z band)
    bool setAccessPoint(AccessSite site, std::string &outMessage);

    // ── Layer 2: click-to-refine ─────────────────────────────────────────
    /// Called with a 3-D world-space click on the vessel surface.
    /// Finds the nearest index in the current path and re-slices from there.
    /// Returns false (with message) if no path is active or click misses the vessel.
    bool setAccessPointFromWorldPoint(const Vec3 &worldPt,
                                      float maxDistModelUnits,
                                      std::string &outMessage);

    // ── Query ────────────────────────────────────────────────────────────
    bool              hasEntry()    const { return m_entryIndex >= 0; }
    int32_t           entryNode()   const;   ///< graph node at entry (for nav)
    Vec3              entryPoint()  const;   ///< world position of entry
    float             entryRadius() const;   ///< vessel radius at entry
    const CenterlinePath &currentPath() const { return m_currentPath; }
    AccessSite        currentSite() const { return m_currentSite; }

    // ── Layer 3: sheath geometry ─────────────────────────────────────────
    /// Build the full introducer at the current entry point.
    ///
    /// `outwardDir` points away from the vessel toward the skin. The reliable
    /// way to obtain it is to negate the direction from the entry node to its
    /// only neighbour: the entry is a vessel terminal, so the vessel leaves it
    /// in exactly one direction.
    ///
    /// `spec` defaults to a 6 Fr femoral introducer. Use sheathSpecForFrench()
    /// to pick a size, and check SheathGeometry::oversized afterwards.
    std::optional<SheathGeometry> buildSheath(const Vec3 &outwardDir,
                                              const SheathSpec &spec) const;
    std::optional<SheathGeometry> buildSheath(const Vec3 &outwardDir) const {
        return buildSheath(outwardDir, sheathSpecForFrench(6, false));
    }

    /// Largest French size whose cannula OD fits the lumen at the entry, using
    /// `fitFraction` of the lumen diameter. Returns 0 when even 4 Fr is too big.
    int largestFittingFrench(float fitFraction = 0.92f, bool slender = false) const;

    // ── Sanity bands ───────────────────────────────────────────────────
    // Expressed as fractions of the graph's own bounding box, not absolute
    // millimetres, so they hold for any model whose legs are at the bottom and
    // whose arms are its most lateral structure.
    /// A groin entry must sit below this fraction of the model's Z span.
    static constexpr float kFemoralZMaxFraction      = 0.20f;
    /// A wrist entry must sit beyond this fraction of the model's half-width.
    static constexpr float kRadialLateralMinFraction = 0.45f;

private:
    bool sanityCheck(AccessSite site, const Vec3 &pt, std::string &outMsg) const;

    const CenterlineGraph   *m_graph    = nullptr;
    float                    m_mmPerUnit = 1000.f;

    // Pre-built paths: one per access site, built at init() time
    std::unordered_map<int, CenterlinePath> m_paths;  // key = (int)AccessSite
    std::unordered_map<int, int32_t>        m_rootNode;

    CenterlinePath m_currentPath;
    int            m_entryIndex   = -1;
    AccessSite     m_currentSite  = AccessSite::RightFemoral;
};

} // namespace cn