#pragma once
#include <algorithm>
#include <cmath>

namespace cn {

/// Fluoroscopic dose/generator model.
///
/// Every dose-related number in this console (kVp, mA, DAP, air kerma,
/// patient/physician dose) used to be a fixed console default - honestly
/// disclosed in the UI's own comments as "no generator/dosimetry model to
/// draw real numbers from", but a real gap all the same: the numbers never
/// moved no matter how long fluoro was on, how the C-arm was angled, or how
/// magnified the field was. This is a REDUCED model, the same precedent
/// EmbolizationModel/CatheterModel already establish for this codebase -
/// not a claim of clinical dosimetry precision, but genuinely time-
/// integrated and genuinely responsive to what the operator actually
/// controls, rather than a static label.
///
/// Two things vary the generator technique (kVp, mA):
///
///   kVp is held CONSTANT at the baseline. Real kVp modulation responds to
///   patient thickness, and this simulator has no patient-thickness
///   variable to drive it from - inventing kVp movement with nothing real
///   behind it would be less honest than a steady, disclosed baseline.
///
///   mA moves with magnification (Params::referenceFovCm vs. the live FOV
///   the operator has actually dialled in via the C-arm SID/MAG controls)
///   - a real automatic exposure control pushes more current through a
///   more collimated, more magnified field to hold image brightness
///   constant over a smaller detector area, so a genuinely live magnification
///   value is exactly the input a real console's own AEC would react to.
///
/// Dose accumulates ONLY while fluoro is actually active (tick()'s own
/// `fluoroActive` flag) - the generator technique (kVp/mA) is a "what the
/// console is prepared to use" read-out and stays live regardless, the
/// same way a real console's technique display persists between exposures,
/// but DAP/air-kerma/patient/physician dose are genuine integrals of real
/// elapsed beam-on time and never move while the beam is off.
///
/// Patient dose (DAP, air kerma) scales with mA alone - more current means
/// more photons means more dose, independent of gantry angle in this
/// reduced model (a real patient-thickness-dependent path-length effect at
/// oblique angles is not modelled, since there is no patient anatomy
/// thickness variable to drive it from here either).
///
/// Physician (operator) dose is modelled as a small SCATTER fraction of
/// the patient's own air-kerma rate, not a fraction of the direct beam -
/// real operator exposure comes from scatter off the patient, not the
/// primary beam. That scatter fraction genuinely does depend on gantry
/// angle: a steep LAO/RAO or CRAN/CAUD angle brings the operator's typical
/// standing position measurably closer to the beam's exit path than a
/// straight AP view does, so physician dose rate rises with how far off
/// AP/zero the current angle is - the one place this model gives gantry
/// angle a real dosimetric consequence, matching genuine radiation-safety
/// practice (why operators are taught steep obliques cost them more).
class DoseModel {
public:
    struct Params {
        float baselineKvp = 78.f;     ///< held constant - see class comment
        float baselineMa  = 320.f;    ///< mA at referenceFovCm magnification
        float referenceFovCm = 25.f;  ///< the FOV the baseline mA above assumes

        /// mA scales as (referenceFovCm / liveFovCm)^2 - halving the FOV
        /// (roughly doubling optical magnification) quadruples the current
        /// needed to hold brightness over a quarter of the detector area, the
        /// same inverse-square relationship a real AEC follows. Clamped so a
        /// very small or very large FOV cannot drive current to an absurd
        /// extreme.
        float maMin = 100.f;
        float maMax = 800.f;

        /// DAP accumulation rate, Gy·cm² per (mA·second). Reasoned from a
        /// real-world neuro-fluoro DAP rate of roughly 0.2 Gy·cm²/min at
        /// this baseline mA (cerebral fluoro runs measurably lower than
        /// cardiac/abdominal), not a clinical dosimetry claim: (0.2/60) /
        /// 320 ≈ 1.04e-5.
        float dapPerMaSecond = 1.04e-5f;

        /// Air-kerma accumulation rate, mGy per (mA·second). Reasoned from
        /// a real-world reference-point kerma rate of roughly 15 mGy/min at
        /// this baseline mA: (15/60) / 320 ≈ 7.81e-4.
        float kermaPerMaSecond = 7.81e-4f;

        /// Fraction of the patient's own air-kerma RATE that reaches the
        /// operator as scatter at a straight AP/zero angle - real
        /// unshielded scatter at typical table-side distance is on the
        /// order of a tenth of a percent of the reference-point kerma rate.
        float physicianScatterFraction = 0.0010f;

        /// Extra scatter fraction per degree of LAO/RAO or CRAN/CAUD off
        /// zero - a steep oblique angle measurably increases operator
        /// exposure over a straight AP view; this is what gives gantry
        /// angle a genuine dosimetric consequence (see class comment).
        float physicianAngleGainPerDegree = 0.012f;
    };

    void setParams(const Params &p) { m_params = p; }
    const Params &params() const { return m_params; }

    /// Advances the model by `dtSeconds` of REAL elapsed time, given the
    /// live C-arm state. Call once per real-time tick (this console's own
    /// procedure timer already ticks once a second - see MainWindow's own
    /// wiring) with the CURRENT fluoro-active flag, field of view (cm),
    /// and LAO/CRAN angles (degrees, either sign).
    ///
    /// kVp/mA (the "ready" generator technique) update regardless of
    /// `fluoroActive`. DAP/air-kerma/patient/physician dose only
    /// accumulate while `fluoroActive` is true - see class comment.
    void tick(float dtSeconds, bool fluoroActive, float fovCm, float laoDeg, float cranDeg) {
        const float safeFov = std::max(fovCm, 1.0f);
        m_currentMa = std::clamp(
            m_params.baselineMa * (m_params.referenceFovCm / safeFov) * (m_params.referenceFovCm / safeFov),
            m_params.maMin, m_params.maMax);

        if (!fluoroActive || dtSeconds <= 0.f) {
            m_currentPatientRateGyCm2PerS = 0.f;
            m_currentPhysicianRateUSvPerHour = 0.f;
            return;
        }

        m_currentPatientRateGyCm2PerS = m_currentMa * m_params.dapPerMaSecond;
        m_totalPatientDapGyCm2 += m_currentPatientRateGyCm2PerS * dtSeconds;

        const float kermaRateMGyPerS = m_currentMa * m_params.kermaPerMaSecond;
        m_totalAirKermaMGy += kermaRateMGyPerS * dtSeconds;

        // Scatter fraction rises with how far off AP/zero the gantry is -
        // see class comment. Converted mGy/s -> uSv/h for the physician
        // rate (x1000 for mGy->uGy, uGy~=uSv for this purpose, x3600 for
        // per-second -> per-hour) since that is the clinically familiar
        // occupational-dose unit or real dosimeter badges report in.
        const float angleFactor = 1.f + m_params.physicianAngleGainPerDegree *
                                            (std::fabs(laoDeg) + std::fabs(cranDeg));
        const float scatterFraction = m_params.physicianScatterFraction * angleFactor;
        m_currentPhysicianRateUSvPerHour = kermaRateMGyPerS * 1000.f * scatterFraction * 3600.f;
        m_totalPhysicianDoseUSv += (m_currentPhysicianRateUSvPerHour / 3600.f) * dtSeconds;
    }

    float kvp() const { return m_params.baselineKvp; }
    float currentMa() const { return m_currentMa; }

    float totalPatientDapGyCm2() const { return m_totalPatientDapGyCm2; }
    float totalAirKermaMGy() const { return m_totalAirKermaMGy; }
    float totalPhysicianDoseUSv() const { return m_totalPhysicianDoseUSv; }

    /// Current DAP accumulation rate, Gy·cm²/s - zero whenever fluoro is
    /// not active. Matches the manual's own "Current patient dose [Gy
    /// cm²/s]" live read-out.
    float currentPatientRateGyCm2PerS() const { return m_currentPatientRateGyCm2PerS; }
    /// Current operator scatter-dose rate, µSv/h - zero whenever fluoro is
    /// not active. Matches the manual's own "Current physician dose
    /// [µSv/h]" live read-out.
    float currentPhysicianRateUSvPerHour() const { return m_currentPhysicianRateUSvPerHour; }

    void reset() {
        m_totalPatientDapGyCm2 = 0.f;
        m_totalAirKermaMGy = 0.f;
        m_totalPhysicianDoseUSv = 0.f;
        m_currentPatientRateGyCm2PerS = 0.f;
        m_currentPhysicianRateUSvPerHour = 0.f;
        // m_currentMa deliberately NOT reset - it is a live read-out of the
        // current C-arm magnification, not accumulated session state, so
        // starting a new case does not mean it should jump to zero.
    }

private:
    Params m_params;
    float m_totalPatientDapGyCm2 = 0.f;
    float m_totalAirKermaMGy = 0.f;
    float m_totalPhysicianDoseUSv = 0.f;
    float m_currentMa = 0.f;
    float m_currentPatientRateGyCm2PerS = 0.f;
    float m_currentPhysicianRateUSvPerHour = 0.f;
};

} // namespace cn
