#include "CarmView.h"
#include "TraceLog.h"

#include <QMouseEvent>
#include <QOpenGLShaderProgram>
#include <QFontMetrics>
#include <QPainter>
#include <QSurfaceFormat>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>

// ── Shaders ──────────────────────────────────────────────────────────────────
namespace {

const char *kVert = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
uniform mat4 uMVP;
uniform mat4 uModel;
out vec3 vNrm;
out vec3 vWorld;
void main() {
    vec4 w   = uModel * vec4(aPos, 1.0);
    vWorld   = w.xyz;
    vNrm     = mat3(transpose(inverse(uModel))) * aNrm;
    gl_Position = uMVP * vec4(aPos, 1.0);
})";

const char *kFrag = R"(#version 330 core
in  vec3 vNrm;
in  vec3 vWorld;
out vec4 fragColor;
uniform vec4 uColor;
uniform vec3 uCamPos;
// Per-part finish. The Phong coefficients used to be hard-coded, so every part
// got the same glossy treatment — the specular lobe picking out every undulation
// is what makes a plain mannequin read as detailed anatomy.
uniform float uSpecular;
uniform float uAmbient;
void main() {
    vec3 n = normalize(vNrm);
    vec3 v = normalize(uCamPos - vWorld);
    if (dot(n, v) < 0.0) n = -n;

    // VTK default two-light Phong model:
    // Key light  — headlight (from camera direction), intensity 1.0
    // Fill light — from upper-right, intensity 0.4 (VTK LightKit fill)
    vec3 Lkey  = normalize(uCamPos);             // headlight: direction = camera position
    vec3 Lfill = normalize(vec3(0.5, 0.0, 1.0)); // fill from upper-right

    // VTK default material was ambient=0.1, diffuse=0.7, specular=0.2, specPow=10.
    // Ambient and specular now come from the draw call; diffuse takes whatever
    // ambient leaves, so raising ambient flattens the shading rather than just
    // brightening it. A high-ambient, zero-specular part reads as smooth matte
    // plastic with no surface features at all.
    float amb   = uAmbient;
    float dcoef = 1.0 - uAmbient;
    float scoef = uSpecular;
    float spow  = 10.0;

    float diffKey  = max(dot(n,  Lkey),  0.0);
    float diffFill = max(dot(n,  Lfill), 0.0) * 0.4;  // fill at 40% intensity

    vec3 hKey  = normalize(Lkey  + v);
    vec3 hFill = normalize(Lfill + v);
    float specKey  = pow(max(dot(n, hKey),  0.0), spow) * 1.0;
    float specFill = pow(max(dot(n, hFill), 0.0), spow) * 0.4;

    vec3 rgb = uColor.rgb * (amb + dcoef * (diffKey + diffFill))
             + vec3(scoef * (specKey + specFill));

    fragColor = vec4(clamp(rgb, 0.0, 1.0), uColor.a);
})";

} // namespace

// ── Part ──────────────────────────────────────────────────────────────────────
void CarmView::Part::Batch::destroy() {
    vao.destroy(); vbo.destroy(); ibo.destroy();
}

// ── Constructor / Destructor ──────────────────────────────────────────────────
CarmView::CarmView(QWidget *parent) : QOpenGLWidget(parent) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSamples(4);
    fmt.setDepthBufferSize(24);
    setFormat(fmt);
    setMinimumSize(260, 220);
    setFocusPolicy(Qt::StrongFocus);

    // Fluoro time is measured off an elapsed timer rather than counted in timer
    // ticks: a QTimer that misses fires under load would run the clock slow.
    // The 1 Hz timer only asks for a repaint so the display advances.
    m_fluoroClock.start();
    connect(&m_hudTimer, &QTimer::timeout, this, [this]{ if (m_fluoroActive) update(); });
    m_hudTimer.start(1000);

    // Patient table longitudinal travel - real-elapsed-time-driven (same
    // QElapsedTimer-based reasoning as m_fluoroClock just above: a timer
    // that fires late under load must not silently run the travel slow),
    // and only running WHILE actually moving - see setBedBodyFraction().
    m_bedMoveClock.start();
    connect(&m_bedMoveTimer, &QTimer::timeout, this, &CarmView::onBedMoveTick);
}

CarmView::~CarmView() {
    makeCurrent();
    for (auto &p : m_parts) p.batch.destroy();
    m_prog.reset();
    doneCurrent();
}

// ── loadPart ──────────────────────────────────────────────────────────────────
void CarmView::recomputeCameraFromBounds() {
    float lo[3] = { 1e9f,  1e9f,  1e9f};
    float hi[3] = {-1e9f, -1e9f, -1e9f};
    bool any = false;
    for (int pi = 0; pi < kNumParts; ++pi) {
        if (!m_parts[pi].loaded) continue;
        const cn::Box3 &b2 = m_parts[pi].mesh.bounds();
        if (!b2.valid()) continue;
        lo[0]=std::min(lo[0],b2.lo.x); lo[1]=std::min(lo[1],b2.lo.y); lo[2]=std::min(lo[2],b2.lo.z);
        hi[0]=std::max(hi[0],b2.hi.x); hi[1]=std::max(hi[1],b2.hi.y); hi[2]=std::max(hi[2],b2.hi.z);
        any = true;
    }
    if (!any) return;

    // Extend the X range to cover PART of the bed's own full travel, not
    // just wherever it happens to be sitting at load time (its unshifted
    // rest position) - see setBedBodyFraction()'s own comment: the table
    // slides by up to the full (m_bedFootX - m_bedHeadX) distance to reach
    // a far access site (e.g. popliteal/foot), and framing only for its
    // resting position is what let the bed - and the patient on it -
    // slide clean off the edge of this small preview panel once a distant
    // access site was chosen, confirmed from an actual recording.
    //
    // Only HALF the full travel, not the full amount, and this is a
    // deliberate trade-off, not an oversight: extending by the complete
    // travel on both sides very nearly doubled the effective diagonal,
    // which directly fights against wanting this view zoomed in for its
    // own default/resting appearance (confirmed from an actual follow-up -
    // reducing zoomFactor alone could not overcome how much bigger that
    // made the frame). Half the travel still gives real protection against
    // the worst-case clipping while leaving the resting view's own zoom
    // level meaningfully tighter.
    if (m_bedFootX != m_bedHeadX) {
        const float travel = std::fabs(m_bedFootX - m_bedHeadX) * 0.5f;
        lo[0] -= travel;
        hi[0] += travel;
    }

    const float cx = (lo[0]+hi[0])*.5f;
    const float cy = (lo[1]+hi[1])*.5f;
    const float cz = (lo[2]+hi[2])*.5f;
    const float dx = hi[0]-lo[0], dy = hi[1]-lo[1], dz = hi[2]-lo[2];
    const float diag = std::sqrt(dx*dx + dy*dy + dz*dz);

    // m_vFrameBias shifts the AIM point's Z, not the eye - since the camera
    // sits level with the target (eye's own Z offset is 0) and ViewUp is
    // (0,0,1), whatever Z the aim point sits at renders at screen-centre.
    // Aiming HIGHER than the scene's true centre (positive bias) pushes the
    // whole rig, bed included, toward the BOTTOM of the frame - which is
    // "move the bed down" without touching the auto-computed distance/zoom
    // math at all. See setVerticalFrameBias()'s own comment.
    m_target   = QVector3D(cx + diag * m_hFrameBias, cy, cz + diag * m_vFrameBias);
    const QVector3D eyeOffset = QVector3D(-diag*0.20f, -diag*1.6f, 0.f) * m_zoomFactor;
    m_eye      = m_target + eyeOffset;   // zoomed out (offset was diag*1.2 before m_zoomFactor existed)
    m_distance = (m_eye - m_target).length();
}

bool CarmView::loadPart(const QString &path, int idx) {
    if (idx < 0 || idx >= kNumParts) return false;
    Part &p = m_parts[idx];
    std::string err;
    if (!p.mesh.load(path.toStdString(), &err)) return false;
    p.loaded = true;

    // Cache the rotation pivot from carm_head, because carm_head is the part
    // that rotates. A body must turn about a point inside itself: pivoting on
    // the other part puts the centre just over a metre away and the mover
    // orbits instead of turning — 269 mm at CRAN 15 degrees, 787 mm at 45.
    if (idx == kPartHead) {
        const cn::Box3 b = p.mesh.bounds();
        m_headCenter = QVector3D(b.center().x, b.center().y, b.center().z);
    }

    // Cache the bed's own two ends along X, so the table's own longitudinal
    // target (setBedBodyFraction()) can be derived purely from the bed's
    // own real geometry - the SAME coordinate space every C-arm part
    // already shares - rather than guessing a sign/direction convention
    // against the vessel graph's own, separate coordinate space. Two
    // earlier attempts at that guess both moved things the wrong way
    // (away from the bed, then still wrong after a sign flip);
    // deriving the target from where the bed actually IS cannot have that
    // failure mode, since the target is always somewhere along the bed's
    // own physical length by construction.
    if (idx == kPartBed) {
        const cn::Box3 b = p.mesh.bounds();
        // Whichever end sits closer to X=0 - the C-arm assembly's own
        // rest/default position, which by this console's convention
        // already correctly images the head - IS the head end.
        if (std::abs(b.lo.x) < std::abs(b.hi.x)) {
            m_bedHeadX = b.lo.x;
            m_bedFootX = b.hi.x;
        } else {
            m_bedHeadX = b.hi.x;
            m_bedFootX = b.lo.x;
        }
    }

    // Recompute camera from combined bounds of all loaded parts.
    // Replicates old project setupVTK():
    //   m_carmRenderer->ComputeVisiblePropBounds(bounds);
    //   cx,cy,cz = centre of bounds
    //   diagLen  = diagonal of bounds
    //   eye = (cx - diagLen*0.20,  cy - diagLen*1.2,  cz + 0)
    //   focal = (cx, cy, cz)
    //   ViewUp = (0, 0, 1)
    recomputeCameraFromBounds();

    if (isValid()) { makeCurrent(); uploadPart(p); doneCurrent(); update(); }
    return true;
}

// ── setAngles / setBedBodyFraction ────────────────────────────────────────────
void CarmView::setAngles(float lao, float cran) {
    traceLog(QStringLiteral("[CARM] CarmView::setAngles  LAO %1 -> %2  CRAN %3 -> %4")
                 .arg(m_lao).arg(lao).arg(m_cran).arg(cran));
    m_lao  = std::clamp(lao,  -90.f,  90.f);
    m_cran = std::clamp(cran, -45.f,  45.f);
    update();
}

void CarmView::setSid(float cm) {
    m_sidCm = std::clamp(cm, 60.f, 150.f);
    update();
}

void CarmView::setMagnification(float cm) {
    m_magCm = std::clamp(cm, 10.f, 45.f);
    update();
}

void CarmView::setFluoroActive(bool on) {
    if (on == m_fluoroActive) return;
    // Bank the elapsed time before switching, so toggling never loses or
    // double-counts a partial interval.
    if (m_fluoroActive && m_fluoroClock.isValid())
        m_fluoroSec += m_fluoroClock.restart() / 1000.0;
    m_fluoroActive = on;
    if (on) m_fluoroClock.restart();
    update();
}

void CarmView::resetFluoroTime() {
    m_fluoroSec = 0.0;
    m_fluoroClock.restart();
    update();
}

void CarmView::setBedBodyFraction(float f) {
    traceLog(QStringLiteral("[CARM] CarmView::setBedBodyFraction(%1)  bedLongPos=%2 bedLongTarget(before)=%3")
                 .arg(f).arg(m_bedLongPos).arg(m_bedLongTarget));
    f = std::clamp(f, 0.f, 1.f);
    // NEGATED relative to the earlier "C-arm moves" version's own formula -
    // this is a genuine physics inversion, not just a sign preference. When
    // the C-ARM moved to reach a fixed point on the bed, the target was
    // directly that point's own local coordinate. Now the BED moves so a
    // point on IT reaches the fixed C-arm instead - shifting the bed by
    // +X moves that same point by +X too, which is backwards: to bring a
    // point sitting at local coordinate (m_bedHeadX + f*span) back into
    // alignment with the C-arm's own fixed position (which already lines
    // up with m_bedHeadX at rest, f=0, by construction), the bed has to
    // move by the NEGATIVE of how far that point sits from the head end -
    // hence the leading minus sign. Confirmed wrong the first time from an
    // actual screenshot of the table moving the wrong direction.
    m_bedLongTarget = -f * (m_bedFootX - m_bedHeadX);
    if (!m_bedMoveTimer.isActive()) {
        m_bedMoveClock.restart();   // don't count whatever time passed since the LAST move as part of THIS one
        m_bedMoveTimer.start(16);   // ~60fps while actually moving
    }
}

void CarmView::onBedMoveTick() {
    const qint64 elapsedMs = m_bedMoveClock.restart();
    const float step = m_bedMoveSpeedMPerSec * (static_cast<float>(elapsedMs) / 1000.f);
    const float remaining = m_bedLongTarget - m_bedLongPos;
    if (std::abs(remaining) <= step) {
        m_bedLongPos = m_bedLongTarget;
        m_bedMoveTimer.stop();   // arrived - stop asking for repaints until the next move
    } else {
        m_bedLongPos += (remaining > 0.f ? step : -step);
    }
    update();
}

// ── GL ────────────────────────────────────────────────────────────────────────
void CarmView::uploadPart(Part &p) {
    if (!p.loaded) return;
    auto &b = p.batch;
    if (!b.ready) {
        b.vao.create();
        b.vbo.create(); b.vbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
        b.ibo.create(); b.ibo.setUsagePattern(QOpenGLBuffer::StaticDraw);
        b.ready = true;
    }
    const auto verts   = p.mesh.buildInterleavedRenderData();
    const auto indices = p.mesh.buildIndexData();

    QOpenGLVertexArrayObject::Binder bnd(&b.vao);
    b.vbo.bind();
    b.vbo.allocate(verts.data(), static_cast<int>(verts.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float),
                          reinterpret_cast<void*>(3*sizeof(float)));
    b.ibo.bind();
    b.ibo.allocate(indices.data(), static_cast<int>(indices.size() * sizeof(uint32_t)));
    b.triCount = static_cast<int>(indices.size());
}

void CarmView::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glDisable(GL_CULL_FACE);

    m_prog = std::make_unique<QOpenGLShaderProgram>();
    m_prog->addShaderFromSourceCode(QOpenGLShader::Vertex,   kVert);
    m_prog->addShaderFromSourceCode(QOpenGLShader::Fragment, kFrag);
    m_prog->bindAttributeLocation("aPos", 0);
    m_prog->bindAttributeLocation("aNrm", 1);
    m_prog->link();

    for (auto &p : m_parts) if (p.loaded) uploadPart(p);
}

void CarmView::resizeGL(int w, int h) { glViewport(0, 0, w, h); }

// ── headTransform — exact replica of CarmController::applyTransforms() ────────
QMatrix4x4 CarmView::headTransform() const {
    // Old project:
    //   tf->Translate(+headCenter)
    //   tf->RotateY(m_laoAngle)   // LAO/RAO
    //   tf->RotateX(m_cranAngle)  // CRAN/CAUD
    //   tf->Translate(-headCenter)
    // Qt PostMultiply order = same sequence:
    QMatrix4x4 t;
    t.translate( m_headCenter);
    t.rotate(m_lao,  0, 1, 0);   // RotateY = LAO/RAO
    t.rotate(m_cran, 1, 0, 0);   // RotateX = CRAN/CAUD
    t.translate(-m_headCenter);
    return t;
}

// ── drawPart ──────────────────────────────────────────────────────────────────
void CarmView::drawPart(Part &p, const QMatrix4x4 &model, const QVector4D &colour,
                        float specular, float ambient) {
    if (!p.loaded || !p.batch.ready || p.batch.triCount == 0) return;

    QMatrix4x4 proj, view;
    const float asp = height() > 0 ? float(width()) / float(height()) : 1.f;
    // 30° FOV — close to VTK default parallel-ish look at this distance
    proj.perspective(30.f, asp, m_distance * 0.005f, m_distance * 10.f);
    // ViewUp = (0,0,1) — matches old project carmCam->SetViewUp(0,0,1)
    view.lookAt(m_eye, m_target, QVector3D(0, 0, 1));
    const QMatrix4x4 mvp = proj * view * model;

    m_prog->setUniformValue("uMVP",    mvp);
    m_prog->setUniformValue("uModel",  model);
    m_prog->setUniformValue("uColor",  colour);
    m_prog->setUniformValue("uCamPos", m_eye);
    m_prog->setUniformValue("uSpecular", specular);
    m_prog->setUniformValue("uAmbient",  ambient);

    QOpenGLVertexArrayObject::Binder bnd(&p.batch.vao);
    glDrawElements(GL_TRIANGLES, p.batch.triCount, GL_UNSIGNED_INT, nullptr);
}

// ── paintGL ───────────────────────────────────────────────────────────────────
void CarmView::paintGL() {
    // Pure black, matching the 3D/Mesh pane in VesselView. Was (0.05, 0.05,
    // 0.09) — a dark blue-grey carried over from the old project's
    // SetBackground call, which read as a slightly lifted, slightly blue panel
    // next to the black vessel views.
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_prog) return;
    m_prog->bind();

    // Colours — exact old project loadSTL() calls:
    //   head  = 0.88, 0.89, 0.92  opacity 1.0
    //   stand = 0.75, 0.77, 0.80  opacity 1.0
    //   bed   = 0.96, 0.82, 0.68  opacity 1.0
    //   xray  = 0.25, 0.55, 0.95  opacity 0.15
    //
    // ── Part appearance ──────────────────────────────────────────────────────
    //
    // Layout: 0=stand, 1=head(arc), 2=bed, 3=xray.
    //
    // The rig is drawn slightly transparent — around 0.80 — so the anatomy and
    // beam behind it stay readable, while remaining solid enough to read as
    // machinery rather than a ghost.
    //
    // The bed part carries the patient figure as well as the table: they are one
    // connected body in carm_bed.stl (component 0 is 96% of that mesh), so they
    // cannot be separated by geometry and share a material. That material is
    // deliberately matte — specular 0, high ambient — which is what turns the
    // figure into a plain smooth mannequin: the highlights were the only thing
    // suggesting surface detail, since the mesh carries no texture at all.
    struct PartStyle {
        QVector4D colour;
        float     specular;
        float     ambient;
    };
    const PartStyle kStyles[kNumParts] = {
                                           // stand — gantry, keeps a little sheen so it reads as painted metal
                                           {{0.75f, 0.77f, 0.80f, 0.80f}, 0.12f, 0.26f},
                                           // head — arc + detector, same treatment
                                           {{0.88f, 0.89f, 0.92f, 0.80f}, 0.12f, 0.26f},
                                           // bed + patient — matte, flat, no highlights
                                           {{0.93f, 0.86f, 0.78f, 0.84f}, 0.00f, 0.46f},
                                           // xray — beam cone, faint and unlit
                                           {{0.25f, 0.55f, 0.95f, 0.22f}, 0.00f, 0.70f},
                                           };

    // Draw order is decoupled from part identity on purpose: change this array
    // to re-sequence rendering without disturbing which part moves.
    //
    // It is also the back-to-front order the blending needs, with the camera at
    // -Y: stand sits farthest (Y 0.54..2.34), then the arc, then the bed nearest
    // (Y -0.48..0.43). Reordering these would show through incorrectly.
    const int kOrder[kNumParts] = {kPartStand, kPartHead, kPartBed, kPartXray};

    for (int oi = 0; oi < kNumParts; ++oi) {
        const int i = kOrder[oi];
        QMatrix4x4 model;

        // carm_head carries the movement controls — A/D drive CRAN/CAUD, W/S
        // drive LAO/RAO, R resets, all through CarmController — and carm_xray
        // rides with it.
        //
        // The beam cone is mounted on the arc: source and detector are rigidly
        // coupled on a real C-arm, so a detector that swings away from its own
        // beam is wrong. Both take the SAME headTransform(), which is a rotation
        // about the arc's own centre, so the cone keeps its relative position to
        // the head exactly — no separate offset to maintain and none to drift.
        //
        // carm_stand, carm_head and carm_xray stay at IDENTITY/rotation-only -
        // a real C-arm gantry does not travel far along the table to
        // reposition between the access site and the imaging target, the
        // PATIENT does, via the table sliding underneath a relatively fixed
        // gantry - per explicit correction. carm_bed is the one that carries
        // the longitudinal travel (m_bedLongPos, see setBedBodyFraction()),
        // not the assembly - an earlier version had this backwards (moved
        // the assembly, kept the bed fixed), which is what this replaces.
        //
        // X, not Z, is the table's own long axis in THIS scene - confirmed by
        // the camera setup just above this function (ViewUp = (0,0,1) makes Z
        // the screen-VERTICAL axis; the camera looks roughly along Y), so the
        // remaining axis, X, is what reads as side-to-side on screen.
        //
        // Translation is applied OUTSIDE headTransform() (constructed first,
        // multiplied in after) so the head/xray still rotate about the arc's
        // own pivot for LAO/CRAN exactly as before - head/xray no longer
        // carry any longitudinal shift at all, but keep this ordering for
        // when a future need for combining the two arises.
        //
        // No *0.001f here - m_bedLongPos is already in this scene's own
        // native (metre) units, derived directly from the bed's own real
        // bounds (see setBedBodyFraction()).
        if (i == kPartHead) {
            model = headTransform();
        } else if (i == kPartXray) {
            // Real bug in the previous version, confirmed from an actual
            // screenshot: rotating 180 degrees with NO pivot correction
            // rotates around whatever centre headTransform() itself pivots
            // around (the arc's shared centre) - since the beam cone hangs
            // some real distance BELOW that pivot, not AT it, a 180-degree
            // turn swung the whole cone across to the OPPOSITE side of
            // that pivot entirely, which is what made it vanish (ended up
            // somewhere overlapping/behind the solid head housing instead
            // of merely upside-down in place).
            //
            // Fixed the same way setBedScale() already handles this same
            // class of problem: rotate around the XRAY PART'S OWN LOCAL
            // CENTRE first (translate to it, rotate, translate back), so
            // the cone flips its own orientation in place without moving
            // to a different position - THEN headTransform()'s shared
            // rotation carries the whole, now-correctly-oriented cone
            // along with the head for LAO/CRAN, same as before.
            //
            // Axis (1,0,0) confirmed correct against an actual screenshot -
            // the cone is visible again and correctly oriented, tapering
            // down toward the bed.
            const cn::Vec3 xc = m_parts[kPartXray].mesh.bounds().center();
            model = headTransform();
            model.translate(xc.x, xc.y, xc.z);
            model.rotate(180.f, 1.f, 0.f, 0.f);
            // Closes a small visible gap between the cone's own wide/top
            // end and the head housing's bottom edge, per explicit
            // follow-up - "fit the xray coming from the stand" - a purely
            // positional nudge, the orientation itself is already correct.
            // Z is this scene's established vertical axis (ViewUp=(0,0,1),
            // same convention every other framing/bias calc in this file
            // already uses). Expressed as a fraction of the xray part's
            // own size (scale-independent, matching every other tunable
            // constant in this class) rather than an absolute distance.
            model.translate(0.f, 0.f, m_xrayVerticalNudge * m_parts[kPartXray].mesh.bounds().diagonal());
            model.translate(-xc.x, -xc.y, -xc.z);
        } else if (i == kPartBed) {
            // Scale AROUND THE BED'S OWN LOCAL CENTRE, not the world
            // origin - scaling around the origin would also shift the
            // bed's own position (everything is multiplied outward from
            // (0,0,0)), which is not what "make the bed look smaller" is
            // asking for. Composed so the slide (m_bedLongPos) still
            // applies to the ALREADY-SCALED bed afterward, not the other
            // way around - the bed shrinks in its own local space first,
            // then that shrunk bed slides to wherever setBedBodyFraction()
            // has it heading, exactly the same as before this existed.
            const cn::Vec3 bc = m_parts[kPartBed].mesh.bounds().center();
            model.translate(m_bedLongPos, 0.f, 0.f);
            model.translate(bc.x, bc.y, bc.z);
            model.scale(m_bedScale);
            model.translate(-bc.x, -bc.y, -bc.z);
        }
        // kPartStand: identity, always - bolted to the floor, does not move.

        const PartStyle &st = kStyles[i];
        const bool blended = st.colour.w() < 1.0f;
        if (blended) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            // Depth writes stay ON for the near-opaque rig parts. At alpha 0.80
            // a solid should still hide its own far side; turning writes off
            // makes every part show its interior and the rig reads as a wire
            // ghost. Only the very faint beam cone drops the depth write, so it
            // does not punch a hole in whatever is behind it.
            glDepthMask(st.colour.w() < 0.5f ? GL_FALSE : GL_TRUE);
        }
        drawPart(m_parts[i], model, st.colour, st.specular, st.ambient);
        if (blended) {
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
    }

    m_prog->release();

    // ── Console read-out cluster, above the C-arm render ─────────────────────
    //
    // One row, four equal columns, each a small label above a large value,
    // spanning the width of the panel - not the previous right-anchored,
    // uneven-width two-row cluster (LAO/CRAN/SID on row 1, Magnification/
    // TIME hanging under row 2). TIME mm:ss is removed from this display
    // entirely per request; the underlying tracking (m_fluoroSec,
    // m_fluoroClock, the public fluoroSeconds() accessor MainWindow's own
    // status bar reads) is completely untouched - only this HUD's own
    // display of it is gone.
    //
    //   LAO      CRAN     SID cm   Magnification
    //   51°      14°      100      32 cm
    //
    // Columns are now four EQUAL divisions of the available width, not
    // measured-text-width columns - a value growing a digit no longer
    // shifts the cluster at all, since each column's centre is fixed
    // regardless of its own content's width.
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QString sLao  = m_lao  >= 0 ? QString("%1°").arg( m_lao,  0, 'f', 0)
                                    : QString("%1°").arg(-m_lao,  0, 'f', 0);
    const QString sCran = m_cran >= 0 ? QString("%1°").arg( m_cran, 0, 'f', 0)
                                      : QString("%1°").arg(-m_cran, 0, 'f', 0);
    // Labels flip with the sign, the way the clinical convention requires:
    // a negative LAO is an RAO, a negative CRAN is a CAUD.
    const QString lLao  = m_lao  >= 0 ? QStringLiteral("LAO")  : QStringLiteral("RAO");
    const QString lCran = m_cran >= 0 ? QStringLiteral("CRAN") : QStringLiteral("CAUD");

    const QString sSid  = QString::number(int(m_sidCm));
    const QString sMag  = QStringLiteral("%1 cm").arg(int(m_magCm));

    QFont valueFont("Titillium Web", std::max(1, static_cast<int>(20 * m_hudFontScale)), QFont::Bold);
    valueFont.setStyleHint(QFont::SansSerif);
    QFont labelFont("Titillium Web", std::max(1, static_cast<int>(8 * m_hudFontScale)));
    labelFont.setStyleHint(QFont::SansSerif);
    const QFontMetrics fl(labelFont), fv(valueFont);

    const int margin = 10;
    const int rowGap = 6;

    // Anchored to the TOP edge: the label baseline sits one ascent below the
    // margin, growing downward, so the cluster stays put when the panel is
    // resized vertically.
    const int rowLabel = margin + fl.ascent();
    const int rowValue = rowLabel + rowGap + fv.ascent();

    const int usableW = std::max(width() - 2 * margin, 4);
    const int colW    = usableW / 4;
    const int c1 = margin + colW * 0 + colW / 2;
    const int c2 = margin + colW * 1 + colW / 2;
    const int c3 = margin + colW * 2 + colW / 2;
    const int c4 = margin + colW * 3 + colW / 2;

    drawReadout(p, c1, rowLabel, rowValue, lLao,  sLao,  labelFont, valueFont);
    drawReadout(p, c2, rowLabel, rowValue, lCran, sCran, labelFont, valueFont);
    drawReadout(p, c3, rowLabel, rowValue, QStringLiteral("SID cm"), sSid, labelFont, valueFont);
    drawReadout(p, c4, rowLabel, rowValue, QStringLiteral("Magnification"), sMag, labelFont, valueFont);
}

// Centres a small label over a large value at `centreX`. Returns the column
// width so callers can lay out without hard-coded positions.
int CarmView::drawReadout(QPainter &p, int centreX, int labelBaseline,
                          int valueBaseline, const QString &label,
                          const QString &value, const QFont &labelFont,
                          const QFont &valueFont) const {
    // Fonts are passed in rather than rebuilt here: this runs five times per
    // frame, and QFont construction plus metrics lookup is not free.
    const QFontMetrics fv(valueFont), fl(labelFont);

    p.setFont(labelFont);
    p.setPen(QColor(210, 215, 225));
    p.drawText(centreX - fl.horizontalAdvance(label) / 2, labelBaseline, label);

    p.setFont(valueFont);
    p.setPen(QColor(255, 214, 0));
    p.drawText(centreX - fv.horizontalAdvance(value) / 2, valueBaseline, value);

    return std::max(fl.horizontalAdvance(label), fv.horizontalAdvance(value));
}

// ── Mouse / wheel — locked, matches old project fixed camera ─────────────────
void CarmView::mousePressEvent(QMouseEvent *e) { Q_UNUSED(e) }
void CarmView::mouseMoveEvent(QMouseEvent *e)  { Q_UNUSED(e) }
void CarmView::wheelEvent(QWheelEvent *e)      { Q_UNUSED(e) }