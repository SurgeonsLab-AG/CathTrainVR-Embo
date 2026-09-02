#include "SheathManager.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <sstream>
#include <vector>

namespace cn {

// ─────────────────────────────────────────────────────────────────────────────
void SheathManager::init(const CenterlineGraph          *graph,
                         const std::vector<AccessPoint> &sites,
                         float                           mmPerUnit) {
    m_graph      = graph;
    m_mmPerUnit  = mmPerUnit;
    m_paths.clear();
    m_rootNode.clear();
    m_currentPath = {};
    m_entryIndex  = -1;

    if (!m_graph) return;

    // Build one path per valid access site.
    // Each path starts at the site node and extends to the farthest reachable
    // terminal via Dijkstra — this is the "verified centerline" for that route.
    for (const auto &ap : sites) {
        if (!ap.valid) continue;
        const int key = static_cast<int>(ap.site);
        m_rootNode[key] = ap.node;

        // Default route runs from the puncture toward the head, so the first
        // thing the operator sees is the direction they actually intend to go.
        //
        // The target is the CRANIAL-MOST reachable terminal — the one with the
        // greatest Z. Selecting by largest |dz| instead (the previous rule) is
        // correct only for a groin puncture: from a wrist at Z=844mm the groin
        // is 805mm away and the vertex is 856mm away, so |dz| is a coin flip,
        // and from a wrist positioned lower it routes the sheath down the
        // patient's leg.
        //
        // Reachability is checked with shortestPath() rather than assumed,
        // because the graph can legitimately contain isolated fragments.
        const auto &nodes = m_graph->nodes();
        const auto  terms = m_graph->terminals();

        std::vector<int32_t> bestPath;
        float bestZ = -std::numeric_limits<float>::max();
        for (int32_t t : terms) {
            if (t == ap.node) continue;
            if (nodes[t].position.z <= bestZ) continue;   // cheap reject first
            auto candidate = m_graph->shortestPath(ap.node, t);
            if (candidate.empty()) continue;              // unreachable
            bestZ    = nodes[t].position.z;
            bestPath = std::move(candidate);
        }
        if (bestPath.empty()) bestPath = {ap.node};       // fallback: single point
        m_paths[key] = m_graph->makePath(bestPath);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
bool SheathManager::setAccessPoint(AccessSite site, std::string &outMsg) {
    const int key = static_cast<int>(site);
    auto it = m_paths.find(key);
    if (it == m_paths.end() || it->second.empty()) {
        outMsg = std::string(accessSiteName(site)) +
                 ": no verified centerline path. Check access site detection.";
        return false;
    }

    m_currentSite = site;
    m_currentPath = it->second;
    m_entryIndex  = 0;   // Layer 1: first point of verified array

    // Sanity check: entry point must lie in the expected anatomical Z band
    const Vec3 &ep = m_currentPath.points[0];
    if (!sanityCheck(site, ep, outMsg)) {
        m_entryIndex = -1;
        return false;
    }

    outMsg = std::string(accessSiteName(site)) + ": entry set at first path point.";
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool SheathManager::setAccessPointFromWorldPoint(const Vec3 &worldPt,
                                                 float        maxDist,
                                                 std::string &outMsg) {
    if (m_currentPath.empty()) {
        outMsg = "No active path. Select an access site first.";
        return false;
    }

    // Linear nearest-point search across the current path's points array.
    // This is the exact array the catheter rides — no new interpolation.
    float  bestDist  = std::numeric_limits<float>::max();
    int    bestIndex = -1;

    const auto &pts = m_currentPath.points;
    for (int i = 0; i < static_cast<int>(pts.size()); ++i) {
        const Vec3 &p  = pts[i];
        float dx = p.x - worldPt.x;
        float dy = p.y - worldPt.y;
        float dz = p.z - worldPt.z;
        float d  = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (d < bestDist) { bestDist = d; bestIndex = i; }
    }

    if (bestIndex < 0 || bestDist > maxDist) {
        std::ostringstream s;
        s << "Click misses vessel (nearest path point "
          << bestDist * m_mmPerUnit << " mm away, tolerance "
          << maxDist  * m_mmPerUnit << " mm).";
        outMsg = s.str();
        return false;
    }

    // Re-slice the path from bestIndex onward.
    // Both m_entryIndex and m_currentPath now reference the SAME underlying
    // data — they can never drift apart.
    m_entryIndex = bestIndex;
    {
        CenterlinePath sub;
        const float    base = m_currentPath.arcLength[bestIndex];
        for (int i = bestIndex; i < static_cast<int>(pts.size()); ++i) {
            sub.points.push_back(m_currentPath.points[i]);
            sub.radii.push_back(m_currentPath.radii[i]);
            sub.arcLength.push_back(m_currentPath.arcLength[i] - base);
            sub.nodeId.push_back(m_currentPath.nodeId[i]);
        }
        m_currentPath = sub;
        m_entryIndex  = 0;   // entry is always index 0 of current path
    }

    std::ostringstream s;
    s << "Entry refined to path index " << bestIndex
      << " (dist=" << bestDist * m_mmPerUnit << " mm).";
    outMsg = s.str();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
int32_t SheathManager::entryNode() const {
    if (!hasEntry() || m_currentPath.empty()) return -1;
    return m_currentPath.nodeId[0];
}

Vec3 SheathManager::entryPoint() const {
    if (!hasEntry() || m_currentPath.empty()) return {};
    return m_currentPath.points[0];
}

float SheathManager::entryRadius() const {
    if (!hasEntry() || m_currentPath.empty()) return 0.f;
    return m_currentPath.radii[0];
}

// ─────────────────────────────────────────────────────────────────────────────
// Layer 3: sheath geometry
// ─────────────────────────────────────────────────────────────────────────────
SheathSpec sheathSpecForFrench(int french, bool radial, bool slender) {
    SheathSpec s;
    s.french = std::clamp(french, 4, 11);

    // French size IS the inner diameter: ID mm = Fr / 3. A 6 Fr sheath passes a
    // 6 Fr catheter. Wall thickness is roughly constant across the range, so the
    // outer diameter tracks about 0.75 mm above the inner.
    s.innerDiaMm = static_cast<float>(s.french) / 3.f;
    // Standard-wall introducers add about 0.75 mm to the inner diameter.
    // Thin-wall ("slender") radial sheaths add about 0.40 mm, which is the whole
    // point of them: a 6 Fr lumen through a 5 Fr hole.
    s.slender    = slender;
    s.outerDiaMm = s.innerDiaMm + (slender ? 0.40f : 0.75f);

    // Working length: 11 cm is the standard femoral introducer, 7 cm the
    // standard radial one (a wrist sheath any longer would reach the elbow).
    s.workingLenMm = radial ? 70.f : 110.f;

    // The hub does not scale much with French size; larger sheaths get a
    // slightly bigger body.
    const float g = 1.f + 0.035f * static_cast<float>(s.french - 6);
    s.hubDiaMm   = 8.5f  * g;
    s.valveDiaMm = 10.5f * g;
    s.hubLenMm   = 28.f;
    s.valveLenMm = 9.f;

    s.sideArmDiaMm    = 2.4f;
    s.sideArmLenMm    = radial ? 75.f : 95.f;
    s.sideArmAngleDeg = 45.f;
    s.stopcockLenMm   = 14.f;
    s.stopcockDiaMm   = 7.f;
    s.exposedLenMm    = 12.f;

    // Industry hub colour code. This is not decoration: it is how a sheath size
    // is identified on the table, so getting it right matters for training.
    struct CC { int fr; float r, g, b; };
    static const CC kCode[] = {
        { 4, 0.78f, 0.12f, 0.14f},   // red
        { 5, 0.62f, 0.64f, 0.66f},   // grey
        { 6, 0.11f, 0.55f, 0.27f},   // green
        { 7, 0.90f, 0.45f, 0.09f},   // orange
        { 8, 0.13f, 0.35f, 0.72f},   // blue
        { 9, 0.13f, 0.13f, 0.14f},   // black
        {10, 0.45f, 0.20f, 0.60f},   // purple
        {11, 0.90f, 0.78f, 0.13f},   // yellow
    };
    for (const CC &c : kCode)
        if (c.fr == s.french) { s.hubColour[0]=c.r; s.hubColour[1]=c.g; s.hubColour[2]=c.b; }
    return s;
}

int SheathManager::largestFittingFrench(float fitFraction, bool slender) const {
    if (!hasEntry()) return 0;
    const bool  radial     = !AccessSiteResolver::isFemoral(m_currentSite);
    const float lumenDiaMm = 2.f * entryRadius() * m_mmPerUnit;
    for (int fr = 11; fr >= 4; --fr)
        if (sheathSpecForFrench(fr, radial, slender).outerDiaMm <= lumenDiaMm * fitFraction)
            return fr;
    return 0;
}

std::optional<SheathGeometry> SheathManager::buildSheath(const Vec3 &outwardDir,
                                                         const SheathSpec &spec) const {
    if (!hasEntry() || m_currentPath.empty()) return std::nullopt;

    const float r = entryRadius();
    if (r < 1e-6f) return std::nullopt;

    const float u = 1.f / m_mmPerUnit;          // millimetres -> model units

    SheathGeometry g;
    g.valid              = true;
    g.french             = spec.french;
    g.vesselRadius       = r;
    g.cannulaOuterRadius = 0.5f * spec.outerDiaMm * u;
    g.cannulaInnerRadius = 0.5f * spec.innerDiaMm * u;
    g.tipTaperLength     = 6.f * u;
    g.hubColour[0] = spec.hubColour[0];
    g.hubColour[1] = spec.hubColour[1];
    g.hubColour[2] = spec.hubColour[2];

    // Report rather than resize. An 6 Fr introducer genuinely does not fit a
    // 2.2 mm radial artery, and hiding that by shrinking the model would remove
    // the one piece of feedback that matters.
    g.oversized = g.cannulaOuterRadius > r;

    Vec3 axis = outwardDir.normalized();
    if (axis.lengthSquared() < 0.5f) axis = Vec3(0.f, 0.f, 1.f);
    g.axis      = axis;
    g.skinPoint = entryPoint();

    // ── Cannula centreline ───────────────────────────────────────────────────
    // The inserted portion follows the vessel centreline. A straight 110 mm tube
    // pushed into a curved iliac would leave the lumen; tracking the centreline
    // keeps it inside by construction, and is what the real device does as it
    // conforms to the vessel.
    const float pathLen    = m_currentPath.totalLength();
    const float insertLen  = std::min(spec.workingLenMm * u, pathLen);
    const float exposedLen = spec.exposedLenMm * u;
    const float step       = std::max(1.5f * u, 1e-6f);

    const int nIn = std::max(2, static_cast<int>(std::ceil(insertLen / step)));
    g.cannulaPath.reserve(static_cast<size_t>(nIn) + 8);
    for (int i = 0; i <= nIn; ++i) {                 // tip first, walking back out
        const float s = insertLen * (1.f - static_cast<float>(i) / static_cast<float>(nIn));
        g.cannulaPath.push_back(m_currentPath.pointAt(s));
    }
    // Strain relief: straight, from the entry out along the axis to the hub.
    const int nOut = std::max(2, static_cast<int>(std::ceil(exposedLen / step)));
    for (int i = 1; i <= nOut; ++i) {
        const float t = exposedLen * static_cast<float>(i) / static_cast<float>(nOut);
        g.cannulaPath.push_back(g.skinPoint + axis * t);
    }

    // ── Hub, valve ───────────────────────────────────────────────────────────
    g.hubBase     = g.skinPoint + axis * exposedLen;
    g.hubRadius   = 0.5f * spec.hubDiaMm * u;
    g.hubTop      = g.hubBase + axis * (spec.hubLenMm * u);
    g.valveRadius = 0.5f * spec.valveDiaMm * u;
    g.valveTop    = g.hubTop + axis * (spec.valveLenMm * u);

    // ── Side arm and stopcock ────────────────────────────────────────────────
    // Leaves the hub body at 45 degrees, angled back away from the patient the
    // way a flush line does so it does not foul the catheter entry.
    const Vec3  perp  = anyPerpendicular(axis);
    const float a     = spec.sideArmAngleDeg * 3.14159265f / 180.f;
    g.sideArmDir      = (axis * std::cos(a) + perp * std::sin(a)).normalized();
    g.sideArmRadius   = 0.5f * spec.sideArmDiaMm * u;
    g.sideArmOrigin   = g.hubBase + axis * (spec.hubLenMm * u * 0.45f);
    g.sideArmEnd      = g.sideArmOrigin + g.sideArmDir * (spec.sideArmLenMm * u);

    g.stopcockRadius  = 0.5f * spec.stopcockDiaMm * u;
    g.stopcockLength  = spec.stopcockLenMm * u;
    g.stopcockCentre  = g.sideArmEnd + g.sideArmDir * (g.stopcockLength * 0.5f);

    return g;
}

// ─────────────────────────────────────────────────────────────────────────────
bool SheathManager::sanityCheck(AccessSite site, const Vec3 &pt,
                                std::string &outMsg) const {
    // Bands are derived from the graph's own bounding box rather than written
    // as absolute millimetres. Hard-coded ranges (the old femoral 100-350 mm /
    // radial 1300-1500 mm) encode one particular dataset's proportions: on this
    // model the groin terminal sits at Z=85 mm and the wrist at Z=844 mm, so
    // both hard-coded bands rejected the correct entry point and femoral access
    // could never be established at all.
    //
    // Deriving from bounds also keeps this in step with AccessSiteResolver,
    // which uses the same two geometric facts: legs are at the bottom of the
    // model, arms are the most lateral part of it.
    if (!m_graph || m_graph->nodeCount() == 0) return true;   // nothing to check against

    const Box3  b     = m_graph->bounds();
    const float zSpan = std::max(b.hi.z - b.lo.z, 1e-9f);
    const float halfW = std::max((b.hi.x - b.lo.x) * 0.5f, 1e-9f);
    const float midX  = b.center().x;
    const char *siteName = accessSiteName(site);
    std::ostringstream s;

    switch (site) {
    case AccessSite::RightFemoral:
    case AccessSite::LeftFemoral: {
        // Femoral resolution was fixed to find the actual groin-level
        // common femoral artery (previously it was using the leg's own
        // ankle terminal, ~2.7% of body height - see AccessSites.cpp's own
        // comment on that resolver for the full story), which sits much
        // higher up the body. Generous band around the verified ~57%
        // value so click-to-refine has room to move without tripping the
        // alarm, while still catching a genuinely wrong vessel.
        const float zMin = b.lo.z + zSpan * kFemoralZMinFraction;
        const float zMax = b.lo.z + zSpan * kFemoralZMaxFraction;
        if (pt.z < zMin || pt.z > zMax) {
            s << siteName << ": entry point Z=" << pt.z * m_mmPerUnit
              << " mm is outside the groin band (" << zMin * m_mmPerUnit
              << "-" << zMax * m_mmPerUnit << " mm). "
              << "Access site detection may have chosen the wrong vessel.";
            outMsg = s.str();
            return false;
        }
        return true;
    }
    case AccessSite::RightRadial:
    case AccessSite::LeftRadial: {
        // A wrist puncture is defined by being far from the midline, not by
        // height: arms hang at the sides in this model, so the wrist is near
        // hip level.
        const float lat    = std::fabs(pt.x - midX);
        const float latMin = halfW * kRadialLateralMinFraction;
        if (lat < latMin) {
            s << siteName << ": entry point is " << lat * m_mmPerUnit
              << " mm from the midline, inside the wrist band (min "
              << latMin * m_mmPerUnit << " mm). "
              << "Access site detection may have chosen the wrong vessel.";
            outMsg = s.str();
            return false;
        }
        return true;
    }
    case AccessSite::RightPopliteal:
    case AccessSite::LeftPopliteal: {
        // Same band the original femoral resolver used before it was
        // corrected to find the true groin-level entry - a genuine leg
        // terminal near the bottom of the model.
        const float zMax = b.lo.z + zSpan * kPoplitealZMaxFraction;
        if (pt.z > zMax) {
            s << siteName << ": entry point Z=" << pt.z * m_mmPerUnit
              << " mm is above the popliteal band (max "
              << zMax * m_mmPerUnit << " mm). "
              << "Access site detection may have chosen the wrong vessel.";
            outMsg = s.str();
            return false;
        }
        return true;
    }
    case AccessSite::RightBrachial:
    case AccessSite::LeftBrachial: {
        const float zMin = b.lo.z + zSpan * kBrachialZMinFraction;
        const float zMax = b.lo.z + zSpan * kBrachialZMaxFraction;
        if (pt.z < zMin || pt.z > zMax) {
            s << siteName << ": entry point Z=" << pt.z * m_mmPerUnit
              << " mm is outside the brachial band (" << zMin * m_mmPerUnit
              << "-" << zMax * m_mmPerUnit << " mm). "
              << "Access site detection may have chosen the wrong vessel.";
            outMsg = s.str();
            return false;
        }
        return true;
    }
    }
    return true;
}

} // namespace cn