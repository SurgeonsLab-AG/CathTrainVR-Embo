#pragma once

#include <QWidget>
#include <QString>

#include "VesselView.h"

class CarmView;
class QSplitter;

// ─────────────────────────────────────────────────────────────────────────────
// QuadPanel
//
// Right-side vessel display. The active mode is selected by the view buttons
// in MainWindow; only one VesselView is visible at a time (not a 2×2 grid).
//
// Modes / panes:
//   3D      — VesselView Reference, Arterial palette, full colour
//   Fluoro  — VesselView Reference, Angiographic palette, semi-transparent wall
//   Mesh    — VesselView Reference, Arterial palette, mesh/wireframe overlay
//   DSA     — VesselView Reference, Monochrome palette, near-opaque wall
//             (white lumen on dark background, like real DSA)
//
// Body mesh layering (skeleton inside muscle inside vessel):
//   setBodyMeshes(musPath, skelPath) passes the STL paths to each VesselView
//   so they can align and render the Muscular + Skeletal meshes as background
//   layers, matching the old project's look.
//
// C-arm coupling:
//   setCarmAngles(lao, cran) rotates the Fluoro and DSA camera to match the
//   gantry angle. The 3D view keeps its own independent orbit.
// ─────────────────────────────────────────────────────────────────────────────
class QuadPanel : public QWidget {
    Q_OBJECT
public:
    enum class Pane { View3D, Fluoro, Mesh, DSA };

    explicit QuadPanel(QWidget *parent = nullptr);

    // Accessors
    VesselView *view3D()     const { return m_view3d; }
    VesselView *viewFluoro() const { return m_viewFluoro; }
    // Per ThreeViews doc: Mesh and DSA are states of the 3D view, not separate renderers
    VesselView *viewMesh()   const { return m_view3d; }
    VesselView *viewDSA()    const { return m_view3d; }

    // Switch the visible pane (called from the view-mode buttons)
    void showPane(Pane p);

    // Pass Muscular.stl and Skeletal.stl paths so each view can layer them
    void setBodyMeshes(const QString &muscularPath, const QString &skeletalPath);

    // Mirror C-arm gantry angles to Fluoro + DSA cameras
    void setCarmAngles(float lao, float cran);

    // Body-layer placement. The shipped Muscular/Skeletal/vessel STLs are
    // already co-registered, so the default is the identity and neither of
    // these normally needs calling. They exist for dialling in a body mesh
    // that came from a different source.
    void setBodyAlignment(const VesselView::BodyAlign &muscular,
                          const VesselView::BodyAlign &skeletal);
    /// Shift both body layers along Z in both views. +dz raises them.
    void nudgeBodyLayers(float dz, float dScale = 0.f);

private:
    VesselView *m_view3d     = nullptr;
    VesselView *m_viewFluoro = nullptr;
    VesselView *m_viewMesh   = nullptr;   // alias → m_view3d (Mesh = wireframe toggle)
    VesselView *m_viewDSA    = nullptr;   // alias → m_view3d (DSA = state toggle)

    Pane m_currentPane = Pane::View3D;
};