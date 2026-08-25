#include "QuadPanel.h"
#include "VesselView.h"
#include "FluoroOverlay.h"
#include <QGraphicsBlurEffect>
#include <QHBoxLayout>
#include <QStackedWidget>

// ─────────────────────────────────────────────────────────────────────────────
// QuadPanel — port of the old project's two-renderer setup.
//
// Per ThreeViews_HowTheyRelate.cpp:
//   • Only TWO real views: 3D (Arterial) and Fluoro (Angiographic).
//   • "Mesh View" = wireframe TOGGLE on the vessel actor only.
//   • "DSA View"  = dark background + vessel colour change only.
//   • Skeleton/muscular actors are identical in both views and never touched
//     by the Mesh or DSA toggles.
//   • Switching views = QStackedWidget page change (instant, no rebuild).
// ─────────────────────────────────────────────────────────────────────────────
QuadPanel::QuadPanel(QWidget *parent) : QWidget(parent) {
    auto *stack = new QStackedWidget(this);
    auto *lay   = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(stack);

    // ── View 0: 3D (Arterial) ────────────────────────────────────────────
    m_view3d = new VesselView(this);
    m_view3d->setPalette(VesselView::Palette::Arterial);
    m_view3d->setWallOpacity(0.30f);  // old project: vessel 30% opacity in 3D view

    // ── View 1: Fluoro (Angiographic) ────────────────────────────────────
    m_viewFluoro = new VesselView(this);
    m_viewFluoro->setPalette(VesselView::Palette::Angiographic);
<<<<<<< HEAD
    m_viewFluoro->setWallOpacity(0.35f);   // vessel now visible in fluoro (was 0.0 - hidden - per prior request)
=======
    m_viewFluoro->setWallOpacity(0.0f);   // old project: vessel hidden by default in fluoro
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7
    // frontalLock removed — camera sync handles position sharing between views

    // FluoroOverlay: grain + vignette (doc spec)
    auto *fluoroOverlay = new FluoroOverlay(m_viewFluoro);
    fluoroOverlay->setGeometry(m_viewFluoro->rect());
    m_viewFluoro->installEventFilter(fluoroOverlay);
    fluoroOverlay->show();

<<<<<<< HEAD
    // Softness — real detector/scatter blur. Cut hard from 1.4: with the DRR
    // accumulation buffer now at full resolution (VesselView.cpp,
    // ensureXrayFbo) it contributes no softness of its own any more, so this
    // is the only blur left in the pipeline, and it needs to be light enough
    // that bone/muscle/vessel stay clearly, individually visible — a
    // realistic detector still isn't perfectly crisp, but this is a hint of
    // that, not a meaningful softening.
    auto *fluoroBlur = new QGraphicsBlurEffect(m_viewFluoro);
    fluoroBlur->setBlurRadius(0.5);
=======
    // Subtle blur — real detector softness
    auto *fluoroBlur = new QGraphicsBlurEffect(m_viewFluoro);
    fluoroBlur->setBlurRadius(1.0);
>>>>>>> 5411818b8ce7f0b7e0ae0f509f0558ac367a04c7
    m_viewFluoro->setGraphicsEffect(fluoroBlur);

    // Mesh and DSA are NOT separate views — they are states of the 3D/fluoro
    // views. We reuse m_view3d for both Mesh and DSA toggling.
    // For compatibility with MainWindow API we keep m_viewMesh and m_viewDSA
    // pointing to m_view3d so all calls go to the same VesselView.
    m_viewMesh = m_view3d;
    m_viewDSA  = m_view3d;

    stack->addWidget(m_view3d);      // index 0 — 3D + Mesh + DSA states
    stack->addWidget(m_viewFluoro);  // index 1 — Fluoro
    stack->setCurrentIndex(0);
}

// ─────────────────────────────────────────────────────────────────────────────
void QuadPanel::showPane(Pane p) {
    m_currentPane = p;
    auto *stack = findChild<QStackedWidget*>();
    if (!stack) return;

    switch (p) {
    case Pane::View3D:
        // Restore solid surface, normal colours
        m_view3d->setDSAActive(false);
        m_view3d->setWireframe(false);
        m_view3d->setPalette(VesselView::Palette::Arterial);
        m_view3d->setWallOpacity(0.30f);
        stack->setCurrentIndex(0);
        break;

    case Pane::Fluoro:
        // Switch to fluoro renderer page, frame head region
        stack->setCurrentIndex(1);
        if (m_viewFluoro) m_viewFluoro->showFluoroDefault();
        break;

    case Pane::Mesh:
        // Wireframe toggle on the 3D view — no separate renderer
        m_view3d->setDSAActive(false);
        m_view3d->setWireframe(true);   // SetRepresentationToWireframe()
        m_view3d->setPalette(VesselView::Palette::Arterial);
        m_view3d->setWallOpacity(0.30f);
        stack->setCurrentIndex(0);
        break;

    case Pane::DSA:
        // Dark background + vessel colour change — still the 3D view
        m_view3d->setWireframe(false);
        m_view3d->setDSAActive(true);
        stack->setCurrentIndex(0);
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void QuadPanel::setBodyMeshes(const QString &muscularPath, const QString &skeletalPath) {
    // Load into both real views — they have separate GL contexts
    if (m_view3d)     m_view3d->setBodyMeshes(muscularPath, skeletalPath);
    if (m_viewFluoro) m_viewFluoro->setBodyMeshes(muscularPath, skeletalPath);
}

// ─────────────────────────────────────────────────────────────────────────────
void QuadPanel::setCarmAngles(float lao, float cran) {
    // Every view mirrors the rig. This previously pushed to the fluoro view
    // only, on the reasoning that the 3D view "keeps its own orbit" — but the
    // C-arm angle is now an offset ON TOP of each view's own orbit, so a view
    // can mirror the rig and still keep its own zoom, pan and target. Leaving
    // the 3D view out simply gave it a different rig orientation from the rest.
    if (m_view3d)     m_view3d->setCarmAngles(lao, cran);
    if (m_viewFluoro) m_viewFluoro->setCarmAngles(lao, cran);
}
// ─────────────────────────────────────────────────────────────────────────────
void QuadPanel::setBodyAlignment(const VesselView::BodyAlign &muscular,
                                 const VesselView::BodyAlign &skeletal) {
    // Both views have separate GL contexts and separate copies of the body
    // batches, so the alignment has to be pushed to each of them or the 3D and
    // Fluoro views drift apart.
    if (m_view3d)     m_view3d->setBodyAlignment(muscular, skeletal);
    if (m_viewFluoro) m_viewFluoro->setBodyAlignment(muscular, skeletal);
}

// ─────────────────────────────────────────────────────────────────────────────
void QuadPanel::nudgeBodyLayers(float dz, float dScale) {
    if (m_view3d)     m_view3d->nudgeBodyLayers(dz, dScale);
    if (m_viewFluoro) m_viewFluoro->nudgeBodyLayers(dz, dScale);
}