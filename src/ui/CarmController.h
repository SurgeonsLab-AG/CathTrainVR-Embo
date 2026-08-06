#pragma once

#include <QObject>

// ─────────────────────────────────────────────────────────────────────────────
// CarmController
//
// Owns the C-arm rig state (angles + bed position) and keeps the CarmView
// actors and the linked vessel VesselView camera in sync.  Ported exactly
// from the old project's CarmController.h/.cpp — no VTK, works with the
// new project's Qt-only OpenGL pipeline.
//
// State:
//   m_laoAngle  ∈ [-90, +90]  positive = LAO, negative = RAO
//   m_cranAngle ∈ [-45, +45]  positive = CRAN, negative = CAUD
//   m_bedPos    ∈ [-200, +200] mm
//
// Usage:
//   controller->setHeadPivot(cx, cy, cz);   // from CarmView after loadPart(0)
//   connect(controller, &CarmController::anglesChanged,
//           this, &MainWindow::onCarmAnglesChanged);
//   controller->rotateLAO(5.0);
// ─────────────────────────────────────────────────────────────────────────────
class VesselView;
class CarmView;

class CarmController : public QObject {
    Q_OBJECT
public:
    explicit CarmController(QObject *parent = nullptr);

    // Provide the CarmView so the controller can call setAngles() on it.
    void setCarmView(CarmView *view);

    // Provide one VesselView whose camera is incrementally rotated with the C-arm.
    // Multiple views can be linked by calling this repeatedly (stores a list).
    void addLinkedVesselView(VesselView *view);
    void clearLinkedViews();

    // Cache the pivot point of the rotating head assembly (bounding-box centre
    // of part 0).  Call once after the head STL is loaded.
    void setHeadPivot(float cx, float cy, float cz);

    // Control interface — same API as old project CarmController
    void rotateLAO(double degrees);   // positive = LAO, negative = RAO
    void rotateCRAN(double degrees);  // positive = CRAN, negative = CAUD
    void moveBed(double mm);          // independent, not linked to camera
    void resetAll();                  // resets rig only; camera reset is separate

    double laoAngle()  const { return m_laoAngle; }
    double cranAngle() const { return m_cranAngle; }
    double bedPos()    const { return m_bedPos; }

signals:
    void anglesChanged(double lao, double cran);

private:
    void applyToCarmView();
    void rotateLinkedCamerasIncremental(double dLao, double dCran);

    CarmView *m_carmView = nullptr;

    // Linked VesselViews — incremental camera rotation applied to each
    QList<VesselView *> m_linkedViews;

    // Pivot of the head assembly (computed once from head STL bounds)
    float m_pivotX = 0.f, m_pivotY = 0.f, m_pivotZ = 0.f;
    bool  m_pivotSet = false;

    double m_laoAngle  = 0.0;
    double m_cranAngle = 0.0;
    double m_bedPos    = 0.0;
};