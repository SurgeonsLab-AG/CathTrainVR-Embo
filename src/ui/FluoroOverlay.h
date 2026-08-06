#pragma once
#include <QWidget>
#include <QTimer>

// ─────────────────────────────────────────────────────────────────────────────
// FluoroOverlay
//
// Transparent child widget painted on top of a VesselView (Angiographic mode)
// to give the X-ray rendering a realistic look. Implements the three effects
// described in the fluoro reference doc:
//
//   1. Fine detector grain  — half-res noise buffer, alpha 5-18, 150ms refresh
//   2. Vignette             — radial gradient darkening the edges (image intensifier)
//   3. Collimation          — optional animated field narrowing (clinical dose reduction)
//
// Usage:
//   auto *overlay = new FluoroOverlay(vesselView);
//   overlay->setGeometry(vesselView->rect());
//   vesselView->installEventFilter(overlay);  // tracks resize
//   overlay->show();
//   // optionally: overlay->setCollimation(0.0 to 1.0);
// ─────────────────────────────────────────────────────────────────────────────
class FluoroOverlay : public QWidget {
    Q_OBJECT
public:
    explicit FluoroOverlay(QWidget *parent = nullptr);

    void setEnabled(bool on);
    // 0 = full field (off), 1 = maximally narrowed. Animates smoothly.
    void setCollimation(double factor);

    // Intercepts resize on the parent VTK widget to track geometry
    bool eventFilter(QObject *obj, QEvent *ev) override;

public slots:
    void refreshGrain();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QTimer  m_timer;
    bool    m_on            = true;
    quint32 m_noiseSeed     = 0;
    double  m_collimTarget  = 0.0;
    double  m_collimCurrent = 0.0;
};