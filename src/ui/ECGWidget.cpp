#include "ECGWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>

// The waveform shape lives in cn::CardiacCycle alongside the phase that
// drives the pulsating vessel wall, so every trace here and the wall are
// views of one beat and cannot drift apart. This widget only samples and
// draws it.

ECGWidget::ECGWidget(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    connect(&m_timer, &QTimer::timeout, this, &ECGWidget::onTick);
    m_timer.start(16);   // ~60 fps
    m_sweepClock.start();

    rebuildBuffer();
}

void ECGWidget::setHeartRate(int bpm) {
    // Range matches cn::CardiacCycle's own clamp.
    m_ownCycle.setHeartRate(bpm);
}

void ECGWidget::triggerEvent(const QString &label, int durationMs) {
    m_inEvent = true;
    m_eventLabel = label;
    QTimer::singleShot(durationMs, this, [this] {
        m_inEvent = false;
        m_eventLabel.clear();
        update();
    });
    update();
}

void ECGWidget::rebuildBuffer() {
    const int w = qMax(width(), 200);
    m_ecgBuffer.resize(w, 0.0);
    m_pressureBuffer.resize(w, 0.0);
    m_plethBuffer.resize(w, 0.0);
    m_writePos = 0;
}

void ECGWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    rebuildBuffer();
}

void ECGWidget::onTick() {
    // One clock for the whole console. When MainWindow has attached its shared
    // cn::CardiacCycle we only SAMPLE it — advancing it here as well would step
    // the phase twice per frame and run the heart at double rate. This is the
    // same mechanism that keeps the heart-region vessel pulsation synchronized
    // with every trace drawn here: all of it reads the same m_cycle, so there
    // is nothing here that needs separate "synchronization" logic — sharing
    // the clock IS the synchronization.
    const cn::CardiacCycle *cyc = m_cycle ? m_cycle : &m_ownCycle;

    // Real elapsed time since the LAST tick, not an assumed-fixed 16ms per
    // callback. Under heavy GL rendering load elsewhere in the app (busy
    // 3D/Fluoro paintGL competing for the same single-threaded event loop),
    // this widget's own 16ms QTimer can fire late or irregularly. The shared
    // cardiac phase keeps advancing correctly in real time regardless (see
    // MainWindow's own onCardiacTick(), which measures real elapsed time via
    // its own QElapsedTimer) - so a late callback here still sees a phase
    // that jumped forward by the FULL real elapsed amount, not a nominal
    // 16ms worth. Writing exactly one buffer sample per callback in that
    // case packed more phase progression into the same one pixel of scroll,
    // which is what made the trace look correctly spaced when the app was
    // minimized (nothing competing for the event loop, callbacks land on
    // time) and visibly compressed when actively on screen (callbacks
    // delayed, each one still only advancing a single pixel).
    const qint64 elapsedMs = m_sweepClock.restart();
    const double dtSeconds = elapsedMs / 1000.0;

    if (!m_cycle) {
        // Standalone fallback. The event speed-up only applies here; when a
        // shared clock is in use, its owner decides the rate. Now driven by
        // the same real elapsed time as the step count below, for the same
        // reason - a fixed 0.016 assumption here would have the identical
        // load-dependent drift this whole fix is for.
        m_ownCycle.advance(static_cast<float>(dtSeconds * (m_inEvent ? 1.45 : 1.0)));
    }

    if (m_ecgBuffer.size() != width()) rebuildBuffer();

    // Real-time-per-pixel of buffer advancement - deliberately SLOWER than
    // the timer's own 16ms callback interval, not equal to it. At 16ms/px,
    // roughly 8-9 beats fit across a typical widget width, which reads as
    // busy/cramped and does not leave any single PQRST complex enough
    // horizontal room to be individually legible - per explicit request for
    // a more spacious trace. At 6ms/px, the SAME real elapsed time between
    // callbacks advances the buffer by proportionally MORE pixels (roughly
    // 2.7x), so the same beat now spans about 2.7x the screen width and
    // correspondingly fewer beats fit across the same fixed widget width -
    // trading "see many beats at once" for "see fewer beats, each one
    // clearly spread out and readable", which is the actual goal here.
    // This deliberately no longer matches the timer's own real 16ms
    // interval - the two are independent by design (this only controls how
    // many PIXELS one interval's worth of real time advances by, not how
    // often the callback itself fires).
    constexpr double kMsPerPixel = 7.0;

    // How many buffer pixels this callback should actually advance by, at
    // the kMsPerPixel sweep rate above. Clamped so a genuine multi-second
    // stall (the app actually suspended, not just one slow frame) does not
    // try to instantly flood hundreds of repeated catch-up samples.
    int steps = qMax(1, qRound(elapsedMs / kMsPerPixel));
    steps = qMin(steps, 80);   // ~0.5s worth of catch-up ceiling at this faster px/ms rate

    // Real heart rate is not metronomic — cn::CardiacCycle::instantaneousBpm()
    // already models respiratory sinus arrhythmia (rate rising on inspiration,
    // falling on expiration) as a genuine physiological read-out, not a
    // decorative wobble on the display: sampling ecg(phase)/arterialPressure(
    // phase)/pleth(phase) here at whatever the CURRENT instantaneous rate
    // implies is what makes beats visibly compress and stretch on every trace
    // as the rate varies, exactly the way a real monitor's sweep does.
    const float p = cyc->phase();

    // Interpolate PHASE across the catch-up window and re-evaluate the
    // waveform at EACH sub-step, rather than sampling once at the current
    // phase and holding that same value across every catch-up slot. A held
    // value produced a visible flat-then-jump staircase whenever `steps` >
    // 1 (heavier per-frame load elsewhere in the app - e.g. actively
    // navigating the catheter, which recomputes device geometry and wall-
    // contact physics across multiple views every frame - makes this
    // widget's own timer fire later/less often, which is exactly when
    // steps grows). Worse than merely boxy: a hold could flatten or
    // entirely skip the narrow QRS spike if a catch-up window happened to
    // land on top of it. Re-deriving each sub-step's own phase and calling
    // ecg()/arterialPressure()/pleth() at THAT phase traces the true curve
    // through the gap instead, correctly reconstructing a spike that
    // occurred during a delayed interval rather than smoothing over or
    // repeating around it.
    float phaseStart = m_lastPhase;
    if (phaseStart < 0.f) phaseStart = p;   // first call since construction - nothing to interpolate from yet
    // Unwrap so interpolation always moves FORWARD in time, even across the
    // 1.0->0.0 wrap a real advancing phase goes through every beat - the
    // same "add 1 if wrapped" convention CardiacCycle::agauss() already
    // uses for its own phase-distance wrapping.
    float phaseEndUnwrapped = p;
    if (phaseEndUnwrapped < phaseStart) phaseEndUnwrapped += 1.f;

    for (int i = 0; i < steps; ++i) {
        const float t = static_cast<float>(i + 1) / static_cast<float>(steps);
        float subPhase = phaseStart + (phaseEndUnwrapped - phaseStart) * t;
        if (subPhase >= 1.f) subPhase -= 1.f;   // wrap back into cn::CardiacCycle::ecg()'s own 0..1 domain

        m_ecgBuffer[m_writePos]      = cyc->ecg(subPhase);
        m_pressureBuffer[m_writePos] = cyc->arterialPressure(subPhase);
        m_plethBuffer[m_writePos]    = cyc->pleth(subPhase);
        m_writePos = (m_writePos + 1) % m_ecgBuffer.size();
    }
    m_lastPhase = p;
    m_spo2Phase += 0.016f;
    update();
}

namespace {
/// Draws one buffer as a scrolling trace inside `field`, using the same
/// "read backward from the live write position" convention every trace here
/// shares, so all three stay pixel-aligned to the same moment in the cycle.
void drawTrace(QPainter &p, const QRect &field, const QVector<double> &buf,
               int writePos, double baselineFraction, double gainFraction,
               const QColor &colour, qreal penWidth) {
    if (buf.isEmpty()) return;
    const double base = field.top() + field.height() * baselineFraction;
    const double gain = field.height() * gainFraction;
    QPainterPath path;
    for (int i = 0; i < field.width(); ++i) {
        const int idx = (writePos + i * buf.size() / field.width()) % buf.size();
        const double y = base - buf[idx] * gain;
        if (i == 0) path.moveTo(field.left() + i, y);
        else        path.lineTo(field.left() + i, y);
    }
    p.setPen(QPen(colour, penWidth));
    p.drawPath(path);
}
} // namespace

void ECGWidget::paintEvent(QPaintEvent *) {
    const cn::CardiacCycle *cyc = m_cycle ? m_cycle : &m_ownCycle;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int W = width(), H = height();
    p.fillRect(rect(), BG_COLOR);

    // ── Layout ───────────────────────────────────────────────────────────────
    // Parameters now run in one horizontal strip ABOVE the traces, four
    // columns wide (HR / SpO2 / NIBP / RR) - not a stacked right-hand column
    // any more, and the trace field is correspondingly full-width rather
    // than sharing the row with a numeral gutter. Softer overall: thinner
    // trace strokes, lighter (non-bold) numeral weight, smaller sizes than
    // the previous heavier column layout used.
    const int padL = 10, padR = 10;
    const int paramBarH = 54;
    const QRect paramBar(padL, 4, W - padL - padR, paramBarH);
    const QRect field(padL, paramBar.bottom() + 6, W - padL - padR, H - paramBar.bottom() - 14);
    if (field.width() < 20 || field.height() < 20) return;

    // ── Parameter strip — 4 equal columns ───────────────────────────────────
    const int colW = paramBar.width() / 4;
    auto drawParam = [&](int col, const QColor &colour, const QString &value,
                         const QString &label) {
        const int x = paramBar.left() + col * colW;
        QFont numFont("Titillium Web", 17);
        numFont.setWeight(QFont::Medium);   // Medium, not Bold — softer/lighter than before
        p.setFont(numFont);
        p.setPen(colour);
        p.drawText(QRect(x, paramBar.top(), colW, 26), Qt::AlignLeft | Qt::AlignVCenter, value);

        QFont lblFont("Titillium Web", 7);
        lblFont.setWeight(QFont::Normal);
        p.setFont(lblFont);
        p.setPen(QColor(150, 150, 150));
        p.drawText(QRect(x, paramBar.top() + 28, colW, 14), Qt::AlignLeft | Qt::AlignVCenter, label);
    };

    // HR — genuinely measured off the live cycle.
    const int hr = int(std::lround(cyc->instantaneousBpm()));
    drawParam(0, ECG_COLOR, QString::number(hr), tr("HR  bpm"));

    // SpO2 — no oximeter model exists in this console; a clinically-typical
    // 96-99% baseline with a small slow wobble (real SpO2 genuinely drifts a
    // percent or two with the respiratory cycle), not a measured value.
    const float spo2 = 97.5f + 1.4f * std::sin(m_spo2Phase * 0.6f);
    drawParam(1, SPO2_COLOR, QString::number(int(std::lround(spo2))), tr("SpO2  %"));

    // NIBP — no cuff/invasive-line model exists either; a clinically-typical
    // 120/80 baseline, tied to the SAME real respiratory-sinus-arrhythmia
    // deviation already driving HR, so it varies WITH the cycle's own
    // physiology rather than independently of it.
    const float bpmDelta = cyc->instantaneousBpm() - static_cast<float>(cyc->heartRate());
    const int systolic  = 120 + int(std::lround(bpmDelta * 0.8f));
    const int diastolic = 80  + int(std::lround(bpmDelta * 0.5f));
    drawParam(2, BP_COLOR, tr("%1/%2").arg(systolic).arg(diastolic), tr("NIBP  mmHg"));

    // RR — genuinely derived from the live cycle's own respiration period,
    // the same value cn::CardiacCycle::instantaneousBpm() itself uses for RSA.
    const float rr = 60.f / std::max(cyc->params().respirationSec, 0.1f);
    drawParam(3, RR_COLOR, QStringLiteral("\u25BC%1").arg(int(std::lround(rr))), tr("RR  /min"));

    // ── Traces — pressure and pleth first, ECG drawn last/on top ────────────
    // Matches a real combined display: the sharp ECG line reads as the most
    // prominent trace, with the arterial line and pleth visible underneath
    // it rather than competing on top of it. Strokes thinned from the
    // previous pass (1.6/1.2 -> 1.1/0.9/1.3) for a lighter, less heavy look.
    drawTrace(p, field, m_pressureBuffer, m_writePos,
              kPressureBaselineFraction, kPressureGainFraction, PRESSURE_COLOR, 1.1);
    drawTrace(p, field, m_plethBuffer, m_writePos,
              kPlethBaselineFraction, kPlethGainFraction, PLETH_COLOR, 0.9);
    drawTrace(p, field, m_ecgBuffer, m_writePos,
              kEcgBaselineFraction, kEcgGainFraction, ECG_COLOR, 1.3);

    // ── Scale marker ─────────────────────────────────────────────────────────
    // A real monitor always labels the ceiling of whichever trace owns the
    // top of the field — here, the arterial pressure scale in mmHg.
    QFont scaleFont("Titillium Web", 8);
    scaleFont.setWeight(QFont::Normal);
    p.setFont(scaleFont);
    p.setPen(PRESSURE_COLOR);
    p.drawText(field.left() + 2, field.top() + 12, QStringLiteral("180"));

    // ── Lead label ───────────────────────────────────────────────────────────
    p.setFont(scaleFont);
    p.setPen(QColor(190, 190, 190));
    p.drawText(field.left() + 6, field.top() + 26, QStringLiteral("I"));

    // ── Event banner ─────────────────────────────────────────────────────────
    if (m_inEvent && !m_eventLabel.isEmpty()) {
        QFont bannerFont("Titillium Web", 9);
        bannerFont.setWeight(QFont::Medium);
        p.setFont(bannerFont);
        const QFontMetrics fm(p.font());
        const int tw = fm.horizontalAdvance(m_eventLabel) + 14;
        const QRect box(field.left(), field.top() - 2, tw, 16);
        p.fillRect(box, QColor(210, 210, 210, 220));
        p.setPen(Qt::black);
        p.drawText(box, Qt::AlignCenter, m_eventLabel);
    }
}