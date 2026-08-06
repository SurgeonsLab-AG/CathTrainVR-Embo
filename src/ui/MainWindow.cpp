#include "MainWindow.h"

#include "core/CatheterCatalog.h"
#include "QuadPanel.h"
#include "CarmView.h"
#include "CarmController.h"
#include "core/VtpCenterlineLoader.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPoint>
#include <QActionGroup>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>
#include <algorithm>
#include <functional>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Device presets (same as old project — Fogarty embolectomy catheters)
// ─────────────────────────────────────────────────────────────────────────────
namespace {

struct EmbolectomyPreset {
    const char *name;
    float shaftOdMm;
    float balloonDiameterMm;
    float balloonLengthMm;
    float tipToBalloonMm;
};
const EmbolectomyPreset kPresets[] = {
    {"2 Fr embolectomy  (balloon  4 mm)",  0.667f,  4.f,  5.f, 4.0f},
    {"3 Fr embolectomy  (balloon  5 mm)",  1.000f,  5.f,  6.f, 4.5f},
    {"4 Fr embolectomy  (balloon  9 mm)",  1.333f,  9.f,  9.f, 5.5f},
    {"5 Fr embolectomy  (balloon 11 mm)",  1.667f, 11.f, 11.f, 6.0f},
    {"6 Fr embolectomy  (balloon 13 mm)",  2.000f, 13.f, 13.f, 6.5f},
    };
// Quick-size range offered in the Calibration box.
constexpr int kMinFrench = 2;
constexpr int kMaxFrench = 5;

constexpr float kSegLenMm  = 1.5f;
constexpr int   kFlexSeg   = 6;
constexpr float kMaxDefDeg = 45.f;
constexpr float kRestCurvDeg = 0.f;

// ── Exact old-project stylesheet ─────────────────────────────────────────────
const char *kStyle = R"(
QMainWindow, QWidget {
    background: #000000;
    color: #E8E8E8;
}
QMenuBar { background:#000000; color:#E8E8E8; }
QMenuBar::item:selected { background:#000000; color:white; }
QMenu { background:#000000; color:#E8E8E8; border:1px solid #313131; }
QMenu::item:selected { background:#000000; color:white; }

QPushButton {
    background: #0B0B0B;
    color: #E8E8E8;
    border: 1px solid #313131;
    border-radius: 4px;
    padding: 8px 12px;
    font-size: 12px;
    font-weight: 500;
    min-height: 38px;
}
QPushButton:hover  { background:#1A1A1A; border-color:#4B4B4B; }
QPushButton:pressed{ background:#000000; }
QPushButton:checked{ background:#D8D8D8; color:#000000; font-weight:bold; border-color:#FFFFFF; }

/* With no hue available, the checked state is carried by inversion and a
   brighter border rather than by tint — a dark-grey-on-dark-grey "active"
   button is indistinguishable from an inactive one in mono. */
QPushButton#btnViewActive, QPushButton#btn3DView {
    background:#D8D8D8; color:#000000; font-weight:bold; border-color:#FFFFFF;
}
QPushButton#btnContrast { color:#D1D1D1; font-weight:600; }
QPushButton#btnNav {
    background:#0B0B0B;
    color:#E8E8E8;
    border:1px solid #3A3A3A;
    border-radius:3px;
    font-size:11px;
    font-weight:bold;
    min-height:0px;
    padding:3px 6px;
}
QPushButton#btnNav:hover  { background:#1A1A1A; }
QPushButton#btnNav:pressed{ background:#000000; }

QLabel#statusLabel { color:#B1B1B1; font-size:11px; padding:6px 10px; background:#000000; }
QLabel#angleLabel  {
    color:#E8E8E8; font-size:13px; font-weight:bold;
    background:#000000; padding:6px 12px;
    border:1px solid #3B3B3B; border-radius:4px;
}
QLabel#keyHintLabel { color:#8B8B8B; font-size:10px; }
QLabel#emboLabel {
    color:#E8E8E8; font-size:12px; font-weight:bold;
    padding:6px 0 4px 4px; border-bottom:2px solid #3B3B3B;
}
QFrame#divider { background:#4B4B4B; border:none; max-height:1px; }
QWidget#ecgWidget { background:#000000; border-top:1px solid #333333; }
QPushButton#btnContrastInjection { color:#D1D1D1; font-weight:600; }

QGroupBox { border:1px solid #3B3B3B; margin-top:8px; padding-top:6px; color:#9B9B9B; font-size:10px; }
QGroupBox::title { subcontrol-origin:margin; left:7px; padding:0 3px; }
QComboBox { background:#000000; border:1px solid #565656; border-radius:3px; padding:4px; color:#E8E8E8; }
QComboBox::drop-down { border:none; }
QDoubleSpinBox { background:#000000; border:1px solid #565656; border-radius:3px; padding:3px; color:#E8E8E8; }
QPlainTextEdit { background:#000000; border:1px solid #3B3B3B; color:#B1B1B1; font-size:10px; }
QSlider::groove:horizontal { background:#3B3B3B; height:4px; border-radius:2px; }
QSlider::handle:horizontal { background:#6F6F6F; width:12px; height:12px; margin:-4px 0; border-radius:6px; }
QScrollBar:vertical { background:#000000; width:8px; }
QScrollBar::handle:vertical { background:#3B3B3B; border-radius:4px; }
QStatusBar { background:#000000; color:#9B9B9B; font-size:10px; }
QRadioButton { color:#C9C9C9; }
QCheckBox    { color:#C9C9C9; }
QDockWidget::title { background:#000000; padding:4px; border-bottom:1px solid #3B3B3B; color:#C9C9C9; }
)";

/// Every button in the procedure dock is built through here, so they are all
/// the same height and weight. Previously the selection buttons were 32 px from
/// setMinimumHeight and the navigation buttons 24 px from setMaximumHeight,
/// while the global QPushButton rule asked for 38 px — three different answers
/// fighting each other.
///
/// One constant now sets the height of every button in the dock, so Sheath
/// Selection, Catheter Selection, Advance, Retract, Left and Right are all
/// identical. Change this to resize the lot.
constexpr int kProcBtnHeight = 40;

/// Minimum width of the Procedure dock. Raising it narrows the vessel view by
/// the same amount, since the C-arm column is fixed and the vessel view carries
/// the layout's only stretch.
constexpr int kProcedurePanelWidth = 360;

QPushButton *makeProcBtn(const QString &text, QWidget *p) {
    auto *b = new QPushButton(text, p);
    b->setObjectName("btnNav");          // shares the compact dock button style
    b->setFixedHeight(kProcBtnHeight);   // fixed, so the QSS min-height cannot win
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return b;
}

QPushButton *makeNavBtn(const QString &text, QWidget *p) { return makeProcBtn(text, p); }

QLabel *makeReadout(QWidget *p) {
    auto *l = new QLabel("--", p);
    l->setStyleSheet("color:#C0C0C0; font-family:monospace; font-size:10px;"
                     "background:#000000; padding:2px 4px; border-radius:2px;"
                     "border:1px solid #272727;");
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    l->setMinimumWidth(80);
    return l;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    // Applied to the whole application, not just this window: dialogs created
    // with `this` as parent inherit a widget stylesheet, but menus, tooltips and
    // top-level popups do not, and they were the pale patches left over.
    qApp->setStyleSheet(qApp->styleSheet() + QString::fromUtf8(kStyle));
    buildUi();
    buildMenus();
    buildViewBar();

    // ── Cardiac clock ──────────────────────────────────────────────────────
    // One timer advances one cycle; the ECG widget and every vessel view read
    // its phase. Driving them from separate timers is what lets a trace and a
    // wall drift out of step over a session.
    m_cardiac.setHeartRate(72);
    if (m_ecgWidget) m_ecgWidget->setCardiacCycle(&m_cardiac);
    for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
        if (v) v->setCardiacCycle(&m_cardiac);

    m_heart = new QTimer(this);
    m_heart->setInterval(16);           // ~60 Hz
    connect(m_heart, &QTimer::timeout, this, [this]{ onCardiacTick(); });
    m_heartClock.start();
    m_heart->start();
    setWindowTitle(tr("EmboNav — Endovascular Navigation Console"));
    resize(1500, 900);
}

// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// buildViewBar — Cine Studio, the four view modes, Contrast Injection and Road
// Map as ordinary menu-bar entries.
//
// A QMenuBar takes QActions directly, not just QMenus, and they render in the
// bar with the same flat style and hover behaviour as File / View / C-arm /
// Help. That is what these are: single commands, not menus with contents, so
// giving them each an empty drop-down would add a click for nothing.
//
// They were previously QPushButtons in a corner widget, which put button
// chrome — borders, background, checked-state fills — into a bar that has none.
//
// Runs after buildMenus() so the four existing menus keep the left-hand end.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::buildViewBar() {
    QMenuBar *bar = menuBar();
    bar->addSeparator();

    auto *cineAct = new QAction(tr("Cine Studio"), this);
    connect(cineAct, &QAction::triggered, this, [this]{ onOpenCineStudio(); });
    bar->addAction(cineAct);

    // The four display modes are mutually exclusive, so they go in an action
    // group: exactly one stays checked, and a checked bar action draws as
    // pressed rather than needing a button to show state.
    m_viewActGroup = new QActionGroup(this);
    m_viewActGroup->setExclusive(true);
    const QString viewNames[4] = {tr("3D View"), tr("Fluro View"),
                                  tr("Mesh View"), tr("DSA View")};
    for (int i = 0; i < 4; ++i) {
        auto *a = new QAction(viewNames[i], this);
        a->setCheckable(true);
        a->setData(i);
        if (i == 0) a->setChecked(true);
        connect(a, &QAction::triggered, this, [this, i]{ onViewModeChanged(i); });
        m_viewActGroup->addAction(a);
        bar->addAction(a);
    }

    auto *contrastAct = new QAction(tr("Contrast Injection"), this);
    connect(contrastAct, &QAction::triggered, this, [this]{ onContrastInjection(); });
    bar->addAction(contrastAct);

    auto *roadAct = new QAction(tr("Road Map"), this);
    connect(roadAct, &QAction::triggered, this, [this]{ onRoadMap(); });
    bar->addAction(roadAct);
}

void MainWindow::buildMenus() {
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *openAct = new QAction(tr("&Open vessel STL…"), this);
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, [this]{ onOpenFile(); });
    fileMenu->addAction(openAct);
    fileMenu->addSeparator();
    auto *quitAct = new QAction(tr("E&xit"), this);
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(quitAct);

    m_centerlineAction = new QAction(tr("Show &centerline"), this);
    m_centerlineAction->setCheckable(true);
    m_centerlineAction->setShortcut(Qt::Key_F6);
    connect(m_centerlineAction, &QAction::toggled, this, [this](bool on) {
        m_view->setShowCenterline(on);
    });

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_centerlineAction);
    auto *camAct = new QAction(tr("Camera follows device"), this);
    camAct->setCheckable(true);
    camAct->setShortcut(Qt::Key_F7);
    connect(camAct, &QAction::toggled, this, [this](bool on){ onCameraFollowToggled(on); });
    viewMenu->addAction(camAct);
    auto *resetAct = new QAction(tr("Reset camera"), this);
    connect(resetAct, &QAction::triggered, this, [this] { m_view->resetCamera(); });
    viewMenu->addAction(resetAct);

    QMenu *carmMenu = menuBar()->addMenu(tr("&C-arm"));
    auto addCarm = [&](const QString &t, Qt::Key k, std::function<void()> fn) {
        auto *a = new QAction(t, this);
        a->setShortcut(k);
        // Application-wide, so a focused QOpenGLWidget cannot swallow the key
        // before the action sees it. Default WindowShortcut is usually enough,
        // but the vessel and C-arm views both take StrongFocus.
        a->setShortcutContext(Qt::ApplicationShortcut);
        connect(a, &QAction::triggered, this, [fn]{ fn(); });
        carmMenu->addAction(a);
    };
    // A/D and W/S swapped: A/D now drive CRAN/CAUD and W/S drive LAO/RAO.
    //
    // Swapped at the binding, not in the handlers. onCarmLAO() and friends are
    // named for the anatomy they produce and are also invoked from these same
    // menu entries by mouse, so rewiring them internally would have made the
    // menu labels lie. Each pair keeps its sign: the first key of a pair is the
    // positive rotation, the second the negative, exactly as before.
    addCarm(tr("CRAN 5° [A]"),    Qt::Key_A, [this]{ onCarmCRAN(); });
    addCarm(tr("CAUD 5° [D]"),    Qt::Key_D, [this]{ onCarmCAUD(); });
    addCarm(tr("LAO 5°  [W]"),    Qt::Key_W, [this]{ onCarmLAO();  });
    addCarm(tr("RAO 5°  [S]"),    Qt::Key_S, [this]{ onCarmRAO();  });
    carmMenu->addSeparator();
    addCarm(tr("Reset [R]"),      Qt::Key_R, [this]{ onCarmReset(); });

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    auto *keysAct = new QAction(tr("&Controls"), this);
    connect(keysAct, &QAction::triggered, this, [this] {
        QMessageBox::information(this, tr("Controls"),
                                 tr("CATHETER NAVIGATION\n"
                                    "  Page Up      advance device\n"
                                    "  Page Down    retract device\n"
                                    "  Left arrow   take left limb at bifurcation\n"
                                    "  Right arrow  take right limb at bifurcation\n"
                                    "  Space        inflate / deflate balloon\n\n"
                                    "C-ARM\n"
                                    "  A / D        LAO / RAO 5°\n"
                                    "  W / S        CRAN / CAUD 5°\n"
                                    "  R            Reset C-arm\n\n"
                                    "SKELETON NUDGE (fine-tune body alignment)\n"
                                    "  I / K        Skeleton up / down\n"
                                    "  J / M        Skeleton left / right\n"
                                    "  [ / ]        Skeleton scale"));
    });
    helpMenu->addAction(keysAct);
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::buildUi() {
    // Page stack: Simulator ↔ Cine Studio
    m_pageStack     = new QStackedWidget(this);
    m_simulatorPage = new QWidget(m_pageStack);
    m_cineStudio    = new CineViewStudio(m_pageStack);
    m_pageStack->addWidget(m_simulatorPage);
    m_pageStack->addWidget(m_cineStudio);
    setCentralWidget(m_pageStack);
    connect(m_cineStudio, &CineViewStudio::backToSimulatorRequested,
            this, &MainWindow::onCineStudioBack);

    m_motion = new QTimer(this);
    m_motion->setInterval(16);
    connect(m_motion, &QTimer::timeout, this, &MainWindow::onMotionTick);

    // ═══════════════════════════════════════════════════════════════════════
    // SIMULATOR PAGE: horizontal root split
    //   Left column (fixed ~460px)  |  view-mode buttons col (narrow)  |  Right (vessel+procedure)
    // ═══════════════════════════════════════════════════════════════════════
    auto *rootHL = new QHBoxLayout(m_simulatorPage);
    rootHL->setContentsMargins(0, 0, 0, 0);
    rootHL->setSpacing(0);

    // ── LEFT COLUMN: C-arm + angle strip + key hints + ECG ───────────────
    auto *leftCol = new QWidget(m_simulatorPage);
    leftCol->setFixedWidth(480);  // wider CarmView panel
    leftCol->setStyleSheet("background:#000000;");
    auto *leftVL = new QVBoxLayout(leftCol);
    leftVL->setContentsMargins(8,0,0,0);   // slight left inset so panel isn't flush to edge
    leftVL->setSpacing(0);

    // ── CarmView pane (top half) ───────────────────────────────────────────
    auto *carmPane = new QWidget(leftCol);
    carmPane->setStyleSheet("background:#1A1A1A;");
    auto *carmPaneVL = new QVBoxLayout(carmPane);
    carmPaneVL->setContentsMargins(0,0,0,0);
    carmPaneVL->setSpacing(0);

    m_carmWidget = new CarmView(carmPane);
    carmPaneVL->addWidget(m_carmWidget, 1);

    // Angle labels: still created for onCarmAnglesChanged() to update,
    // but not added to any layout — angles are shown inside the CarmView HUD.
    m_laoLabel  = new QLabel("LAO 0.0°");
    m_cranLabel = new QLabel("CRAN 0.0°");
    // Key hints removed from main UI — see Help menu for controls.

    // ── ECG pane (bottom half) ──────────────────────────────────────────────
    m_ecgWidget = new ECGWidget(leftCol);
    m_ecgWidget->setHeartRate(72);
    m_ecgWidget->setObjectName("ecgWidget");

    // Equal split using stretch factors — no splitter needed.
    // Both widgets added directly to leftVL with equal stretch=1.
    // Force equal height by using setSizePolicy + equal stretch.
    // Also override CarmView minimum size so it doesn't push carmPane taller.
    m_carmWidget->setMinimumSize(10, 10);
    carmPane->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_ecgWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    leftVL->addWidget(carmPane, 1);
    leftVL->addWidget(m_ecgWidget, 1);

    rootHL->addWidget(leftCol);

    // The middle strip is gone. Cine Studio and the four view-mode buttons now
    // sit as a toolbar across the top of the vessel area, where they read as
    // controls for the view directly beneath them rather than as a floating
    // column between two unrelated panels.

    // ── RIGHT COLUMN: vessel views + embolisation controls ─────────────────
    auto *rightCol = new QWidget(m_simulatorPage);
    auto *rightVL  = new QVBoxLayout(rightCol);
    rightVL->setContentsMargins(0,0,0,0);
    rightVL->setSpacing(0);
    rootHL->addWidget(rightCol, 1);

    // Vessel quad panel (stacked: 3D / Fluoro / Mesh / DSA)
    m_quad = new QuadPanel(rightCol);
    m_view = m_quad->view3D();
    rightVL->addWidget(m_quad, 1);

    // C-arm STL load
    auto loadCarm = [this](const QString &f, int idx) {
        const QString appDir = QCoreApplication::applicationDirPath();
        // Walk up from the exe folder looking for Data/3D — works whether running
        // from Build/Release, Build/Debug, or the project root.
        const QStringList bases = {
            QStringLiteral("Data/3D"),           // relative to working dir (project root)
            appDir + "/../../../Data/3D",        // Build/Release/../../.. -> project root
            appDir + "/../../Data/3D",           // Build/Debug/.. -> project root
            appDir + "/../Data/3D",              // one level up
            appDir + "/Data/3D",                 // Data/3D next to exe
            appDir,                              // flat next to exe (fallback)
        };
        for (const QString &base : bases)
            if (m_carmWidget->loadPart(QDir::cleanPath(base) + "/" + f, idx)) return;
    };
    // Old project colours exactly:
    // head=0.88,0.89,0.92  stand=0.75,0.77,0.80  bed=0.96,0.82,0.68  xray=0.25,0.55,0.95 opacity 0.15
    // Old project order: head=0 (pivot computed from this), stand=1, bed=2, xray=3
    // File names on disk are swapped (user renamed them), so:
    //   carm_head.stl file = arc geometry  → loaded as index 1 (the actual head/arc)
    //   carm_stand.stl file = stand geometry → loaded as index 2
    // BUT pivot must be from index 0 — so we load bed as 0 is wrong.
    // Load head arc as index 0 so pivot is correct, then adjust colours in CarmView.
    // Order is load-bearing: it has to match CarmView::kPartStand/Head/Bed/Xray,
    // which are 0..3, and CarmView caches the rotation pivot from part 1.
    // carm_head (1) is the movable part, driven by A/D (CRAN/CAUD), W/S
    // (LAO/RAO) and R through CarmController; everything else draws at identity.
    loadCarm("carm_stand.stl", 0);   // 0 = stand / floor gantry — fixed
    loadCarm("carm_head.stl",  1);   // 1 = head / arc + detector — the movable part
    loadCarm("carm_bed.stl",   2);   // 2 = bed
    loadCarm("carm_xray.stl",  3);   // 3 = xray

    // ── CarmController — old project pattern, no VTK ──────────────────────
    // Controller owns rig state; routes incremental camera rotation to
    // linked vessel views exactly as the old CarmController did.
    m_carmCtrl = new CarmController(this);
    m_carmCtrl->setCarmView(m_carmWidget);
    // Set pivot from the loaded head part (index 0)
    {
        const QVector3D piv = m_carmWidget->headPivot();
        m_carmCtrl->setHeadPivot(piv.x(), piv.y(), piv.z());
    }
    // Link fluoro + DSA views — they rotate incrementally with the C-arm.
    // 3D view keeps its own independent orbit (same as old project).
    // Every vessel view is linked, so one rig state reaches all of them. Mesh
    // and DSA are aliases of the 3D view in QuadPanel, so this is the 3D view
    // and the fluoro view — but going through the accessors means adding a real
    // third view later cannot silently miss the link.
    m_carmCtrl->addLinkedVesselView(m_quad->view3D());
    m_carmCtrl->addLinkedVesselView(m_quad->viewFluoro());
    connect(m_carmCtrl, &CarmController::anglesChanged,
            this, [this](double lao, double cran){ onCarmAnglesChanged(lao, cran); });

    // Wire primary view signals
    connect(m_view, &VesselView::advanceRequested,       this, [this]{ onAdvance(); });
    connect(m_view, &VesselView::retractRequested,       this, [this]{ onRetract(); });
    connect(m_view, &VesselView::chooseLeftRequested,    this, [this]{ onChooseLeft(); });
    connect(m_view, &VesselView::chooseRightRequested,   this, [this]{ onChooseRight(); });
    connect(m_view, &VesselView::cameraChanged,          this, &MainWindow::onCameraChanged);
    connect(m_view, &VesselView::vesselSurfacePicked, this, &MainWindow::onVesselSurfacePicked);
    // Camera sync: when ANY view's camera changes, ALL other views follow.
    // This ensures 3D/Mesh/DSA/Fluoro always share the same camera position.
    // viewMesh() and viewDSA() alias m_view so effectively 3D↔Fluoro bidirectional.
    auto syncAllCameras = [this](VesselView *source) {
        for (VesselView *v : std::initializer_list<VesselView*>{
                                                                  m_view, m_quad->viewFluoro()}) {
            if (v && v != source) v->syncCameraFrom(*source);
        }
    };
    // Connect 3D view → fluoro sync
    connect(m_view, &VesselView::cameraChanged, this, [this, syncAllCameras]{
        syncAllCameras(m_view);
    });
    // Connect fluoro view → 3D sync
    if (m_quad->viewFluoro() && m_quad->viewFluoro() != m_view) {
        connect(m_quad->viewFluoro(), &VesselView::cameraChanged, this,
                [this, syncAllCameras]{ syncAllCameras(m_quad->viewFluoro()); });
    }
    connect(m_view, &VesselView::balloonToggleRequested, this, [this]{ onBalloonToggled(); });

    // Contrast / Road Map
    // Contrast Injection and Road Map now live in the menu bar corner beside
    // Help (buildViewBar), and the embolic agents hang off Contrast Injection as
    // a dropdown rather than sitting on screen permanently. That reclaims two
    // rows under the vessel view and keeps the agents out of the way until the
    // step that uses them.

    // Embolisation step label
    m_emboLabel = new QLabel(
        tr("Embolisation — Step 1/7 — Sheath Selection → choose Left/Right + Carotid, then click the vessel to place the sheath."),
        rightCol);
    m_emboLabel->setObjectName("emboLabel");
    m_emboLabel->setWordWrap(true);
    m_emboLabel->setFixedHeight(28);
    m_emboLabel->setStyleSheet("color:#E8E8E8; font-size:11px; font-weight:bold; padding:3px 8px; background:#000000;");
    rightVL->addWidget(m_emboLabel);


    // Status label
    // Status label: created but not shown in main UI (instructions moved to Help menu)
    m_statusLabel = new QLabel();
    m_statusLabel->setVisible(false);

    // ── RIGHT DOCK: Procedure panel ────────────────────────────────────────
    auto *dock  = new QDockWidget(tr("Procedure"), this);
    dock->setFeatures(QDockWidget::DockWidgetMovable);
    auto *panel = new QWidget(dock);
    auto *pVL   = new QVBoxLayout(panel);
    pVL->setContentsMargins(6,6,6,6);
    pVL->setSpacing(4);

    // ── Access & devices ──────────────────────────────────────────────────
    // Step 1 of the procedure now lives here rather than in the middle strip.
    auto *accBox = new QGroupBox(tr("Access && devices"), panel);
    auto *accVL  = new QVBoxLayout(accBox);
    accVL->setSpacing(4);
    // Same margins as the Catheter navigation and Procedure log groups. Without
    // this, accBox took the style's default (~9 px) while navBox used 4, so the
    // two Selection buttons came out about 10 px narrower than Advance and
    // Retract even though every button shares one factory and one height.
    accVL->setContentsMargins(4, 4, 4, 4);

    m_btnSheathSelect = makeProcBtn(tr("Sheath Selection…"), accBox);
    connect(m_btnSheathSelect, &QPushButton::clicked, this, [this]{ onSheathSelection(); });
    accVL->addWidget(m_btnSheathSelect);

    m_btnCatheterSelect = makeProcBtn(tr("Catheter Selection…"), accBox);
    // Disabled until a sheath is in: you cannot introduce a catheter through an
    // access you have not made yet.
    m_btnCatheterSelect->setEnabled(false);
    connect(m_btnCatheterSelect, &QPushButton::clicked, this, [this]{ onCatheterSelection(); });
    accVL->addWidget(m_btnCatheterSelect);

    m_fSheath  = makeReadout(accBox);
    m_fCatheter = makeReadout(accBox);
    m_fSheath->setText(tr("no sheath"));
    m_fCatheter->setText(tr("no catheter"));
    accVL->addWidget(m_fSheath);
    accVL->addWidget(m_fCatheter);
    pVL->addWidget(accBox);

    // ── Navigation buttons (advance / retract / left / right) ─────────────
    // Same controls as new project, styled clearly
    auto *navBox = new QGroupBox(tr("Catheter navigation"), panel);
    auto *navGL  = new QGridLayout(navBox);
    navGL->setSpacing(4);
    // Match the Procedure log's margins (4 px) rather than taking the style's
    // default (~9 px). The two boxes sit directly above one another, and the
    // wider default inset made the buttons visibly narrower than the log,
    // which is what read as unbalanced. makeProcBtn already gives every button
    // an Expanding horizontal policy, so they fill whatever the margins allow.
    navGL->setContentsMargins(4, 4, 4, 4);
    m_btnAdvance = makeNavBtn(tr("▲  Advance"),  navBox);
    m_btnRetract = makeNavBtn(tr("▼  Retract"),  navBox);
    m_btnLeft    = makeNavBtn(tr("◄ Left"), navBox);
    m_btnRight   = makeNavBtn(tr("Right ►"), navBox);
    // Small nav buttons — functional, not prominent
    for (auto *b : {m_btnAdvance, m_btnRetract, m_btnLeft, m_btnRight})
        b->setEnabled(false);   // nothing to advance until a catheter is loaded
    // All four span both columns, so every navigation button is the same width
    // as Sheath Selection and Catheter Selection above. Left and Right used to
    // share row 2 — one grid column each — which made them half the width of
    // everything else in the panel.
    //
    // The cost is one extra row of height; put them back on `2, 0` and `2, 1`
    // to restore the side-by-side pair.
    navGL->addWidget(m_btnAdvance, 0, 0, 1, 2);
    navGL->addWidget(m_btnRetract, 1, 0, 1, 2);
    navGL->addWidget(m_btnLeft,    2, 0, 1, 2);
    navGL->addWidget(m_btnRight,   3, 0, 1, 2);
    connect(m_btnAdvance, &QPushButton::clicked, this, [this]{ onAdvance(); });
    connect(m_btnRetract, &QPushButton::clicked, this, [this]{ onRetract(); });
    connect(m_btnLeft,    &QPushButton::clicked, this, [this]{ onChooseLeft(); });
    connect(m_btnRight,   &QPushButton::clicked, this, [this]{ onChooseRight(); });

    // Balloon button removed: the catalogue devices the console offers are
    // guides, distal-access catheters, microcatheters and wires, none of which
    // carry a balloon. The QAction is kept so the Space shortcut still resolves
    // if an embolectomy device is ever wired back in.
    m_balloonAction = new QAction(this);
    m_balloonAction->setCheckable(true);
    m_balloonAction->setEnabled(false);
    // Access site buttons — hidden UI element; sites are resolved automatically
    // and can be changed via Sheath Selection dialog. Radio buttons kept in
    // m_siteButtons for internal state tracking but not shown.
    {
        auto *accBG = new QButtonGroup(this);
        static const char *kSiteNames[4] = {"Right femoral","Left femoral","Right radial","Left radial"};
        for (int i = 0; i < 4; ++i) {
            auto *rb = new QRadioButton(tr(kSiteNames[i]));  // no parent widget — not shown
            rb->setEnabled(false);
            accBG->addButton(rb, i);
            m_siteButtons.push_back(rb);
        }
        connect(accBG, &QButtonGroup::idClicked, this, [this](int i){ onAccessSiteChosen(i); });
    }

    // The Device status box has been removed. Its seven read-outs — Insertion,
    // Vessel left, Lumen ⌀, Device ⌀, Clearance, Branch, Turns — are all shown
    // in the viewport telemetry overlay, so the panel was duplicating them.
    //
    // updateReadouts() still runs and still guards every field with `if (...)`,
    // so the pointers simply stay null and each write is skipped. Nothing else
    // in the procedure panel changes.

    // ── Calibration ────────────────────────────────────────────────────────
    auto *calBox  = new QGroupBox(tr("Calibration"), panel);
    auto *calForm = new QFormLayout(calBox);

    // Device area: the measurements of whatever catheter is currently loaded.
    //
    // This used to be a combo listing the five Fogarty embolectomy balloons,
    // which had two problems: it offered a device class the procedure does not
    // use, and picking from it overwrote whatever Catheter Selection had just
    // loaded. The device is now chosen in one place — Catheter Selection — and
    // this area reports it.
    m_fDevModel = makeReadout(calBox);
    m_fDevModel->setText(tr("no catheter"));
    calForm->addRow(tr("Model"), m_fDevModel);

    // Working French size. French IS the outer diameter for a catheter —
    // OD mm = Fr / 3 — so this changes the device that has to fit the lumen,
    // not just a label. Full per-model dimensions live in the Catheter
    // Selection dialog; this is the quick size control.
    m_frCombo = new QComboBox(calBox);
    for (int fr = kMinFrench; fr <= kMaxFrench; ++fr)
        m_frCombo->addItem(tr("%1 Fr").arg(fr), fr);
    m_frCombo->setCurrentIndex(m_frCombo->count() - 1);   // default 5 Fr
    connect(m_frCombo, &QComboBox::currentIndexChanged,
            this, [this](int i){ onCatheterFrenchChanged(i); });
    calForm->addRow(tr("Size"), m_frCombo);

    m_odSpin = new QDoubleSpinBox(calBox);
    m_odSpin->setRange(0.20, 3.00); m_odSpin->setSingleStep(0.10);
    m_odSpin->setDecimals(2); m_odSpin->setSuffix(tr(" mm"));
    connect(m_odSpin, &QDoubleSpinBox::valueChanged, this, [this](double v){ onDeviceSizeChanged(v); });
    calForm->addRow(tr("OD"), m_odSpin);

    m_stepSpin = new QDoubleSpinBox(calBox);
    m_stepSpin->setRange(0.5, 30.0); m_stepSpin->setValue(4.0); m_stepSpin->setSuffix(tr(" mm"));
    calForm->addRow(tr("Step"), m_stepSpin);

    m_speedSpin = new QDoubleSpinBox(calBox);
    m_speedSpin->setRange(1.0, 120.0); m_speedSpin->setValue(15.0); m_speedSpin->setSuffix(tr(" mm/s"));
    calForm->addRow(tr("Speed"), m_speedSpin);

    m_scaleSpin = new QDoubleSpinBox(calBox);
    m_scaleSpin->setRange(0.001, 10000.0); m_scaleSpin->setDecimals(3);
    m_scaleSpin->setValue(m_mmPerUnit);
    connect(m_scaleSpin, &QDoubleSpinBox::valueChanged, this, [this](double v){ onScaleChanged(v); });
    calForm->addRow(tr("mm/unit"), m_scaleSpin);

    // ── Heart rate ─────────────────────────────────────────────────────────
    // One control for the whole console: it sets the cardiac clock, which drives
    // both the ECG trace and the pulsating vessel wall.
    m_bpmSpin = new QSpinBox(calBox);
    m_bpmSpin->setRange(30, 220);
    m_bpmSpin->setValue(m_cardiac.heartRate());
    m_bpmSpin->setSuffix(tr(" bpm"));
    connect(m_bpmSpin, &QSpinBox::valueChanged, this, [this](int v){ onHeartRateChanged(v); });
    calForm->addRow(tr("Heart rate"), m_bpmSpin);

    m_pulseCheck = new QCheckBox(tr("Pulsatile vessel wall"), calBox);
    m_pulseCheck->setChecked(true);
    connect(m_pulseCheck, &QCheckBox::toggled, this, [this](bool on){
        for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
            if (v) v->setPulsationEnabled(on);
    });
    calForm->addRow(m_pulseCheck);

    m_autoCalibrate = new QCheckBox(tr("Auto-scale from trunk"), calBox);
    m_autoCalibrate->setChecked(true);
    calForm->addRow(m_autoCalibrate);

    // Opacity slider — matches old project
    m_opacitySlider = new QSlider(Qt::Horizontal, calBox);
    m_opacitySlider->setRange(5, 100);
    m_opacitySlider->setValue(68);
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int v){ onOpacityChanged(v); });
    calForm->addRow(tr("Opacity"), m_opacitySlider);
    pVL->addWidget(calBox);

    // ── Catheter navigation ────────────────────────────────────────────────
    pVL->addWidget(navBox);

    // ── Procedure log, directly under Catheter navigation ──────────────────
    // log() already guarded on a null m_log, so the 44 call sites scattered
    // through the file start writing again the moment this exists.
    auto *logBox = new QGroupBox(tr("Procedure log"), panel);
    auto *logVL  = new QVBoxLayout(logBox);
    logVL->setContentsMargins(4, 4, 4, 4);
    m_log = new QPlainTextEdit(logBox);
    m_log->setReadOnly(true);
    m_log->setMinimumHeight(140);
    QFont logMono("Monospace", 8);
    logMono.setStyleHint(QFont::TypeWriter);
    m_log->setFont(logMono);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_log->setMaximumBlockCount(500);   // bounded, so a long session cannot grow forever
    logVL->addWidget(m_log);
    pVL->addWidget(logBox, 1);

    // Procedure dock width. The dock takes its space from the central widget,
    // and leftCol (the C-arm panel) is fixed at 480, so widening this narrows
    // the vessel view and nothing else — which is the intended trade.
    //
    // Raised 270 -> 320 -> 360. At 320 the longest Calibration labels and the
    // device model name still met in the middle, and the Access & devices status
    // lines clipped. 360 leaves every row its full text.
    panel->setMinimumWidth(kProcedurePanelWidth);

    // The dock is only as tall as the window, and the panel is taller than that
    // on short screens — which is why the Left/Right row was being clipped off
    // the bottom. Wrapping it in a scroll area means the content is always
    // reachable regardless of window height.
    auto *panelScroll = new QScrollArea(dock);
    panelScroll->setWidget(panel);
    panelScroll->setWidgetResizable(true);
    panelScroll->setFrameShape(QFrame::NoFrame);
    panelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Reserve room for the vertical scrollbar.
    //
    // The scroll area's viewport is the dock's width MINUS the scrollbar, and
    // widgetResizable(true) sizes the panel to that viewport. The panel's own
    // minimum of kProcedurePanelWidth then forces it wider than the viewport,
    // and because horizontal scrolling is switched off the overhanging strip on
    // the right is simply unreachable — which is why the right-hand edge of the
    // content was cut off.
    //
    // Widening the scroll area by the scrollbar's own width gives the viewport
    // the full kProcedurePanelWidth, so the panel fits inside it and its content
    // sits clear of the scrollbar instead of underneath it. The panel's width is
    // unchanged; only the dock around it grows by the width of the bar.
    // kStyle is installed before buildUi() runs, so sizeHint() already reflects
    // the stylesheet's 8 px bar rather than the platform default. Floored at 10
    // anyway, so a future style change that widens the bar cannot silently
    // reintroduce the clipping.
    const int sbWidth = panelScroll->verticalScrollBar()
                            ? std::max(panelScroll->verticalScrollBar()->sizeHint().width(), 10)
                            : 16;
    panelScroll->setMinimumWidth(kProcedurePanelWidth + sbWidth + 2);

    dock->setWidget(panelScroll);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    statusBar()->hide();   // status bar hidden — instructions in Help menu
    updateDeviceGating();
}

// ─────────────────────────────────────────────────────────────────────────────
// File loading
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onOpenFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open vessel surface"), {},
        tr("STL meshes (*.stl);;VTK PolyData (*.vtp);;All files (*)"));
    if (!path.isEmpty()) loadMesh(path);
}

void MainWindow::tryAutoLoad() {
    // Search for the vessel STL next to the executable and in Data/3D/.
    // Priority: Vessel_Reconstructed.stl (new) before simplified_branches_.stl (legacy).
    // No hardcoded paths in main.cpp — discovery lives here where loadMesh() also lives.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/Simplified_Vessel_2.stl",
        appDir + "/Data/3D/Simplified_Vessel_2.stl",
        appDir + "/../Data/3D/Simplified_Vessel_2.stl",
        appDir + "/../../Data/3D/Simplified_Vessel_2.stl",
        appDir + "/../../../Data/3D/Vessel_Reconstructed.stl",
    };
    for (const QString &p : candidates) {
        if (QFileInfo::exists(p)) {
            loadMesh(p);
            return;
        }
    }
    // No vessel found — app starts empty, user opens via File menu
}

bool MainWindow::loadMesh(const QString &path) {
    std::string err;
    QElapsedTimer t; t.start();
    if (!m_mesh.load(path.toStdString(), &err)) {
        QMessageBox::warning(this, tr("Load failed"), QString::fromStdString(err));
        return false;
    }
    m_ready = false; m_activeSite = -1;
    m_view->setMesh(&m_mesh);
    m_quad->viewFluoro()->setMesh(&m_mesh);
    m_quad->viewDSA()->setMesh(&m_mesh);
    m_quad->viewMesh()->setMesh(&m_mesh);
    m_bodyMeshDir = QFileInfo(path).absolutePath();
    log(tr("Loaded %1: %2 triangles, %3 shells (%4 ms)")
            .arg(QFileInfo(path).fileName())
            .arg(m_mesh.triangles().size())
            .arg(m_mesh.componentCount())
            .arg(t.elapsed()));
    extractCenterline();
    return true;
}

void MainWindow::extractCenterline() {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QElapsedTimer t; t.start();

    // ── Try pre-computed VTP centerline first (FullVasculature_Centerlines.vtp) ──
    // The VTP file must sit alongside the STL or in Data/3D.
    // If found: load directly (instant, 192K points, 1333 branches).
    // If not: fall back to geodesic extraction from the STL.
    bool ok = false;
    bool usedVtp = false;

    // Build candidate VTP paths from the loaded STL location
    const QString appDir = QCoreApplication::applicationDirPath();
    // Search for VTP next to the STL and next to the executable
    const QString stlDir = m_bodyMeshDir;
    const QStringList vtpCandidates = {
        stlDir  + "/Simplified_Vessel_2_centerlines.vtp",
        appDir  + "/Simplified_Vessel_2_centerlines.vtp",
        appDir  + "/Data/3D/Simplified_Vessel_2_centerlines.vtp",
        appDir  + "/../Data/3D/Simplified_Vessel_2_centerlines.vtp",
        appDir  + "/../../Data/3D/Simplified_Vessel_2_centerlines.vtp",
        appDir  + "/../../../Data/3D/Simplified_Vessel_2_centerlines.vtp",
        appDir  + "/../../../../Data/3D/Simplified_Vessel_2_centerlines.vtp",
    };
    // Log every candidate path with its exists status for diagnosis
    for (const QString &p : vtpCandidates) {
        bool ex = QFile::exists(p);
        log(tr("  VTP check: %1 — %2").arg(p).arg(ex ? "EXISTS" : "not found"));
    }

    for (const QString &vtpPath : vtpCandidates) {
        if (!QFile::exists(vtpPath)) continue;
        cn::VtpCenterlineLoader vtpLoader;
        cn::VtpCenterlineLoader::Report vtpReport;
        ok = vtpLoader.load(vtpPath.toStdString(), m_graph, &vtpReport);
        if (!ok) {
            log(tr("  VTP parse error for: %1 — %2")
                    .arg(vtpPath)
                    .arg(QString::fromStdString(vtpReport.error)));
            continue;
        }
        {
            usedVtp = true;
            // VTP files are in metres; Vessel_Reconstructed.stl is also in metres
            m_mmPerUnit = 1000.f;
            if (m_scaleSpin) {
                const QSignalBlocker sb(m_scaleSpin);
                m_scaleSpin->setValue(1000.f);
            }
            // Set maxRadius to 0 to DISABLE autoCalibrate when VTP is used —
            // mmPerUnit=1000 is already correct for metre-unit VTP files.
            // autoCalibrate() would wrongly recompute from the aorta radius.
            m_report.maxRadius  = 0.f;   // sentinel: skip autoCalibrate
            m_report.minRadius  = vtpReport.minRadiusMm  / 1000.f;
            m_report.meanRadius = vtpReport.meanRadiusMm / 1000.f;
            m_report.nodes       = vtpReport.pointsLoaded;
            m_report.bifurcations= vtpReport.bifurcations;
            m_report.terminals   = vtpReport.terminals;
            log(tr("Centerline (VTP): %1 nodes, %2 bifurcations, %3 terminals (%4 ms)"
                   " — %5 branches, max Ø %6 mm, auto-scale: 1 unit = 1000 mm")
                    .arg(vtpReport.pointsLoaded)
                    .arg(vtpReport.bifurcations)
                    .arg(vtpReport.terminals)
                    .arg(t.elapsed())

                    .arg(vtpReport.branchesLoaded)
                    .arg(vtpReport.maxRadiusMm * 2.f, 0, 'f', 1));
            m_vtpLeafNodes = vtpReport.leafNodeIds;  // store for resolveAccessSites
            break;
        }
    }

    if (!ok) {
        // VTP load failed — either file not found or parse error
        log(tr("VTP centerline not loaded. Paths tried:"));
        for (const QString &p : vtpCandidates) {
            if (QFileInfo::exists(p))
                log(tr("  FOUND but parse failed: %1").arg(p));
            else
                log(tr("  not found: %1").arg(p));
        }
        log(tr("Falling back to geodesic extraction from STL (slower)..."));
        // Fall back: extract from STL
        cn::CenterlineExtractor extractor;
        ok = extractor.extract(m_mesh, m_graph, &m_report);
        if (!ok) {
            QApplication::restoreOverrideCursor();
            QMessageBox::warning(this, tr("Extraction failed"),
                                 tr("No centerline could be extracted from this mesh."));
            return;
        }
        log(tr("Centerline: %1 nodes, %2 bifurcations, %3 terminals, %4 mm (%5 ms)")
                .arg(m_report.nodes).arg(m_report.bifurcations).arg(m_report.terminals)
                .arg(m_report.totalCenterlineLength * m_mmPerUnit, 0, 'f', 0)
                .arg(t.elapsed()));
    }
    for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
        v->setGraph(&m_graph);
    m_nav.setGraph(&m_graph);
    m_catheter.setGraph(&m_graph);

    // The pumping animation is confined to the heart and great vessels of the
    // VESSEL mesh; the Muscular and Skeletal layers are never displaced. Log
    // where the heart was auto-located (widest node in the graph — the aortic
    // root) so a mis-located pump is obvious rather than silent.
    if (m_view) {
        const QVector3D h = m_view->heartCentre();
        if (!h.isNull())
            log(tr("Heart region centred at (%1, %2, %3) mm, core ⌀%4 mm.")
                    .arg(h.x() * m_mmPerUnit, 0, 'f', 1)
                    .arg(h.y() * m_mmPerUnit, 0, 'f', 1)
                    .arg(h.z() * m_mmPerUnit, 0, 'f', 1)
                    .arg(2.0 * m_view->heartRegion().coreRadius * m_mmPerUnit, 0, 'f', 0));
    }
    // CatheterModel::Params defaults hasBalloon to true. Nothing in the
    // catalogue the console offers has a balloon, and the preset path that used
    // to set this is no longer reachable, so clear it explicitly rather than
    // leaving a balloon mesh on a guide catheter.
    {
        cn::CatheterModel::Params p = m_catheter.params();
        p.hasBalloon = false;
        p.balloonInflation = 0.f;
        m_catheter.setParams(p);
    }
    autoCalibrate();
    QApplication::restoreOverrideCursor();   // always restore — set at start of extractCenterline
    resolveAccessSites();
    loadBodyMeshes();
}

void MainWindow::loadBodyMeshes() {
    // Search same dirs as old project: next to the STL, ".", "Data/3D"
    const QString appDir2 = QCoreApplication::applicationDirPath();
    QStringList dirs = {
        m_bodyMeshDir,
        QStringLiteral("Data/3D"),
        appDir2 + "/../../../Data/3D",
        appDir2 + "/../../Data/3D",
        appDir2 + "/../Data/3D",
        appDir2 + "/Data/3D",
        appDir2,
    };
    auto find = [&](const QString &name) {
        for (const auto &d : dirs) {
            QString p = d + "/" + name;
            if (QFileInfo::exists(p)) return p;
        }
        return QString();
    };
    const QString mus  = find("Muscular.stl");
    const QString skel = find("Skeletal.stl");
    if (!mus.isEmpty() || !skel.isEmpty()) {
        m_quad->setBodyMeshes(mus, skel);
        log(tr("Body meshes: Muscular=%1  Skeletal=%2 — skeleton aligned inside vessel envelope.")
                .arg(mus.isEmpty() ? "not found" : "ok")
                .arg(skel.isEmpty() ? "not found" : "ok"));
    }
}

void MainWindow::resolveAccessSites() {
    log(tr("resolveAccessSites: graph nodes=%1 mmPerUnit=%2 leafNodes=%3")
            .arg(m_graph.nodeCount()).arg(m_mmPerUnit).arg(m_vtpLeafNodes.size()));
    m_sites = cn::AccessSiteResolver::resolve(m_graph, m_mmPerUnit, true, m_vtpLeafNodes);
    log(tr("  sites resolved: %1").arg(m_sites.size()));
    for (int i = 0; i < static_cast<int>(m_sites.size()); ++i) {
        const auto &s = m_sites[i];
        if (s.valid) {
            const auto &nd = m_graph.nodes()[s.node];
            log(tr("  %1: node=%2 Z=%3m X=%4m r=%5mm")
                    .arg(QString::fromLatin1(cn::accessSiteName(s.site)))
                    .arg(s.node)
                    .arg(nd.position.z, 0,'f',3)
                    .arg(nd.position.x, 0,'f',3)
                    .arg(nd.radius*m_mmPerUnit, 0,'f',2));
        } else {
            log(tr("  %1: NOT FOUND — %2")
                    .arg(QString::fromLatin1(cn::accessSiteName(s.site)))
                    .arg(QString::fromStdString(s.note)));
        }
    }
    int firstValid = -1;
    for (int i = 0; i < 4 && i < static_cast<int>(m_sites.size()); ++i) {
        const auto &s = m_sites[i];
        QRadioButton *rb = m_siteButtons[i];
        rb->setEnabled(s.valid);
        if (s.valid) {
            rb->setText(tr("%1  ⌀%2 mm")
                            .arg(QString::fromLatin1(cn::accessSiteName(s.site)))
                            .arg(s.vesselDiameter * m_mmPerUnit, 0, 'f', 2));
            rb->setToolTip(tr("node %1 — %2").arg(s.node).arg(QString::fromStdString(s.note)));
            if (firstValid < 0) firstValid = i;
        } else {
            rb->setText(tr("%1  (not found)")
                            .arg(QString::fromLatin1(cn::accessSiteName(s.site))));
        }
        log(tr("  access %1: %2")
                .arg(QString::fromLatin1(cn::accessSiteName(s.site)))
                .arg(s.valid ? tr("node %1, ⌀%2 mm").arg(s.node)
                                   .arg(s.vesselDiameter * m_mmPerUnit, 0, 'f', 2)
                             : tr("unavailable")));
    }
    // Initialise SheathManager with verified paths for all resolved sites
    m_sheath.init(&m_graph, m_sites, m_mmPerUnit);

    // No auto-selection: the catheter only positions once a sheath is chosen.
    // The instruction line that used to be written to m_emboLabel here has been
    // removed; the label keeps the step text it was built with. Nothing about
    // access-site resolution or sheath selection changes — m_sites is still
    // populated above and SheathManager still holds a verified path per site.
    m_view->showWholeVessel();
    m_quad->viewMesh()->showWholeVessel();
    m_quad->viewDSA()->showWholeVessel();
    // Fluoro view frames the head/brain region by default (AP, 8° FOV, zoomed)
    m_quad->viewFluoro()->showFluoroDefault();
}

void MainWindow::autoCalibrate() {
    if (!m_autoCalibrate || !m_autoCalibrate->isChecked()) return;
    if (m_report.maxRadius <= 1e-9f) return;
    const double scale = 20.0 / (2.0 * static_cast<double>(m_report.maxRadius));
    m_mmPerUnit = scale;
    const QSignalBlocker sb(m_scaleSpin);
    m_scaleSpin->setValue(scale);
    log(tr("  auto-scale: %1 mm/unit  trunk ⌀%2 mm  narrowest ⌀%3 mm")
            .arg(scale,0,'f',1)
            .arg(2.0*m_report.maxRadius*scale,0,'f',1)
            .arg(2.0*m_report.minRadius*scale,0,'f',1));
}

// ─────────────────────────────────────────────────────────────────────────────
// Sheath / Catheter selection — same logic as old project
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onSheathSelection() { showInsertionDialog(); }
void MainWindow::onCatheterSelection() { showCatheterSelectionMenu(); }

void MainWindow::showInsertionDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Select Access Point"));
    dlg.setFixedSize(400, 280);
    auto *lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(tr("Select Side and Access Point:"), &dlg));

    auto *sideHL = new QHBoxLayout;
    auto *leftBtn  = new QRadioButton(tr("Left Side"),  &dlg);
    auto *rightBtn = new QRadioButton(tr("Right Side"), &dlg);
    leftBtn->setChecked(true);
    sideHL->addWidget(leftBtn); sideHL->addWidget(rightBtn);
    lay->addLayout(sideHL);

    auto *accGrp = new QGroupBox(tr("Insertion Point"), &dlg);
    auto *accVL  = new QVBoxLayout(accGrp);
    auto *femBtn    = new QRadioButton(tr("Femoral (Groin)"),  accGrp);
    auto *radBtn    = new QRadioButton(tr("Radial (Wrist)"),   accGrp);
    femBtn->setChecked(true);
    accVL->addWidget(femBtn); accVL->addWidget(radBtn);
    lay->addWidget(accGrp);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;
    if (!femBtn->isChecked() && !radBtn->isChecked()) {
        QMessageBox::warning(this, tr("Access Point"), tr("Please select an insertion point."));
        return;
    }

    // Map dialog selection to resolved access sites and restart
    cn::AccessSite wantSite;
    bool wantLeft = leftBtn->isChecked();
    if (radBtn->isChecked()) wantSite = wantLeft ? cn::AccessSite::LeftRadial  : cn::AccessSite::RightRadial;
    else                     wantSite = wantLeft ? cn::AccessSite::LeftFemoral : cn::AccessSite::RightFemoral;

    bool siteFound = false;
    for (int i = 0; i < static_cast<int>(m_sites.size()); ++i) {
        if (m_sites[i].valid && m_sites[i].site == wantSite) {
            m_activeSite = i;
            if (i < static_cast<int>(m_siteButtons.size())) {
                const QSignalBlocker sb(m_siteButtons[i]);
                m_siteButtons[i]->setChecked(true);
            }
            siteFound = true;
            break;
        }
    }
    if (!siteFound) {
        QMessageBox::warning(this, tr("Access Site"),
                             tr("The selected access site was not found in the centerline.\n"
                                "Check the procedure log for details."));
        return;
    }

    // Layer 1: hand the site to SheathManager, which looks up the verified
    // centerline path built at init() time and validates the entry point.
    // Everything downstream — the sheath actor, Alt+click refinement, and the
    // navigation start node — keys off hasEntry(), so this must run BEFORE
    // restartFromCurrentSite(). Without it hasEntry() is false all session and
    // no sheath is ever drawn.
    {
        std::string sheathMsg;
        if (!m_sheath.setAccessPoint(wantSite, sheathMsg)) {
            log(tr("Sheath placement failed: %1").arg(QString::fromStdString(sheathMsg)));
            QMessageBox::warning(this, tr("Access Site"), QString::fromStdString(sheathMsg));
            return;
        }
        log(tr("Sheath: %1").arg(QString::fromStdString(sheathMsg)));
    }

    // Alt+click on the vessel now re-slices the verified path (Layer 2).
    for (VesselView *v : {m_view, m_quad->viewFluoro()})
        if (v) v->setPickingEnabled(true);

    // Place sheath at access point FIRST so camera moves to access site
    // THEN show device spec dialog (non-blocking concern)
    restartFromCurrentSite(true);
    showDeviceSpecDialog(tr("Access Sheath"));

    const QString side = wantLeft ? tr("Left") : tr("Right");
    const QString site = radBtn->isChecked() ? tr("Radial") : tr("Femoral");

    if (m_emboLabel)
        m_emboLabel->setText(tr("Embolisation — Step 2/7 — Guide Catheter → Catheter Selection → Guide Catheter."));
    log(tr("--- Sheath placed: %1 %2 access ---").arg(side, site));
}

// ─────────────────────────────────────────────────────────────────────────────
// applyCatheterSpec — turn a catalogue selection into an actual device.
//
// Selecting by name has to change the kinematics, or "Sofia 6F" and
// "Benchmark 071" are two labels on one device. The catalogue carries the
// makers' real dimensions and CatheterCatalog::catheterParams() maps them onto
// the model: outer diameter drives the lumen constraint, the flex-zone length
// and segment size drive trackability, and the resting curve is what lets a
// Cobra C2 select a branch a straight guide would slide past.
// ─────────────────────────────────────────────────────────────────────────────
bool MainWindow::applyCatheterSpec(const QString &displayName, const QString &sizeLabel) {
    const cn::CatheterSpec *spec = cn::findCatheter(displayName.toUtf8().constData());
    if (!spec) {
        log(tr("Unknown device \"%1\" — not in the catalogue.").arg(displayName));
        return false;
    }
    const int sizeIdx = cn::sizeIndexFor(*spec, sizeLabel.toUtf8().constData());
    const cn::CatheterSize &sz = spec->size(sizeIdx);

    // Coaxial check against the sheath actually in place.
    if (m_sheath.hasEntry()) {
        const float lumenMm = 2.f * m_sheath.entryRadius() * static_cast<float>(m_mmPerUnit);
        if (sz.outerDiaMm > lumenMm) {
            log(tr("%1 %2: ⌀%3 mm will not pass a ⌀%4 mm access — expect wall contact.")
                    .arg(displayName)
                    .arg(QString::fromLatin1(sz.label))
                    .arg(sz.outerDiaMm, 0, 'f', 2)
                    .arg(lumenMm, 0, 'f', 2));
        }
    }

    cn::CatheterModel::Params p =
        cn::catheterParams(*spec, sizeIdx, static_cast<float>(m_mmPerUnit));
    // Keep the containment settings the console already uses.
    const cn::CatheterModel::Params &old = m_catheter.params();
    p.wallSafety      = old.wallSafety;
    p.clampToLumen    = old.clampToLumen;
    p.fitToLumen      = old.fitToLumen;
    p.lumenFitFraction = old.lumenFitFraction;
    m_catheter.setParams(p);

    // ── Coaxial telescoping ────────────────────────────────────────────────
    //
    // A microcatheter is not introduced at the groin. It goes up the lumen of
    // the guide already in the patient, so its tip starts AT the guide tip and
    // advances from there. Restarting it at the sheath would model a second
    // femoral puncture.
    //
    // So: when the new device sits further inside the stack than the one
    // already placed, park the current device at its present depth, keep it on
    // screen, and make that depth the new device's floor. Selecting a device at
    // the same level or further out replaces the current one instead — that is
    // an exchange, not a telescope.
    const bool telescoping =
        m_catheterReady && m_activeSpec &&
        deviceStackLevel(spec->devClass) > deviceStackLevel(m_activeSpec->devClass);

    if (telescoping) {
        auto parked = std::make_unique<cn::CatheterModel>(m_catheter.params());
        parked->setGraph(&m_graph);
        parked->setPath(m_nav.path());
        parked->setInsertionDepth(m_nav.depth());
        parked->update();
        m_parkedDevices.push_back(std::move(parked));
        m_coaxialBaseDepth = m_nav.depth();
        log(tr("%1 introduced through %2 at %3 mm — advancing from the %2 tip.")
                .arg(displayName, m_activeCatheter)
                .arg(m_coaxialBaseDepth * m_mmPerUnit, 0, 'f', 1));
    } else if (m_catheterReady) {
        m_parkedDevices.clear();
        m_coaxialBaseDepth = 0.f;
    }

    m_activeCatheter     = displayName;
    m_activeCatheterSize = QString::fromLatin1(sz.label);
    m_activeSpec         = spec;
    m_catheterReady      = true;
    m_ready              = m_sheathReady;

    if (m_odSpin) {
        QSignalBlocker b(m_odSpin);
        m_odSpin->setValue(sz.outerDiaMm);
    }
    if (m_balloonAction) m_balloonAction->setEnabled(spec->hasBalloon);

    std::vector<const cn::CatheterModel *> hosts;
    hosts.reserve(m_parkedDevices.size());
    for (const auto &d : m_parkedDevices) hosts.push_back(d.get());
    for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()}) {
        v->setDevice(&m_catheter);
        v->setHostDevices(hosts);
    }

    updateDeviceGating();
    // The new device starts parked at the host tip, not at the access point.
    m_catheter.setPath(m_nav.path(), /*keepDepth=*/true);
    m_catheter.setInsertionDepth(m_nav.depth());
    syncDevices();

    log(tr("%1 %2 (%3, %4) — %5 Fr, ⌀%6 mm OD, %7 mm lumen, %8 cm. %9")
            .arg(displayName)
            .arg(QString::fromLatin1(sz.label))
            .arg(QString::fromLatin1(spec->maker))
            .arg(QString::fromLatin1(cn::deviceClassName(spec->devClass)))
            .arg(sz.frenchOd, 0, 'f', 1)
            .arg(sz.outerDiaMm, 0, 'f', 2)
            .arg(sz.innerDiaMm, 0, 'f', 2)
            .arg(sz.workingLenCm, 0, 'f', 0)
            .arg(QString::fromLatin1(spec->note)));
    return true;
}

// Enables navigation only when there is both an access and a device in it, and
// keeps the two status read-outs in step.
void MainWindow::updateDeviceGating() {
    const bool canNavigate = m_sheathReady && m_catheterReady;
    for (QPushButton *b : {m_btnAdvance, m_btnRetract, m_btnLeft, m_btnRight})
        if (b) b->setEnabled(canNavigate);
    if (m_btnCatheterSelect) m_btnCatheterSelect->setEnabled(m_sheathReady);

    if (m_fSheath) {
        m_fSheath->setText(m_sheathReady && m_sheath.hasEntry()
                           ? tr("%1  ⌀%2 mm")
                                   .arg(QString::fromLatin1(cn::accessSiteName(m_sheath.currentSite())))
                                   .arg(2.0 * m_sheath.entryRadius() * m_mmPerUnit, 0, 'f', 2)
                           : tr("no sheath"));
    }
    if (m_fCatheter)
        m_fCatheter->setText(m_catheterReady ? m_activeCatheter : tr("no catheter — select one"));

    // Device measurements: the loaded catheter only. Never a balloon preset.
    const cn::CatheterSpec *s = m_catheterReady
                                    ? cn::findCatheter(m_activeCatheter.toUtf8().constData())
                                    : nullptr;
    if (m_fDevModel) m_fDevModel->setText(s ? m_activeCatheter : tr("no catheter"));

    // Track the French combo to the loaded device without re-triggering it.
    if (m_frCombo && s) {
        const cn::CatheterSize &z =
            s->size(cn::sizeIndexFor(*s, m_activeCatheterSize.toUtf8().constData()));
        const int fr = std::clamp(static_cast<int>(std::lround(z.outerDiaMm * 3.0)),
                                  kMinFrench, kMaxFrench);
        const QSignalBlocker block(m_frCombo);
        m_frCombo->setCurrentIndex(fr - kMinFrench);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onCatheterFrenchChanged — resize the loaded catheter to a French size.
//
// French is the outer diameter: OD mm = Fr / 3. Only the diameter changes; the
// flex-zone length, segment size and resting tip curve stay as the catalogue
// device defined them, because those are properties of the model rather than of
// how thick it is.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onCatheterFrenchChanged(int index) {
    if (!m_frCombo || index < 0) return;
    const int    fr   = m_frCombo->itemData(index).toInt();
    const double odMm = fr / 3.0;

    cn::CatheterModel::Params p = m_catheter.params();
    p.deviceRadius = static_cast<float>(0.5 * odMm / m_mmPerUnit);
    m_catheter.setParams(p);

    if (m_odSpin) { const QSignalBlocker sb(m_odSpin); m_odSpin->setValue(odMm); }
    if (m_ready) syncDevices(); else updateReadouts();
}

void MainWindow::showCatheterSelectionMenu() {
    QMenu menu(this);
    auto *guideAct = menu.addAction(tr("Guide Catheter"));
    auto *microAct = menu.addAction(tr("Micro Catheter"));
    auto *wireAct  = menu.addAction(tr("Guide Wire"));
    QAction *chosen = menu.exec(QCursor::pos());
    if (!chosen) return;

    if (chosen == guideAct) {
        showDeviceSpecDialog(tr("Guide Catheter"));
        if (m_emboLabel) m_emboLabel->setText(tr("Embolisation — Step 3/7 — Guide Catheter advancing — use Page Up."));

        log(tr("[%1] Guide Catheter selected: %2 %3").arg(QTime::currentTime().toString("HH:mm:ss"), m_deviceSpec.catheterType, m_deviceSpec.catheterSizeFr));
    } else if (chosen == microAct) {
        showDeviceSpecDialog(tr("Micro Catheter"));
        if (m_emboLabel) m_emboLabel->setText(tr("Embolisation — Step 4/7 — Micro Catheter advancing to lesion."));

        log(tr("[%1] Micro Catheter selected: %2 %3").arg(QTime::currentTime().toString("HH:mm:ss"), m_deviceSpec.catheterType, m_deviceSpec.catheterSizeFr));
    } else if (chosen == wireAct) {
        showDeviceSpecDialog(tr("Guide Wire"));

        log(tr("[%1] Guide Wire selected: %2  tip: %3").arg(QTime::currentTime().toString("HH:mm:ss"), m_deviceSpec.catheterType, m_deviceSpec.wireTipShape));
    }
}

void MainWindow::showDeviceSpecDialog(const QString &stageName) {
    QDialog dlg(this);
    dlg.setWindowTitle(stageName + tr(" — Device Selection"));
    dlg.setFixedSize(400, 300);
    auto *lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(tr("Select the %1:").arg(stageName), &dlg));

    auto *typeBox = new QComboBox(&dlg);
    auto *sizeBox = new QComboBox(&dlg);
    // The size list is a property of the device, not of the stage: a Sofia comes
    // in 5 Fr / 6 Fr / 6 Fr Plus and an Envoy DA in 5/6/7 Fr, and each of those
    // has its own outer diameter, lumen and working length. A fixed list of
    // {"5F","6F","8F"} for every guide offered sizes that do not exist and
    // meant the choice changed nothing.
    auto repopulateSizes = [sizeBox](const QString &nm) {
        const cn::CatheterSpec *s = cn::findCatheter(nm.toUtf8().constData());
        const QSignalBlocker block(sizeBox);
        sizeBox->clear();
        if (!s) return;
        for (int i = 0; i < s->sizeCount; ++i)
            sizeBox->addItem(QString::fromLatin1(s->sizes[i].label));
        sizeBox->setCurrentIndex(s->defaultSize);
    };
    // The model list comes from the catalogue, and the size list is rebuilt from
    // whichever model is picked. A shared 5/6/8 Fr menu was wrong: a Benchmark
    // 071 has exactly one size, a SOFIA has three, and a Neuron MAX has one that
    // is not a French number at all.
    auto fillModels = [&](cn::DeviceClass a, cn::DeviceClass b) {
        size_t n = 0;
        const cn::CatheterSpec *cat = cn::catheterCatalog(n);
        for (size_t i = 0; i < n; ++i)
            if (cat[i].devClass == a || cat[i].devClass == b)
                typeBox->addItem(QString::fromLatin1(cat[i].name));
    };
    if (stageName.contains("Sheath", Qt::CaseInsensitive)) {
        typeBox->addItems({"Terumo Pinnacle (femoral sheath)","Cook Check-Flo (femoral sheath)",
                           "Merit Prelude (radial sheath)","Cook Shuttle (long sheath)"});
        sizeBox->addItems({"4 Fr","5 Fr","6 Fr","7 Fr","8 Fr"});
    } else if (stageName.contains("Micro", Qt::CaseInsensitive)) {
        fillModels(cn::DeviceClass::Microcatheter, cn::DeviceClass::Microcatheter);
    } else if (stageName.contains("Wire", Qt::CaseInsensitive)) {
        fillModels(cn::DeviceClass::Guidewire, cn::DeviceClass::Guidewire);
    } else {
        fillModels(cn::DeviceClass::BaseGuide, cn::DeviceClass::Guide);
        size_t n = 0;
        const cn::CatheterSpec *cat = cn::catheterCatalog(n);
        for (size_t i = 0; i < n; ++i)
            if (cat[i].devClass == cn::DeviceClass::GuideSheath ||
                cat[i].devClass == cn::DeviceClass::DistalAccess)
                typeBox->addItem(QString::fromLatin1(cat[i].name));
    }

    lay->addWidget(new QLabel(tr("Device / Model:"), &dlg)); lay->addWidget(typeBox);

    // Live spec read-out. The catalogue holds the makers' real dimensions, so
    // the operator can see what they are choosing rather than picking a name.
    auto *specLabel = new QLabel(&dlg);
    specLabel->setWordWrap(true);
    specLabel->setStyleSheet("color:#C2C2C2; font-size:10px; padding:4px;"
                             "background:#000000; border:1px solid #2F2F2F;");
    specLabel->setMinimumHeight(64);
    auto describe = [specLabel, sizeBox, this](const QString &nm) {
        const cn::CatheterSpec *s = cn::findCatheter(nm.toUtf8().constData());
        if (!s) { specLabel->setText(tr("No catalogue entry — dimensions unknown.")); return; }
        const int idx = cn::sizeIndexFor(*s, sizeBox->currentText().toUtf8().constData());
        const cn::CatheterSize &z = s->size(idx);
        QString fit;
        if (m_sheath.hasEntry()) {
            const double accessMm = 2.0 * m_sheath.entryRadius() * m_mmPerUnit;
            fit = z.outerDiaMm <= accessMm
                      ? tr("\nFits the ⌀%1 mm access.").arg(accessMm, 0, 'f', 2)
                      : tr("\n⚠ ⌀%1 mm access is too small for this size.")
                            .arg(accessMm, 0, 'f', 2);
        }
        specLabel->setText(tr("%1 — %2\n%3 · ⌀%4 mm OD · %5 mm lumen · %6 cm\n%7%8")
                               .arg(QString::fromLatin1(s->maker))
                               .arg(QString::fromLatin1(cn::deviceClassName(s->devClass)))
                               .arg(QString::fromLatin1(z.label))
                               .arg(z.outerDiaMm, 0, 'f', 2)
                               .arg(z.innerDiaMm, 0, 'f', 2)
                               .arg(z.workingLenCm, 0, 'f', 0)
                               .arg(QString::fromLatin1(s->note))
                               .arg(fit));
    };
    connect(typeBox, &QComboBox::currentTextChanged, &dlg,
            [&](const QString &nm){ repopulateSizes(nm); describe(nm); });
    connect(sizeBox, &QComboBox::currentTextChanged, &dlg,
            [&](const QString &){ describe(typeBox->currentText()); });
    repopulateSizes(typeBox->currentText());
    describe(typeBox->currentText());
    lay->addWidget(specLabel);

    lay->addWidget(new QLabel(tr("Size:"), &dlg)); lay->addWidget(sizeBox);

    QComboBox *tipBox = nullptr;
    if (stageName.contains("Wire", Qt::CaseInsensitive)) {
        lay->addWidget(new QLabel(tr("Tip shape (hand-shaped before insertion):"), &dlg));
        tipBox = new QComboBox(&dlg);
        tipBox->addItems({"Straight","J-Tip","Angled 45°","Angled 90°"});
        lay->addWidget(tipBox);
    }
    QComboBox *agentBox = nullptr;
    if (stageName.contains("Micro", Qt::CaseInsensitive)) {
        lay->addWidget(new QLabel(tr("Planned embolic agent:"), &dlg));
        agentBox = new QComboBox(&dlg);
        agentBox->addItems({"Onyx 18","Onyx 34","Squid 12","Squid 18","nBCA glue","PHIL"});
        lay->addWidget(agentBox);
    }
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        m_deviceSpec.catheterType   = typeBox->currentText();
        m_deviceSpec.catheterSizeFr = sizeBox->currentText();
        m_deviceSpec.coilType       = agentBox ? agentBox->currentText() : tr("—");
        if (tipBox) m_deviceSpec.wireTipShape = tipBox->currentText();

        // Anything that is not the access sheath becomes the live device.
        // Choosing "Sofia 6F" now actually loads a 2.11 mm catheter with a
        // 16-segment compliant tip, not just a string for the log.
        if (!stageName.contains("Sheath", Qt::CaseInsensitive))
            applyCatheterSpec(m_deviceSpec.catheterType, m_deviceSpec.catheterSizeFr);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Contrast injection — same dialog as old project
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onContrastInjection() {
    // m_contrastDone gates the agent dropdown: cancelling the contrast dialog
    // must not then offer a list of embolics.
    m_contrastDone = false;
    if (!showContrastControlDialog()) return;
    m_quad->showPane(QuadPanel::Pane::Fluoro);
    checkViewAction(1);

    if (m_emboLabel)
        m_emboLabel->setText(tr("Embolisation — Step 5/7 — Contrast confirms microcatheter position."));
    m_ecgWidget->triggerEvent(tr("Contrast injection"), 2500);
    log(tr("[%1] Contrast injection — flow: %2, vol: %3%4")
            .arg(QTime::currentTime().toString("HH:mm:ss"), m_contrastFlowRate, m_contrastVolume)
            .arg(m_contrastAgent.isEmpty() ? QString() : tr(", agent: %1").arg(m_contrastAgent)));
    m_contrastDone = true;

    // An agent chosen in the dialog is deployed as part of the same run. The
    // agent path itself is untouched — this is the same onDeployEmbolic() the
    // old on-screen buttons called.
    if (!m_contrastAgent.isEmpty()) onDeployEmbolic(m_contrastAgent);
}

bool MainWindow::showContrastControlDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Contrast Injection — Injector Control"));
    dlg.setFixedSize(380, 280);
    auto *lay = new QVBoxLayout(&dlg);

    // Embolic agent first: it is the decision that determines what the injection
    // is for. "None" is the default so a plain diagnostic contrast run — the
    // existing behaviour — needs no extra clicks.
    lay->addWidget(new QLabel(tr("Embolic agent:"), &dlg));
    auto *agentBox = new QComboBox(&dlg);
    agentBox->addItem(tr("None (contrast only)"), QString());
    for (const QString &a : {tr("Squid"), tr("Onyx"), tr("DMS"), tr("Glue"), tr("Gel")})
        agentBox->addItem(a, a);
    const int prevAgent = agentBox->findData(m_contrastAgent);
    agentBox->setCurrentIndex(prevAgent >= 0 ? prevAgent : 0);
    lay->addWidget(agentBox);

    lay->addWidget(new QLabel(tr("Flow rate:"), &dlg));
    auto *rateBox = new QComboBox(&dlg);
    rateBox->addItems({"Slow (2 mL/s)","Medium (4 mL/s)","Fast (6 mL/s)"});
    rateBox->setCurrentText(m_contrastFlowRate == "Slow" ? "Slow (2 mL/s)"
                            : m_contrastFlowRate == "Fast" ? "Fast (6 mL/s)" : "Medium (4 mL/s)");
    lay->addWidget(rateBox);
    lay->addWidget(new QLabel(tr("Injection volume:"), &dlg));
    auto *volBox = new QComboBox(&dlg);
    volBox->addItems({"Low (5 mL)","Medium (10 mL)","High (18 mL)"});
    volBox->setCurrentText(m_contrastVolume == "Low" ? "Low (5 mL)"
                           : m_contrastVolume == "High" ? "High (18 mL)" : "Medium (10 mL)");
    lay->addWidget(volBox);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return false;
    m_contrastFlowRate = rateBox->currentText().startsWith("Slow") ? "Slow"
                         : rateBox->currentText().startsWith("Fast") ? "Fast" : "Medium";
    m_contrastVolume   = volBox->currentText().startsWith("Low") ? "Low"
                       : volBox->currentText().startsWith("High") ? "High" : "Medium";
    m_contrastAgent    = agentBox->currentData().toString();   // empty = contrast only
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Road Map / DSA — same visual-mode logic as old project
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onRoadMap() {
    m_roadMapActive = !m_roadMapActive;
    // Switch to fluoro pane and tell it what mode
    m_quad->showPane(QuadPanel::Pane::Fluoro);
    checkViewAction(1);
    m_quad->viewFluoro()->setRoadMapActive(m_roadMapActive);
    if (m_statusLabel)

    log(tr("[%1] Road Map %2").arg(QTime::currentTime().toString("HH:mm:ss"),
                                   m_roadMapActive ? tr("ON") : tr("OFF")));
}

void MainWindow::onDSAView() {
    m_dsaViewActive = !m_dsaViewActive;
    m_quad->showPane(QuadPanel::Pane::DSA);
    checkViewAction(3);
    m_quad->viewDSA()->setDSAActive(m_dsaViewActive);
    if (m_statusLabel)

    log(tr("[%1] DSA View %2").arg(QTime::currentTime().toString("HH:mm:ss"),
                                   m_dsaViewActive ? tr("ON") : tr("OFF")));
}

// ─────────────────────────────────────────────────────────────────────────────
// Embolic deployment
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onDeployEmbolic(const QString &agent) {
    m_quad->showPane(QuadPanel::Pane::Fluoro);
    checkViewAction(1);
    m_ecgWidget->triggerEvent(agent + tr(" injection"), 2500);
    if (m_emboLabel)
        m_emboLabel->setText(tr("Embolisation — %1 injected. Open DSA View to confirm occlusion.").arg(agent));
    if (m_statusLabel)

    log(tr("[%1] %2 deployed.").arg(QTime::currentTime().toString("HH:mm:ss"), agent));
}

// ─────────────────────────────────────────────────────────────────────────────
// View-mode switching
// ─────────────────────────────────────────────────────────────────────────────
// Tick the menu-bar action for a display mode without re-triggering it — the
// pane is switched by the caller, so firing triggered() here would switch twice.
void MainWindow::checkViewAction(int id) {
    if (!m_viewActGroup) return;
    for (QAction *a : m_viewActGroup->actions())
        if (a->data().toInt() == id) { a->setChecked(true); return; }
}

void MainWindow::onViewModeChanged(int id) {
    switch (id) {
    case 0: m_quad->showPane(QuadPanel::Pane::View3D); break;
    case 1: m_quad->showPane(QuadPanel::Pane::Fluoro);  break;
    case 2: m_quad->showPane(QuadPanel::Pane::Mesh);    break;
    case 3: m_quad->showPane(QuadPanel::Pane::DSA);     break;
    }
    m_view->setFocus();
}

// ─────────────────────────────────────────────────────────────────────────────
// Navigation
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onVesselSurfacePicked(const QVector3D &worldPt) {
    // Layer 2: click-to-refine placement
    // Only active when a sheath has been placed and user Alt+clicks
    if (!m_sheath.hasEntry()) return;
    std::string msg;
    const float tol = m_sheath.currentPath().empty() ? 0.05f
                                                     : m_sheath.currentPath().radii[0] * 10.f;  // 10× vessel radius
    const cn::Vec3 wp(worldPt.x(), worldPt.y(), worldPt.z());
    if (m_sheath.setAccessPointFromWorldPoint(wp, tol, msg)) {
        log(tr("Sheath refined: %1").arg(QString::fromStdString(msg)));
        // Update the navigation to start from the new entry node
        const int32_t newNode = m_sheath.entryNode();
        if (newNode >= 0) {
            m_graph.labelBranches(newNode);
            m_nav.startFrom(newNode);
            updateSheathActor();   // move the introducer to the refined entry
            m_pendingMm = 0.0;
            syncDevices();
            for (VesselView *v : {m_view, m_quad->viewFluoro(),
                                  m_quad->viewDSA(), m_quad->viewMesh()})
                if (v) v->update();
        }
    } else {
        log(tr("Sheath pick: %1").arg(QString::fromStdString(msg)));
    }
}

void MainWindow::updateSheathActor() {
    // Layer 3: build the introducer at the current entry point and hand the
    // whole device to the renderer.
    if (!m_sheath.hasEntry()) {
        for (VesselView *v : {m_view, m_quad->viewFluoro()})
            if (v) v->clearSheath();
        return;
    }

    const cn::AccessSite site   = m_sheath.currentSite();
    const bool           radial = !cn::AccessSiteResolver::isFemoral(site);

    // Outward direction (tip -> hub, out of the patient).
    //
    // The axis table below is only a fallback. Note the previous version had
    // both radial cases mirrored: patient left is +X, so the patient's RIGHT
    // wrist hub points -X, not +X. Femoral pointed +Z, which is along the
    // direction of travel and buried the hub inside the vessel.
    cn::Vec3 outDir = radial
                          ? (site == cn::AccessSite::RightRadial ? cn::Vec3(-1.f, 0.f, 0.f)
                                                                 : cn::Vec3( 1.f, 0.f, 0.f))
                          : cn::Vec3(0.f, 0.f, -1.f);

    // Better: the entry node is a vessel terminal, so the vessel leaves it in
    // exactly one direction and the hub points along the reverse. Correct for
    // any site and any patient orientation, with no axis table at all.
    const int32_t entryN = m_sheath.entryNode();
    if (entryN >= 0 && entryN < static_cast<int32_t>(m_graph.nodeCount())) {
        const auto nb = m_graph.neighbours(entryN);
        if (nb.size() == 1) {
            const cn::Vec3 inward =
                m_graph.nodes()[nb.front()].position - m_graph.nodes()[entryN].position;
            if (inward.lengthSquared() > 1e-12f) outDir = (-inward).normalized();
        }
    }

    // Size to the vessel. Transradial access uses a thin-wall ("slender")
    // introducer as standard, which is what lets a larger lumen through a wrist
    // artery at all.
    const bool slender = radial;
    int french = m_sheath.largestFittingFrench(0.92f, slender);
    if (french < 4) {
        french = 4;
        log(tr("%1: lumen ⌀%2 mm is below the smallest introducer — sheath is oversized.")
                .arg(QString::fromLatin1(cn::accessSiteName(site)))
                .arg(2.0 * m_sheath.entryRadius() * m_mmPerUnit, 0, 'f', 2));
    }

    const cn::SheathSpec spec = cn::sheathSpecForFrench(french, radial, slender);
    auto geom = m_sheath.buildSheath(outDir, spec);
    if (!geom) {
        for (VesselView *v : {m_view, m_quad->viewFluoro()})
            if (v) v->clearSheath();
        return;
    }

    for (VesselView *v : {m_view, m_quad->viewFluoro()})
        if (v) v->setSheath(*geom);

    log(tr("Sheath: %1 Fr %2%3 — ⌀%4 mm OD in a ⌀%5 mm vessel, %6 mm inserted.%7")
            .arg(french)
            .arg(slender ? tr("slender ") : QString())
            .arg(radial ? tr("radial") : tr("femoral"))
            .arg(spec.outerDiaMm, 0, 'f', 2)
            .arg(2.0 * geom->vesselRadius * m_mmPerUnit, 0, 'f', 2)
            .arg(spec.workingLenMm, 0, 'f', 0)
            .arg(geom->oversized ? tr("  ** OVERSIZED **") : QString()));
}

void MainWindow::onAccessSiteChosen(int index) {
    if (index < 0 || index >= static_cast<int>(m_sites.size())) return;
    if (!m_sites[index].valid) return;
    m_activeSite = index;
    restartFromCurrentSite(true);
    log(tr("--- puncture: %1 ---").arg(QString::fromLatin1(cn::accessSiteName(m_sites[index].site))));
}

void MainWindow::restartFromCurrentSite(bool focusCamera) {
    if (m_activeSite < 0) return;
    const auto &site = m_sites[m_activeSite];

    // Use SheathManager entry node if available (it may have been refined by click)
    // Otherwise fall back to the raw access site node
    int32_t startNode = site.node;
    if (m_sheath.hasEntry() && m_sheath.entryNode() >= 0)
        startNode = m_sheath.entryNode();

    m_graph.labelBranches(startNode);
    m_nav.setViewFrame(
        cn::Vec3(m_view->screenRight().x(), m_view->screenRight().y(), m_view->screenRight().z()),
        cn::Vec3(m_view->screenUp().x(),    m_view->screenUp().y(),    m_view->screenUp().z()));
    m_nav.startFrom(startNode);

    // Build and display sheath geometry at the entry point
    updateSheathActor();

    if (focusCamera) {
        const cn::Vec3 &ap = m_graph.nodes()[startNode].position;
        for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
            v->focusOn(QVector3D(ap.x, ap.y, ap.z), m_graph.nodes()[startNode].radius);
        m_view->setCameraFollows(true);
    }
    m_pendingMm = 0.0;
    if (m_motion->isActive()) m_motion->stop();

    // Placing the sheath establishes the access and the route, nothing more.
    // The catheter is a separate device that has to be chosen and introduced,
    // so it is not attached to the views and navigation stays locked until
    // Catheter Selection runs. Previously this set m_ready and handed the
    // views a device the operator had not picked yet, so a catheter appeared
    // in the vessel the instant the sheath went in.
    m_sheathReady = true;
    m_ready       = m_catheterReady;
    if (m_btnCatheterSelect) m_btnCatheterSelect->setEnabled(true);
    updateDeviceGating();

    if (m_catheterReady) {
        for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
            v->setDevice(&m_catheter);
        syncDevices();
    } else {
        for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
            v->setDevice(nullptr);
        pushOverlay();
        updateReadouts();
    }
    // status: navigation active
    m_view->setFocus();
}

void MainWindow::onFocusAccess() {
    if (m_activeSite < 0 || m_activeSite >= static_cast<int>(m_sites.size())) return;
    const cn::Vec3 &p = m_graph.nodes()[m_sites[m_activeSite].node].position;
    m_view->focusOn(QVector3D(p.x, p.y, p.z), m_graph.nodes()[m_sites[m_activeSite].node].radius);
}

void MainWindow::onShowWholeVessel() {
    for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
        v->showWholeVessel();
}

void MainWindow::onAdvance() {
    if (!m_ready) return;
    m_pendingMm = std::clamp(m_pendingMm + m_stepSpin->value(), -60.0, 60.0);
    if (!m_motion->isActive()) m_motion->start();
}

void MainWindow::onRetract() {
    if (!m_ready) return;
    m_pendingMm = std::clamp(m_pendingMm - m_stepSpin->value(), -60.0, 60.0);
    if (!m_motion->isActive()) m_motion->start();
}

// Position of a device class in the coaxial stack. Larger is further inside:
// a microcatheter goes up a guide, a wire goes up a microcatheter.
int MainWindow::deviceStackLevel(cn::DeviceClass c) {
    switch (c) {
    case cn::DeviceClass::GuideSheath:   return 0;
    case cn::DeviceClass::BaseGuide:     return 1;
    case cn::DeviceClass::Guide:         return 1;
    case cn::DeviceClass::DistalAccess:  return 2;
    case cn::DeviceClass::Microcatheter: return 3;
    case cn::DeviceClass::Guidewire:     return 4;
    case cn::DeviceClass::Embolectomy:   return 2;
    }
    return 1;
}

// Advance with the coaxial floor applied: an inner device cannot be pulled back
// behind the tip of the device it was introduced through — that would withdraw
// it into the host lumen rather than out of the patient.
float MainWindow::advanceDevice(float ds) {
    if (ds < 0.f && m_nav.depth() + ds < m_coaxialBaseDepth)
        ds = m_coaxialBaseDepth - m_nav.depth();
    if (ds == 0.f) return 0.f;
    return m_nav.advance(ds);
}

// ─────────────────────────────────────────────────────────────────────────────
// onCardiacTick — advance the shared cardiac clock and repaint what depends on it.
//
// Stepped by measured elapsed time rather than by the nominal timer interval,
// because a 16 ms QTimer does not fire every 16 ms under load. Using the nominal
// value makes the simulated heart rate quietly slower than the number in the
// spin box whenever the renderer is busy.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onCardiacTick() {
    const qint64 ms = m_heartClock.restart();
    // Clamp: after a stall or a debugger break, replaying a huge dt would jump
    // the phase by several beats at once.
    const float dt = std::clamp(static_cast<float>(ms) / 1000.f, 0.f, 0.1f);
    m_cardiac.advance(dt);

    if (m_bpmSpin && !m_bpmSpin->hasFocus())
        m_bpmSpin->setToolTip(tr("instantaneous %1 bpm")
                                  .arg(m_cardiac.instantaneousBpm(), 0, 'f', 1));

    // Only the visible view needs repainting for the pulsation.
    if (m_view) m_view->update();
    if (m_quad && m_quad->viewFluoro()) m_quad->viewFluoro()->update();
}

void MainWindow::onHeartRateChanged(int bpm) {
    m_cardiac.setHeartRate(bpm);
    if (m_ecgWidget) m_ecgWidget->setHeartRate(bpm);
    log(tr("Heart rate set to %1 bpm (cycle %2 ms).")
            .arg(bpm).arg(m_cardiac.cycleSeconds() * 1000.0, 0, 'f', 0));
}

void MainWindow::onMotionTick() {
    if (!m_ready || std::fabs(m_pendingMm) < 1e-4) {
        m_pendingMm = 0.0; m_motion->stop(); pushOverlay(); return;
    }
    const double dt   = m_motion->interval() / 1000.0;
    const double sign = m_pendingMm > 0.0 ? 1.0 : -1.0;
    const double res  = (sign > 0.0) ? 1.0 - 0.65*m_catheter.state().contactSeverity : 1.0;
    const double want = std::min(std::fabs(m_pendingMm), m_speedSpin->value()*dt*std::max(res,0.15));
    const float  wU   = static_cast<float>(sign * want / m_mmPerUnit);
    const size_t tb   = m_nav.turnsTaken();
    const float  moved= advanceDevice(wU);
    m_pendingMm -= static_cast<double>(moved) * m_mmPerUnit;
    if (m_nav.turnsTaken() < tb)
        log(tr("[%1] pulled back past bifurcation").arg(QTime::currentTime().toString("HH:mm:ss")));
    if (std::fabs(moved) < std::fabs(wU) - 1e-6f) {
        m_pendingMm = 0.0; m_motion->stop();
        // status: motion ended
    }
    syncDevices();
}

void MainWindow::onChooseLeft() {
    if (!m_ready || !m_nav.decisionPending()) return;
    m_pendingMm = 0.0;
    if (m_nav.chooseLeft()) {
        log(tr("[%1] node %2: took LEFT limb").arg(QTime::currentTime().toString("HH:mm:ss")).arg(m_nav.pendingJunctionNode()));
        syncDevices();
    }
}

void MainWindow::onChooseRight() {
    if (!m_ready || !m_nav.decisionPending()) return;
    m_pendingMm = 0.0;
    if (m_nav.chooseRight()) {
        log(tr("[%1] node %2: took RIGHT limb").arg(QTime::currentTime().toString("HH:mm:ss")).arg(m_nav.pendingJunctionNode()));
        syncDevices();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// C-arm
// ─────────────────────────────────────────────────────────────────────────────
// ── C-arm controls — all routed through CarmController ───────────────────────
// Controller owns the clamped state and sends incremental camera deltas to
// linked views (fluoro + DSA), preserving their current zoom/pan.
// ─────────────────────────────────────────────────────────────────────────────
// C-arm rotation handlers.
//
// These are named for the anatomy they produce, NOT for the key that reaches
// them — the same four are invoked by mouse from the C-arm menu, so an entry
// labelled "LAO" has to actually perform an LAO whichever key is bound to it.
// The A/D <-> W/S swap therefore lives in buildMenus(), at the binding, and the
// current wiring is:
//
//     A -> onCarmCRAN()   W -> onCarmLAO()
//     D -> onCarmCAUD()   S -> onCarmRAO()
//
// Each logs which key drove it and the resulting angles, so the mapping can be
// confirmed from the procedure log instead of inferred from the picture. That
// matters here because the C-arm camera looks down -Y: LAO is a rotation about
// Y, which lies in the screen plane and reads as a large spin, while CRAN is a
// rotation about X, which is foreshortened from this viewpoint and moves far
// less on screen. The two are easy to mistake for "nothing happened".
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::logCarm(const char *key, const char *what) {
    if (!m_carmCtrl) return;
    log(tr("[C-arm] %1 -> %2   LAO %3°  CRAN %4°")
            .arg(QString::fromLatin1(key), QString::fromLatin1(what))
            .arg(m_carmCtrl->laoAngle(),  0, 'f', 1)
            .arg(m_carmCtrl->cranAngle(), 0, 'f', 1));
}

void MainWindow::onCarmLAO()  { if (!m_carmCtrl) return; m_carmCtrl->rotateLAO( 5.0); logCarm("W", "LAO  +5"); }
void MainWindow::onCarmRAO()  { if (!m_carmCtrl) return; m_carmCtrl->rotateLAO(-5.0); logCarm("S", "RAO  -5"); }
void MainWindow::onCarmCRAN() { if (!m_carmCtrl) return; m_carmCtrl->rotateCRAN( 5.0); logCarm("A", "CRAN +5"); }
void MainWindow::onCarmCAUD() { if (!m_carmCtrl) return; m_carmCtrl->rotateCRAN(-5.0); logCarm("D", "CAUD -5"); }
void MainWindow::onCarmReset() {
    if (m_carmCtrl) m_carmCtrl->resetAll();   // rig only — camera reset is separate
    // The console's fluoro timer zeroes with the rig, as it does on a real
    // machine at the start of a case.
    if (m_carmWidget) m_carmWidget->resetFluoroTime();
    m_view->resetCamera();                     // vessel camera reset (independent)
    m_quad->viewFluoro()->resetCamera();
    m_quad->viewDSA()->resetCamera();
    log(tr("[%1] C-arm reset PA 0/0").arg(QTime::currentTime().toString("HH:mm:ss")));
}

void MainWindow::applyCarmAngles() {
    // Legacy path — now just syncs from CarmController state if needed.
    // Normally unused; CarmController routes everything directly.
    if (!m_carmCtrl) return;
    onCarmAnglesChanged(m_carmCtrl->laoAngle(), m_carmCtrl->cranAngle());
}

void MainWindow::onCarmAnglesChanged(double lao, double cran) {
    // Clinical convention: sign determines which label is shown (LAO vs RAO, CRAN vs CAUD)
    const QString laoStr  = (lao  >= 0)
                               ? tr("LAO %1°").arg( lao,  0,'f',1)
                               : tr("RAO %1°").arg(-lao,  0,'f',1);
    const QString cranStr = (cran >= 0)
                                ? tr("CRAN %1°").arg( cran, 0,'f',1)
                                : tr("CAUD %1°").arg(-cran, 0,'f',1);
    if (m_laoLabel)  m_laoLabel->setText(laoStr);
    if (m_cranLabel) m_cranLabel->setText(cranStr);
    // Keep MainWindow state in sync with controller for overlay readouts
    if (m_carmCtrl) {
        m_carmLAO  = m_carmCtrl->laoAngle();
        m_carmCRAN = m_carmCtrl->cranAngle();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// View / camera
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onFrontalLockToggled(bool on) { m_view->setFrontalLock(on); }
void MainWindow::onCameraFollowToggled(bool on) {
    m_view->setCameraFollows(on);
    if (on) syncDevices(); else m_view->showWholeVessel();
}
void MainWindow::onCameraChanged() {
    const QVector3D r = m_view->screenRight(), u = m_view->screenUp();
    m_nav.setViewFrame(cn::Vec3(r.x(),r.y(),r.z()), cn::Vec3(u.x(),u.y(),u.z()));
    pushOverlay();
}
void MainWindow::onPaletteChanged(int) {}
void MainWindow::onOpacityChanged(int percent) { m_view->setWallOpacity(percent/100.f); }

// ─────────────────────────────────────────────────────────────────────────────
// Device
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onDeviceChanged(int index) {
    // Retained for the embolectomy preset path only. Nothing calls it now that
    // the device is chosen through Catheter Selection, and it must not run
    // implicitly: it sets hasBalloon and would replace the catalogue device.
    if (index < 0 || index >= static_cast<int>(std::size(kPresets))) return;
    const EmbolectomyPreset &p = kPresets[index];
    cn::CatheterModel::Params c = m_catheter.params();
    c.deviceRadius    = static_cast<float>(0.5 * p.shaftOdMm         / m_mmPerUnit);
    c.segmentLength   = static_cast<float>(kSegLenMm                  / m_mmPerUnit);
    c.segmentCount    = 40;
    c.tipFlexSegments = kFlexSeg;
    c.maxTipDeflection= qDegreesToRadians(kMaxDefDeg);
    c.restingTipCurve = qDegreesToRadians(kRestCurvDeg);
    c.autoLength      = true;
    c.hasBalloon      = true;
    c.balloonMaxRadius= static_cast<float>(0.5 * p.balloonDiameterMm / m_mmPerUnit);
    c.balloonLength   = static_cast<float>(p.balloonLengthMm          / m_mmPerUnit);
    c.balloonOffset   = static_cast<float>(p.tipToBalloonMm           / m_mmPerUnit);
    m_catheter.setParams(c);
    if (m_odSpin) { const QSignalBlocker sb(m_odSpin); m_odSpin->setValue(p.shaftOdMm); }
    if (m_ready) syncDevices();
}
void MainWindow::onDeviceSizeChanged(double od) {
    auto p = m_catheter.params();
    p.deviceRadius = static_cast<float>(0.5 * od / m_mmPerUnit);
    m_catheter.setParams(p);
    if (m_ready) syncDevices();
}
void MainWindow::onScaleChanged(double mmpu) {
    m_mmPerUnit = mmpu;
    if (!m_sites.empty()) resolveAccessSites();
    updateReadouts();
}
void MainWindow::onBalloonToggled() {
    const bool inflate = m_catheter.balloonInflation() < 0.5f;
    m_catheter.setBalloonInflation(inflate ? 1.f : 0.f);
    if (m_balloonAction) { const QSignalBlocker sb(m_balloonAction); m_balloonAction->setChecked(inflate); }
    if (m_ready) {
        syncDevices();
        const auto &st = m_catheter.state();
        log(tr("[%1] balloon %2 at %3 mm — ⌀%4 mm, %5% apposition")
                .arg(QTime::currentTime().toString("HH:mm:ss"))
                .arg(inflate ? tr("INFLATED") : tr("deflated"))
                .arg(m_nav.depth()          * m_mmPerUnit, 0,'f',1)
                .arg(2.0*st.balloonRadius   * m_mmPerUnit, 0,'f',1)
                .arg(100.0*st.balloonApposition, 0,'f',0));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Sync
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::syncDevices() {
    // Enable Left/Right buttons ONLY when at a bifurcation decision point
    const bool atBifurc = m_ready && m_nav.decisionPending();
    if (m_btnLeft)  m_btnLeft->setEnabled(atBifurc);
    if (m_btnRight) m_btnRight->setEnabled(atBifurc);
    // Dim them visually when disabled
    // min-height must stay 0 here too, or a disabled Left/Right button springs
    // back to 44px and re-opens the clipping this was fixed for.
    // Height is fixed on the widget, so this only restyles; it must not set a
    // min-height or the dimmed button would disagree with its neighbours.
    const QString dimStyle = "QPushButton{background:#141414;color:#555555;border:1px solid #333333;"
                             "border-radius:3px;font-size:11px;font-weight:bold;"
                             "min-height:0px;padding:2px 6px;}";
    const QString activeStyle = ""; // use global btnNav style
    if (m_btnLeft)  m_btnLeft->setStyleSheet(atBifurc  ? activeStyle : dimStyle);
    if (m_btnRight) m_btnRight->setStyleSheet(atBifurc ? activeStyle : dimStyle);
    if (!m_ready) return;
    const cn::CenterlinePath &path = m_nav.path();
    const float depth = m_nav.depth();
    m_catheter.setPath(path);
    m_catheter.setInsertionDepth(depth);
    m_catheter.update();
    for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
        v->refreshDevice();
    const auto &cs = m_catheter.state();
    if (cs.wallContact && !m_wasInContact) {
        QApplication::beep();
        // wall contact beep only
        log(tr("[%1] WALL CONTACT at %2 mm — lumen ⌀%3 mm vs device ⌀%4 mm")
                .arg(QTime::currentTime().toString("HH:mm:ss"))
                .arg(depth*m_mmPerUnit,0,'f',1)
                .arg(2.0*cs.lumenRadiusAtTip*m_mmPerUnit,0,'f',2)
                .arg(2.0*m_catheter.params().deviceRadius*m_mmPerUnit,0,'f',2));
        m_ecgWidget->triggerEvent(tr("Wall"), 2000);
    }
    m_wasInContact = cs.wallContact;
    for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
        v->setCameraTrack(&path, depth);
    m_view->syncCameraToDevice();
    const QVector3D r = m_view->screenRight(), u = m_view->screenUp();
    m_nav.setViewFrame(cn::Vec3(r.x(),r.y(),r.z()), cn::Vec3(u.x(),u.y(),u.z()));
    pushOverlay();
    updateReadouts();
}

void MainWindow::pushOverlay() {
    VesselView::Overlay o;
    if (m_activeSite >= 0 && m_activeSite < static_cast<int>(m_sites.size()))
        o.accessSite = QString::fromLatin1(cn::accessSiteName(m_sites[m_activeSite].site));
    o.deviceName  = m_activeCatheter;
    const auto &cs= m_catheter.state();
    o.depthMm     = m_nav.depth() * m_mmPerUnit;
    o.lumenMm     = 2.0 * cs.lumenRadiusAtTip * m_mmPerUnit;
    o.deviceMm    = 2.0 * m_catheter.params().deviceRadius * m_mmPerUnit;
    o.clearanceMm = cs.minWallClearance * m_mmPerUnit;
    o.branchId    = cs.currentBranchId;
    o.turns       = static_cast<int>(m_nav.turnsTaken());
    o.wallContact = cs.wallContact;
    o.deadEnd     = m_nav.atDeadEnd() && !m_nav.decisionPending()
                && m_nav.depth() >= m_nav.navigableLength() - 1e-6f;
    o.decisionPending  = m_nav.decisionPending();
    o.moving           = m_motion && m_motion->isActive();
    o.contactSeverity  = cs.contactSeverity;
    o.balloonMm        = 2.0 * cs.balloonRadius * m_mmPerUnit;
    o.ballooonAppositionPct = 100.0 * cs.balloonApposition;
    o.balloonInflated  = cs.balloonInflated;
    o.carmLAO  = m_carmLAO;
    o.carmCRAN = m_carmCRAN;
    const auto &opts = m_nav.options();
    for (size_t i = 0; i < opts.size(); ++i) {
        VesselView::Overlay::Option vo;
        const cn::Vec3 &p = m_graph.nodes()[opts[i].node].position;
        vo.target    = QVector3D(p.x, p.y, p.z);
        vo.label     = (i==0) ? tr("LEFT") : (i+1==opts.size()) ? tr("RIGHT") : QString::number(i+1);
        vo.highlight = (i==0 || i+1==opts.size());
        vo.lumenMm   = 2.0 * opts[i].radius * m_mmPerUnit;
        vo.takeoffDeg= opts[i].takeoffAngleDeg;
        o.options.push_back(vo);
    }
    for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
        v->setOverlay(o);
}

void MainWindow::updateReadouts() {
    if (!m_ready) return;
    const auto &cs = m_catheter.state();
    if (m_fDepth)     m_fDepth->setText(mm(m_nav.depth()));
    if (m_fRemaining) m_fRemaining->setText(mm(m_nav.navigableLength() - m_nav.depth()));
    if (m_fLumen)     m_fLumen->setText(QString::number(2.0*cs.lumenRadiusAtTip*m_mmPerUnit,'f',2)+" mm");
    if (m_fDevice)    m_fDevice->setText(QString::number(2.0*m_catheter.params().deviceRadius*m_mmPerUnit,'f',2)+" mm");
    if (m_fClear) {
        m_fClear->setText(QString::number(cs.minWallClearance*m_mmPerUnit,'f',2)+" mm");
        // Wall contact was signalled by turning this field red. On a greyscale
        // console a hue carries no meaning, so the alarm is now carried by
        // inversion instead — white-on-black becomes black-on-white and bold.
        // That reads at a glance and survives being printed or screen-grabbed
        // in mono, which a colour cue does not.
        m_fClear->setStyleSheet(cs.wallContact
                                    ? "color:#000000; background:#F0F0F0; font-family:monospace; font-size:10px;"
                                      "font-weight:bold; padding:2px 4px; border-radius:2px; border:1px solid #FFFFFF;"
                                    : "color:#C0C0C0; background:#000000; font-family:monospace; font-size:10px;"
                                      "padding:2px 4px; border-radius:2px; border:1px solid #272727;");
    }
    if (m_fBranch) m_fBranch->setText(QString::number(cs.currentBranchId));
    if (m_fTurns)  m_fTurns->setText(QString::number(m_nav.turnsTaken()));
    if (m_fStatus) {
        QString s;
        if (m_nav.decisionPending()) s = tr("BIFURCATION");
        else if (m_nav.atDeadEnd() && m_nav.depth() >= m_nav.navigableLength()-1e-6f) s = tr("DISTAL");
        else if (cs.wallContact) s = tr("WALL CONTACT");
        else s = tr("tracking");
        m_fStatus->setText(s);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Cine Studio
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onOpenCineStudio() {
    if (m_cineStudio && m_view) {
        m_cineStudio->beginRecording(true, tr("Run %1").arg(++m_cineRunCounter), 15);
        m_cineStudio->recordFrameFromWidget(m_view);
        m_cineStudio->endRecording();
    }
    m_pageStack->setCurrentIndex(1);
    setWindowTitle(tr("EmboNav — Cine Studio"));
}
void MainWindow::onCineStudioBack() {
    m_pageStack->setCurrentIndex(0);
    setWindowTitle(tr("EmboNav — Endovascular Navigation Console"));
    m_view->setFocus();
}

void MainWindow::log(const QString &l) { if (m_log) m_log->appendPlainText(l); }
QString MainWindow::mm(float v, int d) const {
    return QString::number(v * m_mmPerUnit, 'f', d) + " mm";
}