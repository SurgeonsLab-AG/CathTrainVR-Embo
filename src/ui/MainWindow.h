#pragma once
#include <QMainWindow>
#include <QStackedWidget>
#include <QElapsedTimer>
#include <memory>
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
class QLabel;
class QMenu;
class QPlainTextEdit;
class QRadioButton;
class QSlider;
class QSpinBox;
class QTimer;
class QPushButton;

// ─────────────────────────────────────────────────────────────────────────────
// MainWindow — combined EmboNav console
//
//   ┌───────────────────┬──────────────────────────────┬────────────────┐
//   │ File View C-arm    Help  │ Cine Studio │ 3D Fluro Mesh DSA        │
//   ├───────────────────┼──────────────────────────────┼────────────────┤
//   │ C-arm OpenGL view │                              │ Procedure dock │
//   │  (CarmView)       │   QuadPanel                  │  Access &      │
//   │  + angle HUD      │   (VesselView stack:         │    devices     │
//   │                   │    3D / Fluoro / Mesh / DSA) │  Device status │
//   ├───────────────────┤                              │  Calibration   │
//   │ ECG / ABP / Pleth │                              │  Catheter nav  │
//   │  (ECGWidget)      ├──────────────────────────────┤  Procedure log │
//   │                   │ [Contrast][Road Map]         │                │
//   │                   │ embolisation step label      │                │
//   │                   │ [Squid][Onyx][DMS][Glue][Gel]│                │
//   └───────────────────┴──────────────────────────────┴────────────────┘
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
    void onContrastInjection();
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
    bool applyCatheterSpec(const QString &displayName, const QString &sizeLabel = QString());
    /// Navigation is enabled only when there is both an access and a device.
    void updateDeviceGating();
    /// Tick a display-mode action without re-triggering it.
    void checkViewAction(int id);
    /// Depth in the coaxial stack; larger is further inside the patient.
    static int deviceStackLevel(cn::DeviceClass c);
    /// nav.advance() with the coaxial floor applied.
    float advanceDevice(float ds);
    /// Resize the loaded catheter from the Calibration French dropdown.
    void onCatheterFrenchChanged(int index);
    bool showContrastControlDialog();
    void log(const QString &line);
    QString mm(float modelUnits, int decimals = 1) const;

    // ── Geometry / navigation core ──────────────────────────────────────────
    cn::StlMesh                     m_mesh;
    cn::CenterlineGraph             m_graph;
    cn::CenterlineExtractor::Report m_report;
    cn::NavigationController        m_nav;
    cn::CatheterModel               m_catheter;
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
    QString m_contrastFlowRate = "Medium";
    QString m_contrastVolume   = "Medium";
    /// Agent chosen in the injector dialog; empty means a diagnostic run with
    /// contrast only, which is the default.
    QString m_contrastAgent;
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
    QWidget        *m_simulatorPage = nullptr;
    CineViewStudio *m_cineStudio    = nullptr;

    // ── Views ───────────────────────────────────────────────────────────────
    CarmView        *m_carmWidget = nullptr;   // left column — standalone CarmView
    CarmController  *m_carmCtrl  = nullptr;   // owns rig state, routes camera increments
    QuadPanel  *m_quad       = nullptr;   // right centre — stacked vessel views
    VesselView *m_view       = nullptr;   // alias → m_quad->view3D()
    ECGWidget  *m_ecgWidget  = nullptr;

    // ── Menu-bar display-mode actions ───────────────────────────────────────
    // Exclusive group, so exactly one of 3D / Fluoro / Mesh / DSA is ticked.
    QActionGroup *m_viewActGroup = nullptr;
    bool m_contrastDone = false;

    // ── Access site radio buttons ───────────────────────────────────────────
    std::vector<QRadioButton *> m_siteButtons;

    // ── Procedure panel widgets (right dock) ────────────────────────────────
    QDoubleSpinBox *m_odSpin        = nullptr;
    QDoubleSpinBox *m_stepSpin      = nullptr;
    QDoubleSpinBox *m_speedSpin     = nullptr;
    QDoubleSpinBox *m_scaleSpin     = nullptr;
    QSlider        *m_opacitySlider  = nullptr;
    QCheckBox      *m_autoCalibrate = nullptr;
    QPlainTextEdit *m_log           = nullptr;

    // Navigation buttons (in right dock)
    QPushButton *m_btnAdvance  = nullptr;
    QPushButton *m_btnRetract  = nullptr;
    QPushButton *m_btnLeft     = nullptr;
    QPushButton *m_btnRight    = nullptr;
    // Access & devices group (moved out of the middle strip into this dock)
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

    // Device area — the loaded catheter's model name plus a quick working-size
    // selector. The full per-size dimensions live in the Catheter Selection
    // dialog; duplicating them here made the panel long without adding control.
    QLabel    *m_fDevModel = nullptr;
    QComboBox *m_frCombo   = nullptr;   ///< quick working size, 2-5 Fr

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
};