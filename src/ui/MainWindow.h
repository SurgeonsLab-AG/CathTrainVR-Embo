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
#include "core/CardiacCycle.h"
#include "core/CenterlineExtractor.h"
#include "core/CenterlineGraph.h"
#include "core/NavigationController.h"
#include "core/StlMesh.h"
#include "core/EmbolizationModel.h"
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
class QDoubleSpinBox;
class QHBoxLayout;
class QLabel;
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
    void autoCalibrate();
    void loadBodyMeshes();

    // Navigation
    void restartFromCurrentSite(bool focusCamera);
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
    /// Tick a display-mode action without re-triggering it.
    void checkViewAction(int id);
    /// Sets the "active" dynamic property on a nav tile and re-polishes it
    /// so the QSS [active="true"] rule takes effect immediately - see that
    /// rule's own comment in the stylesheet for why this replaced relying
    /// on setChecked()/the :checked pseudo-state for the visual border.
    void setNavTileActive(QPushButton *tile, bool active);
    /// Depth in the coaxial stack; larger is further inside the patient.
    static int deviceStackLevel(cn::DeviceClass c);
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

    // ── Embolisation Module ──────────────────────────────────────────────
    void showEmbolizationDialog();
    void onBeginEmbolization();
    void onInjectEmbolic();
    void onResetEmbolization();
    void updateEmboliReadout();
    void refreshEmboliViews();
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
    /// Contrast Injection's shared temporary-flow model. Wired to m_emboli
    /// (setEmbolization) so it can see - and be genuinely blocked by -
    /// whatever the embolic cast currently occludes.
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
    /// Insertion depth the active device was introduced at; it cannot retract
    /// behind this, because that is inside the host catheter's lumen.
    float m_coaxialBaseDepth = 0.f;
    double m_mmPerUnit    = 100.0;
    double m_pendingMm    = 0.0;
    bool   m_wasInContact = false;

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
    QuadPanel  *m_quad       = nullptr;   // right centre — stacked vessel views
    VesselView *m_view       = nullptr;   // alias → m_quad->view3D()
    ECGWidget  *m_ecgWidget  = nullptr;

    // ── Bottom tile navigation bar (replaces the old menu-bar view actions) ──
    // 3D VIEW / FLUORO / DSA are mutually exclusive (m_viewTileGroup); index
    // 2 (Mesh) is deliberately left null - the reference design this bar
    // matches has no Mesh tile, so Mesh view is no longer reachable from the
    // main UI (onViewModeChanged(2)/checkViewAction(2) remain harmless no-ops
    // rather than removed outright, in case Mesh access needs to come back).
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

    /// Contrast Injection dialog — same non-modal pattern. No agent combo of
    /// its own: the three phase buttons themselves are the workflow.
    QDialog        *m_contrastDialog      = nullptr;
    QLabel         *m_contrastStatusLabel = nullptr;
    QPushButton    *m_contrastInjectBtn   = nullptr;
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
    // DOSE and AIR KERMA are fixed console defaults - same disclosed reason
    // as the Fluoroscopy HUD's kVp/mA/DAP (VesselView::paintOverlay()): no
    // real generator/dosimetry model exists in this build to draw live
    // numbers from. TOTAL FLUORO TIME is genuinely live (CarmView already
    // tracks real fluoro-active seconds via setFluoroActive()/
    // fluoroSeconds() - this just reads it). LAST IMAGE and the clock are
    // both genuinely live too.
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