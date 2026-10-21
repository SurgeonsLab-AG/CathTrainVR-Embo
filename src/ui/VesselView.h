#pragma once

#include <QOpenGLWidget>
#include <QShowEvent>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QMatrix4x4>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include <algorithm>
#include <memory>
#include <vector>

#include "core/CatheterModel.h"
#include "core/CenterlineGraph.h"
#include "core/SheathManager.h"
#include "core/CardiacCycle.h"
#include "core/StlMesh.h"
#include "core/EmbolizationModel.h"
#include "core/CoilEmbolizationModel.h"
#include "core/ContrastInjectionModel.h"

class QOpenGLShaderProgram;
class QOpenGLFramebufferObject;

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
    /// Tells this view whether the currently active device (m_device) is a
    /// microcatheter, purely for Fluoro colour selection - CatheterModel
    /// itself has no notion of device class (that lives one level up, in
    /// CatheterCatalog::DeviceClass), so the caller has to say so explicitly
    /// rather than this view trying to infer it. Doing nothing in the 3D
    /// view: role (active vs. parked host) is already colour-coded there,
    /// and the microcatheter's genuinely thinner radius (its own real
    /// catalogue OD) is what actually distinguishes it visually - a second
    /// colour dimension for device TYPE was not asked for there and would
    /// compete with the existing active/host colour convention.
    void setDeviceIsMicrocatheter(bool v) { m_deviceIsMicrocatheter = v; update(); }
    /// True when the active device is a guidewire (CatheterSpec::devClass
    /// == cn::DeviceClass::Guidewire). Unlike the microcatheter flag above,
    /// this genuinely changes the TIP GEOMETRY, not just colour/opacity:
    /// a real guidewire's distal tip is a tightly wound platinum spring
    /// coil, not a smooth tapered tube with a marker band painted onto it
    /// - see buildSpringTipMesh()'s own comment. Marks the device batches
    /// dirty so the tip is rebuilt with (or back out of) that geometry the
    /// next time it changes.
    void setDeviceIsGuidewire(bool v) { m_deviceIsGuidewire = v; m_deviceDirty = true; update(); }
    /// The active device's 3D View display colour (CatheterSpec::colour),
    /// used to tint the shaft/tip in the non-Fluoro palette - Fluoro is
    /// unaffected, it always renders every device in its own grayscale/
    /// radiopaque tones regardless of this. Defaults to the same blue every
    /// device used before per-device colour existed, matching
    /// CatheterSpec::colour's own default.
    void setDeviceColor(const QVector3D &c) { m_deviceColor = c; update(); }
    /// Arc length (model units, same as CatheterModel::State::insertionDepth)
    /// the ACTIVE device's shaft should not render before, i.e. the host
    /// catheter's own current tip depth when telescoped through one - 0
    /// means "render the full shaft" (a device not telescoped through
    /// anything). Without this, a microcatheter's rendered shaft extends
    /// all the way back to the access site (its node chain's proximal end
    /// is arc-length zero, not the host's tip - CatheterModel itself has no
    /// notion of "telescoped through a host" at all), rendering as if it
    /// occupied the exact same length the host catheter is already drawn
    /// along. Host/parked devices are never clipped - only the active
    /// device passes this through in buildDeviceInto/rebuildDeviceBatches.
    void setDeviceHiddenBehindDepth(float depth) { m_deviceHiddenBehindDepth = depth; update(); }
    /// Real SID/magnification, read from CarmView's own live state
    /// (CarmView::sid()/magnification()) rather than this view guessing its
    /// own independent default - the two were previously out of sync
    /// (CarmView's real default is 32cm magnification; the Fluoroscopy HUD
    /// below had separately hardcoded 25cm), which is exactly the kind of
    /// inconsistency a single shared source of truth prevents by
    /// construction.
    void setDetectorInfo(float sidCm, float magCm, int frameFps) {
        m_hudSidCm = sidCm; m_hudMagCm = magCm; m_hudFrameFps = frameFps; update();
    }

    /// Real kVp/mA/DAP, read from the shared cn::DoseModel rather than the
    /// Fluoroscopy HUD's own previously-hardcoded "78"/"320"/"12.4 Gy·cm²"
    /// - see DoseModel's own class comment for why kVp stays constant while
    /// mA genuinely responds to magnification and DAP genuinely
    /// accumulates over real beam-on time. Same "single shared source of
    /// truth" reasoning setDetectorInfo() above already established.
    void setGeneratorInfo(float kvp, float ma, float dapGyCm2) {
        m_hudKvp = kvp; m_hudMa = ma; m_hudDapGyCm2 = dapGyCm2; update();
    }

    /// Same shared-instance pattern as setDevice(): the caller owns one
    /// EmbolizationModel and hands the same pointer to every VesselView, so
    /// whatever cast is visible in one view is exactly what every other view
    /// computes too - there is nothing here that could diverge per view,
    /// because no view ever grows the cast itself, only renders it.
    void setEmbolization(const cn::EmbolizationModel *emb);
    /// Call after every begin()/inject()/reset() on the shared model, the
    /// same way refreshDevice() is called after the catheter moves.
    void refreshEmbolization();

    /// Same shared-instance pattern, for the separate coil-deployment
    /// model - see CoilEmbolizationModel's own class comment for why a
    /// coil nest and a liquid-agent cast are mechanistically different
    /// things and therefore two models, not one, even though they end up
    /// rendered by the same tube-builder underneath.
    void setCoils(const cn::CoilEmbolizationModel *coils);
    /// Call after every beginAt()/advanceCoil()/finishCoil()/reset() on the
    /// shared model, the same way refreshEmbolization() is called after
    /// the liquid-agent cast changes.
    void refreshCoils();

    /// Same shared-instance pattern for the Contrast Injection module.
    void setContrastInjection(const cn::ContrastInjectionModel *contrast);
    /// Call after every beginPhase()/inject()/tick()/reset() on the shared
    /// model - tick() in particular needs this every frame it runs, since
    /// contrast decays (and therefore visibly shrinks) even with no operator
    /// action at all.
    void refreshContrastInjection();

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

    /// Forces m_heartCentre to be computed RIGHT NOW from whatever graph is
    /// currently set, rather than waiting for the next lazy paintGL()-
    /// driven mesh rebuild to get around to it. A caller that wants to call
    /// focusRibCage() (or read heartCentre() for any other reason)
    /// immediately after setGraph(), before this widget has ever actually
    /// painted, needs this - otherwise m_heartCentre is still sitting at
    /// its uninitialized (0,0,0) default, and framing on that produces
    /// whatever this model's own coordinate origin happens to be near
    /// (confirmed from an actual recording: the foot end), not the real
    /// heart. No GL context required - this only ever needed m_graph.
    void ensureHeartCentreReady();

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
    /// Changes only the orbit azimuth - target position and zoom distance
    /// are left completely untouched. Was intended to switch away from
    /// showHeadFrontal()'s own azimuth after sheath/catheter selection
    /// back when that function defaulted to a sagittal (side, azimuth=90)
    /// view - but was never actually wired to any call site, and
    /// showHeadFrontal() now defaults to the front-facing (azimuth=180)
    /// view directly, which is what this existed to switch TO. Left
    /// declared as a general "rotate around without moving target/zoom"
    /// primitive in case a future view mode needs exactly that, rather
    /// than removed outright.
    void setAzimuthOnly(float azimuthDeg) { m_azimuth = azimuthDeg; update(); emit cameraChanged(); }
    bool cameraFollows() const { return m_cameraFollows; }

    void setMode(Mode m);
    Mode mode() const { return m_mode; }

    void setPalette(Palette p);
    Palette palette() const { return m_palette; }

    void setShowWall(bool v) { m_showWall = v; update(); }
    void setShowCenterline(bool v) { m_showCenterline = v; update(); }
    bool showCenterline() const { return m_showCenterline; }
    void setWallOpacity(float v) { m_wallOpacity = v; update(); }

    /// Resolution the DRR accumulation buffer (ensureXrayFbo) renders the
    /// Fluoro pass at, as a fraction of the pane's actual on-screen
    /// viewport size — 1.0 = full resolution, lower = a genuinely lower-
    /// resolution buffer that then gets linearly upsampled back out,
    /// reading as authentic detector softness rather than a crisp
    /// diagnostic image. Real continuous low-dose fluoroscopy is visibly
    /// softer/noisier than a high-dose cine acquisition; this is what lets
    /// that difference actually show. Clamped well above zero so the
    /// buffer never collapses to something too small to composite
    /// sensibly, and marks the FBO for a resize on the next frame.
    void setFluoroQuality(float scale) {
        m_fluoroQualityScale = std::clamp(scale, 0.15f, 1.0f);
        update();
    }
    float fluoroQuality() const { return m_fluoroQualityScale; }

    /// The circle-and-crosshair markers (with lumen/takeoff-angle labels) and
    /// the "BIFURCATION — LEFT/RIGHT" banner drawn at a pending branch
    /// choice. Off by default: with keyboard/arrow navigation there is
    /// nothing here the operator needs read off-screen, and the markers sit
    /// on top of the fluoroscopy image exactly where the vessel anatomy is.
    /// Wall-contact feedback and every other overlay read-out are unaffected.
    void setShowBranchMarkers(bool v) { m_showBranchMarkers = v; update(); }
    bool showBranchMarkers() const { return m_showBranchMarkers; }

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

    /// Every parameter in the X-Ray Calibration dialog (MainWindow), read
    /// by kFragmentShader's uXrayMode==2 branch. Defaults match the
    /// requested calibration table. Changing any of these only affects the
    /// Fluoro palette instance (m_palette == Angiographic); the Vessel View
    /// / 3D / Mesh / DSA paths never read this struct at all.
    ///
    /// There is no real lighting anywhere in this pipeline (deliberately —
    /// see kFragmentShader's uXrayMode branches), so Ambient/Diffuse/
    /// Specular are NOT the lighting terms their VTK-derived names suggest.
    /// They are repurposed as a density-response shape, which is the closest
    /// honest equivalent in a projection with no light source:
    ///   Ambient  — weight on a fast-saturating "is this material present
    ///              at all here" signal. High ambient reads as a flatter,
    ///              more solid-looking presence.
    ///   Diffuse  — weight on the original slow, graduated response to
    ///              accumulated density (thin crossings look different from
    ///              thick/overlapping ones). High diffuse reads as more
    ///              gradient, less solid.
    ///   Specular — a small grain-correlated glint, off at 0. Deliberately
    ///              NOT view- or normal-dependent (that would reintroduce
    ///              the "lit 3D model" look this pipeline exists to avoid);
    ///              it only ever modulates existing per-fragment texture.
    struct XrayTuning {
        // Every value below is the operator's own final tuning, read off the
        // live X-Ray Calibration dialog (Save/Apply) and set here as the new
        // default so a fresh session opens already looking like the tuned
        // result, rather than requiring the dialog to be re-run each time.
        // Nothing here is derived or re-justified — these are the numbers
        // that were actually dialed in and confirmed to look right, matched
        // to the shader's real per-fragment crossing-count units (mode 1
        // writes ~1.0 per overlapping fragment; Opacity below is the
        // attenuation coefficient per crossing, combined through a true
        // Beer-Lambert exponential exactly once in mode 2 - see that
        // transfer function's own comment).
        QVector3D backgroundColor{0.09f, 0.09f, 0.09f};

        QVector3D boneColor{0.90f, 0.88f, 0.82f};
        float boneOpacity  = 0.50f;
        float boneAmbient  = 0.25f;
        float boneDiffuse  = 0.75f;
        float boneSpecular = 0.0f;

        QVector3D muscleColor{0.60f, 0.60f, 0.58f};
        float muscleOpacity  = 0.15f;
        float muscleAmbient  = 0.90f;
        float muscleDiffuse  = 0.15f;
        float muscleSpecular = 0.0f;

        /// Global post-processing, applied to the combined image after the
        /// per-material shape/colour is resolved — the same order a photo
        /// editor applies them: gamma, then contrast (pivoted at 0.5, so it
        /// can't shift the black point), then a final brightness multiply.
        float gamma      = 1.03f;
        float contrast   = 0.75f;
        float brightness = 0.80f;

        /// Extra scalars multiplying each material's shape after its own
        /// Ambient/Diffuse/Opacity — the final, coarsest "make bone/soft
        /// tissue brighter or dimmer" knobs.
        float boneIntensityMult = 1.15f;
        float softTissueMult    = 1.0f;

        /// Independent per-material brightness — genuinely separate from
        /// Bone/Soft-Tissue Multiplier above and from each other: each one
        /// is a single scalar multiplying only its own material's final
        /// value, so raising Muscle Brightness cannot move Bone Brightness
        /// or anything else, and vice versa.
        float boneBrightness   = 1.20f;
        float muscleBrightness = 1.50f;

        /// Vessel colour/opacity for the Fluoro view specifically. Values are
        /// the operator's own tuning from the live Calibration dialog: a
        /// neutral mid-gray (0.50/0.50/0.50) at a low 0.05 opacity — a
        /// subtle, faint vessel presence rather than the brighter, higher-
        /// opacity gray this was first added with.
        QVector3D vesselColor{0.50f, 0.50f, 0.50f};
        float     vesselOpacity = 0.05f;
    };
    void setXrayTuning(const XrayTuning &t) { m_xrayTuning = t; update(); }
    const XrayTuning &xrayTuning() const { return m_xrayTuning; }

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

    /// Real clinical roadmapping state machine. NoMask: vessel is at
    /// baseline near-invisible opacity, no frozen image exists. Masked: a
    /// static snapshot has been captured and is being composited as a
    /// fixed background layer - it does NOT move if the camera/C-arm
    /// angle subsequently changes, which is deliberate: that mismatch
    /// (misregistration) between a stale mask and live anatomy is a real,
    /// clinically important behavior of roadmapping, not a bug to hide.
    /// RoadmapActive: same as Masked, plus the operator has explicitly
    /// toggled the roadmap overlay on for active use (setRoadMapActive).
    enum class RoadmapState { NoMask, Masked, RoadmapActive };
    RoadmapState roadmapState() const { return m_roadmapState; }

    /// Boosts the vessel to visible opacity for exactly one rendered
    /// frame, captures that frame into an offscreen texture, then reverts
    /// the LIVE vessel back to its near-invisible baseline. From this
    /// point on the captured texture - not the live 3D mesh - is what
    /// represents the vessel on screen, drawn as a static background layer
    /// underneath the live bone/muscle/catheter render. Call again
    /// ("Re-mask") to intentionally refresh it at the current C-arm angle.
    void captureRoadmapMask();
    /// Discards the captured mask and returns to NoMask (vessel invisible,
    /// grayscale baseline) - called on new patient/case load so a stale
    /// mask from a previous case can never silently carry over.
    void clearRoadmapMask();
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
    /// Frame the rib cage specifically — closer than showWholeVessel(), and
    /// centred on the heart/aortic-root region rather than the whole mesh's
    /// bounding box. Uses the same Z-offset reasoning as the Fluoro rib-cage
    /// dampening (kFragmentShader): ribs surround the heart at roughly its
    /// own level, so no separate anatomical query is needed beyond
    /// heartCentre(), which is already computed from the loaded centerline.
    void focusRibCage();

    /// Vertical field of view for the current palette, in degrees.
    ///
    /// Shared by viewProjection() and by every framing function, so the distance
    /// a view is placed at is always computed from the FOV it actually renders
    /// with. Previously the two carried duplicate literals and could disagree.
    float fovDegrees() const;
    // Frame the head/brain region for the Fluoro view default, matching
    // the old project's frameOnFace() + parallel-scale zoom.
    void showFluoroDefault();

    /// Close frontal (AP) view of the head and cerebral vasculature -
    /// changed from the previous close sagittal (lateral, azimuth=90)
    /// framing per explicit request, since the console's whole default
    /// startup view (and every C-arm-driven Fluoro/DSA/Mesh view derived
    /// from it) needs to read as a standard frontal projection, not a
    /// head-turned side view.
    ///
    /// Trade-off worth knowing about: azimuth=180 (AP) is what every OTHER
    /// framing function in this file already uses, but it DOES superimpose
    /// the left and right cerebral circulations on top of each other -
    /// the earlier sagittal choice existed specifically so the circle of
    /// Willis, the carotid siphon and the vertebrobasilar system would
    /// separate rather than overlap. That anatomical clarity is genuinely
    /// traded away here for a standard-looking frontal default; a lateral
    /// view is still reachable on demand (nothing about the ORBIT itself
    /// changed, only what this specific framing sets it to).
    ///
    /// The crop is taken from the model: above Z = 1401 mm the tree narrows to
    /// +/-103 mm in X and holds two thirds of the mesh's vertices, which is the
    /// cerebral region proper.
    void showHeadFrontal();

    /// Animated pan of the camera's own target point along the body's Z
    /// axis toward `f` (0..1, 0=head end, 1=foot end - same convention
    /// CarmView::setBedBodyFraction() uses), so this view's own camera
    /// tracks the same repositioning the C-arm/table animation goes
    /// through when a sheath is placed, per explicit request that the two
    /// move the same way. Applied as an INCREMENTAL pan on top of
    /// whatever framing/zoom/rotation is already set (see
    /// onBodyFocusTick()'s own comment) - this nudges the existing view
    /// along Z, it does not re-frame or re-zoom outright, so it does not
    /// fight or discard whatever the operator already has set up.
    void setBodyFocusFraction(float f);

    /// Sets how many real seconds a FULL 0..1 body-focus pan should take -
    /// see m_bodyFocusSpeedFracPerSec's own comment for why this is set
    /// explicitly rather than hardcoded, and CarmView::
    /// bedFullTraversalSeconds() for the value a caller should actually
    /// pass in to keep this view's own pan visually synchronized with the
    /// table's real animation. No-op (keeps the previous speed) if
    /// `seconds` is not positive - a caller that has not yet loaded the
    /// C-arm parts (bedFullTraversalSeconds() returns 0 before that) should
    /// not accidentally set an infinite-speed pan.
    void setBodyFocusDurationSeconds(float seconds) {
        if (seconds > 1e-4f) m_bodyFocusSpeedFracPerSec = 1.f / seconds;
    }

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

    /// Seeds the follow-target/distance from the CURRENT static camera
    /// (m_target/m_distance) rather than the device's tip. Call this before
    /// handing a freshly-attached device to setCameraFollows(true) so that
    /// engaging follow mode does not itself move the camera - m_followTarget/
    /// m_followDistance default to zero and would otherwise snap the view to
    /// the origin (or wherever they were last left) the instant m_device
    /// stops being null. The device is still tracked normally from here on;
    /// only this one activation moment is a no-op.
    void beginFollowWithoutJump();

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
    /// Continue straight through a junction - the option whose takeoff
    /// angle deviates least from the current travel direction, not
    /// necessarily the leftmost or rightmost one on screen. The only way
    /// to reach a junction's middle option when it presents more than two
    /// candidates (e.g. the aortic bifurcation reached from femoral/
    /// popliteal access, typically degree-4 and presenting 3 options
    /// after excluding the direction the catheter came from) - Left/Right
    /// alone only ever reach the leftmost/rightmost screen-sorted option.
    void chooseStraightRequested();
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
    /// Snaps the follow/endoluminal camera to its current goal the instant
    /// this view becomes visible again (see the .cpp implementation's own
    /// comment for the exact bug this fixes: paintGL() - the only place
    /// updateFollowFrame()/updateEndoluminalFrame() ever run - is never
    /// called for a hidden QStackedWidget page, so a view left in follow
    /// mode while hidden has a stale m_followTarget/m_followDistance the
    /// moment it's shown again, and would otherwise visibly ease from that
    /// stale position toward the correct one over the next several frames
    /// instead of already being there).
    void showEvent(QShowEvent *e) override;

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

    /// The standalone, GL-independent piece of rebuildMeshBatch() - see
    /// ensureHeartCentreReady()'s own comment for why this needs to be
    /// callable on its own, separately from the full (GL-dependent) batch
    /// rebuild.
    void computeHeartCentre();
    void rebuildMeshBatch();
    void rebuildCenterlineBatch();
    void rebuildDeviceBatches();
    /// `isGuidewire` is an explicit parameter, not read from
    /// m_deviceIsGuidewire directly - this function also builds parked HOST
    /// devices (see rebuildDeviceBatches()), which can be a completely
    /// different device type from the currently active one; reading the
    /// class member unconditionally would have wrongly given a parked guide
    /// catheter a spring-coil tip whenever the ACTIVE device happened to be
    /// a wire. Host devices always pass false - a parked "host" in this
    /// console's own coaxial model is the outer device a wire/microcatheter
    /// was advanced THROUGH, essentially never a guidewire itself.
    void buildDeviceInto(const cn::CatheterModel *dev, DeviceBatches &b, bool isGuidewire,
                         float hideBehindDepth = 0.f);
    /// Replaces the plain smooth tube a guidewire's tip would otherwise get
    /// (nodes firstNode..lastNode of `dev`'s own chain, tip-first) with a
    /// tightly wound helical spring-coil mesh - a real guidewire's distal
    /// tip (0.035" diagnostic wires and 0.014" neuro wires alike) is a
    /// wound platinum coil, not a smooth tapered rod with a marker band
    /// painted onto it, and that wound-spring silhouette is the single
    /// most recognizable "this is a guidewire, not a catheter" visual cue
    /// on a real fluoroscopy frame. Only called when m_deviceIsGuidewire is
    /// true (see buildDeviceInto()); every other device keeps the plain
    /// tube unchanged. Resamples the (coarse - kTipEnd is only a handful of
    /// nodes) input polyline into many fine steps first, since a smooth-
    /// looking helix needs far more resolution along its own centreline
    /// than the underlying kinematic chain carries.
    void buildSpringTipMesh(const cn::CatheterModel *dev, int firstNode, int lastNode,
                            std::vector<float> &out, std::vector<uint32_t> &idx) const;
    /// One from/to graph-node pair with a 0..1 fill fraction along it -
    /// the common shape both EmbolizationModel::CastLink and
    /// ContrastInjectionModel::ContrastLink reduce to for rendering
    /// purposes, so one tube-builder can serve both instead of duplicating
    /// the frustum geometry code for each.
    struct CastSpan { int32_t from = -1, to = -1; float frac = 0.f; };
    /// Builds a tube mesh from graph geometry directly - one frustum per
    /// span, tapered between the two nodes' own lumen radii and capped at
    /// the fill fraction so a partially-filled span's leading edge visibly
    /// stops mid-vessel. Deliberately built from graph geometry (not from
    /// CatheterModel::buildTubeMesh, which is tied to a single ordered node
    /// chain) since either a cast or a contrast column can branch across
    /// several links at once, unlike a catheter's own single-file chain.
    ///
    /// `irregularity` (0 = perfectly smooth, the default) perturbs each
    /// ring vertex's radius by a deterministic, position-seeded amount -
    /// stable across frames (no flicker), but breaks up the perfectly
    /// circular, machine-smooth cross-section a plain tube always has.
    /// Left at 0 for contrast (a real liquid has no business looking
    /// lumpy); embolic material uses a nonzero value scaled by the
    /// agent's own viscosity, so a deposited cast reads as an irregular
    /// plug/accumulation rather than a manufactured-looking cylinder.
    void buildTubesFromSpans(const cn::CenterlineGraph *graph,
                             const std::vector<CastSpan> &spans, Batch &out,
                             float irregularity = 0.f);
    /// Core geometry-building logic factored out of buildTubesFromSpans() -
    /// appends into CALLER-PROVIDED vertex/index buffers instead of
    /// uploading its own batch, so buildEmboliInto() can accumulate several
    /// targets' spans (each with its own irregularity) into ONE combined
    /// buffer before uploading once. buildTubesFromSpans() itself is now a
    /// thin single-call wrapper over this for every other existing caller.
    void appendTubesFromSpans(const cn::CenterlineGraph *graph,
                              const std::vector<CastSpan> &spans, float irregularity,
                              std::vector<float> &verts, std::vector<uint32_t> &idx) const;
    /// Extracts spans from EVERY target the shared EmbolizationModel holds
    /// (not just whichever one is currently "active") and builds them all
    /// into one combined pair of batches - see appendTubesFromSpans()'s own
    /// comment for how multiple targets end up in one upload.
    void buildEmboliInto(const cn::EmbolizationModel *emb, Batch &outSet, Batch &outGrowing);
    /// Extracts spans from the shared CoilEmbolizationModel and builds
    /// them. Unlike buildEmboliInto() (a per-link tube reusing
    /// buildTubesFromSpans), this does NOT draw a filled tube along the
    /// vessel: a real coil mass sits as a single tangled, roughly
    /// spherical ball AT the deployment site, not a smooth column filling
    /// the lumen along its length. Iterates EVERY target the shared
    /// CoilEmbolizationModel holds (not just whichever is "active") and
    /// appends each one's own ball into one combined batch via
    /// appendCoilBallInto() - see that function's own comment.
    void buildCoilsInto(const cn::CoilEmbolizationModel *coils, Batch &out);
    /// Core per-target ball-building logic factored out of buildCoilsInto()
    /// so it can be called once per target, each with its OWN committed
    /// nest/staging/release position, accumulating into shared vertex/
    /// index buffers uploaded once by the caller. Builds a procedural
    /// "yarn ball" of wandering closed loops, one per deployed coil, sized
    /// from the local vessel calibre and how many coils have been placed.
    void appendCoilBallInto(const cn::CoilEmbolizationModel::State &state,
                            const std::vector<cn::CoilEmbolizationModel::CoiledLink> &stagingLinks,
                            bool hasPending, const cn::Vec3 &pendingPos,
                            const cn::CenterlineGraph &g,
                            std::vector<float> &verts, std::vector<uint32_t> &idx) const;
    /// Extracts spans from the shared ContrastInjectionModel and builds them
    /// into kContrastBuckets separate batches, one per concentration tier
    /// (ContrastInjectionModel::ContrastLink::level, already 0..1 per link -
    /// low = the advancing leading edge, high = an established column).
    /// Fluoro draws each bucket at a different brightness/opacity, which is
    /// what turns one flat grey block into a genuine tapered-intensity
    /// bolus; the 3D view draws every bucket with the SAME single colour it
    /// always used, so splitting the geometry this way changes nothing
    /// about its appearance.
    ///
    /// Raised from 5 to 24: five buckets meant five visible, discrete
    /// brightness STEPS along the column - real contrast never shows sharp
    /// bands, it fades continuously as it dilutes, and coarse banding reads
    /// as fake/artificial no matter how well the colour or grain on top of
    /// it is tuned. 24 steps across the 0..1 concentration range is well
    /// past where a human eye can distinguish one step from its neighbour,
    /// which is what actually turns the gradient smooth rather than merely
    /// less obviously stepped. Extra draw calls (24 vs. 5) are still cheap
    /// at this scale - nowhere near a real frame-budget concern.
    static constexpr int kContrastBuckets = 24;
    void buildContrastInto(const cn::ContrastInjectionModel *contrast,
                           Batch (&buckets)[kContrastBuckets]);
    void rebuildBodyMeshBatch(const QString &path, Batch &batch, const BodyAlign &align);

    /// Creates (or resizes) the offscreen accumulation buffer the Fluoro
    /// X-ray pass renders into, matching the widget's own resolution (see
    /// paintGL). Downscaling this buffer was tried as a source of
    /// softness, twice, and both times anatomy ended up harder to make out
    /// than intended — softness comes only from the widget-level
    /// QGraphicsBlurEffect (QuadPanel.cpp) now.
    void ensureXrayFbo(int w, int h);
    /// One big clip-space triangle, built once and reused every frame. Used
    /// only by the Fluoro X-ray pass's final compositing step (paintGL):
    /// drawn with uViewProj forced to identity so its vertices land
    /// directly in clip space instead of going through the scene camera,
    /// covering the whole viewport regardless of where that camera is.
    void ensureXrayQuad();

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
    /// See setDeviceIsMicrocatheter() - Fluoro colour only.
    bool m_deviceIsMicrocatheter = false;
    /// See setDeviceIsGuidewire()'s own comment.
    bool m_deviceIsGuidewire = false;
    /// See setDeviceColor() - 3D View only. Default matches
    /// CatheterSpec::colour's own default (0.36, 0.72, 0.94).
    QVector3D m_deviceColor{0.36f, 0.72f, 0.94f};
    /// See setDeviceHiddenBehindDepth() - active device shaft clipping only.
    float m_deviceHiddenBehindDepth = 0.f;
    /// See setDetectorInfo(). Defaults match CarmView's own defaults, so
    /// nothing looks different until MainWindow actually wires the real
    /// value through - see buildUi().
    float m_hudSidCm = 100.f;
    float m_hudMagCm = 32.f;
    int   m_hudFrameFps = 15;   ///< Same value Cine acquisition captures at - see setDetectorInfo()'s own comment.
    /// See setGeneratorInfo(). Defaults match DoseModel::Params' own
    /// baselines, so an early paint before the first real tick still shows
    /// a sensible value rather than 0.
    float m_hudKvp = 78.f;
    float m_hudMa  = 320.f;
    float m_hudDapGyCm2 = 0.f;   ///< genuinely starts at 0 - no dose has accumulated before the first tick
    const cn::EmbolizationModel *m_emboli = nullptr;
    // Split rather than one batch: lets the still-filling leading edge (the
    // active precipitation front) render with a different density than the
    // already-set trailing cast behind it - see buildEmboliInto()'s own
    // comment for why that distinction matters for "realistic release".
    Batch m_emboliBatchSet;      ///< fillFraction >= kEmboliSetThreshold - fully formed
    Batch m_emboliBatchGrowing;  ///< still actively filling - the growing tip
    bool  m_emboliDirty = true;
    const cn::CoilEmbolizationModel *m_coils = nullptr;
    Batch m_coilBatch;
    bool  m_coilsDirty = true;
    const cn::ContrastInjectionModel *m_contrast = nullptr;
    Batch m_contrastBatches[kContrastBuckets];
    bool  m_contrastDirty = true;
    Overlay m_overlay;

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    Batch m_meshBatch, m_centerlineBatch;
    Batch m_muscularBatch, m_skeletalBatch;   // body layer meshes (Muscular + Skeletal)
    Batch m_xrayQuadBatch;   // fullscreen triangle, Fluoro compositing pass
    /// Offscreen render target the Fluoro X-ray pass accumulates raw,
    /// unclamped attenuation into (R=muscle, G=bone, B=vessel), before a
    /// final pass reads it back, sums the channels, and applies the actual
    /// Beer-Lambert exponential exactly once. Floating-point format is what
    /// makes the "unclamped" part possible — a normal [0,1] framebuffer
    /// would clip a dense stack of overlapping anatomy long before this
    /// buffer ever does.
    std::unique_ptr<QOpenGLFramebufferObject> m_xrayFbo;
    XrayTuning m_xrayTuning;
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
    // Set by beginFollowWithoutJump(), consumed by the next showEvent() (or
    // self-clears on the next event-loop turn if no showEvent follows) - see
    // both functions' own comments.
    bool m_followTargetSeeded = false;

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
    float m_carmLAO = 0.f;   /// degrees, drives elevation offset (negated - see cameraPosition())
    float m_carmCRAN = 0.f;  /// degrees, drives azimuth offset
    /// Backs the Fluoroscopy HUD's TIME read-out (paintOverlay()) - started
    /// once in the constructor, so it is genuinely live elapsed time, not a
    /// static placeholder like the HUD's other fixed-default fields.
    QElapsedTimer m_fluoroClock;
    QVector3D m_target;
    QPoint m_lastMouse;

    // ── Animated body-focus pan ──────────────────────────────────────────
    // See setBodyFocusFraction()'s own comment. 0..1 along the SAME Z axis
    // the camera's own "long axis is Z" convention above already uses -
    // matches CarmView::setBedBodyFraction()'s identical 0=head/1=foot
    // convention exactly, so a caller can drive both with the same
    // fraction and have the table and this view's own camera move in
    // visual sync, per explicit request.
    //
    // No "current fraction" member here deliberately - onBodyFocusTick()
    // derives it fresh from m_target.z() every tick instead of tracking a
    // separate variable, which is what a real bug traced to an actual
    // recording turned out to be: a tracked fraction that started at 0
    // and was never synced to wherever the camera's OWN startup framing
    // (focusRibCage(), unrelated to this system) had actually left it,
    // producing a wildly wrong pan distance on the very first real call.
    float m_bodyFocusTargetFraction = 0.f;
    /// Fraction-per-second, not a real-world distance/speed - deliberately
    /// scale-independent (this view's own Z units and CarmView's separate
    /// scene units are completely different systems). Was a hardcoded
    /// guess (1/3.5, "assume CarmView's own full traversal takes about
    /// 3.5s") - confirmed wrong from an actual side-by-side comparison,
    /// the camera pan visibly finishing faster than the table. That guess
    /// and CarmView's OWN real, measured speed (kBedMoveSpeedMPerSec
    /// applied over its own real bed length) had no way to actually agree
    /// with each other, being two independent numbers. Now set explicitly
    /// via setBodyFocusDurationSeconds(), driven by CarmView::
    /// bedFullTraversalSeconds() - the two are mathematically guaranteed
    /// to move in sync since one is now DERIVED from the other's own real
    /// value, not separately guessed. Default here is only a fallback for
    /// before that setter is ever called.
    float m_bodyFocusSpeedFracPerSec = 1.f / 3.5f;
    QElapsedTimer m_bodyFocusClock;
    QTimer        m_bodyFocusTimer;   ///< only runs WHILE actually panning
    void onBodyFocusTick();

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

    /// Where the cerebral region starts, as a fraction of the model's Z span.
    /// 0.82 = Z 1401 mm on this model, above which the tree narrows to +/-103 mm.
    static constexpr float kHeadCropFraction = 0.82f;
    /// Tighter than kAnatomyFrameMargin: this framing is meant to be close.
    static constexpr float kHeadFrameMargin  = 1.06f;

    bool m_showWall = true, m_showCenterline = false;
    /// See setShowBranchMarkers() - off by default, since keyboard/arrow
    /// navigation needs no on-screen circle-and-crosshair prompt.
    bool m_showBranchMarkers = false;

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

    // ── Roadmap mask capture ──────────────────────────────────────────────
    RoadmapState m_roadmapState = RoadmapState::NoMask;
    /// The LIVE vessel's own opacity while roadmapping, driven entirely by
    /// captureRoadmapMask() rather than a fixed constant: near-invisible
    /// baseline (~0.04) whenever no capture is in progress, briefly raised
    /// to a visible level for the exact one frame being captured, then
    /// immediately restored - see captureRoadmapMask()'s own comment.
    float m_roadmapVesselOpacity = 0.04f;
    /// The captured mask frame, via QOpenGLWidget's own grabFramebuffer()
    /// (the same proven mechanism CineViewStudio already uses for frame
    /// capture). Drawn as a plain QPainter image at the very start of
    /// paintGL(), in its own fully self-contained QPainter session
    /// completed before any raw GL draw call begins - simpler and lower-
    /// risk than interleaving a custom textured-quad GL shader pass with
    /// the rest of this view's raw OpenGL rendering. Never re-rendered
    /// from the 3D mesh again until the next explicit capture.
    QImage m_roadmapMaskImage;
    void drawRoadmapMaskLayer();
    bool  m_dsaActive      = false;
    bool  m_wireframe     = false;
    // Body-layer placement. Identity by default: the assets are already
    // co-registered with the vessel, so there is nothing to fit.
    BodyAlign m_muscularAlign;
    BodyAlign m_skeletalAlign;
    bool  m_bodyAutoFit    = false;  // legacy per-axis bbox stretch, off
    float m_wallOpacity = 0.68f;
    /// See setFluoroQuality()'s own comment. Raised twice now (0.35 →
    /// 0.50 → 0.65): at the original low default, individual Skeletal.stl
    /// triangles — and each additional overlapping rib/vertebra crossing
    /// another one in projection — were large enough relative to a texel
    /// to read as visible blocky/voxel steps, and the compositing pass's
    /// own bone blur (mode 2, boneSmooth) couldn't manufacture resolution
    /// that was never captured in the first place; a blur can only soften
    /// blockiness that's already small enough for it to reach. 0.65 gives
    /// that blur enough real source detail to resolve into smooth curves
    /// instead of a smoothed staircase, while staying well short of the
    /// diagnostic-crisp full-resolution the 3D/Mesh/DSA views use — the
    /// Fluoro image is still deliberately softer than those.
    float m_fluoroQualityScale = 0.75f;
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