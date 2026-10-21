// Proves DoseModel's core behaviors:
//   1. Nothing accumulates while fluoro is off - kVp/mA still read live,
//      but DAP/kerma/patient/physician dose and their current rates are
//      all zero.
//   2. Dose genuinely accumulates over real elapsed time while fluoro is
//      on, and stops the instant it turns back off (no drift afterward).
//   3. mA rises as the field of view narrows (higher magnification) -
//      the one live input this reduced model actually has for the
//      generator's automatic exposure control.
//   4. Physician dose rate rises with gantry angle (LAO/CRAN) - the one
//      place gantry angle has a real dosimetric consequence in this model.
//   5. reset() clears accumulated dose but not the live mA read-out.
#include "DoseModel.h"
#include <cstdio>
#include <cmath>

using namespace cn;

int main() {
    DoseModel dose;

    // --- Test 1: fluoro off - technique reads live, dose does not move ---
    dose.tick(1.0f, /*fluoroActive=*/false, 25.f, 0.f, 0.f);
    const bool kvpLiveWhenOff = std::fabs(dose.kvp() - 78.f) < 1e-3f;
    const bool maLiveWhenOff  = dose.currentMa() > 0.f;
    const bool noDoseWhenOff  = dose.totalPatientDapGyCm2() == 0.f &&
                               dose.totalAirKermaMGy() == 0.f &&
                               dose.totalPhysicianDoseUSv() == 0.f;
    const bool noRateWhenOff  = dose.currentPatientRateGyCm2PerS() == 0.f &&
                               dose.currentPhysicianRateUSvPerHour() == 0.f;
    std::printf("[1a] kVp reads live even with fluoro off: %s  (expect YES, got %.1f)\n",
                kvpLiveWhenOff ? "yes" : "NO", dose.kvp());
    std::printf("[1b] mA reads live even with fluoro off: %s  (expect YES, got %.1f)\n",
                maLiveWhenOff ? "yes" : "NO", dose.currentMa());
    std::printf("[1c] no accumulated dose while off: %s  (expect YES)\n", noDoseWhenOff ? "yes" : "NO");
    std::printf("[1d] no dose RATE while off: %s  (expect YES)\n", noRateWhenOff ? "yes" : "NO");

    // --- Test 2: fluoro on for 60 real seconds - dose accumulates, then
    // stops the instant it turns back off ---
    for (int i = 0; i < 60; ++i) dose.tick(1.0f, true, 25.f, 0.f, 0.f);
    const float dapAfter60s = dose.totalPatientDapGyCm2();
    const bool accumulatedWhileOn = dapAfter60s > 0.f;
    dose.tick(1.0f, false, 25.f, 0.f, 0.f);   // one more tick, fluoro OFF
    const bool stoppedImmediately = dose.totalPatientDapGyCm2() == dapAfter60s;
    std::printf("[2a] DAP accumulated after 60s active fluoro: %s  (got %.4f Gy.cm^2)\n",
                accumulatedWhileOn ? "yes" : "NO", dapAfter60s);
    std::printf("[2b] DAP does not drift further once fluoro turns off: %s\n",
                stoppedImmediately ? "yes" : "NO");

    // --- Test 3: mA rises as FOV narrows (higher magnification) ---
    DoseModel wide, narrow;
    wide.tick(1.0f, false, 25.f, 0.f, 0.f);     // reference FOV
    narrow.tick(1.0f, false, 12.5f, 0.f, 0.f);  // half the FOV - more magnified
    const bool maRisesWithMag = narrow.currentMa() > wide.currentMa();
    std::printf("[3] mA at 12.5cm FOV (%.1f) > mA at 25cm FOV (%.1f): %s\n",
                narrow.currentMa(), wide.currentMa(), maRisesWithMag ? "yes" : "NO");

    // --- Test 4: physician dose rate rises with gantry angle ---
    DoseModel ap, oblique;
    ap.tick(1.0f, true, 25.f, 0.f, 0.f);        // straight AP
    oblique.tick(1.0f, true, 25.f, 45.f, 20.f); // steep LAO 45 / CRAN 20
    const bool physicianRisesWithAngle =
        oblique.currentPhysicianRateUSvPerHour() > ap.currentPhysicianRateUSvPerHour();
    std::printf("[4] physician rate at steep oblique (%.2f uSv/h) > at AP (%.2f uSv/h): %s\n",
                oblique.currentPhysicianRateUSvPerHour(), ap.currentPhysicianRateUSvPerHour(),
                physicianRisesWithAngle ? "yes" : "NO");
    // Patient dose rate should be UNCHANGED by angle alone in this reduced
    // model (see class comment - no patient-thickness variable to drive an
    // angle-dependent patient-side effect from).
    const bool patientRateAngleInvariant =
        std::fabs(ap.currentPatientRateGyCm2PerS() - oblique.currentPatientRateGyCm2PerS()) < 1e-9f;
    std::printf("    patient dose rate unaffected by angle alone: %s\n",
                patientRateAngleInvariant ? "yes" : "NO");

    // --- Test 5: reset() clears dose, keeps live mA ---
    dose.reset();
    const bool doseResetOk = dose.totalPatientDapGyCm2() == 0.f && dose.totalAirKermaMGy() == 0.f &&
                             dose.totalPhysicianDoseUSv() == 0.f;
    const bool maSurvivesReset = dose.currentMa() > 0.f;
    std::printf("[5a] reset() clears accumulated dose: %s\n", doseResetOk ? "yes" : "NO");
    std::printf("[5b] reset() does not zero the live mA read-out: %s\n", maSurvivesReset ? "yes" : "NO");

    const bool pass = kvpLiveWhenOff && maLiveWhenOff && noDoseWhenOff && noRateWhenOff &&
                      accumulatedWhileOn && stoppedImmediately && maRisesWithMag &&
                      physicianRisesWithAngle && patientRateAngleInvariant &&
                      doseResetOk && maSurvivesReset;
    std::printf("\n%s\n", pass ? "ALL CHECKS PASSED" : "CHECK FAILED");
    return pass ? 0 : 1;
}
