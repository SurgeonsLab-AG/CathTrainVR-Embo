#include "FluoroOverlay.h"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QTimer>
#include <algorithm>
#include <cmath>

FluoroOverlay::FluoroOverlay(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);   // clicks/drags pass through to VesselView
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);

    // Grain refresh at 150 ms — gives live shimmer like a real detector
    connect(&m_timer, &QTimer::timeout, this, &FluoroOverlay::refreshGrain);
    m_timer.start(150);
}

void FluoroOverlay::setEnabled(bool on) {
    m_on = on;
    update();
    on ? m_timer.start(150) : m_timer.stop();
}

void FluoroOverlay::setCollimation(double factor) {
    m_collimTarget = std::clamp(factor, 0.0, 1.0);
}

void FluoroOverlay::refreshGrain() {
    m_noiseSeed = QRandomGenerator::global()->generate();
    update();
}

bool FluoroOverlay::eventFilter(QObject *obj, QEvent *ev) {
    // Keep overlay geometry synced when the parent VesselView is resized
    if (obj == parent() && ev->type() == QEvent::Resize) {
        if (auto *rw = static_cast<QWidget*>(parent()))
            setGeometry(rw->rect());
    }
    return QWidget::eventFilter(obj, ev);
}

void FluoroOverlay::paintEvent(QPaintEvent *) {
    if (!m_on) return;
    QPainter p(this);
    const QRect r = rect();

    // ── 1. Fine detector grain ───────────────────────────────────────────────
    // Half-resolution noise buffer scaled up — cheaper and reads more like
    // real detector grain than per-pixel noise at full res.
    // Alpha 5-18: very subtle, matches real fluoro grain (not a heavy veil).
    const int nw = std::max(1, r.width()  / 2);
    const int nh = std::max(1, r.height() / 2);
    QImage noise(nw, nh, QImage::Format_ARGB32);
    QRandomGenerator rng(m_noiseSeed);
    for (int y = 0; y < nh; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(noise.scanLine(y));
        for (int x = 0; x < nw; ++x) {
            // Triangular distribution centred ~128 — softer than uniform
            const int gray = (rng.bounded(256) + rng.bounded(256)) / 2;
            const int a    = 5 + rng.bounded(13);   // 5..18 — faint grain only
            line[x] = qRgba(gray, gray, gray, a);
        }
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(r, noise);

    // ── 2. Vignette — image intensifier edge falloff ─────────────────────────
    // Outer alpha 90 — subtle but clearly visible darkening at the edges
    QRadialGradient vg(r.center(), std::max(r.width(), r.height()) * 0.78);
    vg.setColorAt(0.0,  QColor(0, 0, 0,  0));
    vg.setColorAt(0.75, QColor(0, 0, 0, 22));
    vg.setColorAt(1.0,  QColor(0, 0, 0, 90));
    p.fillRect(r, vg);

    // ── 3. Collimation — clinical field narrowing (animated) ─────────────────
    // Smooth interpolation toward target (15% per frame at 60fps)
    m_collimCurrent += (m_collimTarget - m_collimCurrent) * 0.15;
    if (m_collimCurrent > 0.002) {
        const double inset = m_collimCurrent * 0.30;
        const QRectF full = r;
        const QRectF window = full.adjusted(
            full.width()  * inset,  full.height() * inset,
            -full.width()  * inset, -full.height() * inset);
        QPainterPath outer, inner;
        outer.addRect(full);
        inner.addRect(window);
        p.fillPath(outer.subtracted(inner), QColor(0, 0, 0, 255));
        p.setPen(QPen(QColor(210, 210, 210, 90), 1.5));   // bright collimator edge
        p.drawRect(window);
    }
    // Keep animating until collimation is settled
    if (std::abs(m_collimCurrent - m_collimTarget) > 0.002)
        QTimer::singleShot(16, this, [this] { update(); });
}