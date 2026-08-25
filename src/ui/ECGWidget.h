#pragma once
#include <QWidget>
#include <QTimer>
#include <QVector>
#include <QPainter>

#include "core/CardiacCycle.h"

<<<<<<< HEAD
/// A real bedside monitor never shows ECG alone: the trace field always
/// carries arterial pressure and pulse-ox pleth stacked with it, and the
/// numeric column always carries HR, SpO2, a BP pair, and respiratory rate
/// together — that combined look is what actually reads as "clinical
/// monitor" rather than "isolated ECG strip", which is what this widget was
/// before this pass.
///
/// An earlier request had deliberately stripped the pressure trace out of
/// this widget down to ECG-only ("keep only the relevant ECG waveform and
/// clinical values"). This reverses that, on a new explicit request to match
/// a real monitor reference image — the physiology underneath was never
/// touched either time (cn::CardiacCycle::arterialPressure()/pleth() have
/// been sitting there fully implemented throughout), only which of it this
/// widget chooses to draw.
=======
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7
class ECGWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ECGWidget(QWidget* parent = nullptr);

    /// Share the console's cardiac clock. The trace is then a view of the same
    /// phase that drives the pulsating vessel wall, so the two can never drift:
    /// previously this widget ran its own 16 ms timer and its own phase, and any
    /// difference in rounding accumulated over a session.
    void setCardiacCycle(const cn::CardiacCycle *c) { m_cycle = c; update(); }

    /// Only used when no shared cycle is attached (standalone use).
    void setHeartRate(int bpm);
    void triggerEvent(const QString& label, int durationMs = 3000);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTick();

private:
    void rebuildBuffer();

    const cn::CardiacCycle *m_cycle = nullptr;
    cn::CardiacCycle m_ownCycle;   ///< fallback when no shared clock is set

    QTimer m_timer;
<<<<<<< HEAD

    // Three ring buffers sharing one write position, so all three traces
    // scroll in lockstep — a bedside monitor's ECG/pressure/pleth are three
    // views of the exact same beat, never three independently-scrolling
    // strips.
    QVector<double> m_ecgBuffer;
    QVector<double> m_pressureBuffer;   ///< cn::CardiacCycle::arterialPressure(), 0..1
    QVector<double> m_plethBuffer;      ///< cn::CardiacCycle::pleth(), 0..1
    int m_writePos = 0;

    /// Slow independent phase driving SpO2's small natural wobble (real
    /// pulse-oximeter readings drift a percent or two breath to breath, from
    /// perfusion changes on inspiration/expiration) — deliberately NOT tied
    /// to cn::CardiacCycle's own respiration phase, since that is private to
    /// the class; this is a widget-local cosmetic approximation of the same
    /// real effect, documented as such rather than presented as a measured
    /// value.
    float m_spo2Phase = 0.f;

    QString m_eventLabel;
    bool m_inEvent = false;

    // ── ECG trace placement (unchanged from the ECG-only version) ──────────
    // Baseline position and vertical gain, both as a fraction of the field
    // height. Solved against the ECG waveform's actual verified range
    // (R peak ~+1.18 with beat-to-beat variation at its maximum, S dip
    // ~-0.24) rather than guessed, so the R spike and the Q/S dip both sit
    // with a safe margin inside the field with no clipping.
    static constexpr double kEcgBaselineFraction = 0.72;
    static constexpr double kEcgGainFraction     = 0.34;

    // ── Arterial pressure trace placement ───────────────────────────────────
    // arterialPressure() is already normalised to roughly [0.34, 1.0]
    // (CardiacCycle.cpp's own kDiastolic/kPulse constants) - baseline sits
    // near the TOP of the field and gain is negative-going upward, the same
    // "y = base - value*gain" convention the ECG trace already uses, so a
    // rising pressure draws a rising line rather than an inverted one.
    static constexpr double kPressureBaselineFraction = 0.30;
    static constexpr double kPressureGainFraction     = 0.26;

    // ── Pleth trace placement ───────────────────────────────────────────────
    // Smaller and lower than the pressure trace it is derived from (a real
    // pulse-ox waveform reads as a softer, smaller companion trace under the
    // arterial line on a combined display, not an equally-prominent one).
    static constexpr double kPlethBaselineFraction = 0.50;
    static constexpr double kPlethGainFraction     = 0.14;

    const QColor BG_COLOR       = QColor(0, 0, 0);
    /// Softened from a fully-saturated neon (0,255,100) to a gentler mint —
    /// per "simple, soft, lightweight", the ECG trace no longer needs to be
    /// the most visually aggressive element on screen to still read clearly
    /// against black.
    const QColor ECG_COLOR      = QColor(70, 220, 140);
    /// Dark, slightly brown-red — an arterial line trace is never drawn in
    /// the same saturated red used for the numeric BP read-out on a real
    /// monitor; the two are visually distinct on purpose so the eye doesn't
    /// conflate "trace colour" with "alarm-relevant numeral colour".
    const QColor PRESSURE_COLOR = QColor(185, 95, 88);
    /// Pale, desaturated — a pleth trace reads as the faintest of the three
    /// on a real display, present but clearly secondary to ECG and pressure.
    const QColor PLETH_COLOR    = QColor(205, 225, 218, 180);

    const QColor SPO2_COLOR = QColor(232, 198, 82);     // softened gold, not pure yellow
    const QColor BP_COLOR   = QColor(210, 96, 90);       // softened red
    const QColor RR_COLOR   = QColor(84, 196, 196);      // softened teal
=======
    QVector<double> m_ecgBuffer, m_abpBuffer;
    int m_writePos = 0;
    // Phase and respiratory wander moved into cn::CardiacCycle, which is the
    // single source of cardiac timing for the whole console.
    QString m_eventLabel;
    bool m_inEvent = false;

    // Displayed pressures. Held here rather than printed inline so the numerals
    // and the trace cannot disagree.
    static constexpr double kSystolicMmHg  = 118.0;
    static constexpr double kDiastolicMmHg =  76.0;
    /// Fixed pressure axis. Not auto-scaled: a trace that silently rescales
    /// itself between beats is unreadable as a measurement.
    static constexpr double kScaleTopMmHg  = 180.0;
    static constexpr double kScaleBotMmHg  =  40.0;
    /// ECG amplitude as a fraction of the field height. Small on purpose — it
    /// shares the field with the pressure trace rather than owning a lane.
    static constexpr double kEcgHeightFraction = 0.17;

    const QColor BG_COLOR   = QColor(0, 0, 0);
    // GRID_COLOR removed with the grid — it was the only non-black element
    // in the panel. Re-add as a neutral dark grey if the ruling is wanted back.
    const QColor ECG_COLOR  = QColor(0, 255, 100);
    const QColor ABP_COLOR  = QColor(255, 60, 60);
    const QColor RESP_COLOR = QColor(230, 230, 230);
    // The plethysmograph lane and its PLETH_COLOR were removed. cn::CardiacCycle
    // still provides pleth(); only this display is gone.
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7
};