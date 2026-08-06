#include "VesselView.h"

#include <QOpenGLShaderProgram>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVector>
#include <QQuaternion>
#include <QSurfaceFormat>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

namespace {

const char *kVertexShader = R"(#version 330 core
layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
// Supplied only by the vessel wall. Relying on these reading back as 0 for
// other batches was not enough: attribute defaults are context state rather than
// VAO state, and the body layers are drawn with raw glDrawElements outside
// drawBatch(), so the guarantee was too subtle to be worth trusting. The
// uPulsatile uniform below makes it explicit at every draw call instead.
layout(location = 2) in float aRootDist;    // arc length from the aortic root
layout(location = 3) in float aPulseAmp;    // displacement amplitude, model units

uniform mat4  uViewProj;
uniform float uPhase;      // cardiac phase 0..1
uniform float uCycleSec;   // seconds per beat at the current rate
uniform float uPwv;        // pulse wave velocity, model units/second
uniform float uStrain;     // peak fractional radius increase; 0 disables
uniform int   uPulsatile;  // 1 ONLY for the vessel wall; 0 for everything else
uniform float uPulseLUT[64];

out vec3 vNormal;
out vec3 vWorld;

// The pressure pulse is defined once, in cn::CardiacCycle, and handed to the
// shader as 64 samples. Duplicating the waveform in GLSL would guarantee the
// wall and the ECG drift apart the first time one of them is edited.
float pulseAt(float p) {
    float x = fract(p) * 64.0;
    int   i = int(x);
    return mix(uPulseLUT[i], uPulseLUT[(i + 1) % 64], fract(x));
}

void main() {
    vec3 pos = aPos;
    // Two independent conditions have to hold. uPulsatile is set by the draw
    // call, so a batch that never opts in cannot move whatever its vertex data
    // happens to contain — the Muscular and Skeletal layers among them.
    if (uPulsatile == 1 && uStrain > 0.0 && aPulseAmp > 0.0) {
        // The pressure wave travels: a vertex 1 m down the tree distends about
        // 170 ms after the aortic root at 6 m/s, which is a fifth of a cycle.
        float delayed = uPhase - (aRootDist / uPwv) / max(uCycleSec, 1e-4);
        pos += normalize(aNormal) * (aPulseAmp * uStrain * pulseAt(delayed));
    }
    vNormal = aNormal;
    vWorld  = pos;
    gl_Position = uViewProj * vec4(pos, 1.0);
}
)";

const char *kFragmentShader = R"(#version 330 core
in vec3 vNormal;
in vec3 vWorld;
uniform vec4  uColor;
uniform vec3  uCameraPos;
uniform vec3  uLightDir;
uniform vec3  uSheen;
uniform float uSpecular;
uniform float uShininess;
uniform float uAmbient;    // 0.12 default (vessel), 0.25 for 3D bone, 0.70 for fluoro bone
uniform float uDiffuse;    // 0.88 default (vessel), 0.75 for 3D bone, 0.30 for fluoro bone
uniform float uFalloff;
uniform int   uShaded;
uniform int   uHeadlight;
out vec4 fragColor;

void main() {
    if (uShaded == 0) { fragColor = uColor; return; }

    vec3 n = normalize(vNormal);
    vec3 toEye = uCameraPos - vWorld;
    float dist = length(toEye);
    vec3 v = toEye / max(dist, 1e-8);

    // Flip the normal toward the eye rather than taking abs(): tissue is
    // rendered translucent and we see back faces constantly, and a flipped
    // normal gives a correct specular lobe where abs() gives a doubled one.
    if (dot(n, v) < 0.0) n = -n;

    // Two-light model — key from upper-front, fill from lower-back (45%)
    // Ensures all faces of tube meshes receive some diffuse contribution.
    vec3  Lkey  = normalize(vec3(0.2,  0.4,  0.6));
    vec3  Lfill = normalize(vec3(-0.2, -0.4, -0.3));
    float diffKey  = max(dot(n, Lkey),  0.0);
    float diffFill = max(dot(n, Lfill), 0.0) * 0.45;
    float diff = (uHeadlight == 1) ? max(dot(n, v), 0.0)
                                   : (diffKey + diffFill);
    vec3  h    = normalize(Lkey + v);
    float spec = pow(max(dot(n, h), 0.0), uShininess) * uSpecular;
    float fres = pow(1.0 - clamp(dot(n, v), 0.0, 1.0), 3.0);

    float atten = 1.0;
    if (uHeadlight == 1) atten = 1.0 / (1.0 + pow(dist / max(uFalloff, 1e-6), 2.0));

    vec3 rgb = uColor.rgb * (uAmbient + uDiffuse * diff) * atten
             + uSheen * spec * atten
             + uSheen * fres * 0.16;
    float a = clamp(uColor.a + fres * uColor.a * 1.2, 0.0, 1.0);
    fragColor = vec4(rgb, a);
}
)";

QVector3D toQt(const cn::Vec3 &v) { return QVector3D(v.x, v.y, v.z); }

/// Chain indices that split the device into its three materials.
constexpr int kTipEnd = 3;    // nodes 0..3   soft tapered tip
constexpr int kBandEnd = 4;   // nodes 3..4   radiopaque marker band

} // namespace

void VesselView::Batch::destroy() {
    vao.destroy();
    vbo.destroy();
    ibo.destroy();
}

VesselView::VesselView(QWidget *parent) : QOpenGLWidget(parent) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSamples(4);
    fmt.setDepthBufferSize(24);
    setFormat(fmt);
    setMinimumSize(520, 420);
    setFocusPolicy(Qt::StrongFocus);

    // The device advances at a physical speed; the camera is eased toward it
    // separately so that neither the zoom nor the recentring snaps.
    m_camEase.setInterval(16);
    connect(&m_camEase, &QTimer::timeout, this, [this] {
        bool busy = false;
        const float da = m_camArcTarget - m_camArc;
        if (std::fabs(da) > 1e-7f) { m_camArc += da * 0.25f; busy = true; }
        else m_camArc = m_camArcTarget;

        const QVector3D dp = m_followTargetGoal - m_followTarget;
        if (dp.lengthSquared() > 1e-16f) { m_followTarget += dp * 0.22f; busy = true; }
        else m_followTarget = m_followTargetGoal;

        const float dd = m_followDistanceGoal - m_followDistance;
        if (std::fabs(dd) > 1e-9f) { m_followDistance += dd * 0.15f; busy = true; }
        else m_followDistance = m_followDistanceGoal;

        if (!busy) m_camEase.stop();
        update();
    });
}

VesselView::~VesselView() {
    makeCurrent();
    m_meshBatch.destroy();
    m_centerlineBatch.destroy();
    // Body layers, sheath parts and the balloon were all leaking GL objects
    // here — only the mesh, centerline and three device spans were released.
    m_muscularBatch.destroy();
    m_skeletalBatch.destroy();
    m_sheathCannulaBatch.destroy();
    m_sheathHubBatch.destroy();
    m_sheathValveBatch.destroy();
    m_sheathArmBatch.destroy();
    if (m_deviceBatches) {
        m_deviceBatches->tip.destroy();
        m_deviceBatches->band.destroy();
        m_deviceBatches->shaft.destroy();
        m_deviceBatches->balloon.destroy();
    }
    for (auto &hb : m_hostBatches)
        if (hb) { hb->tip.destroy(); hb->band.destroy(); hb->shaft.destroy(); hb->balloon.destroy(); }
    m_program.reset();
    doneCurrent();
}

// ------------------------------------------------------------------ inputs

void VesselView::setMesh(const cn::StlMesh *mesh) {
    m_mesh = mesh;
    m_meshDirty = true;
    if (mesh) m_modelScale = std::max(1e-6f, mesh->bounds().diagonal());
    resetCamera();
    update();
}

void VesselView::setGraph(const cn::CenterlineGraph *graph) {
    m_graph = graph;
    m_centerlineDirty = true;
    update();
}

void VesselView::setDevice(const cn::CatheterModel *device) {
    m_device = device;
    if (!m_deviceBatches) m_deviceBatches = std::make_unique<DeviceBatches>();
    m_deviceDirty = true;
    update();
}

void VesselView::setCameraTrack(const cn::CenterlinePath *path, float arc) {
    m_camPath = path;
    // Always track the arc: the endoluminal eye is *inside* the device and has
    // to follow it regardless. m_cameraFollows only decides whether the
    // reference camera re-targets, and gating this on it froze the endoluminal
    // view whenever following was switched off.
    m_camArcTarget = arc;
    startCameraEase();
    update();
}

void VesselView::setCameraFollows(bool v) {
    m_cameraFollows = v;
    if (v) startCameraEase();
    update();
}

void VesselView::startCameraEase() {
    if (!m_camEase.isActive()) m_camEase.start();
}

void VesselView::setMode(Mode m) {
    m_mode = m;
    // Entering follow mode from elsewhere: snap once rather than sweeping the
    // camera across the model.
    if (m == Mode::Reference && m_device) {
        updateFollowFrame();
        m_followTarget = m_followTargetGoal;
        m_followDistance = m_followDistanceGoal;
    }
    emit cameraChanged();
    update();
}

void VesselView::setPalette(Palette p) {
    m_palette = p;
    update();
}

void VesselView::refreshDevice() {
    m_deviceDirty = true;
    update();
}

void VesselView::setFrontalLock(bool v) {
    m_frontalLock = v;
    if (v) {
        m_azimuth = 180.f;
        m_elevation = 0.f;
        emit cameraChanged();
    }
    update();
}

void VesselView::showWholeVessel() {
    if (!m_mesh) return;
    const cn::Box3 b = m_mesh->bounds();
    if (!b.valid()) return;

    m_cameraFollows = false;
    // +Z shifts target up → vessel appears lower in viewport
    const float downShift = b.size().z * 0.22f;
    m_target = toQt(b.center()) + QVector3D(0.f, 0.f, downShift);
    m_azimuth = 180.f;
    m_elevation = 0.f;

    // Frame the bounding box in the frontal projection: X across the screen,
    // Z up it. Fitting the diagonal instead would leave the tree floating in
    // the middle of a mostly empty viewport, because this model is a thin
    // sheet - its Y extent is a twentieth of its height.
    //
    // One formula for both palettes, using the FOV each actually renders with
    // and the same margin. Arterial used to multiply the fit by 2.00 and
    // Angiographic by 1.00, so the anatomy was half the size in one pane and
    // edge-to-edge in the other — the same model at two different scales
    // depending on which button was pressed.
    const float halfV  = std::max(b.size().z, 1e-6f) * 0.5f;
    const float halfH  = std::max(b.size().x, 1e-6f) * 0.5f;
    const float aspect = height() > 0 ? float(width()) / float(height()) : 1.f;
    const float tanHalf = std::tan(qDegreesToRadians(fovDegrees() * 0.5f));

    m_distance = kAnatomyFrameMargin *
                 std::max(halfV / tanHalf, halfH / (tanHalf * std::max(aspect, 0.2f)));
    update();
}

void VesselView::setCamera(float az, float el, float dist, const QVector3D &tgt) {
    m_cameraFollows = false;
    m_azimuth   = az;
    m_elevation = el;
    m_distance  = dist;
    m_target    = tgt;
    update();
}

void VesselView::syncCameraFrom(const VesselView &other) {
    setCamera(other.azimuth(), other.elevation(), other.distance(), other.target());
}

void VesselView::showFluoroDefault() {
    // Frame the head/brain region for the fluoro view — matches the old project's
    // frameOnFace() approach: crop to the top 30% of the vessel Z-extent (brain end),
    // set target to that sub-region's centre, compute distance from its diagonal.
    //
    // The 8° FOV (set in paintGL for Angiographic palette) simulates parallel
    // projection. At 8° FOV, distance = subRegionHalf / tan(4°) gives correct framing.
    if (!m_mesh) { showWholeVessel(); return; }

    const cn::Box3 b = m_mesh->bounds();
    // Top 30% of Z-extent = brain/head region (vessel goes groin→brain in +Z)
    const float zTop    = b.hi.z;
    const float zCrop   = b.lo.z + (b.hi.z - b.lo.z) * 0.70f;  // bottom of the top-30% region
    const float croppedCentreZ = (zCrop + zTop) * 0.5f;
    const float croppedHalfZ   = (zTop - zCrop) * 0.5f;
    const float croppedHalfX   = b.size().x * 0.5f;

    // Target = centre of cropped head region, slightly lower for better framing
    m_cameraFollows = false;
    // Shift target down so more of the head region fills the upper viewport area
    m_target = QVector3D(toQt(b.center()).x(), toQt(b.center()).y(),
                         croppedCentreZ - croppedHalfZ * 0.5f);
    m_azimuth   = 180.f;   // AP (front)
    m_elevation = 0.f;

    // Same rule as showWholeVessel, applied to the cropped head region: the FOV
    // this palette actually renders with, and the same margin. The hard-coded
    // tan(10 deg) did not match the 30 deg FOV in use, and the extra 0.62 on top
    // was compensating for that — together they put the head at a different
    // scale from the anatomy in every other pane.
    const float aspect = height() > 0 ? float(width()) / float(height()) : 1.f;
    const float tanHalf = std::tan(qDegreesToRadians(fovDegrees() * 0.5f));
    m_distance = kAnatomyFrameMargin *
                 std::max(croppedHalfZ / tanHalf,
                          croppedHalfX / (tanHalf * std::max(aspect, 0.2f)));

    emit cameraChanged();
    update();
}

void VesselView::focusOn(const QVector3D &position, float lumenRadius) {
    // Opening on the whole tree after a puncture is chosen is useless: the
    // access vessel is a couple of millimetres across in a model a metre long.
    m_cameraFollows = true;   // from here on the camera rides the device
    m_target = position;
    m_followTarget = position;
    m_followTargetGoal = position;
    const float d = std::clamp(std::max(lumenRadius * 18.f, m_modelScale * 0.008f),
                               m_modelScale * 0.006f, m_modelScale * 0.45f);
    m_distance = d;
    m_followDistance = d;
    m_followDistanceGoal = d;
    update();
}

void VesselView::resetCamera() {
    m_followZoom = 1.f;
    m_endoFov    = 72.f;
    // The C-arm angles are deliberately NOT cleared here. They mirror the rig,
    // which is shared by every view; zeroing them locally would put this view
    // at a different orientation from the others until the next rotation.
    // Resetting the rig is a separate action (R -> CarmController::resetAll),
    // and that pushes 0/0 to every view at once.
    if (m_palette == Palette::Angiographic)
        showFluoroDefault();   // head region, AP position, 8° FOV
    else
        showWholeVessel();
    emit cameraChanged();
}

// ------------------------------------------------------------------ camera

void VesselView::updateFollowFrame() {
    if (!m_device) return;
    const auto &st = m_device->state();

    // Only the *target* follows the device. Orientation stays on the overview's
    // orbit angles, so the operator keeps the vantage they set up and the scene
    // never rotates itself out from under them.
    m_followTargetGoal = toQt(st.tipPosition);

    // Auto-zoom to the vessel, not to the model: framing on the local lumen is
    // what keeps a 2 mm distal branch and a 20 mm trunk both legible.
    const float lumen = std::max(st.lumenRadiusAtTip, m_device->params().deviceRadius);
    const float want = m_followZoom * std::max(lumen * 16.f,
                                               m_device->params().deviceRadius * 34.f);
    m_followDistanceGoal = std::clamp(want, m_modelScale * 0.006f, m_modelScale * 0.45f);
    if (m_followDistance <= 0.f) m_followDistance = m_followDistanceGoal;
    startCameraEase();
}

void VesselView::updateEndoluminalFrame() {
    if (!m_camPath || m_camPath->empty()) return;

    const float total = m_camPath->totalLength();
    const float standoff = m_modelScale * m_endoStandoffFrac;
    const float lookahead = m_modelScale * m_endoLookaheadFrac;

    const float sEye = std::clamp(m_camArc - standoff, 0.f, total);
    const float sLook = std::clamp(sEye + lookahead, 0.f, total);

    m_endoEye = toQt(m_camPath->pointAt(sEye));
    QVector3D fwd = toQt(m_camPath->pointAt(sLook)) - m_endoEye;
    if (fwd.lengthSquared() < 1e-14f) fwd = toQt(m_camPath->tangentAt(sEye));
    if (fwd.lengthSquared() < 1e-14f) fwd = QVector3D(0, 0, 1);
    fwd.normalize();

    QVector3D up = m_endoUp - fwd * QVector3D::dotProduct(m_endoUp, fwd);
    if (up.lengthSquared() < 1e-10f) {
        const QVector3D seed = std::fabs(fwd.z()) < 0.9f ? QVector3D(0, 0, 1) : QVector3D(1, 0, 0);
        up = seed - fwd * QVector3D::dotProduct(seed, fwd);
    }
    m_endoUp = up.normalized();
    m_endoForward = fwd;
}

void VesselView::syncCameraToDevice() {
    if (m_mode == Mode::Endoluminal) updateEndoluminalFrame();
    else if (m_mode == Mode::Reference) updateFollowFrame();
}

QVector3D VesselView::cameraPosition() const {
    if (m_mode == Mode::Endoluminal) return m_endoEye;

    // One orbit, two targets. Following is a property of the reference view,
    // not a separate camera: the operator keeps the vantage they set up either
    // way, and only what the camera is pointed at changes.
    // C-arm angles are added here rather than baked into m_azimuth/m_elevation,
    // so the rig orientation is identical in every view and the operator's own
    // orbit is left untouched underneath it.
    // LAO > 0 swings the detector left, which reads as the camera rotating
    // right, hence + on azimuth.
    const float az = qDegreesToRadians(m_azimuth + m_carmLAO);
    const float el = qDegreesToRadians(std::clamp(m_elevation + m_carmCRAN, -89.f, 89.f));
    const bool following = m_cameraFollows && m_device;
    const QVector3D at = following ? m_followTarget : m_target;
    const float d = following ? m_followDistance : m_distance;
    // Z-up spherical: azimuth sweeps the XY plane, elevation lifts toward +Z.
    return at + QVector3D(d * std::cos(el) * std::sin(az),
                          d * std::cos(el) * std::cos(az),
                          d * std::sin(el));
}

QVector3D VesselView::cameraForward() const {
    if (m_mode == Mode::Endoluminal) return m_endoForward;
    const QVector3D at = (m_cameraFollows && m_device) ? m_followTarget : m_target;
    const QVector3D d = at - cameraPosition();
    return d.lengthSquared() > 1e-16f ? d.normalized() : QVector3D(0, 0, 1);
}

QVector3D VesselView::cameraUp() const {
    if (m_mode == Mode::Endoluminal) return m_endoUp;
    return QVector3D(0, 0, 1);   // the vessel stands up
}

QVector3D VesselView::screenRight() const {
    return QVector3D::crossProduct(cameraForward(), cameraUp()).normalized();
}

QVector3D VesselView::screenUp() const {
    return QVector3D::crossProduct(screenRight(), cameraForward()).normalized();
}

float VesselView::fovDegrees() const {
    // Angiographic uses a moderate FOV — narrow enough to reduce perspective
    // distortion the way a real X-ray does, but not so narrow the scene looks
    // flat from side angles. 40 suits the anatomical 3D view.
    return (m_palette == Palette::Angiographic) ? 30.f : 40.f;
}

QMatrix4x4 VesselView::viewProjection() const {
    const float aspect = height() > 0 ? float(width()) / float(height()) : 1.f;
    QMatrix4x4 proj, view;
    if (m_mode == Mode::Endoluminal) {
        proj.perspective(m_endoFov, aspect, m_modelScale * 2e-4f, m_modelScale * 1.2f);
        view.lookAt(m_endoEye, m_endoEye + m_endoForward, m_endoUp);
    } else {
        const bool following = m_cameraFollows && m_device;
        const QVector3D at = following ? m_followTarget : m_target;
        const float d = std::max(following ? m_followDistance : m_distance, m_modelScale * 1e-4f);
        // Angiographic uses a moderate FOV — narrow enough to reduce perspective
        // distortion (like a real X-ray), but not so narrow that the scene looks
        // completely flat from side angles.
        proj.perspective(fovDegrees(), aspect, d * 0.001f, m_modelScale * 8.f);
        view.lookAt(cameraPosition(), at, QVector3D(0, 0, 1));
    }
    return proj * view;
}

// ---------------------------------------------------------------- materials

VesselView::Material VesselView::wallMaterial() const {
    Material m;
    const bool endo = m_mode == Mode::Endoluminal;
    switch (m_palette) {
    case Palette::Arterial:
        // Old project exact colour: (1.0, 0.20, 0.3686, 0.30)
        // High ambient so all faces read as arterial red regardless of normal orientation
        // #C21807 = (0.7608, 0.0941, 0.0275)
        m.colour  = endo ? QVector4D(0.92f, 0.24f, 0.33f, 1.f)
                        : QVector4D(0.92f, 0.24f, 0.33f, m_wallOpacity);
        m.sheen    = QVector3D(1.0f, 0.85f, 0.85f);
        m.specular = endo ? 0.45f : 0.20f;
        m.shininess= 22.f;
        m.ambient  = 0.22f;   // lower ambient — less bright
        break;
    case Palette::Angiographic:
        // Fluoro baseline: vessel is hidden (opacity 0.0) without contrast/roadmap.
        // This matches the old project: real fluoro doesn't show lumen without dye.
        // Road Map mode overrides the colour/opacity in the draw section.
        m.colour = endo ? QVector4D(0.88f, 0.88f, 0.90f, 1.f)
                        : QVector4D(0.85f, 0.86f, 0.90f, 0.0f);  // 0% — hidden
        m.sheen = QVector3D(0.f, 0.f, 0.f);
        m.specular = 0.0f;
        m.shininess = 1.f;
        break;
    case Palette::Monochrome:
        m.colour = endo ? QVector4D(0.55f, 0.55f, 0.55f, 1.f)
                        : QVector4D(0.62f, 0.62f, 0.62f, m_wallOpacity);
        m.sheen = QVector3D(0.9f, 0.9f, 0.9f);
        m.specular = 0.15f;
        m.shininess = 18.f;
        break;
    }
    return m;
}

// --------------------------------------------------------------------- GL

void VesselView::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Never cull: endoluminal rendering shows the inside of an outward-facing
    // surface, exactly the faces culling would remove.
    glDisable(GL_CULL_FACE);

    m_program = std::make_unique<QOpenGLShaderProgram>();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    m_program->bindAttributeLocation("aPos", 0);
    m_program->bindAttributeLocation("aNormal", 1);
    m_program->link();
}

void VesselView::resizeGL(int w, int h) { glViewport(0, 0, w, h); }

void VesselView::ensureBatch(Batch &b) {
    if (b.ready) return;
    b.vao.create();
    b.vbo.create();
    b.vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    b.ready = true;
}

void VesselView::uploadBatch(Batch &b, const std::vector<float> &interleaved,
                             const std::vector<uint32_t> &indices) {
    ensureBatch(b);
    QOpenGLVertexArrayObject::Binder binder(&b.vao);

    b.vbo.bind();
    b.vbo.allocate(interleaved.data(), static_cast<int>(interleaved.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void *>(3 * sizeof(float)));

    b.indexed = !indices.empty();
    if (b.indexed) {
        if (!b.ibo.isCreated()) {
            b.ibo.create();
            b.ibo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        }
        b.ibo.bind();
        b.ibo.allocate(indices.data(), static_cast<int>(indices.size() * sizeof(uint32_t)));
        b.elementCount = static_cast<int>(indices.size());
    } else {
        b.elementCount = static_cast<int>(interleaved.size() / 6);
    }
}

void VesselView::drawBatch(Batch &b, GLenum mode, const Material &m, bool shaded,
                           bool pulsatile) {
    if (!b.ready || b.elementCount == 0) return;
    // Default off: a caller has to ask for pulsation explicitly.
    m_program->setUniformValue("uPulsatile", pulsatile ? 1 : 0);
    m_program->setUniformValue("uColor", m.colour);
    m_program->setUniformValue("uSheen", m.sheen);
    m_program->setUniformValue("uSpecular", m.specular);
    m_program->setUniformValue("uShininess", m.shininess);
    m_program->setUniformValue("uShaded", shaded ? 1 : 0);
    // Vessel uses default lighting; body meshes set these themselves before drawing
    m_program->setUniformValue("uAmbient", m.ambient);
    m_program->setUniformValue("uDiffuse", 1.0f - m.ambient);
    QOpenGLVertexArrayObject::Binder binder(&b.vao);
    if (b.indexed) glDrawElements(mode, b.elementCount, GL_UNSIGNED_INT, nullptr);
    else           glDrawArrays(mode, 0, b.elementCount);
}

// Uploads an 8-float-per-vertex wall buffer: position, normal, arc length from
// the aortic root, and local lumen radius. The last two drive the pulsation and
// are computed once here rather than per frame.
void VesselView::uploadWallBatch(Batch &b, const std::vector<float> &interleaved,
                                 const std::vector<uint32_t> &indices) {
    ensureBatch(b);
    QOpenGLVertexArrayObject::Binder binder(&b.vao);
    b.vbo.bind();
    b.vbo.allocate(interleaved.data(), static_cast<int>(interleaved.size() * sizeof(float)));
    const int stride = 8 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void *>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void *>(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void *>(7 * sizeof(float)));
    if (!b.ibo.isCreated()) {
        b.ibo.create();
        b.ibo.setUsagePattern(QOpenGLBuffer::StaticDraw);
    }
    b.ibo.bind();
    b.ibo.allocate(indices.data(), static_cast<int>(indices.size() * sizeof(uint32_t)));
    b.indexed = true;
    b.elementCount = static_cast<int>(indices.size());
}

void VesselView::rebuildMeshBatch() {
    m_meshDirty = false;
    if (!m_mesh) { m_meshBatch.elementCount = 0; return; }

    const std::vector<float> base = m_mesh->buildInterleavedRenderData();
    const std::vector<uint32_t> idx = m_mesh->buildIndexData();
    const size_t nVerts = base.size() / 6;

    std::vector<float> wall(nVerts * 8, 0.f);
    for (size_t i = 0; i < nVerts; ++i)
        for (int k = 0; k < 6; ++k) wall[i * 8 + k] = base[i * 6 + k];

    // Without a centerline there is nothing to measure distance or calibre
    // against, so the wall simply does not pulse — attributes stay 0.
    if (m_graph && m_graph->nodeCount() > 1) {
        const auto &nodes = m_graph->nodes();

        // Aortic root proxy: the widest node in the tree. On this model that is
        // the 15 mm aorta, which is where the pressure wave actually starts.
        int32_t root = 0;
        for (size_t n = 1; n < nodes.size(); ++n)
            if (nodes[n].radius > nodes[root].radius) root = static_cast<int32_t>(n);

        // Arc length from the root to every node, over the graph.
        const std::vector<float> arc = m_graph->distancesFrom(root);

        // Uniform grid over the centerline nodes, so assigning 260k mesh
        // vertices to their nearest node is O(n) rather than O(n*m).
        float cell = 0.f;
        for (const auto &l : m_graph->links()) cell += l.length;
        cell = std::max(cell / std::max<size_t>(m_graph->links().size(), 1) * 6.f, 1e-4f);

        std::unordered_map<int64_t, std::vector<int32_t>> grid;
        auto key = [&](float x, float y, float z) {
            const int64_t cx = int64_t(std::floor(x / cell));
            const int64_t cy = int64_t(std::floor(y / cell));
            const int64_t cz = int64_t(std::floor(z / cell));
            return (cx * 73856093LL) ^ (cy * 19349663LL) ^ (cz * 83492791LL);
        };
        for (size_t n = 0; n < nodes.size(); ++n)
            grid[key(nodes[n].position.x, nodes[n].position.y, nodes[n].position.z)]
                .push_back(static_cast<int32_t>(n));

        float maxArc = 0.f;
        for (float a : arc)
            if (a < std::numeric_limits<float>::max()) maxArc = std::max(maxArc, a);

        // ── Heart region ────────────────────────────────────────────────────
        // The pulse is confined to the heart and great vessels rather than
        // applied to the whole tree. Auto-location takes the widest node in the
        // graph: on this model that is the 15 mm aortic root, which is exactly
        // where the pump is. Set autoLocate false to pin it by hand.
        cn::Vec3 heart = nodes[root].position;
        if (!m_heart.autoLocate)
            heart = cn::Vec3(m_heart.centre.x(), m_heart.centre.y(), m_heart.centre.z());
        m_heartCentre = QVector3D(heart.x, heart.y, heart.z);

        const float core  = std::max(m_heart.coreRadius, 1e-6f);
        const float fade  = std::max(m_heart.falloff, 1e-6f);
        const float amp   = m_heart.heartAmplify;
        const float perif = std::clamp(m_heart.peripheralFraction, 0.f, 1.f);

        // Full amplitude out to coreRadius, smoothstepping down to the
        // peripheral level by coreRadius + falloff. A hard cut-off would show
        // as a visible seam ring across the aorta.
        auto spatialWeight = [&](const cn::Vec3 &v) {
            const float d = (v - heart).length();
            if (d <= core) return amp;
            const float t = std::clamp((d - core) / fade, 0.f, 1.f);
            const float s = t * t * (3.f - 2.f * t);        // smoothstep
            return amp + (perif * amp - amp) * s;
        };

        // Calibre gate — sub-millimetre noise only. It is NOT what excludes the
        // carotids: they and the coronaries are both small, so no radius
        // threshold separates them, and setting one high enough to drop the
        // carotids also drops the coronaries that make up most of the moving
        // surface. The superior cut-off below does that job instead.
        const float cFull = std::max(m_heart.calibreFull, 1e-6f);
        const float cZero = std::clamp(m_heart.calibreZero, 0.f, cFull - 1e-6f);
        auto calibreWeight = [&](float lumenR) {
            const float t = std::clamp((lumenR - cZero) / (cFull - cZero), 0.f, 1.f);
            return t * t * (3.f - 2.f * t);                 // smoothstep
        };

        // Superior cut-off: the heart region ends just above the arch. This is
        // what keeps the carotids static — they ascend past the arch while the
        // coronaries sit at or below the root, so position separates them where
        // calibre cannot. No inferior limit: the descending aorta belongs to the
        // great vessels as much as the ascending one does.
        const float supLim  = m_heart.superiorLimit;
        const float supFade = std::max(m_heart.superiorFade, 1e-6f);
        auto superiorWeight = [&](const cn::Vec3 &v) {
            const float dz = v.z - heart.z;
            if (dz <= supLim) return 1.f;                   // at or below the arch
            const float t = std::clamp((dz - supLim) / supFade, 0.f, 1.f);
            return 1.f - t * t * (3.f - 2.f * t);
        };

        for (size_t i = 0; i < nVerts; ++i) {
            const cn::Vec3 p(wall[i * 8 + 0], wall[i * 8 + 1], wall[i * 8 + 2]);
            int32_t best = -1;
            float bestD = std::numeric_limits<float>::max();
            const int64_t cx = int64_t(std::floor(p.x / cell));
            const int64_t cy = int64_t(std::floor(p.y / cell));
            const int64_t cz = int64_t(std::floor(p.z / cell));
            for (int dx = -1; dx <= 1 && best < 0; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dz = -1; dz <= 1; ++dz) {
                        auto it = grid.find(((cx + dx) * 73856093LL) ^
                                            ((cy + dy) * 19349663LL) ^
                                            ((cz + dz) * 83492791LL));
                        if (it == grid.end()) continue;
                        for (int32_t n : it->second) {
                            const float d = (nodes[n].position - p).lengthSquared();
                            if (d < bestD) { bestD = d; best = n; }
                        }
                    }
            if (best < 0) continue;   // vertex far from any centerline: no pulse
            const float a = arc[best];
            wall[i * 8 + 6] = (a < std::numeric_limits<float>::max()) ? a : maxArc;

            // Distance to the centerline IS the local radius on a tube, and it
            // is a better figure than the node's stored radius because it varies
            // smoothly across the surface instead of stepping at each node.
            const float localRadius = std::sqrt(bestD);

            // Localise the motion to the heart: near the pump AND large enough
            // to be a great vessel. The amplitude is folded into this one
            // attribute rather than carried as separate weights, because the
            // shader only ever uses their product.
            //
            // The calibre gate reads the graph's own MaximumInscribedSphereRadius
            // at the nearest node rather than the vertex's distance to the
            // centerline: the stored radius is what VMTK measured, and it does
            // not wobble across a surface the way the point distance does.
            const float lumenR = nodes[best].radius;
            wall[i * 8 + 7] = localRadius
                              * spatialWeight(p)          // near the pump
                              * calibreWeight(lumenR)     // and a great vessel
                              * superiorWeight(p);        // and not above the arch
        }
    }

    uploadWallBatch(m_meshBatch, wall, idx);
}

void VesselView::setHeartRegion(const HeartRegion &r) {
    m_heart = r;
    // The weighting is baked into the vertex buffer, so the wall has to be
    // rebuilt — this is not a uniform that can be changed per frame.
    m_meshDirty = true;
    update();
}

void VesselView::setCardiacCycle(const cn::CardiacCycle *c) {
    m_cycle = c;
    if (c) {
        // Sample the wall-motion waveform once into the LUT the shader indexes.
        // Kept normalised 0..1 so uStrain alone sets the amplitude.
        //
        // This reads CardiacCycle::wallWaveform() rather than re-deriving a
        // shape from arterialPressure() here. The wall follows a chamber fill
        // and release — a smooth expand, a brief hold, a smooth contract — while
        // the ABP trace still follows the true pressure pulse with its dicrotic
        // notch. Two shapes, one clock, one definition of each.
        for (int i = 0; i < kPulseLutSize; ++i) {
            const float p = static_cast<float>(i) / static_cast<float>(kPulseLutSize);
            m_pulseLUT[i] = std::clamp(c->wallWaveform(p), 0.f, 1.f);
        }
    }
    update();
}

void VesselView::rebuildCenterlineBatch() {
    m_centerlineDirty = false;
    if (!m_graph) { m_centerlineBatch.elementCount = 0; return; }
    std::vector<float> v;
    v.reserve(m_graph->links().size() * 12);
    for (const auto &l : m_graph->links())
        for (int32_t id : {l.from, l.to}) {
            const cn::Vec3 &p = m_graph->nodes()[id].position;
            v.insert(v.end(), {p.x, p.y, p.z, 0.f, 1.f, 0.f});
        }
    uploadBatch(m_centerlineBatch, v, {});
}


void VesselView::rebuildBodyMeshBatch(const QString &path, Batch &batch,
                                      const BodyAlign &align) {
    if (path.isEmpty()) { batch.elementCount = 0; return; }
    cn::StlMesh bodyMesh;
    if (!bodyMesh.load(path.toStdString())) { batch.elementCount = 0; return; }
    if (!m_mesh) { batch.elementCount = 0; return; }

    // ── Body-to-vessel placement ─────────────────────────────────────────────
    //
    // Muscular.stl, Skeletal.stl and the vessel STL are authored in one frame at
    // one scale. Measured bounds (mm):
    //
    //     vessel    X -287.6..287.6   Y  -88.4.. 95.0   Z  38.6..1700.6
    //     skeletal  X -334.6..334.6   Y -137.3..112.8   Z   9.2..1705.5
    //     muscular  X -335.7..334.9   Y -137.2..132.7   Z   6.5..1707.8
    //
    // X centres agree to 0.4 mm and Z centres to 12 mm. Vessel nests inside
    // skeletal nests inside muscular on every axis, and the body correctly
    // extends 30 mm below the vessel (the arterial tree stops at the ankle) and
    // a few mm above it (scalp above the cerebral vessels). So the correct
    // transform is the identity.
    //
    // The previous code fitted each body mesh's bounding box onto the vessel's,
    // per axis. Because the vessel's Y extent is a fact about where arteries run
    // (183 mm) and not about body depth (250-270 mm), that fit compressed the
    // skeleton to 73% of its authored depth and 86% of its width, pulled the top
    // of the skull DOWN 8.5 mm and lifted the feet UP 33 mm. Two call sites also
    // passed different margins and opposite back-shifts, so the 3D and Fluoro
    // views could place the muscle layer 30 mm apart.
    //
    // `align` is a fine adjustment on top of identity and is a no-op by default.
    // `m_bodyAutoFit` restores the old per-axis stretch for a body mesh that
    // genuinely comes from a different source.

    const cn::Box3 vb = m_mesh->bounds();
    const cn::Vec3 vesselC = vb.center();

    float sx = align.scale, sy = align.scale, sz = align.scale;
    cn::Vec3 pivot = vesselC;

    if (m_bodyAutoFit) {
        const cn::Box3 bb = bodyMesh.bounds();
        pivot = bb.center();
        sx = (std::max(vb.size().x, 1e-6f) / std::max(bb.size().x, 1e-6f)) * align.scale;
        sy = (std::max(vb.size().y, 1e-6f) / std::max(bb.size().y, 1e-6f)) * align.scale;
        sz = (std::max(vb.size().z, 1e-6f) / std::max(bb.size().z, 1e-6f)) * align.scale;
    }

    // With identity (scale 1, offset 0, autofit off) this loop reproduces the
    // source vertices exactly: (v - pivot) * 1 + pivot + 0.
    const auto &verts = bodyMesh.buildInterleavedRenderData();
    std::vector<float> transformed;
    transformed.reserve(verts.size());
    for (size_t i = 0; i + 5 < verts.size(); i += 6) {
        const float wx = (verts[i]     - pivot.x) * sx + vesselC.x + align.offset.x();
        const float wy = (verts[i + 1] - pivot.y) * sy + vesselC.y + align.offset.y();
        const float wz = (verts[i + 2] - pivot.z) * sz + vesselC.z + align.offset.z();
        transformed.insert(transformed.end(),
                           {wx, wy, wz, verts[i + 3], verts[i + 4], verts[i + 5]});
    }
    uploadBatch(batch, transformed, bodyMesh.buildIndexData());
}

void VesselView::setBodyAlignment(const BodyAlign &muscular, const BodyAlign &skeletal) {
    m_muscularAlign = muscular;
    m_skeletalAlign = skeletal;
    m_bodyMeshDirty = true;
    update();
}

void VesselView::nudgeBodyLayers(float dz, float dScale) {
    m_muscularAlign.offset.setZ(m_muscularAlign.offset.z() + dz);
    m_skeletalAlign.offset.setZ(m_skeletalAlign.offset.z() + dz);
    m_muscularAlign.scale += dScale;
    m_skeletalAlign.scale += dScale;
    m_bodyMeshDirty = true;
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
// Sheath geometry
// ─────────────────────────────────────────────────────────────────────────────

void VesselView::appendTube(const std::vector<cn::Vec3> &path,
                            const std::vector<float> &radii, int sides,
                            std::vector<float> &vOut, std::vector<uint32_t> &iOut,
                            bool capStart, bool capEnd) {
    if (path.size() < 2 || radii.size() != path.size() || sides < 3) return;
    // A zero-length first segment makes every frame vector NaN, which then
    // propagates into the vertex buffer and takes the whole draw with it.
    if ((path[1] - path[0]).lengthSquared() < 1e-18f) return;

    const uint32_t base = static_cast<uint32_t>(vOut.size() / 6);

    // Parallel transport rather than a fresh Frenet frame per ring: the cannula
    // follows the vessel centreline, and recomputing the frame at every sample
    // makes the tube visibly twist wherever the curvature flips.
    cn::Vec3 tan = (path[1] - path[0]).normalized();
    cn::Vec3 ref = cn::anyPerpendicular(tan);

    for (size_t k = 0; k < path.size(); ++k) {
        cn::Vec3 t;
        if (k == 0)                    t = (path[1] - path[0]).normalized();
        else if (k + 1 == path.size()) t = (path[k] - path[k - 1]).normalized();
        else                           t = (path[k + 1] - path[k - 1]).normalized();
        if (t.lengthSquared() < 0.5f) t = tan;

        // Rotate the reference vector by the same rotation that takes the old
        // tangent to the new one.
        const cn::Vec3 axis = tan.cross(t);
        const float    s2   = axis.lengthSquared();
        if (s2 > 1e-12f) {
            const float ang = std::atan2(std::sqrt(s2),
                                         std::max(-1.f, std::min(1.f, tan.dot(t))));
            ref = cn::rotateAbout(ref, axis.normalized(), ang);
        }
        tan = t;
        ref = (ref - t * ref.dot(t)).normalized();
        const cn::Vec3 bit = t.cross(ref);

        for (int s = 0; s < sides; ++s) {
            const float a = 2.f * float(M_PI) * float(s) / float(sides);
            const cn::Vec3 n = ref * std::cos(a) + bit * std::sin(a);
            const cn::Vec3 p = path[k] + n * radii[k];
            vOut.insert(vOut.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
        }
    }

    for (size_t k = 0; k + 1 < path.size(); ++k) {
        for (int s = 0; s < sides; ++s) {
            const uint32_t a = base + uint32_t(k * sides + s);
            const uint32_t b = base + uint32_t(k * sides + (s + 1) % sides);
            const uint32_t cc = a + uint32_t(sides);
            const uint32_t d = b + uint32_t(sides);
            iOut.insert(iOut.end(), {a, b, d, a, d, cc});
        }
    }

    auto cap = [&](size_t ring, const cn::Vec3 &nrm) {
        const uint32_t centre = static_cast<uint32_t>(vOut.size() / 6);
        vOut.insert(vOut.end(), {path[ring].x, path[ring].y, path[ring].z,
                                 nrm.x, nrm.y, nrm.z});
        for (int s = 0; s < sides; ++s) {
            const uint32_t a = base + uint32_t(ring * sides + s);
            const uint32_t b = base + uint32_t(ring * sides + (s + 1) % sides);
            iOut.insert(iOut.end(), {centre, a, b});
        }
    };
    if (capStart) cap(0, (path[0] - path[1]).normalized());
    if (capEnd)   cap(path.size() - 1, (path.back() - path[path.size() - 2]).normalized());
}

void VesselView::setSheath(const cn::SheathGeometry &geom) {
    if (!geom.valid || geom.cannulaPath.size() < 2) { clearSheath(); return; }
    // Store only. Building the batches touches glEnableVertexAttribArray and
    // friends, whose function pointers are resolved in initializeGL; calling
    // them from a dialog handler with no current context crashes.
    m_sheathGeom    = geom;
    m_sheathVisible = true;
    m_sheathDirty   = true;
    update();
}

void VesselView::rebuildSheathBatches() {
    m_sheathDirty = false;
    const cn::SheathGeometry &geom = m_sheathGeom;
    if (!geom.valid || geom.cannulaPath.size() < 2) { m_sheathVisible = false; return; }

    m_sheathHubColour = QVector3D(geom.hubColour[0], geom.hubColour[1], geom.hubColour[2]);
    m_sheathOversized = geom.oversized;

    const int sides = 20;
    std::vector<float> v;
    std::vector<uint32_t> i;

    // ── Cannula ──────────────────────────────────────────────────────────────
    // Tapered over the distal few millimetres, exactly as a real introducer is:
    // the taper is what lets it follow the dilator through the arteriotomy.
    {
        std::vector<float> radii(geom.cannulaPath.size(), geom.cannulaOuterRadius);
        float arc = 0.f;
        for (size_t k = 0; k < geom.cannulaPath.size(); ++k) {
            if (k > 0) arc += cn::distance(geom.cannulaPath[k], geom.cannulaPath[k - 1]);
            if (geom.tipTaperLength > 1e-9f && arc < geom.tipTaperLength) {
                const float t = arc / geom.tipTaperLength;
                radii[k] = geom.cannulaOuterRadius * (0.45f + 0.55f * t);
            }
        }
        v.clear(); i.clear();
        appendTube(geom.cannulaPath, radii, sides, v, i, true, false);
        if (!m_sheathCannulaBatch.ready) ensureBatch(m_sheathCannulaBatch);
        uploadBatch(m_sheathCannulaBatch, v, i);
    }

    // ── Hub ──────────────────────────────────────────────────────────────────
    // Flared: narrow at the skin end where the strain relief meets the cannula,
    // full width at the back where the valve screws on.
    {
        const cn::Vec3 a = geom.hubBase, b = geom.hubTop;
        std::vector<cn::Vec3> p;
        std::vector<float>    r;
        const int rings = 8;
        for (int k = 0; k <= rings; ++k) {
            const float t = float(k) / float(rings);
            p.push_back(cn::lerp(a, b, t));
            // smoothstep flare from the strain relief out to the full body
            const float f = t * t * (3.f - 2.f * t);
            r.push_back(geom.cannulaOuterRadius * 1.6f +
                        (geom.hubRadius - geom.cannulaOuterRadius * 1.6f) * f);
        }
        v.clear(); i.clear();
        appendTube(p, r, sides, v, i, true, true);
        if (!m_sheathHubBatch.ready) ensureBatch(m_sheathHubBatch);
        uploadBatch(m_sheathHubBatch, v, i);
    }

    // ── Haemostasis valve cap ────────────────────────────────────────────────
    {
        std::vector<cn::Vec3> p{geom.hubTop, cn::lerp(geom.hubTop, geom.valveTop, 0.5f),
                                geom.valveTop};
        std::vector<float>    r{geom.valveRadius, geom.valveRadius,
                             geom.valveRadius * 0.82f};
        v.clear(); i.clear();
        appendTube(p, r, sides, v, i, true, true);
        if (!m_sheathValveBatch.ready) ensureBatch(m_sheathValveBatch);
        uploadBatch(m_sheathValveBatch, v, i);
    }

    // ── Side arm + three-way stopcock ────────────────────────────────────────
    {
        v.clear(); i.clear();
        std::vector<cn::Vec3> p;
        std::vector<float>    r;
        const int rings = 10;
        for (int k = 0; k <= rings; ++k) {
            const float t = float(k) / float(rings);
            p.push_back(cn::lerp(geom.sideArmOrigin, geom.sideArmEnd, t));
            r.push_back(geom.sideArmRadius);
        }
        appendTube(p, r, 12, v, i, false, false);

        // Stopcock body: a short barrel with a transverse handle port.
        const cn::Vec3 s0 = geom.stopcockCentre - geom.sideArmDir * (geom.stopcockLength * 0.5f);
        const cn::Vec3 s1 = geom.stopcockCentre + geom.sideArmDir * (geom.stopcockLength * 0.5f);
        appendTube({s0, s1}, {geom.stopcockRadius, geom.stopcockRadius},
                   12, v, i, true, true);
        const cn::Vec3 hnd = cn::anyPerpendicular(geom.sideArmDir);
        appendTube({geom.stopcockCentre, geom.stopcockCentre + hnd * (geom.stopcockRadius * 1.9f)},
                   {geom.stopcockRadius * 0.42f, geom.stopcockRadius * 0.52f},
                   10, v, i, true, true);

        if (!m_sheathArmBatch.ready) ensureBatch(m_sheathArmBatch);
        uploadBatch(m_sheathArmBatch, v, i);
    }
}

// Builds the three material spans (and the balloon) for one device into one
// DeviceBatches. Factored out so the parked host devices of a coaxial stack —
// the guide a microcatheter was introduced through — render the same way as the
// active device without duplicating the meshing.
void VesselView::buildDeviceInto(const cn::CatheterModel *dev, DeviceBatches &b) {
    if (!dev) {
        b.tip.elementCount = b.band.elementCount = 0;
        b.shaft.elementCount = b.balloon.elementCount = 0;
        return;
    }
    std::vector<float> v;
    std::vector<uint32_t> i;
    dev->buildTubeMesh(16, v, i, 0, kTipEnd);
    uploadBatch(b.tip, v, i);
    dev->buildTubeMesh(16, v, i, kTipEnd, kBandEnd);
    uploadBatch(b.band, v, i);
    dev->buildTubeMesh(16, v, i, kBandEnd, -1);
    uploadBatch(b.shaft, v, i);
}

void VesselView::rebuildDeviceBatches() {
    m_deviceDirty = false;
    if (!m_deviceBatches) return;
    // setDevice(nullptr) means "no device in the patient" — e.g. a sheath is in
    // but no catheter has been selected yet. Zero the spans so the previous
    // device stops being drawn instead of lingering as a stale mesh.
    buildDeviceInto(m_device, *m_deviceBatches);
    if (!m_device) return;
    rebuildBalloonBatch();

    // Parked host devices of the coaxial stack, from outermost inwards.
    m_hostBatches.resize(m_hostDevices.size());
    for (size_t k = 0; k < m_hostDevices.size(); ++k) {
        if (!m_hostBatches[k]) m_hostBatches[k] = std::make_unique<DeviceBatches>();
        buildDeviceInto(m_hostDevices[k], *m_hostBatches[k]);
    }
}

void VesselView::setHostDevices(const std::vector<const cn::CatheterModel *> &hosts) {
    m_hostDevices = hosts;
    m_deviceDirty = true;
    update();
}

void VesselView::rebuildBalloonBatch() {
    if (!m_device || !m_deviceBatches) return;
    std::vector<float> v;
    std::vector<uint32_t> i;
    m_device->buildBalloonMesh(18, 24, v, i);
    uploadBatch(m_deviceBatches->balloon, v, i);
}

void VesselView::paintGL() {
    if (m_meshDirty) rebuildMeshBatch();
    if (m_centerlineDirty) rebuildCenterlineBatch();
    if (m_deviceDirty) rebuildDeviceBatches();
    if (m_sheathDirty) rebuildSheathBatches();
    if (m_bodyMeshDirty) {
        // Same alignment as setBodyMeshes(). Previously these two sites used
        // different margins and opposite back-shifts, so which one ran last
        // decided where the muscle layer ended up.
        rebuildBodyMeshBatch(m_skeletalPath, m_skeletalBatch, m_skeletalAlign);
        rebuildBodyMeshBatch(m_muscularPath, m_muscularBatch, m_muscularAlign);
        m_bodyMeshDirty = false;
    }
    if (m_mode == Mode::Endoluminal) updateEndoluminalFrame();
    else if (m_mode == Mode::Reference) updateFollowFrame();

    const bool endo   = m_mode == Mode::Endoluminal;
    const bool fluoro = (m_palette == Palette::Angiographic) && !endo;
    if (endo)
        glClearColor(0.02f, 0.014f, 0.014f, 1.f);
    else if (fluoro || m_dsaActive || m_roadMapActive)
        glClearColor(0.03f, 0.03f, 0.035f, 1.f);   // dark — fluoro/DSA/roadmap
    else
        glClearColor(0.0f, 0.0f, 0.0f, 1.f);   // black for 3D/mesh
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_program) return;

    m_program->bind();
    m_program->setUniformValue("uViewProj", viewProjection());

    // Pulsation uniforms. Only the wall batch carries attributes 2 and 3, so
    // every other batch — including the Muscular and Skeletal layers — sees
    // aPulseAmp == 0 and is left undisplaced.
    if (m_cycle && m_pulsation) {
        m_program->setUniformValue("uPhase",    m_cycle->phase());
        m_program->setUniformValue("uCycleSec", m_cycle->cycleSeconds());
        m_program->setUniformValue("uPwv",      m_cycle->params().pulseWaveSpeed);
        m_program->setUniformValue("uStrain",   m_cycle->params().distensionStrain);
        m_program->setUniformValueArray("uPulseLUT", m_pulseLUT, kPulseLutSize, 1);
    } else {
        m_program->setUniformValue("uStrain", 0.f);
    }
    m_program->setUniformValue("uPulsatile", 0);   // nothing pulses unless it asks
    m_program->setUniformValue("uCameraPos", cameraPosition());
    m_program->setUniformValue("uLightDir", QVector3D(-0.35f, -0.72f, -0.6f).normalized());
    m_program->setUniformValue("uHeadlight", endo ? 1 : 0);
    m_program->setUniformValue("uFalloff",   endo ? m_modelScale * 0.4f : 0.0f);

    Material line;
    line.colour = QVector4D(0.35f, 0.62f, 0.75f, 1.f);

    if (endo) {
        // The eye is inside the catheter, so its own tube would wrap the camera
        // and fill the frame. A real intraluminal camera shows nothing of its
        // own hardware either.
        drawBatch(m_meshBatch, GL_TRIANGLES, wallMaterial(), true, /*pulsatile=*/true);
    } else {
        if (m_showCenterline) {
            glLineWidth(1.f);
            drawBatch(m_centerlineBatch, GL_LINES, line, false);
        }
        // ── Body mesh layers — exact old-project material properties ──────────
        // Old project used VTK depth-peeling for translucent accumulation.
        // We approximate with standard alpha blending + correct draw order:
        //   1. Muscular (outermost, opacity=0.04, flat ambient, no lighting)
        //   2. Skeletal (opacity=0.45 in 3D / 0.22 in fluoro, Phong shading)
        //   3. Vessel   (innermost, on top of both body layers)
        // All drawn with glDepthMask(GL_FALSE) + GL_BLEND so they accumulate
        // correctly without overwriting the depth buffer.

        const bool hasMuscular = m_muscularBatch.elementCount > 0;
        const bool hasSkeletal = m_skeletalBatch.elementCount > 0;
        const bool arterial3D  = (m_palette == Palette::Arterial);

        // Enable alpha blending for all body layers
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);

        // 1. Muscular — barely-there haze (same in all views per old project)
        if (hasMuscular) {
            // X-ray look: muscular = soft tissue silhouette (very faint, lighter colour)
            // Fluoro = DSA: same faint muscular haze
            const float musOp = (fluoro || m_dsaActive) ? 0.04f : 0.05f;
            m_program->setUniformValue("uColor", QVector4D(0.72f, 0.72f, 0.70f, musOp));
            m_program->setUniformValue("uSheen",    QVector3D(0.f, 0.f, 0.f));
            m_program->setUniformValue("uSpecular", 0.0f);
            m_program->setUniformValue("uShininess",1.0f);
            m_program->setUniformValue("uAmbient",  0.90f);
            m_program->setUniformValue("uDiffuse",  0.15f);
            m_program->setUniformValue("uShaded",   0);
            // Muscular.stl is anatomy, not vasculature: it never pulses.
            m_program->setUniformValue("uPulsatile", 0);
            QOpenGLVertexArrayObject::Binder bnd(&m_muscularBatch.vao);
            glDrawElements(GL_TRIANGLES, m_muscularBatch.elementCount, GL_UNSIGNED_INT, nullptr);
        }

        // 2. Skeletal — bone structure
        // Doc WhyFlattened: for 3D view use solid material (ambient=0.25, diffuse=0.75)
        //                   for fluoro keep X-ray flat (ambient=0.70, diffuse=0.30)
        if (hasSkeletal) {
            // 3D: 0.30 (clearly visible ghost), Fluoro: 0.08 (real X-ray = very faint bone)
            // X-ray look: bone = dark projection on white (high opacity, flat shading)
            // Fluoro: dark bone silhouette like real AP X-ray
            // 3D: lighter, more transparent ghost hint
            // Fluoro = DSA: same skeletal rendering
            const float skelOp = arterial3D ? 0.25f : 0.22f;  // DSA/fluoro: 0.22
            m_program->setUniformValue("uColor", QVector4D(0.86f, 0.86f, 0.84f, skelOp));
            if (arterial3D) {
                // 3D: solid directional shading
                m_program->setUniformValue("uAmbient",  0.25f);
                m_program->setUniformValue("uDiffuse",  0.75f);
                m_program->setUniformValue("uSheen",    QVector3D(0.86f, 0.86f, 0.84f));
                m_program->setUniformValue("uSpecular", 0.25f);
                m_program->setUniformValue("uShininess",30.0f);
            } else {
                // DSA: medium flat
                m_program->setUniformValue("uAmbient",  0.70f);
                m_program->setUniformValue("uDiffuse",  0.30f);
                m_program->setUniformValue("uSheen",    QVector3D(0.86f, 0.86f, 0.84f));
                m_program->setUniformValue("uSpecular", 0.10f);
                m_program->setUniformValue("uShininess",60.0f);
            }
            m_program->setUniformValue("uShaded",   1);
            // Skeletal.stl likewise: bone does not move with the cardiac cycle.
            m_program->setUniformValue("uPulsatile", 0);
            QOpenGLVertexArrayObject::Binder bnd(&m_skeletalBatch.vao);
            glDrawElements(GL_TRIANGLES, m_skeletalBatch.elementCount, GL_UNSIGNED_INT, nullptr);
        }

        // 3. Vessel — innermost
        // Old project: 3D opacity=0.30 (red), fluoro opacity=0.0 (hidden)
        // Wireframe = SetRepresentationToWireframe() toggle (Mesh View button)
        if (m_showWall && !m_dsaActive && !fluoro) {
            const bool isFluoro  = false;  // fluoro already excluded above
            const bool vesselVis = true;   // 3D/mesh/roadmap: always show vessel
            if (vesselVis) {
                Material wm = wallMaterial();
                if (m_roadMapActive) {
                    wm.colour   = QVector4D(0.92f, 0.24f, 0.33f, 0.85f);  // #EA3C53 road map
                    wm.ambient  = 0.9f;
                    wm.specular = 0.0f;
                }
                // Wireframe mode (Mesh View): draw as lines, full opacity
                if (m_wireframe) {
                    // Wireframe: same colour+opacity as 3D view but rendered as mesh lines
                    // wm already has the correct 3D colour (#EA3C53, opacity=m_wallOpacity)
                    // Draw filled surface first (gives depth), then wireframe lines on top
                    glPolygonOffset(1.f, 1.f);
                    glEnable(GL_POLYGON_OFFSET_FILL);
                    drawBatch(m_meshBatch, GL_TRIANGLES, wm, true, /*pulsatile=*/true);   // filled, same transparency
                    glDisable(GL_POLYGON_OFFSET_FILL);
                    // Wireframe lines: same colour, slightly more opaque so edges read clearly
                    Material wire = wm;
                    wire.colour.setW(std::min(wm.colour.w() + 0.15f, 1.0f));
                    wire.ambient = 0.8f;  wire.specular = 0.f;
                    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                    glLineWidth(1.2f);
                    drawBatch(m_meshBatch, GL_TRIANGLES, wire, false, /*pulsatile=*/true);
                    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                } else {
                    drawBatch(m_meshBatch, GL_TRIANGLES, wm, true, /*pulsatile=*/true);
                }
            }
        }

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        // Device on top in all views
        if (m_deviceBatches) {
            Material tip;
            tip.colour = QVector4D(0.62f, 0.85f, 1.00f, 1.f);
            tip.sheen = QVector3D(1.f, 1.f, 1.f);
            tip.specular = 0.55f;
            tip.shininess = 40.f;

            Material band;
            band.colour = QVector4D(0.88f, 0.89f, 0.93f, 1.f);
            band.sheen = QVector3D(1.f, 1.f, 1.f);
            band.specular = 0.95f;
            band.shininess = 96.f;

            Material shaft;
            shaft.colour = QVector4D(0.36f, 0.72f, 0.94f, 1.f);
            shaft.sheen = QVector3D(0.80f, 0.90f, 1.f);
            shaft.specular = 0.50f;
            shaft.shininess = 48.f;

            // Host devices first: the guide a microcatheter was telescoped
            // through is still in the patient and still visible, but it is no
            // longer the thing being steered, so it is drawn desaturated and
            // slightly translucent to keep the active device legible against it.
            Material host;
            host.colour    = QVector4D(0.55f, 0.60f, 0.68f, 0.75f);
            host.sheen     = QVector3D(0.75f, 0.80f, 0.88f);
            host.specular  = 0.30f;
            host.shininess = 32.f;
            host.ambient   = 0.20f;
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            for (auto &hb : m_hostBatches) {
                if (!hb) continue;
                drawBatch(hb->shaft, GL_TRIANGLES, host, true);
                drawBatch(hb->band,  GL_TRIANGLES, host, true);
                drawBatch(hb->tip,   GL_TRIANGLES, host, true);
            }
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);

            drawBatch(m_deviceBatches->shaft, GL_TRIANGLES, shaft, true);
            drawBatch(m_deviceBatches->band,  GL_TRIANGLES, band,  true);
            drawBatch(m_deviceBatches->tip,   GL_TRIANGLES, tip,   true);

            Material balloon;
            const float ap = static_cast<float>(m_overlay.ballooonAppositionPct / 100.0);
            balloon.colour = QVector4D(0.90f, 0.95f, 1.00f, 0.30f + 0.28f * ap);
            balloon.sheen = QVector3D(1.f, 1.f, 1.f);
            balloon.specular = 0.95f;
            balloon.shininess = 90.f;
            glDepthMask(GL_FALSE);
            drawBatch(m_deviceBatches->balloon, GL_TRIANGLES, balloon, true);
            glDepthMask(GL_TRUE);
        }
    }
    // ── Introducer sheath (built by SheathManager, uploaded on setSheath) ──
    if (m_sheathVisible && !endo) {
        // Cannula: translucent PTFE. Drawn first so the opaque hub sits over it.
        Material cannula;
        cannula.colour    = QVector4D(0.86f, 0.88f, 0.90f, 0.55f);
        cannula.specular  = 0.75f;
        cannula.shininess = 64.f;
        cannula.ambient   = 0.28f;

        // Hub: opaque, colour-coded by French size. This is the part an
        // operator reads the sheath size off, so it is deliberately the most
        // saturated thing in the scene apart from the device itself.
        Material hub;
        hub.colour    = QVector4D(m_sheathHubColour, 1.f);
        hub.specular  = 0.45f;
        hub.shininess = 32.f;
        hub.ambient   = 0.22f;
        // An oversized sheath is flagged by desaturating the hub toward warning
        // red rather than by silently resizing the device.
        if (m_sheathOversized)
            hub.colour = QVector4D(0.85f, 0.18f, 0.14f, 1.f);

        Material valve;                               // silicone haemostasis cap
        valve.colour    = QVector4D(0.16f, 0.17f, 0.19f, 1.f);
        valve.specular  = 0.30f;
        valve.shininess = 24.f;
        valve.ambient   = 0.20f;

        Material arm;                                 // side-arm tubing + stopcock
        arm.colour    = QVector4D(0.80f, 0.82f, 0.85f, 0.85f);
        arm.specular  = 0.55f;
        arm.shininess = 40.f;
        arm.ambient   = 0.25f;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        drawBatch(m_sheathCannulaBatch, GL_TRIANGLES, cannula, true);
        drawBatch(m_sheathArmBatch,     GL_TRIANGLES, arm,     true);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        drawBatch(m_sheathHubBatch,   GL_TRIANGLES, hub,   true);
        drawBatch(m_sheathValveBatch, GL_TRIANGLES, valve, true);
    }

    m_program->release();

    paintOverlay();
}

// ----------------------------------------------------------------- overlay

void VesselView::paintOverlay() {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor amber(255, 196, 64);
    const QColor pale(214, 220, 226);
    const QColor alarm(255, 96, 96);
    // Labels sit back from their values, the way they do on the C-arm cluster:
    // the value is what is read, the label only says which value it is.
    const QColor dim(150, 156, 164);
    QFont mono("Monospace", 9);
    // Two sizes: a small label over a larger value, matching the C-arm console.
    QFont labelFont("Monospace", 8);
    labelFont.setStyleHint(QFont::TypeWriter);
    QFont valueFont("Monospace", 11, QFont::Bold);
    valueFont.setStyleHint(QFont::TypeWriter);
    const QFontMetrics fm2(labelFont), fmVal(valueFont);
    mono.setStyleHint(QFont::TypeWriter);
    p.setFont(mono);

    if (m_overlay.decisionPending && !m_overlay.options.empty()) {
        const QMatrix4x4 vp = viewProjection();
        for (const auto &o : m_overlay.options) {
            const QVector4D clip = vp * QVector4D(o.target, 1.f);
            if (clip.w() <= 0.f) continue;
            const float sx = (clip.x() / clip.w() * 0.5f + 0.5f) * width();
            const float sy = (1.f - (clip.y() / clip.w() * 0.5f + 0.5f)) * height();

            const QColor c = o.highlight ? amber : pale;
            p.setPen(QPen(c, o.highlight ? 2.0 : 1.2));
            p.setBrush(Qt::NoBrush);
            const qreal r = 16.0;
            p.drawEllipse(QPointF(sx, sy), r, r);
            p.drawLine(QPointF(sx - r - 6, sy), QPointF(sx - r, sy));
            p.drawLine(QPointF(sx + r, sy), QPointF(sx + r + 6, sy));
            p.drawText(QPointF(sx - r, sy - r - 6),
                       QString("%1  %2 mm  %3°")
                           .arg(o.label)
                           .arg(o.lumenMm, 0, 'f', 2)
                           .arg(o.takeoffDeg, 0, 'f', 0));
        }
        p.setPen(amber);
        p.drawText(QRect(0, height() - 46, width(), 20), Qt::AlignHCenter,
                   tr("BIFURCATION — ← LEFT   RIGHT →"));
    }

    if (m_overlay.deadEnd) {
        p.setPen(amber);
        p.drawText(QRect(0, height() - 46, width(), 20), Qt::AlignHCenter,
                   tr("DISTAL VESSEL — no further advance"));
    }

    // ── Telemetry, stacked label-over-value ──────────────────────────────────
    //
    // Same shape as the C-arm console read-outs: a small dim label with its
    // value on the line beneath, one parameter per block, running down the
    // right-hand side. Both lines are right-aligned to the same edge, so the
    // values form a clean column and a long device name pushes only its own
    // line leftward.
    //
    // Every block gets the same height, so the spacing between parameters is
    // uniform — no group separator, which is what made the old layout uneven.
    const int pad     = 10;
    const int lineH   = fm2.height();          // label line
    const int valueH  = fmVal.height();        // value line
    const int rowGap  = 7;                     // between one parameter and the next
    const int blockH  = lineH + valueH + rowGap;

    struct Row { QString label, value; };
    const QVector<Row> rows = {
                                {QStringLiteral("ACCESS"),   m_overlay.accessSite},
                                {QStringLiteral("DEVICE"),   m_overlay.deviceName},
                                {QStringLiteral("BRANCH"),   QString::number(m_overlay.branchId)},
                                {QStringLiteral("TURNS"),    QString::number(m_overlay.turns)},
                                {QStringLiteral("DEPTH"),    QStringLiteral("%1 mm").arg(m_overlay.depthMm,     0, 'f', 1)},
                                {QStringLiteral("LUMEN ⌀"),  QStringLiteral("%1 mm").arg(m_overlay.lumenMm,     0, 'f', 2)},
                                {QStringLiteral("DEVICE ⌀"), QStringLiteral("%1 mm").arg(m_overlay.deviceMm,    0, 'f', 2)},
                                {QStringLiteral("CLEAR"),    QStringLiteral("%1 mm").arg(m_overlay.clearanceMm, 0, 'f', 2)},
                                };

    const int clearRow = rows.size() - 1;      // CLEAR reddens on wall contact
    const int right    = width() - pad;
    int y = pad + fm2.ascent();

    for (int i = 0; i < rows.size(); ++i) {
        p.setFont(labelFont);
        p.setPen(dim);
        p.drawText(right - fm2.horizontalAdvance(rows[i].label), y, rows[i].label);

        p.setFont(valueFont);
        p.setPen(i == clearRow && m_overlay.wallContact ? alarm : pale);
        p.drawText(right - fmVal.horizontalAdvance(rows[i].value),
                   y + valueH, rows[i].value);

        y += blockH;
    }

    if (m_overlay.wallContact) {
        // Feedback the operator cannot miss while looking at the vessel rather
        // than at the read-outs: the frame itself reddens with the severity.
        const float sev = std::clamp(m_overlay.contactSeverity, 0.15f, 1.f);
        const int bandPx = static_cast<int>(6 + 16 * sev);
        QColor edge = alarm;
        edge.setAlphaF(0.20 + 0.55 * static_cast<double>(sev));
        QPen border(edge, bandPx);
        p.setPen(border);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(bandPx / 2, bandPx / 2, width() - bandPx, height() - bandPx));

        p.setPen(alarm);
        p.drawText(QRect(0, pad, width(), 20), Qt::AlignHCenter,
                   tr("WALL CONTACT   %1%").arg(static_cast<int>(sev * 100)));
    }

    QString modeTag = (m_mode == Mode::Endoluminal) ? tr("ENDOLUMINAL") : tr("REFERENCE");
    if (m_mode != Mode::Endoluminal && m_cameraFollows) modeTag += tr("  ·  FOLLOWING TIP");
    if (m_overlay.moving) modeTag += tr("   ▶ ADVANCING");
    p.setPen(QColor(255, 255, 255, 100));
    p.drawText(QRect(0, height() - 24, width(), 18), Qt::AlignHCenter, modeTag);

    if (m_mode == Mode::Endoluminal) {
        p.setPen(QPen(QColor(255, 255, 255, 46), 1));
        const QPointF c(width() / 2.0, height() / 2.0);
        p.drawLine(c + QPointF(-14, 0), c + QPointF(-4, 0));
        p.drawLine(c + QPointF(4, 0), c + QPointF(14, 0));
        p.drawLine(c + QPointF(0, -14), c + QPointF(0, -4));
        p.drawLine(c + QPointF(0, 4), c + QPointF(0, 14));
    }
}

// ------------------------------------------------------------------- input

void VesselView::mousePressEvent(QMouseEvent *e) {
    // Alt+left-click: vessel surface picking (Layer 2 — click-to-refine sheath)
    if (m_pickingEnabled && e->button() == Qt::LeftButton &&
        (e->modifiers() & Qt::AltModifier) && m_graph) {
        // Unproject click to world-space ray using the current view/projection.
        const QPointF sp  = e->position();
        const float   W   = float(width()), H = float(height());
        const float   asp = H > 0.f ? W / H : 1.f;

        QMatrix4x4 proj, view;
        // Same FOV the frame was rendered with, or the unprojected ray misses
        // what the operator actually clicked on.
        proj.perspective(fovDegrees(), asp, m_modelScale * 0.001f, m_modelScale * 8.f);
        view.lookAt(cameraPosition(),
                    (m_cameraFollows && m_device) ? m_followTarget : m_target,
                    QVector3D(0, 0, 1));
        const QMatrix4x4 vpInv = (proj * view).inverted();

        // NDC coordinates of click
        const float nx =  (sp.x() / W) * 2.f - 1.f;
        const float ny = -(sp.y() / H) * 2.f + 1.f;

        // Unproject near and far plane to get ray
        const QVector4D n4 = vpInv * QVector4D(nx, ny, -1.f, 1.f);
        const QVector4D f4 = vpInv * QVector4D(nx, ny,  1.f, 1.f);
        const QVector3D nearPt(n4.x()/n4.w(), n4.y()/n4.w(), n4.z()/n4.w());
        const QVector3D farPt (f4.x()/f4.w(), f4.y()/f4.w(), f4.z()/f4.w());
        const QVector3D ray = (farPt - nearPt).normalized();

        // Linear scan: find graph node nearest to this ray
        float   bestD = std::numeric_limits<float>::max();
        int32_t bestN = -1;
        for (int32_t i = 0; i < (int32_t)m_graph->nodeCount(); ++i) {
            const auto &nd = m_graph->nodes()[i];
            const QVector3D p(nd.position.x, nd.position.y, nd.position.z);
            const QVector3D cp = p - nearPt;
            const float t = QVector3D::dotProduct(cp, ray);
            if (t < 0.f) continue;   // behind camera
            const float d = (p - (nearPt + ray * t)).length();
            if (d < bestD) { bestD = d; bestN = i; }
        }

        // Tolerance = 5% of camera distance — generous enough for thin tubes
        const float tol = (m_cameraFollows ? m_followDistance : m_distance) * 0.05f;
        if (bestN >= 0 && bestD < tol) {
            const auto &nd = m_graph->nodes()[bestN];
            emit vesselSurfacePicked(QVector3D(nd.position.x,
                                               nd.position.y,
                                               nd.position.z));
            e->accept();
            return;
        }
    }
    m_lastMouse = e->position().toPoint();
    setFocus(Qt::MouseFocusReason);
}

void VesselView::mouseMoveEvent(QMouseEvent *e) {
    const QPoint d = e->position().toPoint() - m_lastMouse;
    m_lastMouse = e->position().toPoint();

    if (m_mode == Mode::Endoluminal) {
        // Inside the vessel the only free axis is roll about the direction of
        // travel - exactly the wrist rotation the operator has.
        if (e->buttons() & Qt::LeftButton) {
            m_endoUp = QQuaternion::fromAxisAndAngle(m_endoForward, d.x() * 0.5f)
            .rotatedVector(m_endoUp);
            emit cameraChanged();
            update();
        }
        return;
    }
    if ((e->buttons() & Qt::LeftButton) && !m_frontalLock) {
        m_azimuth -= d.x() * 0.15f;   // slowed — was 0.4, too fast
        m_elevation = std::clamp(m_elevation + d.y() * 0.15f, -89.f, 89.f);
        emit cameraChanged();
        update();
    } else if (e->buttons() & (Qt::MiddleButton | Qt::RightButton)) {
        const QVector3D fwd = cameraForward();
        const QVector3D right = QVector3D::crossProduct(fwd, QVector3D(0, 0, 1)).normalized();
        const QVector3D up = QVector3D::crossProduct(right, fwd);
        const float k = (m_cameraFollows ? m_followDistance : m_distance) * 0.0015f;
        m_target += -right * d.x() * k + up * d.y() * k;
        update();
    }
}

void VesselView::wheelEvent(QWheelEvent *e) {
    const float steps = e->angleDelta().y() / 120.f;
    if (m_mode == Mode::Endoluminal) {
        m_endoFov = std::clamp(m_endoFov - steps * 3.f, 30.f, 110.f);
    } else if (m_cameraFollows && m_device) {
        // Scales the auto-zoom rather than replacing it, so framing still
        // adapts as the device moves between a trunk and a distal branch.
        m_followZoom = std::clamp(m_followZoom * std::pow(0.85f, steps), 0.15f, 8.f);
        startCameraEase();
    } else {
        m_distance = std::max(m_distance * std::pow(0.88f, steps), 1e-4f);
    }
    update();
}

void VesselView::keyPressEvent(QKeyEvent *e) {
    switch (e->key()) {
    case Qt::Key_PageUp:   emit advanceRequested();     e->accept(); return;
    case Qt::Key_PageDown: emit retractRequested();     e->accept(); return;
    case Qt::Key_Left:     emit chooseLeftRequested();  e->accept(); return;
    case Qt::Key_Right:    emit chooseRightRequested(); e->accept(); return;
    case Qt::Key_Space:    emit balloonToggleRequested(); e->accept(); return;
    default: break;
    }
    QOpenGLWidget::keyPressEvent(e);
}

void VesselView::setCarmAngles(float lao, float cran) {
    // ── One C-arm state, applied identically in every view ───────────────────
    //
    // This used to accumulate the DELTA since the last call into m_azimuth and
    // m_elevation. That makes the orbit path-dependent: a view that missed a
    // call, was constructed later, or started from a different framing ends up
    // at a different orientation for the same rig angles, and the views drift
    // apart with no way back. It is precisely how separate per-view C-arm
    // positions came about.
    //
    // The angles are now stored as an ABSOLUTE offset and added to the orbit
    // where the camera basis is built. Every view given the same (lao, cran)
    // therefore shows the same C-arm orientation by construction, while keeping
    // its own zoom, pan and target — which the delta scheme was trying to
    // protect and this preserves anyway.
    if (std::fabs(lao - m_carmLAO) < 1e-4f && std::fabs(cran - m_carmCRAN) < 1e-4f)
        return;

    m_carmLAO  = lao;
    m_carmCRAN = cran;

    update();
    emit cameraChanged();
}

void VesselView::setBodyMeshes(const QString &muscularPath, const QString &skeletalPath) {
    m_muscularPath  = muscularPath;
    m_skeletalPath  = skeletalPath;
    m_bodyMeshDirty = true;
    // Trigger immediate rebuild if GL is already up.
    // Per the alignment doc: different margins for different layers so they nest:
    // skeletal (inner) margin=1.04, muscular (outer) margin=1.05.
    if (isValid()) {
        makeCurrent();
        rebuildBodyMeshBatch(m_skeletalPath, m_skeletalBatch, m_skeletalAlign);
        rebuildBodyMeshBatch(m_muscularPath, m_muscularBatch, m_muscularAlign);
        m_bodyMeshDirty = false;
        doneCurrent();
    }
    update();
}