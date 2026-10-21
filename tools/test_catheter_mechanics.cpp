// Validates CatheterModel::MechanicalState and its mechanical RESPONSE
// (advancement/bending/torque/contact, device-specific, continuous across
// advance/retract/rotate/support changes) against synthetic straight and
// tightly curved vessel segments - no Qt, no Vessel.stl, same headless
// pass/fail convention tools/test_dose.cpp already uses.
//
// Scenarios, matching the validation list this test was written against:
//   1. Straight-vessel advancement            -> free, near-zero resistance
//   2. Tight curved-vessel advancement         -> stiff device resists/
//                                                 contacts more than a soft one
//   3. Guide catheter support of the wire      -> effectiveSupport engages
//   4. Wire protruding beyond the support       -> effectiveSupport drops to 0
//   5. Microcatheter advancing "over" the wire  -> only a STIFFER neighbour
//                                                 reinforces it
//   6/7. Wire rotation supported vs unsupported -> supported catches up faster
//   8. Device contacting the vessel wall        -> contact node/location set
//   9. Pushing against significant resistance   -> regime escalates, still
//                                                  never saturates to a hard wall
//  10. Retracting away from the wall            -> contact naturally decreases
#include "CatheterModel.h"
#include "CatheterCatalog.h"
#include "MechanicalEvents.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace cn;

namespace {

constexpr float kMmPerUnit = 1000.f;   // 1 model unit = 1 metre - CatheterCatalog's own convention

CenterlinePath makeStraightPath(float length, float radius, float spacing = 0.0005f) {
    CenterlinePath p;
    const int n = std::max(2, static_cast<int>(length / spacing) + 1);
    for (int i = 0; i < n; ++i) {
        const float s = static_cast<float>(i) * spacing;
        p.points.push_back(Vec3(s, 0.f, 0.f));
        p.radii.push_back(radius);
        p.arcLength.push_back(s);
        p.nodeId.push_back(0);
    }
    return p;
}

// Straight, but the lumen narrows linearly from radiusStart to radiusEnd -
// a distal taper/relative stenosis. constrainToLumen()'s own wallSafety
// margin (see CatheterModel::Params::wallSafety) guarantees at least
// lumen*(1-wallSafety) of raw clearance from BENDING alone, however tight
// the curvature - real wall contact in this model only comes from the
// device genuinely being oversized for the local lumen (State::oversized),
// so that is what this path is built to exercise, deliberately isolated
// from the curvature/bending-resistance path above.
CenterlinePath makeTaperedPath(float length, float radiusStart, float radiusEnd, float spacing = 0.0005f) {
    CenterlinePath p;
    const int n = std::max(2, static_cast<int>(length / spacing) + 1);
    for (int i = 0; i < n; ++i) {
        const float s = static_cast<float>(i) * spacing;
        const float t = std::min(s / length, 1.f);
        p.points.push_back(Vec3(s, 0.f, 0.f));
        p.radii.push_back(radiusStart + (radiusEnd - radiusStart) * t);
        p.arcLength.push_back(s);
        p.nodeId.push_back(0);
    }
    return p;
}

// A circular arc of radius `turnRadius`, turning through `totalAngle`
// radians - a tight, continuous curve to engage bending resistance and
// (for a device wide/stiff enough) wall contact.
CenterlinePath makeCurvedPath(float turnRadius, float totalAngle, float radius, float spacing = 0.0005f) {
    CenterlinePath p;
    const float totalArc = turnRadius * totalAngle;
    const int n = std::max(2, static_cast<int>(totalArc / spacing) + 1);
    float cumulative = 0.f;
    Vec3 prev;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n - 1);
        const float ang = t * totalAngle;
        const Vec3 pt(turnRadius * std::sin(ang), 0.f, turnRadius * (1.f - std::cos(ang)));
        if (i > 0) cumulative += distance(pt, prev);
        p.points.push_back(pt);
        p.radii.push_back(radius);
        p.arcLength.push_back(cumulative);
        p.nodeId.push_back(0);
        prev = pt;
    }
    return p;
}

CatheterModel::Params paramsFor(const char *name) {
    const CatheterSpec *spec = findCatheter(name);
    return catheterParams(*spec, spec->defaultSize, kMmPerUnit);
}

} // namespace

int main() {
    bool pass = true;
    auto note = [&](bool cond, const char *label) {
        std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", label);
        if (!cond) pass = false;
    };

    const CenterlinePath straight = makeStraightPath(0.15f, 0.0025f);   // roomy 5mm-ID straight run
    // Tight, continuous bend for the bending-resistance/curvature tests
    // (2, and the continuity sweep) - roomy enough in RAW lumen radius that
    // the device is never oversized here, so any bending resistance/
    // advancement resistance measured is purely curvature-driven.
    const CenterlinePath curved = makeCurvedPath(0.003f, 4.0f, 0.0016f); // 3.2mm-ID, 3mm-radius bend
    // Narrows from a comfortable 4.4mm ID down to 1.2mm ID over 60mm - the
    // wall-CONTACT tests (8/9/10) need the device to become genuinely
    // oversized for the local lumen (see makeTaperedPath's own comment),
    // which a distal taper does cleanly without conflating it with the
    // curvature-driven bending-resistance mechanism tested separately above.
    const CenterlinePath tapered = makeTaperedPath(0.06f, 0.0022f, 0.0006f);

    // --- 1. Straight-vessel advancement -----------------------------------
    {
        CatheterModel guide(paramsFor("Benchmark 071 (guide)"));
        guide.setPath(straight);
        guide.setInsertionDepth(0.05f);
        guide.update();
        const auto &m = guide.mechanics();
        std::printf("    [1] wallContact=%.4f advResist=%.4f bendResist=%.4f regime=%s\n",
                    m.wallContactAmount, m.advancementResistance, m.bendingResistance,
                    mechanicalRegimeName(m.regime));
        note(m.wallContactAmount < 0.05f && m.advancementResistance < 0.05f &&
                 m.regime == CatheterModel::MechanicalState::Regime::Free,
             "1. Straight-vessel advancement is free (near-zero contact/resistance)");
    }

    // --- 2. Tight curved-vessel advancement: stiff vs soft on the SAME bend
    {
        CatheterModel guide(paramsFor("Benchmark 071 (guide)"));
        guide.setPath(curved);
        guide.setInsertionDepth(0.008f);
        guide.update();

        CatheterModel wire(paramsFor("Mirage 008"));
        wire.setPath(curved);
        wire.setInsertionDepth(0.008f);
        wire.update();

        const auto &mg = guide.mechanics();
        const auto &mw = wire.mechanics();
        std::printf("    [2] guide: bendResist=%.4f wallContact=%.4f advResist=%.4f regime=%s\n",
                    mg.bendingResistance, mg.wallContactAmount, mg.advancementResistance,
                    mechanicalRegimeName(mg.regime));
        std::printf("    [2] wire : bendResist=%.4f wallContact=%.4f advResist=%.4f regime=%s\n",
                    mw.bendingResistance, mw.wallContactAmount, mw.advancementResistance,
                    mechanicalRegimeName(mw.regime));
        note(mg.bendingResistance > mw.bendingResistance,
             "2a. Stiff guide resists the same bend more than a soft wire");
        note(mg.bendingResistance > 0.05f,
             "2b. Tight curvature registers meaningful bending resistance at all");
    }

    // --- 3/4. Guide support engages within its reach, drops once the wire
    // protrudes past the guide's own tip - continuity: same model instance,
    // no setPath()/reset between the two readings, only setInsertionDepth().
    {
        CatheterModel guide(paramsFor("Benchmark 071 (guide)"));
        guide.setPath(straight);
        guide.setInsertionDepth(0.05f);
        guide.update();
        const float guideDepth = guide.state().insertionDepth;
        const float guideStiff = guide.params().shaftStiffness;

        CatheterModel wire(paramsFor("Synchro-14"));
        wire.setPath(straight);
        wire.setSupports({ {guideDepth, guideStiff} });

        wire.setInsertionDepth(guideDepth * 0.5f);   // well inside the guide
        wire.update();
        const float supportedInside = wire.mechanics().effectiveSupport;
        std::printf("    [3] wire inside guide: effectiveSupport=%.4f (guideStiff=%.2f)\n",
                    supportedInside, guideStiff);
        note(supportedInside > 0.05f, "3. Guide catheter support engages on the wire within its reach");

        wire.setInsertionDepth(guideDepth * 1.5f);   // now protruding past the guide's own tip
        wire.update();
        const float supportedProtruding = wire.mechanics().effectiveSupport;
        std::printf("    [4] wire past guide tip: effectiveSupport=%.4f\n", supportedProtruding);
        note(supportedProtruding < 0.02f,
             "4. Support drops once the wire protrudes beyond the supporting device");
    }

    // --- 5. Microcatheter reinforced by a wire threaded through it - only
    // when the wire is actually stiffer; a softer wire must not loosen it.
    {
        CatheterModel wireStiff(paramsFor("Synchro-14"));       // stiffness 0.35
        wireStiff.setPath(straight);
        wireStiff.setInsertionDepth(0.06f);
        wireStiff.update();

        CatheterModel wireSoft(paramsFor("Mirage 008"));        // stiffness 0.12
        wireSoft.setPath(straight);
        wireSoft.setInsertionDepth(0.06f);
        wireSoft.update();

        CatheterModel microA(paramsFor("Marathon (DMSO-compatible)"));  // stiffness 0.15
        microA.setPath(straight);
        microA.setSupports({ {wireStiff.state().insertionDepth, wireStiff.params().shaftStiffness} });
        microA.setInsertionDepth(0.03f);
        microA.update();

        CatheterModel microB(paramsFor("Marathon (DMSO-compatible)"));
        microB.setPath(straight);
        microB.setSupports({ {wireSoft.state().insertionDepth, wireSoft.params().shaftStiffness} });
        microB.setInsertionDepth(0.03f);
        microB.update();

        std::printf("    [5] micro w/ stiffer wire inside: effectiveSupport=%.4f\n",
                    microA.mechanics().effectiveSupport);
        std::printf("    [5] micro w/ softer  wire inside: effectiveSupport=%.4f\n",
                    microB.mechanics().effectiveSupport);
        note(microA.mechanics().effectiveSupport > 0.05f,
             "5a. Microcatheter is reinforced by a stiffer wire riding inside it");
        note(microB.mechanics().effectiveSupport < 0.02f,
             "5b. A softer wire inside the same microcatheter adds no support");
    }

    // --- 6/7. Wire rotation: supported catches up to a commanded roll
    // faster (smaller residual torsion after the SAME single update()) than
    // the same wire riding unsupported.
    {
        constexpr float kCommandedRoll = 0.6f; // radians

        CatheterModel supported(paramsFor("Synchro-14"));
        supported.setPath(straight);
        supported.setSupports({ {0.10f, 0.90f} });  // a stiff host reaching well past this depth
        supported.setInsertionDepth(0.04f);
        supported.update();
        supported.setRollAngle(kCommandedRoll);
        supported.update();
        const float torsionSupported = supported.mechanics().torsionMagnitude;

        CatheterModel unsupported(paramsFor("Synchro-14"));
        unsupported.setPath(straight);
        unsupported.setInsertionDepth(0.04f);   // no Support at all - riding on its own
        unsupported.update();
        unsupported.setRollAngle(kCommandedRoll);
        unsupported.update();
        const float torsionUnsupported = unsupported.mechanics().torsionMagnitude;

        std::printf("    [6/7] residual torsion after one update(): supported=%.4f unsupported=%.4f rad\n",
                    torsionSupported, torsionUnsupported);
        note(torsionSupported < torsionUnsupported,
             "6/7. A supported wire catches up to a commanded roll faster than an unsupported one");
        note(torsionSupported > 1e-4f && torsionUnsupported > 1e-4f,
             "    (neither snaps instantly - both still show residual wind-up after one update())");
    }

    // --- 8/9. Wall contact (device oversized for the local lumen once the
    // taper narrows enough), then pushed deeper into significant resistance
    float depthAtContact = 0.f;
    {
        CatheterModel guide(paramsFor("Benchmark 071 (guide)"));
        guide.setPath(tapered);
        guide.setInsertionDepth(0.045f);   // taper has narrowed to ~1.0mm ID here
        guide.update();
        const auto &m8 = guide.mechanics();
        std::printf("    [8] contactNode=%d wallContact=%.4f contactAt=(%.4f,%.4f,%.4f)\n",
                    m8.contactNode, m8.wallContactAmount,
                    m8.contactLocation.x, m8.contactLocation.y, m8.contactLocation.z);
        note(m8.contactNode >= 0 && m8.wallContactAmount > 0.05f,
             "8. Device contacting the vessel wall reports a contact node/location");

        // Push further into the narrowing taper - resistance/regime should
        // escalate, but advancementResistance must stay short of a hard
        // wall (never reach exactly 1.0 - "not an abrupt invisible wall").
        guide.setInsertionDepth(0.057f);   // taper down to ~0.68mm ID here
        guide.update();
        const auto &m9 = guide.mechanics();
        std::printf("    [9] wallContact=%.4f advResist=%.4f regime=%s\n",
                    m9.wallContactAmount, m9.advancementResistance, mechanicalRegimeName(m9.regime));
        note(m9.wallContactAmount >= m8.wallContactAmount,
             "9a. Pushing deeper into the taper does not spontaneously relieve contact");
        note(m9.advancementResistance > 0.f && m9.advancementResistance < 1.f,
             "9b. Resistance escalates smoothly without ever hitting a hard (1.0) wall");
        depthAtContact = 0.057f;
    }

    // --- 10. Retracting away from the wall - contact should decrease -----
    {
        CatheterModel guide(paramsFor("Benchmark 071 (guide)"));
        guide.setPath(tapered);
        guide.setInsertionDepth(depthAtContact);
        guide.update();
        const float contactBefore = guide.mechanics().wallContactAmount;

        guide.setInsertionDepth(depthAtContact * 0.3f);   // retract most of the way back out
        guide.update();
        const float contactAfter = guide.mechanics().wallContactAmount;

        std::printf("    [10] wallContact before retract=%.4f after retract=%.4f\n",
                    contactBefore, contactAfter);
        note(contactAfter < contactBefore,
             "10. Retracting away from the wall naturally reduces contact force");
    }

    // --- continuity: advancing in small steps along the tight bend never
    // shows a discontinuous jump in advancementResistance - "remain smooth
    // and continuous", not a step function.
    {
        CatheterModel guide(paramsFor("Benchmark 071 (guide)"));
        guide.setPath(curved);
        float prevRes = -1.f;
        float maxStep = 0.f;
        bool first = true;
        for (float d = 0.002f; d <= 0.011f; d += 0.001f) {
            guide.setInsertionDepth(d);
            guide.update();
            const float r = guide.mechanics().advancementResistance;
            if (!first) maxStep = std::max(maxStep, std::fabs(r - prevRes));
            prevRes = r;
            first = false;
        }
        std::printf("    [continuity] largest single-step change in advancementResistance = %.4f\n",
                    maxStep);
        note(maxStep < 0.35f,
             "continuity. advancementResistance changes gradually along the bend, not in jumps");
    }

    // ─────────────────────────────────────────────────────────────────────
    // Mechanical CONSEQUENCES: the same device, A/B'd against itself on a
    // benign vs a challenging path, so the comparison isolates the effect
    // of the vessel rather than of a device swap.
    // ─────────────────────────────────────────────────────────────────────

    // --- V1. Mechanical state visibly changes on entering wall contact ---
    {
        CatheterModel guideFree(paramsFor("Benchmark 071 (guide)"));
        guideFree.setPath(straight);
        guideFree.setInsertionDepth(0.05f);
        guideFree.update();

        CatheterModel guideContact(paramsFor("Benchmark 071 (guide)"));
        guideContact.setPath(tapered);
        guideContact.setInsertionDepth(0.045f);
        guideContact.update();

        std::printf("    [V1] free: wallContact=%.4f advResist=%.4f regime=%s\n",
                    guideFree.mechanics().wallContactAmount, guideFree.mechanics().advancementResistance,
                    mechanicalRegimeName(guideFree.mechanics().regime));
        std::printf("    [V1] contact: wallContact=%.4f advResist=%.4f regime=%s\n",
                    guideContact.mechanics().wallContactAmount, guideContact.mechanics().advancementResistance,
                    mechanicalRegimeName(guideContact.mechanics().regime));
        note(guideContact.mechanics().wallContactAmount > guideFree.mechanics().wallContactAmount &&
                 guideContact.mechanics().advancementResistance > guideFree.mechanics().advancementResistance &&
                 guideContact.mechanics().regime != guideFree.mechanics().regime,
             "V1. Mechanical state changes meaningfully on entering wall contact");
    }

    // --- V2. Mechanical state visibly changes on entering tight curvature
    {
        CatheterModel guideStraight(paramsFor("Benchmark 071 (guide)"));
        guideStraight.setPath(straight);
        guideStraight.setInsertionDepth(0.008f);
        guideStraight.update();

        CatheterModel guideCurved(paramsFor("Benchmark 071 (guide)"));
        guideCurved.setPath(curved);
        guideCurved.setInsertionDepth(0.008f);
        guideCurved.update();

        std::printf("    [V2] straight: bendResist=%.4f regime=%s\n",
                    guideStraight.mechanics().bendingResistance,
                    mechanicalRegimeName(guideStraight.mechanics().regime));
        std::printf("    [V2] curved:   bendResist=%.4f regime=%s\n",
                    guideCurved.mechanics().bendingResistance,
                    mechanicalRegimeName(guideCurved.mechanics().regime));
        note(guideCurved.mechanics().bendingResistance > guideStraight.mechanics().bendingResistance &&
                 guideCurved.mechanics().regime == CatheterModel::MechanicalState::Regime::TightCurvature,
             "V2. Mechanical state changes meaningfully on entering tight curvature");
    }

    // --- V3. Support changes alter downstream device behavior CONTINUOUSLY
    // (not a step change) as the supporting device's own position moves -
    // "advance microcatheter after support reduction" is exactly a support
    // depthLimit shrinking on an otherwise-unchanged wire.
    {
        CatheterModel wire(paramsFor("Synchro-14"));
        wire.setPath(straight);
        wire.setInsertionDepth(0.05f);

        float prevSupport = -1.f;
        bool monotonic = true;
        bool sawZero = false, sawPositive = false;
        // Sweep the supporting device's own reach from well short of the
        // wire's tip to well past it - the same continuous quantity
        // MainWindow feeds in from a REAL host's insertionDepth every
        // syncDevices() call, just driven directly here.
        for (float hostDepth = 0.f; hostDepth <= 0.08f; hostDepth += 0.005f) {
            wire.setSupports({ {hostDepth, 0.90f} });
            wire.update();
            const float s = wire.mechanics().effectiveSupport;
            if (s <= 1e-4f) sawZero = true;
            if (s > 0.5f) sawPositive = true;
            if (prevSupport >= 0.f && s < prevSupport - 1e-4f) monotonic = false;
            prevSupport = s;
        }
        std::printf("    [V3] effectiveSupport swept from %s to >0.5 as host depth grew, monotonic=%s\n",
                    sawZero ? "0" : "(never zero)", monotonic ? "yes" : "no");
        note(sawZero && sawPositive && monotonic,
             "V3. Downstream device support responds continuously to the support device's position, "
             "not independently of it");
    }

    // ─────────────────────────────────────────────────────────────────────
    // Training-EVENT detection: state transitions only, never continuous
    // spam, using MechanicalEventDetector directly (the same detector
    // MainWindow::reportMechanicalEvents() drives).
    // ─────────────────────────────────────────────────────────────────────
    {
        CatheterModel guide(paramsFor("Benchmark 071 (guide)"));
        guide.setPath(straight);
        guide.setInsertionDepth(0.05f);
        guide.update();

        MechanicalEventDetector detector;
        auto ev0 = detector.update(guide.mechanics());
        note(ev0.empty(), "E1. The very first sample reports no event (nothing to transition from)");

        // Unchanged state, repeated - must stay silent (no spam).
        int spamCount = 0;
        for (int i = 0; i < 5; ++i) {
            guide.update();
            spamCount += static_cast<int>(detector.update(guide.mechanics()).size());
        }
        note(spamCount == 0, "E2. Repeated identical updates produce zero events (not continuous spam)");

        // Move onto the tapered path's contact zone - a genuine Free ->
        // (something) regime transition, reported exactly once.
        guide.setPath(tapered, /*keepDepth=*/false);
        guide.setInsertionDepth(0.045f);
        guide.update();
        auto evContact = detector.update(guide.mechanics());
        bool sawRegimeChange = false;
        for (const auto &ev : evContact) {
            if (ev.kind == MechanicalEvent::Kind::RegimeChanged) {
                sawRegimeChange = true;
                std::printf("    [E3] event: %s\n", ev.describe().c_str());
                note(ev.fromRegime == CatheterModel::MechanicalState::Regime::Free && ev.toRegime != ev.fromRegime,
                     "E3. Entering contact reports a RegimeChanged event with the correct FROM/TO");
            }
        }
        note(sawRegimeChange, "E3b. Entering contact produces at least one RegimeChanged event");

        // Repeat the SAME depth again - already reported, must not fire again.
        guide.update();
        auto evRepeat = detector.update(guide.mechanics());
        note(evRepeat.empty(), "E4. Re-observing the same regime again produces no further event");

        // reset() then re-observe the SAME state: no comparison to the
        // pre-reset regime, so still no event (only the starting point is
        // remembered) - this is exactly what protects a device switch from
        // being misreported as a mechanical event.
        detector.reset();
        auto evAfterReset = detector.update(guide.mechanics());
        note(evAfterReset.empty(), "E5. reset() discards history - the next sample alone is not an event");
    }

    // --- E6. High torsional load / loss-of-support events fire on their
    // own threshold crossings, independent of regime.
    {
        CatheterModel wire(paramsFor("Synchro-14"));
        wire.setPath(straight);
        wire.setInsertionDepth(0.04f);
        wire.update();   // starts with zero commanded roll, fully "supported" by default (no Support set -> effectiveSupport 0, so start from a known low-support baseline instead)

        MechanicalEventDetector detector;
        detector.update(wire.mechanics());   // establish baseline (no Support set: effectiveSupport already 0)

        // Command a large roll - torsionalResistance should cross the
        // "high" threshold on the very next update() (a big instantaneous
        // gap between commanded and realised roll).
        wire.setRollAngle(3.0f);
        wire.update();
        auto evTorsion = detector.update(wire.mechanics());
        bool sawHighTorsion = false;
        for (const auto &ev : evTorsion)
            if (ev.kind == MechanicalEvent::Kind::HighTorsionalLoad) sawHighTorsion = true;
        std::printf("    [E6] torsionalResistance after large commanded roll = %.4f\n",
                    wire.mechanics().torsionalResistance);
        note(sawHighTorsion, "E6. A large commanded roll fires a HighTorsionalLoad event");

        // Let it settle - repeated update()s ease rollAngleRealized toward
        // rollAngle, eventually dropping torsionalResistance back down and
        // firing the recovery event exactly once, when it actually crosses.
        bool sawNormalized = false;
        for (int i = 0; i < 200 && !sawNormalized; ++i) {
            wire.update();
            for (const auto &ev : detector.update(wire.mechanics()))
                if (ev.kind == MechanicalEvent::Kind::TorsionNormalized) sawNormalized = true;
        }
        note(sawNormalized, "E6b. Torsion settling back down fires a TorsionNormalized recovery event");
    }

    std::printf("\n%s\n", pass ? "ALL CHECKS PASSED" : "CHECK FAILED");
    return pass ? 0 : 1;
}
