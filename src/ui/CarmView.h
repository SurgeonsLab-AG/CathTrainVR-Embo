#pragma once

#include <QOpenGLWidget>
#include <algorithm>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QTimer>
#include <QVector3D>
#include <memory>

#include "core/StlMesh.h"

class QOpenGLShaderProgram;
class QPainter;
class QFont;

// ─────────────────────────────────────────────────────────────────────────────
// CarmView  —  Qt/OpenGL port of the old VTK CarmController rendering.
//
// Part indices (must match MainWindow::loadCarm's call order):
//   0 = stand (floor gantry)           — fixed
//   1 = head  (C-arm arc + detector)   — THE ONLY MOVABLE PART
//   2 = bed   (patient table)          — fixed, apart from its own bed position
//   3 = xray  (beam proxy cone)        — rides the head transform
//
// carm_head alone receives the movement controls: A/D drive CRAN/CAUD, W/S
// drive LAO/RAO, R resets. All three route through CarmController, which owns
// and clamps the angles.
//
// carm_xray takes the SAME transform as the head, so the beam cone rides with
// the arc: source and detector are rigidly coupled on a real C-arm, and because
// headTransform() rotates about the arc's own centre the cone keeps its relative
// position exactly — there is no offset to maintain and none to drift.
//
// carm_stand and carm_bed are drawn at identity and are never moved by those
// keys; the bed keeps only its own bed-position translation.
//
// Camera replicates the old project's exact formula:
//   focal  = combined bounding-box centre (cx, cy, cz)
//   eye    = (cx - diag*0.20,  cy - diag*1.2,  cz + 0)
//   ViewUp = (0, 0, 1)
//   FOV    = 30°  (close to VTK default; narrower than a wide-angle view)
//
// Rotation pivot = the HEAD's bounding-box centre (m_headCenter), cached from
// part 1 — the part that actually rotates. A body has to turn about a point
// inside itself, and taking the pivot from the other part put it just over a
// metre away, so the mover orbited rather than turning: 269 mm of displacement
// at CRAN 15 degrees, 787 mm at 45.
//
// Measured on carm_head.stl: (0.240, 0.423, 1.144). This is also the physically
// right answer for a C-arm — the arc turns about its own isocentre.
//
// Caveat: it is the bbox centre of an asymmetric assembly, while the beam cone
// is symmetric about Y = 0. LAO is unaffected (RotateY touches only X and Z, and
// both agree on X = 0.240), but RotateX(CRAN) pivots about Y = 0.423, so at
// CRAN 45 degrees the beam axis swings ~0.42 m off isocentre.
// setHeadPivot(0.240f, 0.0f, 1.177f) from MainWindow overrides it.
// ─────────────────────────────────────────────────────────────────────────────
class CarmView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit CarmView(QWidget *parent = nullptr);
    ~CarmView() override;

    bool loadPart(const QString &path, int partIndex);

    void setAngles(float lao, float cran);

    /// Animated target for the patient TABLE's longitudinal position, along
    /// the same X axis this scene's own camera setup establishes as "side
    /// to side" - see paintGL()'s own comment on why X, not Z. `f` is 0..1:
    /// 0 = the head end (matching the console's own default/rest position,
    /// which already images the cranial region), 1 = the far/foot end.
    /// Derived from the bed's own real geometry (see loadPart()'s own
    /// comment), not a caller-supplied mm offset converted from a different
    /// coordinate space - two earlier attempts at that conversion both
    /// moved things the wrong way before this settled on reading the bed's
    /// own bounds directly.
    ///
    /// Moves the TABLE, not the C-arm assembly (stand/head/xray stay
    /// exactly where they are, aside from their own LAO/CRAN rotation) -
    /// per explicit correction: a real C-arm does not travel far along the
    /// table to reposition between the access site and the imaging target,
    /// the PATIENT does, via the table itself sliding underneath a
    /// relatively fixed gantry. An earlier version of this method moved the
    /// stand/head/xray assembly instead, which this replaces outright
    /// rather than keeping both - this now fully absorbs the role a
    /// separate, never-actually-called setBedPosition()/m_bedPos pair had
    /// been sitting there for beforehand.
    ///
    /// A real table repositioning between access site and imaging target is
    /// a genuine several-second travel, not an instant jump, so this eases
    /// m_bedLongPos toward the target over time (see the animation timer in
    /// the .cpp) rather than snapping straight there.
    void setBedBodyFraction(float f);
    float bedLongitudinalPos() const { return m_bedLongPos; }
    /// Changes only how fast m_bedLongPos eases toward whatever target
    /// setBedBodyFraction() last set - never the target itself. Takes
    /// effect on the next animation tick, mid-travel or not, the same way
    /// a setBedBodyFraction() call while already moving does (see that
    /// method's own comment). Clamped away from zero/negative so a bad
    /// caller value can never stall the table outright.
    void setBedMoveSpeedMPerSec(float mps) { m_bedMoveSpeedMPerSec = std::max(mps, 0.001f); }
    float bedMoveSpeedMPerSec() const { return m_bedMoveSpeedMPerSec; }
    /// The speed a caller resets to when it wants normal (non-slowed)
    /// travel - see setBedMoveSpeedMPerSec()'s own comment.
    static constexpr float kDefaultBedMoveSpeedMPerSec = 0.15f;
    /// The bed's own real head-to-foot travel range, in this scene's native
    /// metre units - the same m_bedHeadX/m_bedFootX measurement
    /// setBedBodyFraction() itself derives its target from (see that
    /// method's own comment), just exposed as a length rather than
    /// consumed internally. Lets a caller convert a real-world distance
    /// (already in the same physical units the rest of the app uses, e.g.
    /// via its own mm-per-world-unit calibration) into the 0..1 fraction
    /// setBedBodyFraction() expects, instead of normalizing against some
    /// other, unrelated length that only coincidentally tracks this one.
    /// 0 if the bed hasn't loaded yet.
    float bedSpanMetres() const { return std::abs(m_bedFootX - m_bedHeadX); }
    /// 0.0005f = 0.5mm - m_bedLongPos/m_bedLongTarget are in this scene's
    /// own native metre units (see setBedBodyFraction()), not millimetres.
    bool bedIsMoving() const { return std::abs(m_bedLongTarget - m_bedLongPos) > 0.0005f; }

    /// Real seconds this scene's own bed animation takes to travel its
    /// FULL head-to-foot range at the CURRENT bedMoveSpeedMPerSec() - i.e.
    /// what a setBedBodyFraction(1.f) call from a fraction-0 rest position
    /// would actually take right now. Exposed so a caller driving a
    /// SEPARATE animation (VesselView's own body-focus camera pan) that
    /// needs to visually stay in sync with this one can derive ITS OWN
    /// speed from this real, measured value instead of a second,
    /// independently-guessed duration constant that can only ever
    /// coincidentally match this one - which is exactly the bug an actual
    /// side-by-side comparison caught (the camera pan visibly finishing
    /// faster than the table). 0 if the bed hasn't loaded yet.
    float bedFullTraversalSeconds() const {
        return std::abs(m_bedFootX - m_bedHeadX) / m_bedMoveSpeedMPerSec;
    }

    // ── Console read-outs ───────────────────────────────────────────────────
    /// Source-to-image distance, cm. Real C-arms run roughly 90-120 cm.
    void setSid(float cm);
    float sid() const { return m_sidCm; }

    /// Image-intensifier field size, cm — the "magnification" figure. Standard
    /// formats are 32 / 22 / 16 cm; smaller means more magnified.
    void setMagnification(float cm);
    float magnification() const { return m_magCm; }

    /// Pulsed fluoroscopy frame rate, fps - the same "FRAME" figure the
    /// Fluoroscopy HUD displays (VesselView::setDetectorInfo()) and Cine
    /// acquisition actually captures at (MainWindow::startCineRecordingIfNeeded()).
    /// A single real value both read, rather than two independently
    /// hardcoded "15"s that happen to agree right now but have no
    /// structural reason to stay that way if either is ever changed alone.
    void setFrameRate(int fps) { m_frameRateFps = std::clamp(fps, 1, 60); }

    /// Multiplies the camera distance computed from the model's own
    /// bounding box - >1.0 moves the camera farther away (zoomed out).
    /// The distance calculation itself is based purely on the model's own
    /// size, not this widget's own size/aspect ratio, so a much smaller,
    /// more square embedded instance (versus the main simulator page's
    /// wide C-Arm Status card) can end up with a narrower effective
    /// horizontal field of view at the same fixed vertical FOV, cropping a
    /// wide model like the C-arm rig out of frame. This is the direct,
    /// explicit way to compensate, rather than fighting the aspect ratio.
    void setZoomFactor(float f) { m_zoomFactor = std::max(0.1f, f); recomputeCameraFromBounds(); update(); }

    /// Shifts the camera's aim point up/down (fraction of the combined
    /// parts' own bounding-box diagonal, scale-independent) without
    /// touching the auto-computed distance/zoom - see
    /// recomputeCameraFromBounds()'s own comment for the exact mechanism.
    /// Positive moves the rendered rig/bed toward the BOTTOM of the frame
    /// (aims the camera higher than the scene's true centre); negative
    /// moves it toward the top.
    void setVerticalFrameBias(float frac) { m_vFrameBias = frac; recomputeCameraFromBounds(); update(); }

    /// Shifts the camera's aim point left/right (fraction of the combined
    /// parts' own bounding-box diagonal, scale-independent), the same
    /// mechanism as setVerticalFrameBias() just along the OTHER screen
    /// axis. Sign picked, not verified against a live render - if this
    /// moves the rendered rig/bed the wrong way, negate the value passed
    /// in rather than assuming this comment's own claimed direction.
    void setHorizontalFrameBias(float frac) { m_hFrameBias = frac; recomputeCameraFromBounds(); update(); }

    /// Shrinks/grows ONLY the bed/table mesh, around its own local centre -
    /// per explicit request, distinct from setZoomFactor() (which moves the
    /// CAMERA, affecting every part equally) or the frame-bias setters
    /// (which shift what the camera is AIMED at, not any part's own size).
    /// 1.0 = the bed's own real modelled size, <1.0 shrinks it (reads as
    /// "further away" without actually being further away - the C-arm
    /// parts stay exactly their own real size and position either way).
    /// Purely visual - does not feed into recomputeCameraFromBounds()'s own
    /// framing math, which still measures the bed's REAL, unscaled extent
    /// (including its own full-travel safety margin), so shrinking the bed
    /// cosmetically can never accidentally make the camera zoom in tighter
    /// and clip it after all.
    void setBedScale(float s) { m_bedScale = std::max(0.05f, s); update(); }

    /// Nudges the xray/beam-cone part up/down along this scene's own Z
    /// axis, as a fraction of the part's OWN diagonal (scale-independent) -
    /// purely cosmetic, closes a small visible gap between the cone's own
    /// wide end and the head housing's bottom edge. Positive moves it
    /// toward +Z; sign/magnitude not yet verified against a live render.
    void setXrayVerticalNudge(float frac) { m_xrayVerticalNudge = frac; update(); }

    /// Shrinks/grows ONLY the xray/beam-cone part, around its own local
    /// centre - the analogous control to setBedScale() above, for the
    /// xray part specifically. 1.0 = its own real modelled size, <1.0
    /// shrinks it. Purely visual - does not feed into recomputeCamera
    /// FromBounds()'s own framing math, same reasoning as setBedScale().
    void setXrayScale(float s) { m_xrayScale = std::max(0.05f, s); update(); }

    /// Scales the HUD readout font sizes (normally 20pt values / 8pt
    /// labels) - the main simulator page's own C-Arm Status card has
    /// plenty of width for those sizes, but a much narrower embedded
    /// instance (like Cine Studio's C-Arm Quick View) needs them scaled
    /// down to avoid the text overflowing or getting cropped at the edge.
    void setHudFontScale(float scale) { m_hudFontScale = std::max(0.1f, scale); update(); }
    int frameRate() const { return m_frameRateFps; }

    /// Fluoroscopy time accumulates only while this is true, the way a real
    /// console counts only while the pedal is down.
    void setFluoroActive(bool on);
    bool fluoroActive() const { return m_fluoroActive; }
    void resetFluoroTime();
    double fluoroSeconds() const { return m_fluoroSec; }

    float lao()    const { return m_lao; }
    float cran()   const { return m_cran; }
    QVector3D headPivot() const { return m_headCenter; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    struct Part {
        cn::StlMesh mesh;
        struct Batch {
            QOpenGLVertexArrayObject vao;
            QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
            QOpenGLBuffer ibo{QOpenGLBuffer::IndexBuffer};
            int triCount = 0;
            bool ready   = false;
            void destroy();
        } batch;
        bool loaded = false;
    };

    void uploadPart(Part &p);
    /// `specular` 0 gives a matte finish with no highlights; `ambient` raises
    /// the shadow floor and correspondingly flattens the diffuse term.
    void drawPart(Part &p, const QMatrix4x4 &model, const QVector4D &colour,
                  float specular, float ambient);
    QMatrix4x4 headTransform() const;
    /// Recomputes m_target/m_eye/m_distance from every currently-loaded
    /// part's combined bounds - factored out of loadPart() so
    /// setZoomFactor()/setVerticalFrameBias() can also trigger an
    /// immediate recompute regardless of call order relative to loading
    /// (paintGL() only ever reads the cached m_eye/m_target, it does not
    /// recompute them itself, so changing the zoom/bias after all parts
    /// have already loaded would otherwise silently do nothing until some
    /// later loadPart() call happened to run).
    void recomputeCameraFromBounds();

    static const int kNumParts = 4;
    // Named indices so the layout is stated once instead of appearing as bare
    // integers in loadPart() and paintGL().
    static const int kPartStand = 0;   ///< floor gantry — fixed
    static const int kPartHead  = 1;   ///< the only movable part, and the pivot source
    static const int kPartBed   = 2;
    static const int kPartXray  = 3;

    Part m_parts[kNumParts];

    std::unique_ptr<QOpenGLShaderProgram> m_prog;

    // Gantry pivot = head bounding-box centre (index 0), cached in loadPart(0)
    QVector3D m_headCenter;

    // Camera — derived from old project formula once all parts are loaded
    QVector3D m_eye;
    QVector3D m_target;
    float     m_distance = 1.f;

    float m_lao    = 0.f;
    float m_cran   = 0.f;

    // ── Patient table longitudinal travel ────────────────────────────────
    // See setBedBodyFraction()'s own comment - moves the TABLE (kPartBed),
    // not the C-arm assembly. Native scene units (metres), NOT millimetres
    // - paintGL() applies m_bedLongPos directly as a translate() with no
    // unit conversion, since it is derived straight from the bed's own
    // real bounds below, already in the same units.
    float m_bedLongPos    = 0.f;   ///< current, eases toward the target
    float m_bedLongTarget = 0.f;
    /// The bed's own two ends along X, cached once in loadPart() when the
    /// bed loads - see that function's own comment for why
    /// setBedBodyFraction() derives its target from these rather than a
    /// converted external coordinate.
    float m_bedHeadX = 0.f;
    float m_bedFootX = 0.f;
    /// Real-world travel speed, metres/s (default 0.15 m/s = 150mm/s) -
    /// reasoned, not measured, to read as a genuine several-second
    /// repositioning (a typical ~0.4-0.6m access-to-target travel takes
    /// roughly 3-4 seconds at this rate) rather than an instant snap or an
    /// unrealistically slow crawl. m_bedLongPos/m_bedLongTarget are in the
    /// SAME native (metre) units as the bed's own bounds (see
    /// setBedBodyFraction()), not millimetres, so this is metres/s to
    /// match rather than requiring a conversion at every use site.
    ///
    /// A caller-settable member rather than a compile-time constant so a
    /// specific access site's own per-tick tracking (see
    /// MainWindow::syncDevices()) can slow the table's response down
    /// without touching where the target itself comes from - the target
    /// (setBedBodyFraction()'s own argument) and how fast the table eases
    /// toward it are independent concerns. See the public
    /// kDefaultBedMoveSpeedMPerSec for the starting/reset value.
    float m_bedMoveSpeedMPerSec = kDefaultBedMoveSpeedMPerSec;
    QElapsedTimer m_bedMoveClock;   ///< real elapsed time between animation ticks
    QTimer        m_bedMoveTimer;   ///< only runs WHILE actually moving - see setBedBodyFraction()
    void onBedMoveTick();

    // ── Console read-outs ───────────────────────────────────────────────────
    float  m_sidCm        = 100.f;
    float  m_magCm        =  32.f;
    int    m_frameRateFps = 15;
    float  m_zoomFactor = 1.f;   ///< see setZoomFactor()
    float  m_vFrameBias = 0.f;   ///< see setVerticalFrameBias()
    float  m_hFrameBias = 0.f;   ///< see setHorizontalFrameBias()
    float  m_bedScale   = 1.f;   ///< see setBedScale()
    float  m_xrayVerticalNudge = 0.f;   ///< see setXrayVerticalNudge()
    float  m_xrayScale = 1.f;   ///< see setXrayScale()
    float  m_hudFontScale = 1.f;   ///< see setHudFontScale()
    double m_fluoroSec    =   0.0;
    bool   m_fluoroActive = true;
    QElapsedTimer m_fluoroClock;   ///< measured, not counted in timer ticks
    QTimer        m_hudTimer;      ///< 1 Hz, only to advance the clock display

    /// Draws one label-over-value column of the read-out cluster and returns
    /// its width, so the caller can lay the columns out without hard-coding
    /// pixel positions that would drift when a value changes width.
    int drawReadout(QPainter &p, int centreX, int labelBaseline, int valueBaseline,
                    const QString &label, const QString &value,
                    const QFont &labelFont, const QFont &valueFont) const;
};