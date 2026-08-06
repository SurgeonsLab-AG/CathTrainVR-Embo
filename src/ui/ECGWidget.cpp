#include "ECGWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>

// The waveform shapes are no longer defined here. They live in cn::CardiacCycle
// alongside the phase that drives the pulsating vessel wall, so the trace and
// the wall are two views of one beat and cannot drift apart.

ECGWidget::ECGWidget(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    connect(&m_timer, &QTimer::timeout, this, &ECGWidget::onTick);
    m_timer.start(16);   // ~60 fps

    rebuildBuffer();
}

void ECGWidget::setHeartRate(int bpm) {
    // Range matches cn::CardiacCycle's own clamp; the previous 40-180 here
    // silently disagreed with the core and with the console's 30-220 spin box.
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
    m_abpBuffer.resize(w, 0.0);
    m_writePos = 0;
}

void ECGWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    rebuildBuffer();
}

void ECGWidget::onTick() {
    // One clock for the whole console. When MainWindow has attached its shared
    // cn::CardiacCycle we only SAMPLE it — advancing it here as well would step
    // the phase twice per frame and run the heart at double rate.
    const cn::CardiacCycle *cyc = m_cycle ? m_cycle : &m_ownCycle;

    if (!m_cycle) {
        // Standalone fallback. The event speed-up only applies here; when a
        // shared clock is in use, its owner decides the rate.
        m_ownCycle.advance(static_cast<float>(0.016 * (m_inEvent ? 1.45 : 1.0)));
    }

    if (m_ecgBuffer.size() != width()) rebuildBuffer();

    const float p = cyc->phase();
    m_ecgBuffer[m_writePos]   = cyc->ecg(p);
    m_abpBuffer[m_writePos]   = cyc->arterialPressure(p);

    m_writePos = (m_writePos + 1) % m_ecgBuffer.size();
    update();
}

void ECGWidget::paintEvent(QPaintEvent *) {
    const cn::CardiacCycle *cyc = m_cycle ? m_cycle : &m_ownCycle;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int W = width(), H = height();
    p.fillRect(rect(), BG_COLOR);

    // ── Layout ───────────────────────────────────────────────────────────────
    // One shared plotting field rather than stacked lanes. A bedside monitor
    // overlays the pressure and ECG traces in a single band: the pressure spikes
    // occupy the height, the ECG rides along the bottom, and no rules or lane
    // dividers are drawn at all. Splitting them into boxed lanes is what made
    // the old panel look like a chart rather than a monitor.
    const int numerW = 96;                       // right-hand numerics column
    const int padL   = 34;                       // room for the pressure scale
    const int padT   = 8,  padB = 10;
    const QRect field(padL, padT, W - padL - numerW, H - padT - padB);
    if (field.width() < 20 || field.height() < 20) return;

    // Pressure axis, in mmHg. Fixed rather than auto-scaled so the trace does
    // not silently rescale itself between beats.
    const double pTop = kScaleTopMmHg, pBot = kScaleBotMmHg;
    auto yFromMmHg = [&](double v) {
        return field.bottom() - (v - pBot) / (pTop - pBot) * field.height();
    };
    auto toMmHg = [](double norm) {
        return kDiastolicMmHg + (norm - 0.34) / 0.66 * (kSystolicMmHg - kDiastolicMmHg);
    };

    // ── Arterial pressure ────────────────────────────────────────────────────
    if (!m_abpBuffer.isEmpty()) {
        QPainterPath path;
        for (int i = 0; i < field.width(); ++i) {
            const int idx = (m_writePos + i * m_abpBuffer.size() / field.width())
            % m_abpBuffer.size();
            const double y = yFromMmHg(toMmHg(m_abpBuffer[idx]));
            if (i == 0) path.moveTo(field.left() + i, y);
            else        path.lineTo(field.left() + i, y);
        }
        p.setPen(QPen(ABP_COLOR, 1.3));
        p.drawPath(path);
    }

    // ── ECG ──────────────────────────────────────────────────────────────────
    // Drawn along the bottom of the same field, at a small fraction of the
    // height, so the R spikes read clearly without competing with the pressure
    // trace above them.
    if (!m_ecgBuffer.isEmpty()) {
        const double base = field.bottom() - field.height() * 0.06;
        const double gain = field.height() * kEcgHeightFraction;
        QPainterPath path;
        for (int i = 0; i < field.width(); ++i) {
            const int idx = (m_writePos + i * m_ecgBuffer.size() / field.width())
            % m_ecgBuffer.size();
            const double y = base - m_ecgBuffer[idx] * gain;
            if (i == 0) path.moveTo(field.left() + i, y);
            else        path.lineTo(field.left() + i, y);
        }
        p.setPen(QPen(ECG_COLOR, 1.3));
        p.drawPath(path);
    }

    // ── Scale marks and lead label ───────────────────────────────────────────
    // Two ticks only: the top and bottom of the pressure axis. Anything more is
    // a grid, which is what the reference deliberately does without.
    p.setFont(QFont("Arial", 7));
    p.setPen(ABP_COLOR.darker(140));
    p.drawText(4, int(yFromMmHg(pTop)) + 6, QString::number(int(pTop)));
    p.drawText(4, int(yFromMmHg(pBot)) - 2, QString::number(int(pBot)));
    p.setPen(QColor(150, 150, 150));
    p.drawText(field.left() + 6, field.bottom() - int(field.height() * 0.22),
               QStringLiteral("II"));

    // ── Numerics ─────────────────────────────────────────────────────────────
    // Measured off the buffers on screen, not printed as constants, so the
    // numbers can never contradict the traces beside them.
    double lo = 1e9, hi = -1e9;
    for (double v : m_abpBuffer) { lo = std::min(lo, v); hi = std::max(hi, v); }
    const int sys = int(std::lround(toMmHg(hi)));
    const int dia = int(std::lround(toMmHg(lo)));
    const int map = int(std::lround(dia + (sys - dia) / 3.0));
    const int hr  = int(std::lround(cyc->instantaneousBpm()));
    // Respiratory rate comes from the same respiration cycle that drives the
    // sinus arrhythmia in the trace, so it is a real read-out rather than a
    // decorative constant.
    const int rr  = int(std::lround(60.0 / std::max(cyc->params().respirationSec, 0.1f)));

    const int xR = W - 8;
    int y = padT + 26;
    auto numeral = [&](const QColor &col, const QString &big, const QString &small) {
        p.setPen(col);
        p.setFont(QFont("Arial", 22, QFont::Bold));
        QFontMetrics fm(p.font());
        p.drawText(xR - fm.horizontalAdvance(big), y, big);
        if (!small.isEmpty()) {
            p.setFont(QFont("Arial", 7));
            QFontMetrics fs(p.font());
            p.drawText(xR - fm.horizontalAdvance(big) - fs.horizontalAdvance(small) - 4,
                       y - 12, small);
        }
        y += 34;
    };
    numeral(ECG_COLOR, QString::number(hr),  QStringLiteral("HR"));
    numeral(ABP_COLOR, QString::number(sys), QStringLiteral("ABP"));
    numeral(ABP_COLOR.darker(130), QString::number(map), QStringLiteral("MAP"));
    numeral(RESP_COLOR, QString::number(rr), QStringLiteral("RR"));

    // Diastolic sits under the systolic as a small figure, the way a monitor
    // shows sys/dia without giving the pair two full-size numerals.
    p.setFont(QFont("Arial", 8));
    p.setPen(ABP_COLOR.darker(130));
    {
        QFontMetrics fs(p.font());
        const QString d = QStringLiteral("/%1").arg(dia);
        p.drawText(xR - fs.horizontalAdvance(d), padT + 38, d);
    }

    // ── Event banner ─────────────────────────────────────────────────────────
    if (m_inEvent && !m_eventLabel.isEmpty()) {
        p.setFont(QFont("Arial", 9, QFont::Bold));
        const QFontMetrics fm(p.font());
        const int tw = fm.horizontalAdvance(m_eventLabel) + 14;
        const QRect box(field.left(), 2, tw, 16);
        p.fillRect(box, QColor(210, 210, 210));
        p.setPen(Qt::black);
        p.drawText(box, Qt::AlignCenter, m_eventLabel);
    }
}