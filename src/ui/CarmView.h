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
    void setBedPosition(float mm);

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
    void setZoomFactor(float f) { m_zoomFactor = std::max(0.1f, f); update(); }

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
    float bedPos() const { return m_bedPos; }
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
    float m_bedPos = 0.f;

    // ── Console read-outs ───────────────────────────────────────────────────
    float  m_sidCm        = 100.f;
    float  m_magCm        =  32.f;
    int    m_frameRateFps = 15;
    float  m_zoomFactor = 1.f;   ///< see setZoomFactor()
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