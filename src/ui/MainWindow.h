#pragma once
#include <QMainWindow>
#include <QStackedWidget>
#include <QElapsedTimer>
#include <memory>
#include <array>
#include <vector>
#include <QString>

#include "core/AccessSites.h"
#include "core/SheathManager.h"
#include "core/CatheterModel.h"
#include "core/CatheterCatalog.h"
#include "core/MechanicalEvents.h"
#include "core/CardiacCycle.h"
#include "core/DoseModel.h"
#include "core/CenterlineExtractor.h"
#include "core/CenterlineGraph.h"
#include "core/NavigationController.h"
#include "core/StlMesh.h"
#include "core/EmbolizationModel.h"
#include "core/CoilEmbolizationModel.h"
#include "core/ContrastInjectionModel.h"

#include "ui/VesselView.h"
#include "ui/CarmView.h"
#include "ui/CarmController.h"
#include "ui/QuadPanel.h"
#include "ui/CineViewStudio.h"
#include "ui/ECGWidget.h"

class QAction;
class QActionGroup;
class QCheckBox;
class QComboBox;
class QDialog;
class QImage;
class QDoubleSpinBox;
class QHBoxLayout;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QPlainTextEdit;
class QRadioButton;
class QSlider;
class QSpinBox;
class QTimer;
class QPushButton;
class QButtonGroup;

// ─────────────────────────────────────────────────────────────────────────────
// MainWindow — combined EmboNav console
//
//   ┌──────────────────────┬──────────────────────────────────────────────┐
//   │ File View C-arm Help │ Calibration Cine Studio 3D Fluro Mesh DSA … │
//   ├───────────────────┬──────────────────────────────────────────────────┤
//   │ C-arm OpenGL view │                                                  │
//   │  (CarmView)       │   QuadPanel                                     │
//   │  + angle HUD      │   (VesselView stack:                            │
//   │                   │    3D / Fluoro / Mesh / DSA)                    │
//   ├───────────────────┤                                                  │
//   │ ECG / ABP / Pleth │                                                  │
//   │  (ECGWidget)      ├──────────────────────────────────────────────────┤
//   │                   │ [Contrast][Road Map]                            │
//   │                   │ [Squid][Onyx][DMS][Glue][Gel]                   │
//   ├───────────────────┴──────────────────────────────────────────────────┤
//   │ Access & devices:  [Sheath Selection…] [Catheter Selection…]  …     │
//   └────────────────────────────────────────────────────────────────────────┘
//
// There is no right-hand Procedure dock any more. Access & devices is a
// single horizontal bar the full width of the window, along the bottom
// (buildUi). Calibration is a menu-bar action next to Help (buildViewBar)
// that opens the same controls in a small non-modal dialog (m_calibrationDialog)
// instead of a docked group box.
//
// Catheter navigation (Advance/Retract/Left/Right) has no panel of its own —
// it is driven by the I/K/J/L keyboard shortcuts. The Procedure log has no
// panel either; log() is a guarded no-op with nowhere to print to. The
// "Embolisation — Step N/7" banner under QuadPanel is gone too; m_emboLabel
// stays declared as nullptr and every setText() call site is already
// guarded with `if (m_emboLabel)`.
//
// The view-mode buttons live in the menu bar's right corner (buildViewBar), not
// in a strip between the panels; the middle column they used to occupy is gone.
//
// Two staged gates run the procedure. m_sheathReady is set when an access is
// established, m_catheterReady when a device is chosen through Catheter
// Selection; navigation unlocks only when both hold. Selecting a device that
// sits further inside the coaxial stack telescopes it through the one already
// placed rather than restarting at the groin.
// ─────────────────────────────────────────────────────────────────────────────
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    bool loadMesh(const QString &path);
    void tryAutoLoad();   ///< called by main() when no command-line path is given

private slots:
    // Only true Qt slots — connected via pointer-to-member, must match signal signature exactly.
    // Every other handler is a plain private method connected via lambda to avoid MOC mismatches.
    void onMotionTick();          // QTimer::timeout() — no args
    void onCineStudioBack();      // CineViewStudio::backToSimulatorRequested() — no args
    void onCameraChanged();       // VesselView::cameraChanged() — no args
    void onCardiacTick();         // QTimer::timeout() — advances the cardiac clock

private:
    // UI construction
    void buildUi();
    void buildMenus();
    /// Cine Studio + view-mode buttons, docked in the menu bar corner.
    void buildViewBar();

    // Pipeline
    void extractCenterline();
    void resolveAccessSites();
    /// Splices each resolved femoral entry a modest distance further down
    /// its own vessel - see the .cpp's own comment for the full reasoning.
    void nudgeFemoralEntriesDownward();
    void autoCalibrate();
    void loadBodyMeshes();

    // Navigation
    void restartFromCurrentSite(bool focusCamera);

    /// Fixed, reasoned C-arm bed-fraction for each access-site TYPE -
    /// confirmed correct by actual manual testing against the real model,
    /// replacing an earlier geometric approach that proved unreliable
    /// across all four site types. Shared between sheath placement and
    /// catheter selection (both reposition the C-arm/table to the site's
    /// own anatomical region), so there is exactly one place these numbers
    /// live rather than two copies that could drift apart if one is
    /// updated later and the other forgotten. 0.f (the head end) for any
    /// AccessSite not covered below - there should not be one, but this
    /// avoids an uninitialized read rather than relying on every call site
    /// to already validate its own input.
    float carmFractionForSite(cn::AccessSite site) const;
    void onVesselSurfacePicked(const QVector3D &worldPt);
    void updateSheathActor();
    void syncDevices();
    void pushOverlay();
    void updateReadouts();

    // Event handlers (plain methods, connected via lambda — MOC does not touch these)
    void onOpenFile();
    void onAccessSiteChosen(int index);
    void onFocusAccess();
    void onShowWholeVessel();
    void onFrontalLockToggled(bool on);
    void onCameraFollowToggled(bool on);
    void onViewModeChanged(int id);
    void onPaletteChanged(int index);
    void onOpacityChanged(int percent);
    void onDeviceChanged(int index);
    void onDeviceSizeChanged(double odMm);
    void onScaleChanged(double mmPerUnit);
    void onHeartRateChanged(int bpm);
    void onBalloonToggled();
    void onAdvance();
    void onRetract();
    void onChooseLeft();
    void onChooseRight();
    void onChooseStraight();
    /// Rotate the catheter about its own axis — the missing operator input
    /// this conversation identified: setTipDeflection()/setRollAngle() were
    /// never called from anywhere in this file, so the sophisticated tip-
    /// articulation model in CatheterModel had no way to be driven by the
    /// operator at all. The device's orientation was previously determined
    /// entirely by its fixed resting shape and the path it happened to be
    /// on, with no steering input — which is a concrete, confirmed reason
    /// navigation could read as "on autopilot" rather than operator-driven.
    void onRollLeft();
    void onRollRight();
    void onCarmLAO();
    void onCarmRAO();
    void onCarmCRAN();
    void onCarmCAUD();
    void onCarmReset();
    /// Log which key drove a C-arm rotation and the resulting angles.
    void logCarm(const char *key, const char *what);
    void onCarmAnglesChanged(double lao, double cran);
    void onSheathSelection();
    /// Builds and shows the patient-selection dialog: case list on the
    /// left, module categories on the right. See the .cpp's own comment
    /// for the full layout description.
    void showPatientSelectionDialog();
    void onCatheterSelection();
    void onRoadMap();
    void onDSAView();
    void onDeployEmbolic(const QString &agent);
    void onOpenCineStudio();

    // Device spec dialog helpers
    void applyCarmAngles();
    void showInsertionDialog();
    void showCatheterSelectionMenu();
    void showDeviceSpecDialog(const QString &stageName);
    /// Load a catalogue device by its display name and make it the live
    /// catheter. Returns false when the name is not in the catalogue.
    ///
    /// `tipShapeName` only applies to the diagnostic-catheter body
    /// ("Terumo Guide", DeviceClass::BaseGuide) — see
    /// CatheterCatalog::TipShapeDef. It is ignored for every other device,
    /// which carries its own fixed shape in the catalogue as before.
    bool applyCatheterSpec(const QString &displayName, const QString &sizeLabel = QString(),
                           const QString &tipShapeName = QString());
    /// Navigation is enabled only when there is both an access and a device.
    void updateDeviceGating();
    /// Clears the catheter unconditionally (device detached from every
    /// view, m_catheterReady/m_ready both false) - a stale catheter must
    /// never carry over to a sheath it was not explicitly attached to.
    /// When alsoResetSheath is true, also clears the sheath itself
    /// (m_sheathReady false, sheath geometry removed from every view) -
    /// used when the patient/case changes, not on an ordinary new sheath
    /// placement, which sets m_sheathReady true right after.
    void resetDeviceState(bool alsoResetSheath);
    /// Tick a display-mode action without re-triggering it.
    void checkViewAction(int id);
    /// Sets the "active" dynamic property on a nav tile and re-polishes it
    /// so the QSS [active="true"] rule takes effect immediately - see that
    /// rule's own comment in the stylesheet for why this replaced relying
    /// on setChecked()/the :checked pseudo-state for the visual border.
    void setNavTileActive(QPushButton *tile, bool active);
    /// Depth in the coaxial stack; larger is further inside the patient.
    static int deviceStackLevel(cn::DeviceClass c);

    // ── Per-device pause/deactivate ──────────────────────────────────────
    // Matches the manual's own "Active guide catheter"/"Active catheter"/
    // "Active guide wire" toggles: lets a trainee freeze one device once
    // it is positioned and focus purely on the next step (e.g. park the
    // guide catheter at the ECA, then work only on the microcatheter
    // without risking accidentally moving the guide). Three explicit slots
    // (not a generic per-device-class map) - a real procedure genuinely
    // only ever has these three device ROLES in play at once, matching
    // the manual's own three-field device display (Figure 1.3.a).
    bool m_pauseGuideCatheter = false;
    bool m_pauseMicroCatheter = false;
    bool m_pauseWire          = false;
    QCheckBox *m_activeGuideCatheterBox = nullptr;
    QCheckBox *m_activeMicroCatheterBox = nullptr;
    QCheckBox *m_activeWireBox          = nullptr;
    QPushButton *m_btnSavePosition = nullptr;
    QPushButton *m_btnLoadPosition = nullptr;
    /// Which pause flag applies to the CURRENTLY selected device, based on
    /// m_deviceSpec.catheterType's own catalogue entry - or nullptr if the
    /// current device does not map to any of the three slots (e.g. a
    /// sheath, which is not one of the three pausable roles).
    bool *activeDevicePauseFlag();
    void onToggleDevicePause(cn::DeviceClass slot);
    void updateDevicePauseUi();

    // ── Save/load device position ────────────────────────────────────────
    // One device's own saved state - either a parked one or the active
    // one. Both need exactly the same fields (identity + exact route +
    // depth + roll), so this is shared between SavedDevicePosition's
    // `parkedStack` and `active` below rather than duplicating the fields
    // in two places.
    struct SavedDeviceState {
        QString deviceName;        ///< catalogue name, e.g. "Sofia 6F (distal access)"
        QString deviceSizeLabel;   ///< e.g. "6 Fr"
        /// Only meaningful for DeviceClass::BaseGuide - the diagnostic
        /// catheter's heat-set tip shape active at save time. Empty for
        /// every other device class.
        QString tipShapeName;
        float insertionDepth = 0.f;
        float rollAngle = 0.f;
        /// Exact node-id sequence from cn::NavigationController::route() -
        /// replayed via replayRouteIntoNav() to restore the precise path
        /// taken, not just a depth along an assumed-default route.
        std::vector<int32_t> route;
    };
    struct SavedDevicePosition {
        QString name;
        /// Every PARKED device in the coaxial stack at save time, outermost
        /// (introduced first - normally the guide catheter) to innermost,
        /// same order m_parkedDevices itself already uses. A save made
        /// with a microcatheter active through a parked guide catheter
        /// captures BOTH devices here - without this, loading such a save
        /// silently dropped the guide catheter entirely and left only the
        /// microcatheter's own position restored, with no host device
        /// underneath it at all.
        std::vector<SavedDeviceState> parkedStack;
        /// The device actually engaged at save time - the one Advance/
        /// Retract drove then, and the one they drive again immediately
        /// after this position is loaded.
        SavedDeviceState active;
        /// Reserved for a future "Init State" (auto-load on new case) -
        /// not wired to any UI yet in this pass; named Save/Load already
        /// delivers the core training value (repeat drills from a fixed
        /// point) without it. Kept declared rather than added later, the
        /// same "documents the intended extension point" reasoning other
        /// currently-unused fields elsewhere in this codebase already use.
        bool isInitState = false;
    };
    QVector<SavedDevicePosition> m_savedPositions;
    void onSavePosition();
    void onLoadPosition();
    /// Actually captures/restores state - split out from the two dialog-
    /// driving slots above so a future "auto-load Init State on new case"
    /// path can call restoreSavedPosition() directly without going through
    /// a menu.
    SavedDevicePosition captureSavedPosition(const QString &name) const;
    void restoreSavedPosition(const SavedDevicePosition &pos);
    /// Replays `route` (a saved node-id sequence) into m_nav via its own
    /// public API (startFrom()+chooseOption()), then advances to `depth`.
    /// Shared by restoreSavedPosition() (for every device in a saved
    /// stack) and applyCatheterSpec()'s own "reverting to a shallower
    /// device" branch - previously each had its own separately-written
    /// copy of this exact replay loop, which is exactly the kind of
    /// duplication that lets one copy get fixed and the other not.
    /// Returns false (not a fatal error - the caller decides what that
    /// means) if the saved route stopped matching available options
    /// partway through, e.g. because the vessel model changed since it
    /// was recorded.
    bool replayRouteIntoNav(const std::vector<int32_t> &route, float depth);
    /// Builds CatheterModel::Params for `st`'s own catalogue entry (device
    /// name + size +, for a BaseGuide, its tip shape) - the same lookup
    /// applyCatheterSpec() does for a live selection, factored out so
    /// restoreSavedPosition() can rebuild a device's exact physical
    /// parameters without going through applyCatheterSpec()'s own
    /// telescoping/view-sync side effects, which assume a fresh live
    /// selection rather than a saved stack being replayed wholesale.
    /// Returns false if `st.deviceName` is no longer in the catalogue.
    bool buildParamsForState(const SavedDeviceState &st, cn::CatheterModel::Params &outParams,
                             const cn::CatheterSpec *&outSpec) const;

    // ── Angle exercise minigame ───────────────────────────────────────────
    // Matches the manual's own angle-exercise training function: a
    // reference projection is shown, the trainee has to find and set the
    // matching LAO/CRAN on the live C-arm, then check their answer. This
    // build has no archive of real recorded case images to freeze a
    // reference from (the manual's own version pulls from a patient case
    // library this simulator does not have) - instead, a RANDOM target
    // angle is picked and the reference "image" is a genuine live capture
    // of this session's own vessel model rendered at that angle, which is
    // actually a more honest reference than a canned image would be: the
    // trainee is learning to read THIS patient's own anatomy at an unknown
    // angle, the same task a real angle exercise is teaching, just sourced
    // from the live model instead of an archive.
    struct AngleExerciseState {
        bool active = false;
        double targetLao = 0.0;
        double targetCran = 0.0;
        /// Session-level tally, not reset between exercises - shown in the
        /// dialog so a trainee can see genuine progress across a training
        /// session rather than a single isolated check-and-reveal with no
        /// memory of anything before it.
        int attemptCount = 0;
        int passCount = 0;
        /// Whether the CURRENT exercise has already been counted as a pass
        /// - Check Angle can be clicked more than once while adjusting
        /// toward the target, and a repeated correct check on the SAME
        /// exercise must not inflate passCount a second time.
        bool currentPassed = false;
    };
    AngleExerciseState m_angleExercise;
    QDialog *m_angleExerciseDialog       = nullptr;
    QLabel  *m_angleExerciseImageLabel   = nullptr;
    QLabel  *m_angleExerciseResultLabel  = nullptr;
    QLabel  *m_angleExerciseScoreLabel   = nullptr;
    void onStartAngleExercise();
    void showAngleExerciseDialog(const QImage &snapshot);
    void onCheckAngleExercise();
    void onShowCorrectAngle();
    void updateAngleExerciseScoreLabel();
    /// The X-Ray Calibration dialog's 28 spin boxes (m_xraySpins) and
    /// VesselView::XrayTuning's 28 fields, converted between each other
    /// positionally — one fixed order, used by "Save / Apply" (spins ->
    /// struct), "Reset to defaults" and reopening the dialog (struct ->
    /// spins), so all three always agree on which value is which.
    static std::array<double, 28> xrayTuningToArray(const VesselView::XrayTuning &t);
    static VesselView::XrayTuning xrayArrayToTuning(const std::array<double, 28> &v);
    /// nav.advance() with the coaxial floor applied.
    float advanceDevice(float ds);
    /// Resize the loaded catheter from the Calibration French dropdown.
    void onCatheterFrenchChanged(int index);
    void log(const QString &line);
    QString mm(float modelUnits, int decimals = 1) const;
    /// Dumps m_catheter.mechanics() to the log, tagged with `context` (e.g.
    /// "advance", "branch", "device switch") - a FULL state dump, called
    /// unconditionally at a few discrete, already-rare events (device
    /// switch, branch choice, position restore) regardless of whether the
    /// mechanical regime actually changed. For the continuous case
    /// (advance/retract/rotate), see reportMechanicalEvents() instead -
    /// that one only ever logs on an actual transition.
    void logMechanicalState(const QString &context);
    /// Feeds m_catheter.mechanics() through m_mechEvents and logs a short
    /// line for each MechanicalEvent it reports (usually none - see
    /// MechanicalEventDetector's own comment on why this does not flood
    /// the log). This is the single call site for the training-event
    /// feed: syncDevices() calls it once, so advance/retract/branch/device
    /// switch/position-restore all go through it automatically; rotate
    /// calls it directly since it does not go through syncDevices(). Also
    /// refreshes the (normally hidden) mechanics debug overlay - see
    /// toggleMechanicsDebugOverlay().
    void reportMechanicalEvents(const QString &context);
    /// Ctrl+Shift+M - shows/hides a small always-on-top diagnostic window
    /// (regime, contact, the three resistances, support/exposure, force,
    /// torsion). Entirely separate from the production HUD/Overlay VesselView
    /// already draws: this is a standalone floating QLabel, off by default,
    /// and deleting this method plus m_mechDebugWindow removes the hook
    /// completely with no effect on anything else.
    void toggleMechanicsDebugOverlay();
    void updateMechanicsDebugOverlay();

    // ── Embolisation Module ──────────────────────────────────────────────
    void showEmbolizationDialog();
    void onBeginEmbolization();
    void onInjectEmbolic();
    void onResetEmbolization();
    void updateEmboliReadout();
    void refreshEmboliViews();
    // ── Coil deployment: Advance pushes AND commits a coil in one call,
    // Retract undoes the most recently committed one. ─────────────────────
    void onAdvanceCoil();
    void onRetractCoil();
    void onResetCoils();
    void updateCoilReadout();
    /// The graph neighbor of `tipNode` that lies back toward the access
    /// site — i.e. the vessel segment the catheter's own body currently
    /// occupies, immediately proximal to its tip. Passed to
    /// EmbolizationModel::begin() as the segment to exclude from the flood-
    /// fill, so the cast cannot reflux backward through the catheter's own
    /// shaft into the (usually wider, lower-resistance) parent vessel
    /// instead of advancing into the actual target branches. Returns -1 if
    /// no access site is resolved or tipNode has no such neighbor.
    int32_t findProximalNeighbor(int32_t tipNode) const;

    // ── Contrast Injection — real 3-phase procedural simulation ──────────
    // Replaces the previous log-only stub (showContrastControlDialog /
    // onContrastInjection) entirely - that path never drove any simulation
    // at all, only a combo-box choice logged to text, so there is nothing
    // of substance being removed here, only superseded.
    void showContrastInjectionDialog();
    void onBeginContrastPhase(cn::ContrastPhase phase);
    void onInjectContrast();
    /// Fully clears m_contrastModel (phase back to Idle) and every view's
    /// rendered contrast batch - previously there was no way to do this
    /// from the UI at all; only beginning a fresh phase implicitly reset
    /// the model, with no explicit "remove everything" action available.
    void onResetContrast();
    void onContrastTick();
    void updateContrastReadout();
    void refreshContrastViews();

    // ── Geometry / navigation core ──────────────────────────────────────────
    cn::StlMesh                     m_mesh;
    cn::CenterlineGraph             m_graph;
    cn::CenterlineExtractor::Report m_report;
    cn::NavigationController        m_nav;
    cn::CatheterModel               m_catheter;
    /// Embolisation Module's shared cast-growth model — one instance, handed
    /// to every VesselView exactly the way m_catheter already is.
    cn::EmbolizationModel           m_emboli;
    /// Discrete coil deployment - deliberately a separate model from
    /// m_emboli, not folded into it: see CoilEmbolizationModel's own class
    /// comment for why a coil nest and a liquid-agent flood fill are
    /// mechanistically different things. Wired into m_contrastModel
    /// alongside m_emboli so either one - or both together, proximal
    /// protection coil + distal liquid agent is a real MMA technique -
    /// can block a post-embolization angiogram.
    cn::CoilEmbolizationModel       m_coils;
    /// Contrast Injection's shared temporary-flow model. Wired to m_emboli
    /// and m_coils (setEmbolization/setCoils) so it can see - and be
    /// genuinely blocked by - whatever either embolic model currently
    /// occludes.
    cn::ContrastInjectionModel      m_contrastModel;
    std::vector<cn::AccessPoint>    m_sites;
    std::vector<int32_t>             m_vtpLeafNodes;
    cn::SheathManager                m_sheath;
    int    m_activeSite   = -1;
    bool   m_ready        = false;
    /// Sheath and catheter are separate stages. Placing the sheath establishes
    /// the access and the route; the catheter is only introduced when one is
    /// chosen, so navigation stays locked until both are true.
    bool   m_sheathReady   = false;
    bool   m_catheterReady = false;
    QString m_activeCatheter;
    QString m_activeCatheterSize;
    /// Which named tip shape (Angled taper/VERT, SIM1-3, Newt, CK1, H1) is
    /// baked onto the diagnostic-catheter body, when that is the active
    /// device. Empty for every other device class.
    QString m_activeTipShape;
    const cn::CatheterSpec *m_activeSpec = nullptr;
    /// Devices already placed and left in the patient, outermost first. A
    /// microcatheter telescoped through a guide does not make the guide vanish.
    std::vector<std::unique_ptr<cn::CatheterModel>> m_parkedDevices;
    /// Parallel to m_parkedDevices, same index - the catalogue identity
    /// (name/size/tip-shape) of each parked device, which a raw
    /// CatheterModel snapshot does not carry on its own. Needed so
    /// reverting to a shallower device (see applyCatheterSpec()'s own
    /// "reverting" branch) can call applyCatheterSpec() again on the
    /// correct parked device instead of leaving whatever deeper device
    /// superseded it still selected.
    QVector<QString> m_parkedDeviceNames;
    QVector<QString> m_parkedDeviceSizes;
    QVector<QString> m_parkedDeviceTipShapes;
    /// Insertion depth the active device was introduced at; it cannot retract
    /// behind this, because that is inside the host catheter's lumen.
    float m_coaxialBaseDepth = 0.f;
    double m_mmPerUnit    = 100.0;
    double m_pendingMm    = 0.0;
    bool   m_wasInContact = false;
    /// Watches m_catheter.mechanics() for state TRANSITIONS (see
    /// MechanicalEventDetector) - the training-event feed. reset() at
    /// every device switch/position restore so a fresh device's own
    /// starting regime is never misreported as an event.
    cn::MechanicalEventDetector m_mechEvents;
    /// See toggleMechanicsDebugOverlay() - null until first shown.
    QLabel *m_mechDebugWindow = nullptr;

    // ── State ───────────────────────────────────────────────────────────────
    double  m_carmLAO  = 0.0;
    double  m_carmCRAN = 0.0;
    bool    m_roadMapActive = false;
    bool    m_dsaViewActive = false;
    QString m_bodyMeshDir;

    // Device spec (from dialog — informational, drives log)
    struct DeviceSpec {
        QString catheterType;
        QString catheterSizeFr;
        QString coilType;
        QString wireTipShape = "Straight";
    } m_deviceSpec;

    // ── Page stack ──────────────────────────────────────────────────────────
    QStackedWidget *m_pageStack     = nullptr;
    /// [left vertical nav sidebar][m_pageStack] - see buildUi()'s own
    /// comment for why the nav sidebar has to live here, as a persistent
    /// sibling of the page stack, rather than inside just one page.
    QHBoxLayout *m_contentRow = nullptr;
    QWidget        *m_simulatorPage = nullptr;
    CineViewStudio *m_cineStudio    = nullptr;

    // ── Views ───────────────────────────────────────────────────────────────
    CarmView        *m_carmWidget = nullptr;   // left column — standalone CarmView
    CarmController  *m_carmCtrl  = nullptr;   // owns rig state, routes camera increments
    // Anchor for the table's per-tick real-world tracking in syncDevices():
    // the validated carmFractionForSite() value and the vessel-graph world
    // Z it was set at, captured at sheath placement. syncDevices() tracks
    // the table as this anchor plus the REAL distance (via m_mmPerUnit)
    // the tip has since travelled, as a fraction of the table's own real
    // length (CarmView::bedSpanMetres()) - not an absolute fraction of the
    // whole vessel graph's own bounding box, which has no necessary
    // relationship to the table's real length or to Vessel View's own
    // (unnormalized, real-world) tip tracking. Anchoring to the tuned
    // fraction rather than recomputing one from scratch also avoids a jump
    // away from the validated rest position on the very first advance tick.
    float m_carmTrackBaseFraction = 0.f;
    float m_carmTrackBaseZ        = 0.f;
    QuadPanel  *m_quad       = nullptr;   // right centre — stacked vessel views
    VesselView *m_view       = nullptr;   // alias → m_quad->view3D()
    ECGWidget  *m_ecgWidget  = nullptr;

    // ── Bottom tile navigation bar (replaces the old menu-bar view actions) ──
    // 3D VIEW / FLUORO / MESH / DSA are mutually exclusive (m_viewTileGroup).
    // Mesh (index 2) was previously removed to match a reference design that
    // had no Mesh tile, then re-added per explicit request - the underlying
    // wireframe rendering (VesselView::setWireframe(), QuadPanel::
    // showPane(Pane::Mesh)) and onViewModeChanged(2)/checkViewAction(2) were
    // deliberately left working the whole time specifically so restoring
    // access only ever needed the tile itself, nothing else.
    // Road Map / Cine / Contrast / Embolisation are independent action tiles,
    // not part of the exclusive group.
    QButtonGroup *m_viewTileGroup = nullptr;
    QPushButton  *m_viewTiles[4]  = {nullptr, nullptr, nullptr, nullptr};
    QPushButton  *m_roadMapTile   = nullptr;
    QPushButton  *m_cineTile      = nullptr;
    QPushButton  *m_hamburgerBtn  = nullptr;

    // ── Access site radio buttons ───────────────────────────────────────────
    std::vector<QRadioButton *> m_siteButtons;

    // ── Calibration widgets — live in m_calibrationDialog, opened from the
    // menu bar (buildViewBar) rather than a right-hand dock. Same widgets,
    // same connections; only the container changed.
    QDialog        *m_calibrationDialog = nullptr;
    QDialog        *m_xrayTuningDialog  = nullptr;
    /// Embolisation Module dialog — same non-modal, built-once-in-buildUi(),
    /// shown/raised/activated-from-the-menu pattern as the two dialogs above.
    /// Kept as the "advanced" path (full description text, explicit Reset)
    /// alongside the new embedded quick-inject sidebar panel below - both
    /// drive the exact same shared m_emboli, so neither can fall out of sync
    /// with the other.
    QDialog        *m_emboDialog       = nullptr;
    QComboBox      *m_emboAgentBox     = nullptr;
    QLabel         *m_emboStatusLabel  = nullptr;
    QPushButton    *m_emboInjectBtn    = nullptr;
    QPushButton    *m_emboResetBtn     = nullptr;
    /// Continuous injection state - onInjectEmbolic() toggles this rather
    /// than injecting one large discrete step; while true, onContrastTick()
    /// (shared with the contrast timer - see that member's own comment)
    /// injects a small, speed-scaled amount every 150 ms, giving a visibly
    /// gradual, continuously-advancing front instead of an instant jump.
    /// Stops automatically once volumeInjected reaches m_emboVolumeSpin's
    /// value, or immediately if the operator presses the button again.
    bool             m_emboliInjecting = false;
    QDoubleSpinBox  *m_emboSpeedSpin   = nullptr;   ///< rate multiplier, 0.5-3.0x
    QDoubleSpinBox  *m_emboVolumeSpin  = nullptr;   ///< target total volume for this injection
    /// How much total volume the current continuous injection should reach
    /// before auto-stopping - shared between the dialog's own toggle (which
    /// sets this from m_emboVolumeSpin) and the quick sidebar panel's INJECT
    /// button (which adds to it), so both feed the same gradual, ongoing
    /// injection instead of each doing its own separate instant jump.
    float            m_emboliTargetVolume = 0.f;

    /// Embedded "Embolic Agent" quick-inject panel, built directly into the
    /// Procedure sidebar per the reference design (agent dropdown, a
    /// volume readout with +/- steppers, one big INJECT button) rather than
    /// requiring the Embolisation Module dialog to be opened for the common
    /// case. onQuickEmboInject() begins the cast at the catheter tip on its
    /// first press if not already active, then injects on every press after.
    QComboBox   *m_quickEmboAgentBox = nullptr;
    QLabel      *m_quickEmboVolumeLabel = nullptr;
    QPushButton *m_quickEmboInjectBtn = nullptr;
    /// Displayed volume in the quick panel's own arbitrary mL-like unit
    /// (this console has no real fluid-volume model - see
    /// EmbolizationModel::inject()'s own unit note); scaled to the model's
    /// injection step when INJECT is actually pressed.
    float m_quickEmboVolumeMl = 0.10f;

    /// Coil section of the Embolisation Module dialog (m_emboDialog) - see
    /// showEmbolizationDialog(). Simplified to two actions per explicit
    /// request: Advance pushes a whole coil out AND commits it immediately
    /// (CoilEmbolizationModel::deployWholeCoil()), Retract undoes the most
    /// recently committed one (CoilEmbolizationModel::undoLastCoil()) - no
    /// separate review-before-commit step in between. m_coilRetractBtn is
    /// only enabled while there is a committed coil to undo - see
    /// updateCoilReadout(), which manages both buttons' enabled state
    /// together every time coil state changes.
    QComboBox   *m_coilBox         = nullptr;
    QLabel      *m_coilStatusLabel = nullptr;
    QPushButton *m_coilAdvanceBtn  = nullptr;
    QPushButton *m_coilRetractBtn  = nullptr;

    /// Contrast Injection dialog — same non-modal pattern. No agent combo of
    /// its own: the three phase buttons themselves are the workflow.
    QDialog        *m_contrastDialog      = nullptr;
    QLabel         *m_contrastStatusLabel = nullptr;
    QPushButton    *m_contrastInjectBtn   = nullptr;
    QPushButton    *m_contrastResetBtn    = nullptr;
    /// Continuous injection state - same pattern as m_emboliInjecting
    /// above: onInjectContrast() toggles this instead of injecting one
    /// large discrete step, and onContrastTick() injects a small,
    /// speed-scaled amount every tick while true.
    bool             m_contrastInjecting  = false;
    QDoubleSpinBox  *m_contrastSpeedSpin  = nullptr;   ///< rate multiplier, 0.5-3.0x
    QDoubleSpinBox  *m_contrastVolumeSpin = nullptr;   ///< target total volume for this injection
    /// Ticks m_contrastModel forward in real time (decay/washout) whether or
    /// not the operator is doing anything - contrast has to clear on its own
    /// even with the dialog just sitting idle, the same way it would in a
    /// real vessel. Runs for the lifetime of the app; tick() itself is a
    /// cheap no-op while phase() is Idle, so there is no meaningful cost to
    /// leaving it running rather than starting/stopping it per phase.
    QTimer         *m_contrastTimer       = nullptr;
    /// The 28 spin boxes in m_xrayTuningDialog, in the same fixed order as
    /// xrayTuningToArray/xrayArrayToTuning. A member (not a local in
    /// buildUi()) specifically so buildViewBar()'s menu action can refresh
    /// their displayed values from the Fluoro view's current XrayTuning
    /// every time the dialog is reopened — showing whatever was last
    /// actually applied, not a stale or discarded in-progress edit.
    std::vector<QDoubleSpinBox *> m_xraySpins;
    QDoubleSpinBox *m_odSpin        = nullptr;
    QDoubleSpinBox *m_stepSpin      = nullptr;
    QDoubleSpinBox *m_speedSpin     = nullptr;
    QDoubleSpinBox *m_scaleSpin     = nullptr;
    QSlider        *m_opacitySlider  = nullptr;
    QCheckBox      *m_autoCalibrate = nullptr;
    QPlainTextEdit *m_log           = nullptr;

    // Catheter navigation (Advance/Retract/Left/Right) has no on-screen
    // buttons — it is driven only by the I/K/J/L keyboard shortcuts wired in
    // buildUi(), which call the same onAdvance()/onRetract()/onChooseLeft()/
    // onChooseRight() handlers the buttons used to.
    // Access & devices — a horizontal bar along the bottom of the whole
    // window (buildUi), not a boxed group in a side dock.
    QPushButton *m_btnSheathSelect   = nullptr;
    QPushButton *m_btnCatheterSelect = nullptr;
    QLabel      *m_fSheath   = nullptr;
    QLabel      *m_fCatheter = nullptr;

    // Select Patient — opens showPatientSelectionDialog(): case list on the
    // left, module categories on the right, matching the reference design.
    // Only one actual vessel dataset exists in this project, so picking a
    // case tracks which one is "active" (shown in the header's own
    // PROCEDURE field, kept in sync across every view) rather than loading
    // different anatomy per case - see onPatientCaseSelected()'s own
    // comment for the full explanation.
    QPushButton *m_btnSelectPatient  = nullptr;
    QLabel      *m_activeModuleLabel = nullptr;   // shown in the header bar
    QString      m_activeModuleCategory;
    QString      m_activeModuleName;
    QString      m_activePatientCaseId;

    // Status / embo labels (in vessel panel area)
    QLabel *m_emboLabel   = nullptr;
    QLabel *m_statusLabel = nullptr;

    // Angle strip labels (below CarmView)
    QLabel *m_laoLabel  = nullptr;
    QLabel *m_cranLabel = nullptr;

    // Device status readouts
    QLabel *m_fDepth     = nullptr;
    QLabel *m_fRemaining = nullptr;
    QLabel *m_fLumen     = nullptr;
    QLabel *m_fDevice    = nullptr;
    QLabel *m_fClear     = nullptr;
    QLabel *m_fBranch    = nullptr;
    QLabel *m_fTurns     = nullptr;
    QLabel *m_fStatus    = nullptr;   ///< no longer shown; updateReadouts guards it
    /// New for the right-side "Procedure Info" card (buildUi()) - access site
    /// name has never had a dedicated label before now.
    QLabel *m_fAccess = nullptr;
    /// "Device Status" card rows - each a single QLabel with an inline
    /// coloured bullet + status word (rich text), rather than a separate dot
    /// widget per row, updated from updateReadouts().
    QLabel *m_deviceStatusSheath   = nullptr;
    QLabel *m_deviceStatusCatheter = nullptr;
    QLabel *m_deviceStatusEmbolic  = nullptr;

    // Device area — the loaded catheter's model name plus a quick working-size
    // selector. The full per-size dimensions live in the Catheter Selection
    // dialog; duplicating them here made the panel long without adding control.
    QLabel    *m_fDevModel = nullptr;
    QComboBox *m_frCombo   = nullptr;   ///< quick working size, 1-3 Fr, default 2 Fr

    // Actions
    QAction *m_balloonAction = nullptr;
    QAction *m_centerlineAction = nullptr;

    // ── Cardiac clock ───────────────────────────────────────────────────────
    // Shared by the ECG trace and the pulsating vessel wall, so both are views
    // of the same beat rather than two independent animations.
    cn::CardiacCycle m_cardiac;
    QTimer          *m_heart = nullptr;
    QElapsedTimer    m_heartClock;

    // ── Dose/generator model ─────────────────────────────────────────────
    // Replaces the previously fixed console-default kVp/mA/DAP/air-kerma/
    // patient-physician-dose display with a genuinely time-integrated one -
    // see DoseModel's own class comment. Ticked once a second from the same
    // procedure timer that already refreshes TOTAL FLUORO TIME, using
    // CarmView's own live fluoroActive()/magnification()/lao()/cran().
    cn::DoseModel m_dose;
    QSpinBox        *m_bpmSpin    = nullptr;
    QCheckBox       *m_pulseCheck = nullptr;

    // Timers
    QTimer *m_motion = nullptr;
    int     m_cineRunCounter = 0;

    // ── Cine acquisition — real continuous recording, not a single captured
    // frame on opening Cine Studio ─────────────────────────────────────────
    // Starts automatically the first time a Pre-Embolization Angiogram
    // begins (the natural start of an actual imaging run) and keeps
    // capturing from the FLUORO view specifically (not the 3D view) at a
    // clinically-plausible frame rate for the rest of the session, so
    // whatever the operator does next - catheter movement, the Flush phase,
    // embolic delivery, the Post-Embolization Angiogram - is genuinely
    // present in the recorded sequence in the order it actually happened.
    // Ended explicitly on End Procedure.
    QTimer *m_cineCaptureTimer = nullptr;
    bool    m_cineRecording    = false;
    void startCineRecordingIfNeeded();
    void stopCineRecording();
    void onCineCaptureTick();

    // ── Top header bar ────────────────────────────────────────────────────
    // Patient/ID/Age-Sex/Procedure fields are fixed demo values - this
    // console has no patient-record system to pull them from, unlike
    // Procedure Time, which is a genuine live elapsed timer (m_procedureClock
    // via m_procedureTimer), and unlike every other field in this file's
    // Procedure Info / Device Status cards, which are all real app state.
    QLabel  *m_headerProcedureTime = nullptr;
    QTimer  *m_procedureTimer = nullptr;
    QElapsedTimer m_procedureClock;

    // ── Bottom system status bar (QMainWindow::statusBar()) ─────────────────
    // DOSE and AIR KERMA are now genuinely live - driven by the shared
    // cn::DoseModel (m_dose, ticked once a second from m_procedureTimer's
    // own callback) rather than the fixed console defaults they used to
    // be. The Fluoroscopy HUD's kVp/mA/DAP (VesselView::setGeneratorInfo())
    // read the exact same model. TOTAL FLUORO TIME is genuinely live too
    // (CarmView already tracks real fluoro-active seconds via
    // setFluoroActive()/fluoroSeconds() - this just reads it). LAST IMAGE
    // and the clock are both genuinely live as well.
    QLabel *m_sbDose        = nullptr;
    QLabel *m_sbAirKerma    = nullptr;
    QLabel *m_sbFluoroTime  = nullptr;
    QLabel *m_sbLastImage   = nullptr;
    QLabel *m_sbClock       = nullptr;
    /// Set (and restarted) by onScreenshot() on every successful capture;
    /// "LAST IMAGE" reads "—" until the first one, never a fabricated time.
    QElapsedTimer m_lastImageClock;
    bool m_hasLastImage = false;

    void onQuickEmboVolumeStep(float deltaMl);
    void onQuickEmboInject();
    void onScreenshot();
    void onGenerateReport();
    void onEndProcedure();
};