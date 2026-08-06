#pragma once
#include <cstdint>

namespace cn {

// ─────────────────────────────────────────────────────────────────────────────
// CardiacCycle — one clock for the whole console.
//
// The ECG trace and the pulsating vessel wall are two views of the same event,
// so they have to share a phase. Driving them from separate timers is what makes
// a simulator look wrong: the wall expands on a beat the trace has not drawn yet,
// or drifts a few percent per minute until the two are in antiphase.
//
// Phase runs 0..1 over one cardiac cycle. Within it:
//
//     0.09   P wave        atrial depolarisation
//     0.20   R peak        ventricular depolarisation  <- electrical reference
//     0.22   S
//     0.40   T wave        ventricular repolarisation
//
// Mechanical events LAG the electrical ones. Ventricular ejection begins about
// 60-80 ms after the R peak (the pre-ejection period: isovolumic contraction has
// to raise ventricular pressure above aortic before the valve opens), and the
// pressure pulse then travels down the arterial tree at a finite speed. Both
// delays are modelled, because a wall that expands exactly on the R spike is the
// single most obvious tell that a pulsation is decorative.
//
// Qt-free so it can be unit tested with the rest of the core.
// ─────────────────────────────────────────────────────────────────────────────
class CardiacCycle {
public:
    struct Params {
        int   bpm = 72;                  ///< clamped to [30, 220]

        /// Respiratory sinus arrhythmia. Real heart rate is not metronomic: it
        /// rises on inspiration and falls on expiration by a few percent. This
        /// small wobble is most of what separates a live trace from a loop.
        float rsaFraction     = 0.03f;   ///< peak deviation, fraction of rate
        float respirationSec  = 4.0f;    ///< breath period

        /// Beat-to-beat amplitude variation, again a few percent, applied to
        /// the R and T waves so successive complexes are not pixel-identical.
        float beatVariation   = 0.04f;

        /// Baseline wander on the respiratory cycle — real leads are never flat.
        float baselineWander  = 0.02f;

        /// Pre-ejection period: R peak to the start of the aortic upstroke.
        float preEjectionSec  = 0.07f;

        /// Pulse wave velocity, model units per second. The pressure wave is not
        /// instantaneous: at ~6 m/s it takes ~170 ms to travel a 1 m aorta, an
        /// appreciable fraction of an 830 ms cycle, so distal vessels visibly
        /// pulse after proximal ones.
        float pulseWaveSpeed  = 6.0f;

        /// Peak fractional increase in vessel RADIUS at systole. Large elastic
        /// arteries distend about 8-10% in diameter, muscular arteries rather
        /// less; 0.045 on the radius is a reasonable whole-tree average.
        float distensionStrain = 0.045f;

        // ── Wall motion shape ───────────────────────────────────────────────
        // What the wall follows is NOT the pressure trace. An aortic pressure
        // pulse rises in about 46 ms at 72 bpm, carries a dicrotic notch and
        // then decays exponentially — as wall motion that reads as a flick and
        // a stutter, not as a chamber filling.
        //
        // The wall instead follows a chamber-volume shape: a smooth fill, a
        // brief hold at peak, then a smooth release. Both ends are flat in
        // derivative, so there is no visible tick where the cycle wraps.
        //
        // Timing is unchanged — the shape still starts one pre-ejection period
        // after the R peak and still propagates at pulseWaveSpeed — so the wall
        // remains locked to the same beat the ECG is drawing.

        /// 0 = follow the pressure pulse exactly, 1 = pure balloon fill/release.
        float balloonBlend  = 1.0f;
        /// Fraction of the cycle spent filling.
        float fillFraction  = 0.36f;
        /// Fraction held at full before the release begins.
        float holdFraction  = 0.08f;
    };

    CardiacCycle() = default;
    explicit CardiacCycle(const Params &p) : m_params(p) {}

    void setParams(const Params &p) { m_params = p; }
    const Params &params() const { return m_params; }

    void setHeartRate(int bpm);
    int  heartRate() const { return m_params.bpm; }

    /// Advance the clock. Call once per frame with the elapsed wall time.
    void advance(float dtSeconds);

    /// Phase within the current cycle, 0..1. 0.20 is the R peak.
    float phase() const { return m_phase; }
    /// Seconds per cycle at the instantaneous (RSA-modulated) rate.
    float cycleSeconds() const;
    /// Instantaneous rate including respiratory variation, for the read-out.
    float instantaneousBpm() const;
    /// Cycles completed since construction — lets a caller detect a new beat.
    uint64_t beatCount() const { return m_beats; }

    // ── Waveforms, all as a function of phase ───────────────────────────────
    /// Lead II ECG in normalised units; the R peak is about +1.15.
    float ecg(float phase) const;
    /// Arterial pressure 0..1, with a dicrotic notch at valve closure.
    float arterialPressure(float phase) const;
    /// Pulse oximeter plethysmograph 0..1.
    float pleth(float phase) const;

    /// Normalised wall-motion waveform, 0 at end-diastole and 1 at peak fill.
    /// This is what the vessel geometry follows, and what VesselView samples
    /// into its shader LUT — keeping one definition means the wall and the
    /// trace cannot be edited apart.
    float wallWaveform(float phase) const;

    /// Fractional change in vessel RADIUS at a point `distanceFromRoot` along
    /// the arterial tree, in model units. Multiply a local lumen radius by
    /// (1 + this) to get the instantaneous radius.
    ///
    /// This is the arterial pressure pulse delayed by the pre-ejection period
    /// and by the propagation time to that point — not the ECG, which is the
    /// electrical event that triggers it.
    float distension(float distanceFromRoot) const;

    /// Convenience: current ECG/pressure/pleth sample at the live phase.
    float ecgNow() const { return ecg(m_phase); }
    float pressureNow() const { return arterialPressure(m_phase); }
    float plethNow() const { return pleth(m_phase); }

private:
    float wrapped(float p) const;

    Params   m_params;
    float    m_phase       = 0.f;
    float    m_respPhase   = 0.f;
    float    m_beatAmp     = 1.f;   ///< amplitude scale for the current beat
    uint64_t m_beats       = 0;
    uint32_t m_rng         = 0x9E3779B9u;
};

} // namespace cn