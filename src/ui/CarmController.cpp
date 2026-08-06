#include "CarmController.h"
#include "CarmView.h"
#include "VesselView.h"

#include <algorithm>
#include <cmath>

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
    m_laoAngle  = 0.0;
    m_cranAngle = 0.0;
    m_bedPos    = 0.0;
    applyToCarmView();
    emit anglesChanged(0.0, 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// applyToCarmView — push current angle+bed state to the CarmView renderer.
// CarmView::setAngles() handles the pivot-based rotation internally.
// ─────────────────────────────────────────────────────────────────────────────
void CarmController::applyToCarmView() {
    if (!m_carmView) return;
    m_carmView->setAngles(static_cast<float>(m_laoAngle),
                          static_cast<float>(m_cranAngle));
    m_carmView->setBedPosition(static_cast<float>(m_bedPos));
}

// ─────────────────────────────────────────────────────────────────────────────
// rotateLinkedCamerasIncremental — apply ONLY the delta to each linked view's
// camera.  This is the core trick from the old project: using incremental
// rotation (not absolute set-to-saved-state) preserves whatever zoom/pan the
// operator has done in the vessel view.
// ─────────────────────────────────────────────────────────────────────────────
void CarmController::rotateLinkedCamerasIncremental(double dLao, double dCran) {
    for (VesselView *v : m_linkedViews) {
        if (!v) continue;
        // setCarmAngles uses the same incremental pattern internally —
        // it computes the delta from the previous state and adds it to
        // the camera's current azimuth/elevation.
        v->setCarmAngles(static_cast<float>(m_laoAngle),
                         static_cast<float>(m_cranAngle));
    }
    Q_UNUSED(dLao); Q_UNUSED(dCran);
}