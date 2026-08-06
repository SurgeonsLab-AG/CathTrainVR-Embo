#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>
#include <QString>
#include <QTimer>
#include <memory>
#include <vector>

#include "core/CatheterModel.h"
#include "core/CenterlineGraph.h"
#include "core/SheathManager.h"
#include "core/CardiacCycle.h"
#include "core/StlMesh.h"

class QOpenGLShaderProgram;

/// Vessel viewport.
///
/// Two cameras:
///   * **Reference** (default) - the overview camera. Its target tracks the
///     catheter tip and its distance auto-zooms to the local vessel calibre,
///     unless following is switched off, in which case it frames the whole
///     tree. There is no separate follow mode; following is a property of the
///     reference view.
///   * **Endoluminal** - eye inside the catheter looking down the lumen. The
///     device is not drawn here; you cannot see your own hardware from inside
///     it.
///
/// No planned route is ever drawn. The centerline is a navigation aid and is
/// toggled on demand.
class VesselView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    enum class Mode { Reference, Endoluminal };

    /// Vessel wall appearance. Arterial is the anatomical one: a pinkish-red
    /// muscular wall under a pale fibrous adventitia, wet enough to have a
    /// specular sheen.
    enum class Palette { Arterial, Angiographic, Monochrome };

    struct Overlay {
        QString accessSite;
        QString deviceName;
        double depthMm = 0.0;
        double lumenMm = 0.0;
        double deviceMm = 0.0;
        double clearanceMm = 0.0;
        int branchId = -1;
        int turns = 0;
        bool wallContact = false;
        bool deadEnd = false;
        bool decisionPending = false;
        bool moving = false;
        float contactSeverity = 0.f;
        double carmLAO  = 0.0;   /// degrees for HUD
        double carmCRAN = 0.0;
        double balloonMm = 0.0;
        double ballooonAppositionPct = 0.0;
        bool balloonInflated = false;

        struct Option {
            QVector3D target;
            QString label;
            double lumenMm = 0.0;
            double takeoffDeg = 0.0;
            bool highlight = false;
        };
        std::vector<Option> options;
    };

    explicit VesselView(QWidget *parent = nullptr);
    ~VesselView() override;

    void setMesh(const cn::StlMesh *mesh);
    void setGraph(const cn::CenterlineGraph *graph);
    void setDevice(const cn::CatheterModel *device);

    /// Drive the pulsating vessel wall from the console's cardiac clock. The
    /// same clock feeds the ECG, so the wall distends on the beat the trace is
    /// drawing rather than on a timer of its own. The cycle must outlive the view.
    void setCardiacCycle(const cn::CardiacCycle *c);
    /// Wall pulsation on/off (it is a rendering effect, not a measurement).
    void setPulsationEnabled(bool on) { m_pulsation = on; update(); }
    bool pulsationEnabled() const { return m_pulsation; }

    /// Where the pumping motion is applied.
    ///
    /// The pulse is confined to the heart and great vessels of the VESSEL mesh.
    /// The Muscular and Skeletal layers are uploaded through the plain 6-float
    /// path and never receive the displacement attributes at all, so they cannot
    /// move regardless of these settings.
    struct HeartRegion {
        /// Take the heart centre as the widest node in the centerline graph. On
        /// this model that is the 15 mm aortic root — the pump itself.
        bool      autoLocate = true;
        QVector3D centre;                 ///< used only when autoLocate is false

        /// Full amplitude out to coreRadius, smoothstepping down to
        /// peripheralFraction by coreRadius + falloff. Model units.
        float coreRadius = 0.090f;        ///< 90 mm — heart and aortic root
        float falloff    = 0.160f;        ///< faded out by 250 mm — arch and great vessels

        /// Multiplier on the base arterial strain inside the core. The heart
        /// wall moves far more than an artery does, so 1.0 would read as a
        /// barely-visible shimmer where a pump is expected.
        float heartAmplify = 3.0f;

        /// Residual motion outside the region, as a fraction of the heart
        /// amplitude. 0 confines the animation entirely to the heart, which is
        /// the default; raise it to let the arterial tree pulse as well.
        float peripheralFraction = 0.f;

        /// Calibre gate — sub-millimetre noise only, by default a no-op for
        /// real vessels.
        ///
        /// This was briefly used to exclude the carotids, which was a mistake:
        /// the carotids and the coronaries are BOTH small (4.5 mm and under),
        /// so no calibre threshold separates them. Setting the gate high enough
        /// to drop the carotids also dropped the coronaries, and the coronaries
        /// are most of the moving surface in the heart region — 27,400 of the
        /// 29,000 vertices near the pump sit on vessels under 2 mm. Excluding
        /// them left 534 moving vertices and no visible pulsation.
        ///
        /// Raise calibreZero/calibreFull only if you want large vessels alone.
        float calibreFull = 0.0008f;   ///< 0.8 mm and above — full amplitude
        float calibreZero = 0.0002f;   ///< 0.2 mm and below — nothing

        /// Superior cut-off, measured upward from the heart centre. THIS is what
        /// keeps the carotids static, and position is the right discriminator:
        /// carotids ascend past the arch, coronaries sit at or below the root.
        ///
        /// Measured on this model: heart centre Z 1324.5 mm, arch apex (widest
        /// skeleton) Z 1411.2 mm. A limit of +75 mm faded over 20 mm puts zero
        /// at Z 1419.5 — above the arch, below every carotid vertex — while the
        /// arch apex itself still moves at about a third amplitude, so there is
        /// no seam where the motion stops.
        ///
        /// There is deliberately no inferior limit: the descending aorta is as
        /// much a great vessel as the ascending one.
        float superiorLimit = 0.075f;  ///< 75 mm above centre — full amplitude below
        float superiorFade  = 0.020f;  ///< nothing 20 mm further up
    };
    void setHeartRegion(const HeartRegion &r);
    const HeartRegion &heartRegion() const { return m_heart; }
    /// Where the heart was actually located, after auto-location. Valid once a
    /// mesh and graph have been set.
    QVector3D heartCentre() const { return m_heartCentre; }

    /// Devices already placed and left in the patient, outermost first — the
    /// guide a microcatheter was telescoped through, and so on. They are drawn
    /// desaturated behind the active device. Coaxial technique means the
    /// previous catheter does not vanish when the next one goes up it.
    void setHostDevices(const std::vector<const cn::CatheterModel *> &hosts);
    void setOverlay(const Overlay &o) { m_overlay = o; update(); }

    /// Tip arc position along the current route; drives both the endoluminal
    /// eye and the follow camera's auto-zoom.
    void setCameraTrack(const cn::CenterlinePath *path, float arc);
    void setCameraFollows(bool v);
    bool cameraFollows() const { return m_cameraFollows; }

    void setMode(Mode m);
    Mode mode() const { return m_mode; }

    void setPalette(Palette p);
    Palette palette() const { return m_palette; }

    void setShowWall(bool v) { m_showWall = v; update(); }
    void setShowCenterline(bool v) { m_showCenterline = v; update(); }
    bool showCenterline() const { return m_showCenterline; }
    void setWallOpacity(float v) { m_wallOpacity = v; update(); }

    /// Load Muscular.stl and Skeletal.stl for the body-layer background.
    ///
    /// These two meshes and the vessel STL are authored in ONE coordinate frame
    /// at ONE scale, so the default alignment is the identity: no fitting, no
    /// per-axis scaling, no shift. Measured on the shipped assets (mm):
    ///
    ///     vessel    X -287.6..287.6   Y  -88.4.. 95.0   Z  38.6..1700.6
    ///     skeletal  X -334.6..334.6   Y -137.3..112.8   Z   9.2..1705.5
    ///     muscular  X -335.7..334.9   Y -137.2..132.7   Z   6.5..1707.8
    ///
    /// Vessel nests inside skeletal nests inside muscular on every axis, and the
    /// bodies correctly extend past the arterial tree at the feet (the vessel
    /// stops at the ankle arteries) and the scalp. Anything other than identity
    /// makes that worse.
    void setBodyMeshes(const QString &muscularPath, const QString &skeletalPath);

    /// Fine adjustment applied on top of the identity alignment. `scale` is
    /// uniform about the vessel bounding-box centre; `offset` is added
    /// afterwards, in world units. Defaults are a no-op.
    struct BodyAlign {
        QVector3D offset;        ///< world units; +Z raises the mesh
        float     scale = 1.f;   ///< uniform, about the vessel centre
    };
    void setBodyAlignment(const BodyAlign &muscular, const BodyAlign &skeletal);
    BodyAlign muscularAlignment() const { return m_muscularAlign; }
    BodyAlign skeletalAlignment() const { return m_skeletalAlign; }

    /// Nudge both body layers along Z (and optionally rescale) at runtime, for
    /// dialling an unfamiliar asset in. Emits nothing; call from a shortcut.
    void nudgeBodyLayers(float dz, float dScale = 0.f);

    /// Legacy per-axis bounding-box fit: stretches each body mesh so its bbox
    /// matches the vessel's. Off by default. Only useful when a body mesh comes
    /// from a different source than the vessel and genuinely needs fitting; on
    /// the shipped assets it compresses the skeleton to 73% of its authored
    /// depth and 86% of its width.
    void setBodyAutoFit(bool on) { m_bodyAutoFit = on; m_bodyMeshDirty = true; update(); }
    bool bodyAutoFit() const { return m_bodyAutoFit; }

    /// Road Map mode: draw the vessel in red at high opacity on a dark background,
    /// like the old project's road-map look (vessel colour 1.0, 0.20, 0.3686 at 0.75 opacity).
    void setRoadMapActive(bool on) { m_roadMapActive = on; update(); }
    /// DSA mode: dark background, skeleton visible, vessel surface hidden (opacity 0).
    void setDSAActive(bool on) { m_dsaActive = on; update(); }
    void setWireframe(bool on)  { m_wireframe = on; update(); }
    bool wireframe() const { return m_wireframe; }

    void resetCamera();
    void refreshDevice();

    /// Lock the camera to the frontal (AP) projection. On by default: this is
    /// a projection an operator reads, not a scene they fly around.
    void setFrontalLock(bool v);
    void setCarmAngles(float lao, float cran);  /// C-arm LAO/CRAN rotation (degrees)
    bool frontalLock() const { return m_frontalLock; }

    /// Frame the entire tree, frontal and upright. The startup view.
    void showWholeVessel();

    /// Vertical field of view for the current palette, in degrees.
    ///
    /// Shared by viewProjection() and by every framing function, so the distance
    /// a view is placed at is always computed from the FOV it actually renders
    /// with. Previously the two carried duplicate literals and could disagree.
    float fovDegrees() const;
    // Frame the head/brain region for the Fluoro view default, matching
    // the old project's frameOnFace() + parallel-scale zoom.
    void showFluoroDefault();

    /// Copy camera state from another VesselView so all views share the same position.
    void syncCameraFrom(const VesselView &other);

    // Camera state accessors (for syncCameraFrom)
    float azimuth()   const { return m_azimuth; }
    float elevation() const { return m_elevation; }
    float distance()  const { return m_distance; }
    QVector3D target() const { return m_target; }
    void setCamera(float az, float el, float dist, const QVector3D &target);

    /// Snap the camera onto a point and frame it against a local vessel radius.
    /// Used when an access site is selected so the view opens on the puncture
    /// rather than on the whole tree.
    void focusOn(const QVector3D &position, float lumenRadius);

    /// Recompute the camera from the device *now* rather than at the next
    /// paint - the navigator sorts branches by screen position and one frame of
    /// lag is enough to swap left and right at a sharp takeoff.
    void syncCameraToDevice();

    QVector3D screenRight() const;
    QVector3D screenUp() const;

signals:
    void advanceRequested();
    void balloonToggleRequested();
    void retractRequested();
    void chooseLeftRequested();
    void chooseRightRequested();
    void cameraChanged();
    /// Emitted when the user Alt+clicks on the vessel surface (picking enabled).
    /// worldPt is the nearest graph node position to the click ray.
    void vesselSurfacePicked(QVector3D worldPt);

public:
    void setPickingEnabled(bool on) { m_pickingEnabled = on; }
    bool pickingEnabled() const     { return m_pickingEnabled; }

    /// Install a full introducer sheath for rendering: colour-coded hub,
    /// haemostasis valve, side arm and stopcock, with the cannula following the
    /// vessel centreline. Build it with SheathManager::buildSheath().
    ///
    /// The mesh is generated once here rather than rebuilt every frame, which
    /// is what the old single-cylinder path did.
    void setSheath(const cn::SheathGeometry &geom);
    void clearSheath() { m_sheathVisible = false; m_sheathDirty = false; update(); }
    bool sheathVisible() const { return m_sheathVisible; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    struct Batch {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
        QOpenGLBuffer ibo{QOpenGLBuffer::IndexBuffer};
        int elementCount = 0;
        bool indexed = false;
        bool ready = false;
        void destroy();
    };

    /// A real catheter is not a uniform pipe: soft tapered tip, radiopaque
    /// marker band, then shaft - three materials along one chain.
    struct DeviceBatches {
        Batch tip, band, shaft, balloon;
    };

    struct Material {
        QVector4D colour;
        QVector3D sheen{1.f, 1.f, 1.f};
        float specular = 0.f;
        float shininess = 24.f;
        float ambient = 0.12f;   ///< floor brightness in shadow
        float gain = 1.f;        ///< overall exposure for this material
    };

    void ensureBatch(Batch &b);
    void uploadBatch(Batch &b, const std::vector<float> &interleaved,
                     const std::vector<uint32_t> &indices);
    /// 8-float wall format: position, normal, root arc length, local radius.
    void uploadWallBatch(Batch &b, const std::vector<float> &interleaved,
                         const std::vector<uint32_t> &indices);
    /// `pulsatile` must be true ONLY for the vessel wall. It drives the
    /// uPulsatile uniform, which is the single gate on the cardiac displacement;
    /// the Muscular and Skeletal layers pass false (and set it directly, as they
    /// are drawn outside this function).
    void drawBatch(Batch &b, GLenum mode, const Material &m, bool shaded,
                   bool pulsatile = false);

    void rebuildMeshBatch();
    void rebuildCenterlineBatch();
    void rebuildDeviceBatches();
    void buildDeviceInto(const cn::CatheterModel *dev, DeviceBatches &b);
    void rebuildBodyMeshBatch(const QString &path, Batch &batch, const BodyAlign &align);

    void updateEndoluminalFrame();
    void updateFollowFrame();
    void rebuildBalloonBatch();
    void startCameraEase();
    QMatrix4x4 viewProjection() const;
    QVector3D cameraPosition() const;
    QVector3D cameraForward() const;
    QVector3D cameraUp() const;
    Material wallMaterial() const;
    void paintOverlay();

    const cn::StlMesh *m_mesh = nullptr;
    const cn::CenterlineGraph *m_graph = nullptr;
    const cn::CatheterModel *m_device = nullptr;
    Overlay m_overlay;

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    Batch m_meshBatch, m_centerlineBatch;
    Batch m_muscularBatch, m_skeletalBatch;   // body layer meshes (Muscular + Skeletal)
    std::unique_ptr<DeviceBatches> m_deviceBatches;
    std::vector<const cn::CatheterModel *> m_hostDevices;
    std::vector<std::unique_ptr<DeviceBatches>> m_hostBatches;
    bool m_meshDirty = false, m_centerlineDirty = false, m_deviceDirty = false;

    Mode m_mode = Mode::Reference;
    Palette m_palette = Palette::Arterial;

    // shared camera track
    const cn::CenterlinePath *m_camPath = nullptr;
    float m_camArc = 0.f, m_camArcTarget = 0.f;
    bool m_cameraFollows = false;   ///< off until an access site is chosen
    QTimer m_camEase;

    // follow camera - orbit angles are shared with the overview
    QVector3D m_followTarget, m_followTargetGoal;
    float m_followDistance = 0.f, m_followDistanceGoal = 0.f;
    float m_followZoom = 1.f;

    // endoluminal camera
    QVector3D m_endoEye, m_endoForward{0, 0, 1}, m_endoUp{0, 1, 0};
    float m_endoStandoffFrac = 0.004f;
    float m_endoLookaheadFrac = 0.012f;
    float m_endoFov = 72.f;

    // Orbit camera. Up is +Z: the model's long axis is Z, so a Y-up camera lays
    // the whole tree on its side. Azimuth 180 deg puts the eye on -Y looking
    // toward +Y, which is the standard AP projection - patient left (+x) falls
    // on the viewer's right.
    float m_azimuth = 180.f, m_elevation = 0.f, m_distance = 3.f;
    bool m_frontalLock = false;  // orbit enabled in all views by default
    float m_carmLAO = 0.f;   /// degrees, drives azimuth offset
    float m_carmCRAN = 0.f;  /// degrees, drives elevation offset
    QVector3D m_target;
    QPoint m_lastMouse;

    /// How much larger than a tight fit the anatomy is framed. One constant for
    /// every view, so Muscular / Skeletal / Vessel appear at the SAME size in
    /// 3D, Fluoro, Mesh and DSA — the framing multipliers used to differ per
    /// palette (2.00 in Arterial, 1.00 in Angiographic), which is why the
    /// anatomy changed size when the pane was switched.
    ///
    /// 1.25 leaves a quarter of the frame as margin, so the anatomy has visible
    /// space around it — the tree fills about 80% of the viewport height rather
    /// than the 91% a 1.10 margin gave. Raise for more air, lower for a tighter
    /// crop; below about 1.05 the tree can clip at large LAO angles, since
    /// rotating a 1662 x 575 mm model in the screen plane presents its long axis
    /// across the short one.
    ///
    /// This does NOT affect the C-arm view, which has its own camera and is
    /// deliberately left alone — its rig framing is set in CarmView::loadPart.
    static constexpr float kAnatomyFrameMargin = 1.25f;

    bool m_showWall = true, m_showCenterline = false;

    // ── Cardiac pulsation ─────────────────────────────────────────────────
    static const int kPulseLutSize = 64;
    const cn::CardiacCycle *m_cycle = nullptr;
    float m_pulseLUT[kPulseLutSize] = {};   ///< normalised pressure pulse
    bool  m_pulsation = true;
    HeartRegion m_heart;
    QVector3D   m_heartCentre;

    // Body mesh layers (Muscular + Skeletal, aligned to the vessel frame)
    QString m_muscularPath, m_skeletalPath;
    bool  m_bodyMeshDirty  = false;
    bool  m_roadMapActive  = false;
    bool  m_dsaActive      = false;
    bool  m_wireframe     = false;
    // Body-layer placement. Identity by default: the assets are already
    // co-registered with the vessel, so there is nothing to fit.
    BodyAlign m_muscularAlign;
    BodyAlign m_skeletalAlign;
    bool  m_bodyAutoFit    = false;  // legacy per-axis bbox stretch, off
    float m_wallOpacity = 0.68f;
    float m_modelScale = 1.f;

    // ── Picking ───────────────────────────────────────────────────────────
    bool m_pickingEnabled = false;

    // ── Sheath (built by SheathManager, uploaded once on setSheath) ──────
    bool m_sheathVisible = false;
    /// One batch per material: the cannula is translucent PTFE, the hub is
    /// opaque colour-coded plastic, the valve is dark silicone, and the side
    /// arm/stopcock are clear tubing and white plastic.
    Batch m_sheathCannulaBatch, m_sheathHubBatch, m_sheathValveBatch, m_sheathArmBatch;
    QVector3D m_sheathHubColour{0.11f, 0.55f, 0.27f};
    bool  m_sheathOversized = false;
    cn::SheathGeometry m_sheathGeom;   ///< kept so paintGL can rebuild on demand
    bool  m_sheathDirty = false;

    /// Uploads the sheath batches. MUST run with a current GL context, so it is
    /// driven from paintGL via m_sheathDirty like every other batch — never
    /// called straight from setSheath(), which is invoked from dialog handlers
    /// where no context is current.
    void rebuildSheathBatches();

    /// Swept-tube helper shared by every sheath part. Appends a capped tube of
    /// `radiusAt(i)` around `path`, using a parallel-transport frame so the
    /// rings do not twist where the centreline bends.
    static void appendTube(const std::vector<cn::Vec3> &path,
                           const std::vector<float> &radii, int sides,
                           std::vector<float> &vOut, std::vector<uint32_t> &iOut,
                           bool capStart, bool capEnd);
};