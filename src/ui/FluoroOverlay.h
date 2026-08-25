#pragma once
#include <QWidget>
#include <QTimer>

// ─────────────────────────────────────────────────────────────────────────────
// FluoroOverlay
//
// Transparent child widget painted on top of a VesselView (Angiographic mode)
<<<<<<< HEAD
// to give the X-ray rendering a realistic look. Implements two of the three
// effects originally described in the fluoro reference doc:
//
//   1. (removed) Fine detector grain used to be drawn here as a full-screen
//      noise image, but this widget has no access to what VesselView
//      actually rendered underneath it, so it had no way to confine that
//      noise to anatomy vs. background — it painted across the entire
//      widget rectangle unconditionally. Grain now comes only from
//      VesselView's own fragment shader (hash13(vWorld * 41.0), mode 1),
//      which only ever runs where an actual mesh fragment is rasterized,
//      so it's correctly confined to anatomy by construction.
=======
// to give the X-ray rendering a realistic look. Implements the three effects
// described in the fluoro reference doc:
//
//   1. Fine detector grain  — half-res noise buffer, alpha 5-18, 150ms refresh
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7
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