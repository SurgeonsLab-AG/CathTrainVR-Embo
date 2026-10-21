#include "CarmController.h"
#include "CarmView.h"
#include "VesselView.h"
#include "TraceLog.h"

#include <algorithm>

CarmController::CarmController(QObject *parent) : QObject(parent) {}

void CarmController::setCarmView(CarmView *view) {
    m_carmView = view;
}

void CarmController::addLinkedVesselView(VesselView *view) {
    if (view && !m_linkedViews.contains(view))
        m_linkedViews.append(view);
}

void CarmController::clearLinkedViews() {
    m_linkedViews.clear();
}

void CarmController::setHeadPivot(float cx, float cy, float cz) {
    m_pivotX = cx;
    m_pivotY = cy;
    m_pivotZ = cz;
    m_pivotSet = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// rotateLAO — positive = LAO, negative = RAO, clamped ±90°
// Applies only the DELTA to the vessel camera (old project pattern — preserves
// existing zoom/pan; doesn't reset the camera on every press).
// ─────────────────────────────────────────────────────────────────────────────
void CarmController::rotateLAO(double degrees) {
    const double before = m_laoAngle;
    m_laoAngle = std::clamp(m_laoAngle + degrees, -90.0, 90.0);
    const double delta = m_laoAngle - before;   // 0 when already at limit
    traceLog(QStringLiteral("[CARM] rotateLAO(%1)  LAO %2 -> %3  CRAN unchanged %4")
                 .arg(degrees).arg(before).arg(m_laoAngle).arg(m_cranAngle));
    applyToCarmView();
    if (delta != 0.0) rotateLinkedCamerasIncremental(delta, 0.0);
    emit anglesChanged(m_laoAngle, m_cranAngle);
}

// ─────────────────────────────────────────────────────────────────────────────
// rotateCRAN — positive = CRAN, negative = CAUD, clamped ±45°
// ─────────────────────────────────────────────────────────────────────────────
void CarmController::rotateCRAN(double degrees) {
    const double before = m_cranAngle;
    m_cranAngle = std::clamp(m_cranAngle + degrees, -45.0, 45.0);
    const double delta = m_cranAngle - before;
    traceLog(QStringLiteral("[CARM] rotateCRAN(%1)  CRAN %2 -> %3  LAO unchanged %4")
                 .arg(degrees).arg(before).arg(m_cranAngle).arg(m_laoAngle));
    applyToCarmView();
    if (delta != 0.0) rotateLinkedCamerasIncremental(0.0, delta);
    emit anglesChanged(m_laoAngle, m_cranAngle);
}

// ─────────────────────────────────────────────────────────────────────────────
// moveBed — translation only, NOT linked to any camera (old project comment:
// "dollying the view from bed movement caused the vessel view to disappear").
// ─────────────────────────────────────────────────────────────────────────────
void CarmController::moveBed(double mm) {
    m_bedPos = std::clamp(m_bedPos + mm, -200.0, 200.0);
    applyToCarmView();
    // Bed movement deliberately NOT sent to linked cameras.
}

// ─────────────────────────────────────────────────────────────────────────────
// resetAll — resets rig state only.  Vessel camera reset is the caller's
// responsibility (bind both to R if desired, but keep them separate functions).
// ─────────────────────────────────────────────────────────────────────────────
void CarmController::resetAll() {
    traceLog(QStringLiteral("[CARM] resetAll()  LAO %1 -> 0  CRAN %2 -> 0")
                 .arg(m_laoAngle).arg(m_cranAngle));
    m_laoAngle  = 0.0;
    m_cranAngle = 0.0;
    m_bedPos    = 0.0;
    applyToCarmView();
    emit anglesChanged(0.0, 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// applyToCarmView — push current angle state to the CarmView renderer.
// CarmView::setAngles() handles the pivot-based rotation internally.
//
// No longer also pushes m_bedPos - CarmView::setBedPosition() was removed
// (it was never actually reachable from any UI control - moveBed() below
// is a complete, working method but is not itself called from anywhere
// either, the same "genuinely vestigial" state on this class's own side of
// that pair). Table repositioning is now driven directly by MainWindow via
// CarmView::setBedBodyFraction() when a sheath is placed - a completely
// separate mechanism from this controller's own m_bedPos/moveBed(), which
// remain here unused rather than removed, in case a manual bed-nudge
// control is wired up to them later.
// ─────────────────────────────────────────────────────────────────────────────
void CarmController::applyToCarmView() {
    if (!m_carmView) return;
    m_carmView->setAngles(static_cast<float>(m_laoAngle),
                          static_cast<float>(m_cranAngle));
}

// ─────────────────────────────────────────────────────────────────────────────
// rotateLinkedCamerasIncremental — apply ONLY the delta to each linked view's
// camera.  This is the core trick from the old project: using incremental
// rotation (not absolute set-to-saved-state) preserves whatever zoom/pan the
// operator has done in the vessel view.
// ─────────────────────────────────────────────────────────────────────────────
void CarmController::rotateLinkedCamerasIncremental(double dLao, double dCran) {
    traceLog(QStringLiteral("[CARM] rotateLinkedCamerasIncremental  pushing LAO=%1 CRAN=%2 to %3 linked view(s)")
                 .arg(m_laoAngle).arg(m_cranAngle).arg(m_linkedViews.size()));
    for (VesselView *v : m_linkedViews) {
        if (!v) continue;
        // setCarmAngles() stores these as an ABSOLUTE rig offset (see its
        // own comment in VesselView.cpp), not a delta from its previous
        // call - every linked view given the same (lao, cran) here ends up
        // at the same C-arm orientation regardless of what it was doing
        // before, which is what keeps them from drifting apart.
        v->setCarmAngles(static_cast<float>(m_laoAngle),
                         static_cast<float>(m_cranAngle));
    }
    Q_UNUSED(dLao); Q_UNUSED(dCran);
}