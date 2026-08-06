#include "CardiacCycle.h"
#include <algorithm>
#include <cmath>

namespace cn {

namespace {

constexpr float kTwoPi = 6.28318530717958648f;

/// Asymmetric Gaussian: independent sigma before and after the peak.
///
/// Real ECG deflections are not symmetric — a T wave rises slowly and falls
/// faster, and the QRS downstroke is quicker than its upstroke. A single
/// symmetric Gaussian per wave cannot express that, and the result reads as a
/// stylised icon rather than a trace. `d` is wrapped so a wave sitting near
/// phase 0 or 1 still joins across the cycle boundary.
inline float agauss(float t, float mu, float sigmaLeft, float sigmaRight, float amp) {
    float d = t - mu;
    if (d >  0.5f) d -= 1.f;
    if (d < -0.5f) d += 1.f;
    const float s = (d < 0.f) ? sigmaLeft : sigmaRight;
    return amp * std::exp(-(d * d) / (2.f * s * s));
}

} // namespace

void CardiacCycle::setHeartRate(int bpm) {
    m_params.bpm = std::clamp(bpm, 30, 220);
}

float CardiacCycle::instantaneousBpm() const {
    const float rsa = 1.f + m_params.rsaFraction *
                                std::sin(kTwoPi * m_respPhase / std::max(m_params.respirationSec, 0.1f));
    return static_cast<float>(m_params.bpm) * rsa;
}

float CardiacCycle::cycleSeconds() const {
    return 60.f / std::max(instantaneousBpm(), 1.f);
}

float CardiacCycle::wrapped(float p) const {
    p = std::fmod(p, 1.f);
    return p < 0.f ? p + 1.f : p;
}

void CardiacCycle::advance(float dtSeconds) {
    if (dtSeconds <= 0.f) return;
    m_respPhase += dtSeconds;

    const float before = m_phase;
    m_phase += dtSeconds / std::max(cycleSeconds(), 1e-4f);
    if (m_phase >= 1.f) {
        m_phase -= std::floor(m_phase);
        ++m_beats;
        // New beat: redraw the amplitude scale. xorshift keeps this
        // deterministic and dependency-free.
        m_rng ^= m_rng << 13; m_rng ^= m_rng >> 17; m_rng ^= m_rng << 5;
        const float u = static_cast<float>(m_rng & 0xFFFFFF) / 16777215.f;   // 0..1
        m_beatAmp = 1.f + m_params.beatVariation * (2.f * u - 1.f);
    }
    (void)before;
}

// ─────────────────────────────────────────────────────────────────────────────
float CardiacCycle::ecg(float p) const {
    p = wrapped(p);
    float v = 0.f;

    // P wave — atrial depolarisation. Small, low, gently rounded.
    v += agauss(p, 0.095f, 0.016f, 0.020f, 0.13f);

    // PR segment is isoelectric: no term, deliberately.

    // Q — narrow negative dip immediately before R.
    v += agauss(p, 0.192f, 0.006f, 0.006f, -0.10f * m_beatAmp);

    // R — the tall narrow spike, downstroke slightly faster than the upstroke.
    v += agauss(p, 0.205f, 0.009f, 0.007f, 1.15f * m_beatAmp);

    // S — deeper and narrower than Q, quick recovery to baseline.
    v += agauss(p, 0.222f, 0.007f, 0.014f, -0.28f * m_beatAmp);

    // ST segment isoelectric, then...

    // T wave — broad and distinctly asymmetric, slow rise and quicker fall.
    // This asymmetry is the strongest single cue that a T wave is real.
    v += agauss(p, 0.400f, 0.075f, 0.045f, 0.32f * m_beatAmp);

    // U wave — barely visible on a real monitor, which is the point.
    v += agauss(p, 0.500f, 0.030f, 0.030f, 0.045f);

    // Baseline wander on the respiratory cycle.
    v += m_params.baselineWander *
         std::sin(kTwoPi * m_respPhase / std::max(m_params.respirationSec, 0.1f));
    return v;
}

// ─────────────────────────────────────────────────────────────────────────────
// Aortic pressure pulse, phase-locked to the same cycle.
//
// Shape, in order: a steep systolic upstroke starting one pre-ejection period
// after R, a rounded peak, the dicrotic notch as the aortic valve shuts, a small
// rebound, then the diastolic runoff decay.
float CardiacCycle::arterialPressure(float p) const {
    const float ejection = 0.205f + m_params.preEjectionSec / std::max(cycleSeconds(), 1e-4f);
    float d = wrapped(p) - wrapped(ejection);
    if (d < 0.f) d += 1.f;                      // time since ejection began

    const float kDiastolic = 0.34f;             // trough, normalised
    const float kPulse     = 0.66f;             // systolic rise above it

    const float upstroke = 0.055f;              // to peak
    const float notchAt  = 0.230f;              // valve closure

    if (d < upstroke) {
        // Steep, slightly S-shaped rise.
        const float t = d / upstroke;
        return kDiastolic + kPulse * (t * t * (3.f - 2.f * t));
    }
    if (d < notchAt) {
        // Rounded systolic peak decaying toward the notch.
        const float t = (d - upstroke) / (notchAt - upstroke);
        return kDiastolic + kPulse * (1.f - 0.28f * t * t);
    }
    // Notch, then the runoff.
    const float notchDepth = 0.10f * std::exp(-std::pow((d - notchAt) / 0.022f, 2.f));
    const float t          = (d - notchAt) / (1.f - notchAt);
    const float runoff     = kDiastolic + kPulse * 0.72f * std::exp(-3.2f * t);
    return runoff - kPulse * notchDepth;
}

float CardiacCycle::pleth(float p) const {
    // The peripheral waveform is the pressure pulse, smoothed and delayed by the
    // transit time out to a fingertip.
    const float delay = 0.22f / std::max(cycleSeconds(), 1e-4f);
    const float a = arterialPressure(p - delay);
    const float b = arterialPressure(p - delay - 0.02f);
    return std::clamp(0.6f * a + 0.4f * b, 0.f, 1.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// wallWaveform — what the vessel wall actually follows.
//
// Smoothstep in, hold, smoothstep out. Both transitions have zero derivative at
// each end, so the fill eases in rather than snapping and the release settles
// rather than stopping dead; and because the curve is flat at both 0 and 1 the
// wrap between beats is invisible.
//
// Anchored to the same ejection onset as the pressure pulse, so the balloon
// still begins one pre-ejection period after the R peak.
float CardiacCycle::wallWaveform(float p) const {
    const float cyc      = std::max(cycleSeconds(), 1e-4f);
    const float ejection = 0.205f + m_params.preEjectionSec / cyc;

    float u = wrapped(p) - wrapped(ejection);      // cycles since ejection began
    if (u < 0.f) u += 1.f;

    const float fill = std::clamp(m_params.fillFraction, 0.02f, 0.9f);
    const float hold = std::clamp(m_params.holdFraction, 0.f, 1.f - fill - 0.02f);

    float balloon;
    if (u < fill) {
        const float t = u / fill;
        balloon = t * t * (3.f - 2.f * t);                    // fill
    } else if (u < fill + hold) {
        balloon = 1.f;                                        // brief hold at peak
    } else {
        const float t = (u - fill - hold) / std::max(1.f - fill - hold, 1e-4f);
        balloon = 1.f - t * t * (3.f - 2.f * t);              // release
    }

    // The physiological alternative, kept so the two can be blended or compared.
    const float kDiastolic = 0.34f, kPulse = 0.66f;
    const float pressure = std::clamp((arterialPressure(p) - kDiastolic) / kPulse, 0.f, 1.f);

    const float b = std::clamp(m_params.balloonBlend, 0.f, 1.f);
    return pressure + (balloon - pressure) * b;
}

float CardiacCycle::distension(float distanceFromRoot) const {
    const float cyc = std::max(cycleSeconds(), 1e-4f);

    // Propagation delay out to this point, expressed in cycles.
    const float travel = std::max(distanceFromRoot, 0.f) /
                         std::max(m_params.pulseWaveSpeed, 1e-3f);
    const float p = m_phase - travel / cyc;

    return m_params.distensionStrain * wallWaveform(p) * m_beatAmp;
}

} // namespace cn