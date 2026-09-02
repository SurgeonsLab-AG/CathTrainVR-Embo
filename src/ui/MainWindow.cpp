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
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFont>
#include <QListWidget>
#include <QDateTime>
#include <QOpenGLWidget>
#include <QPixmap>
#include <QStyle>
#include <QPainter>
#include <QPaintEvent>
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

struct SampleCase {
    const char *caseId;
    const char *name;         ///< "Surname, F.M."
    const char *sex;          ///< "Male" / "Female"
    int age;
    int heightCm;
    int weightKg;
    const char *pathology;    ///< empty ("") for the default/generic list
};

struct ModuleInfo {
    const char *name;
    std::initializer_list<SampleCase> cases;
};

struct ModuleCategory {
    const char *name;
    std::initializer_list<ModuleInfo> modules;
};

/// Default case list shown before any module is selected - generic vessel
/// anatomy, no pathology line, matching the reference design's own default
/// view. Only one actual vessel dataset exists in this project, so every
/// entry (default and per-module) resolves to the same loaded anatomy when
/// picked - see MainWindow::onCaseSelected()'s own comment for the full
/// explanation of what "selecting a case" actually does here.
const SampleCase kDefaultCases[] = {
    { "Case_1100", "Foxcroft, R.M.", "Male",   56, 186, 85, "" },
    { "Case_1101", "McGarry, P.",    "Male",   54, 189, 84, "" },
    { "Case_1102", "Coaker, V.",     "Female", 52, 177, 81, "" },
    { "Case_1103", "Chishti, R.",    "Female", 48, 176, 76, "" },
    { "Case_1104", "Sheppard, T.",   "Female", 53, 167, 68, "" },
    { "Case_1105", "Thornberry, I.L.","Female",83, 175, 75, "" },
    { "Case_1106", "Thewliss, A.",   "Female", 43, 174, 75, "" },
    { "Case_1107", "Okafor, B.",     "Male",   61, 178, 92, "" },
};

const ModuleCategory kModuleCategories[] = {
    { "Neuroradiology", {
        { "Module AIS", {
            { "Case_1200", "Larkin, S.",     "Male",   67, 183, 83, "Right M1 segment, thrombus: 10 mm" },
            { "Case_1201", "Vance, D.",      "Female", 71, 175, 79, "Left M1 segment, thrombus: 8 mm" },
            { "Case_1202", "Pemberton, K.",  "Male",   58, 182, 85, "Right M1 segment, thrombus: 9 mm" },
            { "Case_1203", "Achebe, N.",     "Female", 64, 184, 81, "Left M1 segment, thrombus: 14 mm" },
        } },
        { "Module CA", {
            { "Case_1105", "Thornberry, I.L.","Female",83, 175, 75, "Left internal carotid, aneurysm diameter: 7 mm, opening: 0.7 mm" },
            { "Case_1108", "Whitlow, E.",    "Male",   59, 182, 92, "Left internal carotid, aneurysm diameter: 6 mm, opening: 0.7 mm" },
            { "Case_1109", "Marsh, T.",      "Male",   66, 170, 70, "Left vertebral artery, aneurysm diameter: 11 mm, opening: 0.7 mm" },
            { "Case_1126", "Osei, F.",       "Female", 61, 174, 68, "Basilar artery, aneurysm diameter: 12 mm, opening: 0.8 mm" },
        } },
        { "Module ICS", {
            { "Case_1210", "Brannigan, W.",  "Male",   55, 179, 88, "Right ICA, severe stenosis: 82%" },
            { "Case_1211", "Delacroix, M.",  "Female", 69, 168, 71, "Left ICA, severe stenosis: 75%" },
            { "Case_1212", "Yusuf, H.",      "Male",   72, 174, 79, "Right ICA, near-occlusive stenosis: 90%" },
        } },
        { "Module CAS", {
            { "Case_1220", "Fothergill, R.", "Male",   64, 180, 90, "Left carotid bifurcation, calcified plaque, stenosis: 78%" },
            { "Case_1221", "Nakamura, S.",   "Female", 70, 165, 63, "Right carotid bifurcation, stenosis: 70%" },
            { "Case_1222", "Odutola, J.",    "Male",   68, 177, 84, "Left carotid bifurcation, stenosis: 85%" },
        } },
    } },
    { "Cardiology", {
        { "Module PCI angiography", {
            { "Case_2100", "Wickramaratne, D.","Male", 60, 176, 82, "Diagnostic angiography, LAD irregularity noted" },
            { "Case_2101", "Halloran, P.",   "Female", 57, 165, 70, "Diagnostic angiography, no significant stenosis" },
            { "Case_2102", "Grantham, O.",   "Male",   63, 181, 95, "Diagnostic angiography, RCA irregularity noted" },
        } },
        { "Module PCI", {
            { "Case_2110", "Ibori, C.",      "Male",   58, 174, 88, "LAD, stenosis: 80%" },
            { "Case_2111", "Sundqvist, L.",  "Female", 62, 168, 74, "RCA, stenosis: 70%" },
            { "Case_2112", "Featherstone, G.","Male",  55, 179, 91, "LCx, stenosis: 75%" },
        } },
        { "Module PCI Bypass", {
            { "Case_2120", "Adeyemi, T.",    "Male",   67, 177, 86, "SVG to RCA, graft stenosis: 65%" },
            { "Case_2121", "Kowalczyk, M.",  "Female", 71, 162, 69, "LIMA to LAD, graft stenosis: 55%" },
        } },
        { "Module PCI anomalies", {
            { "Case_2130", "Ssemakula, R.",  "Male",   44, 180, 79, "Anomalous RCA origin from left sinus" },
            { "Case_2131", "Pettengill, A.", "Female", 39, 170, 65, "Anomalous LCx origin from right sinus" },
        } },
        { "Module PCI bifurcation", {
            { "Case_2140", "Draycott, H.",   "Male",   61, 178, 84, "LAD/Diagonal bifurcation, Medina 1,1,1" },
            { "Case_2141", "Falkenrath, U.", "Female", 66, 166, 72, "LCx/OM bifurcation, Medina 1,0,1" },
        } },
        { "Module PCI complication", {
            { "Case_2150", "Chibuzo, E.",    "Male",   59, 175, 90, "RCA dissection during wiring" },
            { "Case_2151", "Vandermolen, K.","Female", 64, 169, 77, "LAD perforation, contained" },
        } },
        { "Module PCI AMI", {
            { "Case_2160", "Osayande, F.",   "Male",   52, 176, 88, "STEMI, RCA total occlusion" },
            { "Case_2161", "Brechtel, N.",   "Female", 58, 163, 71, "STEMI, LAD total occlusion" },
        } },
        { "Module PCI FFR", {
            { "Case_2170", "Adegoke, L.",    "Male",   56, 179, 85, "LAD intermediate stenosis, FFR pending" },
            { "Case_2171", "Holbrook, S.",   "Female", 61, 167, 73, "RCA intermediate stenosis, FFR pending" },
        } },
        { "Module PCI multi vessel disease", {
            { "Case_2180", "Nnamdi, C.",     "Male",   65, 177, 92, "LAD 80%, RCA 70%, LCx 60%" },
            { "Case_2181", "Ostrowski, J.",  "Female", 69, 164, 76, "LAD 75%, RCA 85%" },
        } },
        { "Module CRM", {
            { "Case_2190", "Balewa, S.",     "Male",   74, 173, 80, "Complete heart block, pacemaker indicated" },
            { "Case_2191", "Trelawny, F.",   "Female", 78, 160, 66, "Sick sinus syndrome, pacemaker indicated" },
        } },
        { "Module TAVI", {
            { "Case_2200", "Okonkwo, V.",    "Male",   81, 172, 78, "Severe aortic stenosis, valve area 0.7 cm2" },
            { "Case_2201", "Winterbourne, M.","Female",84, 158, 61, "Severe aortic stenosis, valve area 0.6 cm2" },
        } },
    } },
    { "Peripheral Interventions", {
        { "Module PVI Angiography", {
            { "Case_3100", "Duplessis, R.",  "Male",   62, 178, 87, "Diagnostic run-off, mild diffuse disease" },
            { "Case_3101", "Enendu, K.",     "Female", 58, 166, 70, "Diagnostic run-off, no significant disease" },
        } },
        { "Module PVI", {
            { "Case_3110", "Balogun, T.",    "Male",   64, 179, 89, "Superficial femoral artery, stenosis: 70%" },
            { "Case_3111", "Castellanos, I.","Female", 60, 165, 72, "Popliteal artery, stenosis: 65%" },
        } },
        { "Module PVI Iliac", {
            { "Case_3120", "Ogunleye, D.",   "Male",   66, 177, 91, "Right common iliac, stenosis: 80%" },
            { "Case_3121", "Warrington, B.", "Female", 63, 168, 74, "Left external iliac, occlusion" },
        } },
        { "Module PVI Femoropopliteal", {
            { "Case_3130", "Adebayo, M.",    "Male",   68, 175, 84, "SFA, long-segment occlusion: 18 cm" },
            { "Case_3131", "Fennimore, C.",  "Female", 71, 161, 65, "Popliteal, focal stenosis: 75%" },
        } },
        { "Module PVI Below Knee (BTK)", {
            { "Case_3140", "Igwebuike, S.",  "Male",   70, 174, 79, "Peroneal artery, occlusion, CLI" },
            { "Case_3141", "Alderwood, P.",  "Female", 73, 159, 60, "Anterior tibial, diffuse disease" },
        } },
        { "Module PVI Renal", {
            { "Case_3150", "Nwachukwu, G.",  "Male",   57, 176, 88, "Right renal artery, stenosis: 75%" },
            { "Case_3151", "Sinclair-Wren, O.","Female",60, 164, 71, "Left renal artery, stenosis: 68%" },
        } },
        { "Module PVI Carotid", {
            { "Case_3160", "Uzoigwe, A.",    "Male",   69, 178, 85, "Left carotid, stenosis: 72%" },
            { "Case_3161", "Beauchamp, L.",  "Female", 65, 167, 73, "Right carotid, stenosis: 68%" },
        } },
        { "Module PVI Subclavian", {
            { "Case_3170", "Chukwuma, E.",   "Male",   61, 180, 90, "Left subclavian, stenosis: 80%, subclavian steal" },
            { "Case_3171", "Radulescu, I.",  "Female", 55, 163, 69, "Right subclavian, occlusion" },
        } },
        { "Module PVI Mesenteric", {
            { "Case_3180", "Okwuosa, B.",    "Male",   67, 177, 86, "Superior mesenteric artery, stenosis: 78%" },
            { "Case_3181", "Vasilenko, T.",  "Female", 72, 162, 67, "Celiac trunk, stenosis: 70%" },
        } },
        { "Module PVI CTO", {
            { "Case_3190", "Emeagwali, N.",  "Male",   63, 179, 88, "SFA chronic total occlusion: 22 cm" },
            { "Case_3191", "Kaczmarek, W.",  "Female", 68, 165, 72, "Popliteal chronic total occlusion: 14 cm" },
        } },
        { "Module PVI Bifurcation", {
            { "Case_3200", "Oyelaran, F.",   "Male",   59, 178, 83, "Common femoral bifurcation, Medina 1,1,0" },
            { "Case_3201", "Herrenschmidt, U.","Female",64, 166, 71, "Popliteal trifurcation, stenosis: 65%" },
        } },
        { "Module PVI Critical Limb Ischemia (CLI/CLTI)", {
            { "Case_3210", "Adeoye, R.",     "Male",   72, 173, 76, "Multilevel occlusive disease, tissue loss" },
            { "Case_3211", "Prokopenko, S.", "Female", 75, 158, 58, "Multilevel occlusive disease, rest pain" },
        } },
        { "Module PVI Acute Limb Ischemia (ALI)", {
            { "Case_3220", "Chiedozie, K.",  "Male",   66, 176, 84, "Acute SFA thrombosis, Rutherford IIb" },
            { "Case_3221", "Wozniak, E.",    "Female", 70, 164, 70, "Acute popliteal embolism, Rutherford IIa" },
        } },
        { "Module PVI Thrombectomy", {
            { "Case_3230", "Ogundipe, L.",   "Male",   61, 177, 87, "SFA fresh thrombus, aspiration indicated" },
            { "Case_3231", "Marchetti, V.",  "Female", 65, 166, 73, "Popliteal fresh thrombus, aspiration indicated" },
        } },
        { "Module PVI Complication", {
            { "Case_3240", "Nnaji, D.",      "Male",   58, 179, 89, "Common femoral access site pseudoaneurysm" },
            { "Case_3241", "Bergstrom, A.",  "Female", 63, 165, 72, "Retroperitoneal hematoma post-access" },
        } },
        { "Module PVI Aneurysm", {
            { "Case_3250", "Okorocha, P.",   "Male",   71, 176, 82, "Popliteal artery aneurysm, diameter: 28 mm" },
            { "Case_3251", "Villanueva, C.", "Female", 74, 160, 63, "Common femoral aneurysm, diameter: 22 mm" },
        } },
    } },
};

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
// Quick-size range offered in the Calibration box: 1-3 Fr, matching the
// diagnostic-catheter body's own catalogue size list (CatheterCatalog.cpp).
constexpr int kMinFrench = 1;
constexpr int kMaxFrench = 3;

constexpr float kSegLenMm  = 1.5f;
constexpr int   kFlexSeg   = 6;
constexpr float kMaxDefDeg = 45.f;
constexpr float kRestCurvDeg = 0.f;

// ── Exact old-project stylesheet ─────────────────────────────────────────────
const char *kStyle = R"(
* {
    font-family: "Titillium Web";
}
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
QListWidget {
    background:#000000; border:1px solid #3B3B3B; border-radius:3px;
    color:#C9C9C9; font-size:11px; outline:none;
}
QListWidget::item { padding:4px 6px; border-bottom:1px solid #232323; }
QListWidget::item:selected { background:#2A2A2E; color:#E8E8E8; }
QListWidget::item:hover    { background:#1A1A1A; }
QSlider::groove:horizontal { background:#3B3B3B; height:4px; border-radius:2px; }
QSlider::handle:horizontal { background:#6F6F6F; width:12px; height:12px; margin:-4px 0; border-radius:6px; }
QScrollBar:vertical { background:#000000; width:8px; }
QScrollBar::handle:vertical { background:#3B3B3B; border-radius:4px; }
QStatusBar { background:#000000; color:#9B9B9B; font-size:10px; }
QRadioButton { color:#C9C9C9; }
QCheckBox    { color:#C9C9C9; }
QDockWidget::title { background:#000000; padding:4px; border-bottom:1px solid #3B3B3B; color:#C9C9C9; }

/* ── Card panels (Patient Monitor / C-Arm Status / Procedure Info / Device
   Status) — bordered dark boxes with an uppercase header row, matching the
   reference design. Built by makeCard() in this file. ── */
QFrame#card { background:#0B0B0E; border:1px solid #2A2A2E; border-radius:6px; }
QLabel#cardHeader {
    color:#9B9B9B; font-size:10px; font-weight:bold;
    padding:8px 10px; border-bottom:1px solid #2A2A2E; background:transparent;
}

/* ── Left vertical navigation sidebar ── icon glyph above a short label,
   both in the one button's own two-line text (no separate icon asset
   exists, so this is a deliberate, disclosed approximation rather than a
   claim of pixel-matched icons). ── */
QPushButton#navTile {
    background:#0B0B0E; color:#C9C9C9;
    border-top: 1px solid #2A2A2E;
    border-right: 1px solid #2A2A2E;
    border-bottom: 1px solid #2A2A2E;
    border-left: 1px solid #2A2A2E;
    border-radius:6px;
    font-size:10px; font-weight:bold; text-align:center;
}
QPushButton#navTile:hover {
    background:#151519;
    border-top-color: #3A3A40;
    border-right-color: #3A3A40;
    border-bottom-color: #3A3A40;
    border-left-color: #3A3A40;
}
/* Dynamic property, NOT the :checked pseudo-state — this used to be
   "QPushButton#navTile:checked { ... border: 1px solid #2ECC71; ... }",
   and splitting that into explicit border-width/border-style/border-color
   sub-properties (a real fix for a genuine competing-rule cascade issue
   found earlier) still did not make all four sides render the active
   colour. That points to something Qt's Fusion style itself does for the
   built-in :checked state specifically — Fusion draws its own native
   sunken/bevel effect for a checked button, which can visually interfere
   with a QSS border on one edge regardless of what the stylesheet
   specifies for it, because that bevel is painted by the style's own
   button primitive, not resolved through the same box-model QSS borders
   use. A custom dynamic property has no such built-in native rendering
   attached to it at all, so QSS has completely uncontested control over
   it — set via setProperty("active", true/false) + style()->
   unpolish()/polish() in C++ (see setNavTileActive()), not by relying on
   isChecked()'s own automatic style state. */
QPushButton#navTile[active="true"] {
    background:#0F1A12;
    color:#2ECC71;
    font-weight:bold;
    /* No border properties here any more - painted directly by
       NavTileButton::paintEvent() instead, after three separate QSS-only
       attempts still left the right edge rendering incompletely (see that
       class's own comment for the full reasoning). QSS still handles
       everything else about this state (background/text colour). */
}
/* Dynamic property, not objectName - setObjectName() would replace "navTile"
   entirely, which is a SINGLE name Qt's #id selector matches exactly, so the
   base #navTile rule above (background/border-radius/font-size/text-align)
   would stop applying to this button altogether. A property selector layers
   the accent colour on TOP of the base style instead of replacing it. */
QPushButton#navTile[accent="embolization"] {
    color:#E07A4A;
    border-top-color: #7A3B22;
    border-right-color: #7A3B22;
    border-bottom-color: #7A3B22;
    border-left-color: #7A3B22;
}
QPushButton#navTile[accent="embolization"]:hover {
    background:#1A0F0B;
    border-top-color: #E07A4A;
    border-right-color: #E07A4A;
    border-bottom-color: #E07A4A;
    border-left-color: #E07A4A;
}
)";

/// Every button along the bottom Access & devices bar and in the Calibration
/// dialog is built through here, so they are all the same height and weight.
/// Previously the selection buttons were 32 px from setMinimumHeight and the
/// navigation buttons 24 px from setMaximumHeight, while the global
/// QPushButton rule asked for 38 px — three different answers fighting each
/// other. One constant now sets the height of every one of them.
constexpr int kProcBtnHeight = 40;

QPushButton *makeProcBtn(const QString &text, QWidget *p) {
    auto *b = new QPushButton(text, p);
    b->setObjectName("btnNav");          // shares the compact dock button style
    b->setFixedHeight(kProcBtnHeight);   // fixed, so the QSS min-height cannot win
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return b;
}

QLabel *makeReadout(QWidget *p) {
    auto *l = new QLabel("--", p);
    l->setStyleSheet("color:#C0C0C0; font-family:monospace; font-size:10px;"
                     "background:#000000; padding:2px 4px; border-radius:2px;"
                     "border:1px solid #272727;");
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    l->setMinimumWidth(80);
    return l;
}

/// Bordered "card" panel with an uppercase title header - the Patient
/// Monitor / C-Arm Status / Procedure Info / Device Status panel style the
/// reference design uses throughout. Adds the card to `addTo` (with
/// `stretch`) and returns the layout to put the card's own content into;
/// most callers only need that returned layout.
QVBoxLayout *makeCard(const QString &title, QWidget *parent, QBoxLayout *addTo, int stretch = 0) {
    auto *frame = new QFrame(parent);
    frame->setObjectName("card");
    auto *outer = new QVBoxLayout(frame);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *header = new QLabel(title.toUpper(), frame);
    header->setObjectName("cardHeader");
    outer->addWidget(header);

    auto *content = new QWidget(frame);
    auto *contentLay = new QVBoxLayout(content);
    contentLay->setContentsMargins(10, 8, 10, 10);
    contentLay->setSpacing(6);
    outer->addWidget(content, 1);

    if (addTo) addTo->addWidget(frame, stretch);
    return contentLay;
}

/// One tile in the bottom navigation bar (3D VIEW / FLUORO / DSA / ROADMAP /
/// CINE / CONTRAST INJECTION / EMBOLIZATION). Checkable so the active
/// display mode can show the reference design's highlighted-border state;
/// Paints its own active-state border directly, rather than relying on QSS.
/// Three separate stylesheet-only fixes (a competing global :checked rule,
/// then a shorthand-vs-longhand mismatch against this button's own base
/// rule, then explicit per-side properties throughout) still left the right
/// edge of the border rendering incompletely in practice - which points to
/// something in how the Fusion style itself paints a QPushButton's rounded
/// border/bevel, underneath or alongside whatever the stylesheet specifies,
/// not something further stylesheet tuning can fully control. Painting the
/// border ourselves, on top of the normal QSS-styled paint, removes that
/// ambiguity entirely: this is the one and only place that decides whether
/// all four sides render, and it always draws all four via a single
/// QPainter::drawRoundedRect() call, not four independently-resolved CSS
/// edges.
class NavTileButton : public QPushButton {
public:
    explicit NavTileButton(const QString &text, QWidget *parent) : QPushButton(text, parent) {}

protected:
    void paintEvent(QPaintEvent *event) override {
        QPushButton::paintEvent(event);   // normal QSS-styled paint first: background, text, rounded corners
        if (!property("active").toBool()) return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool isEmbolization = property("accent").toString() == QStringLiteral("embolization");
        const QColor borderColor = isEmbolization ? QColor(0xE0, 0x7A, 0x4A)   // #E07A4A
                                                   : QColor(0x2E, 0xCC, 0x71); // #2ECC71
        p.setPen(QPen(borderColor, 1));
        p.setBrush(Qt::NoBrush);
        // Inset by half the pen width so the 1px stroke is crisp and fully
        // visible on every side, not clipped by the widget's own edge.
        const QRectF r(0.5, 0.5, width() - 1.0, height() - 1.0);
        p.drawRoundedRect(r, 6.0, 6.0);
    }
};

/// callers that are one-shot actions (Cine, Contrast, Embolisation) simply
/// never toggle it programmatically, so it behaves like a normal button for
/// them despite being checkable.
/// One entry in the left vertical navigation sidebar - icon glyph above a
/// short label, both as one two-line button text (no real icon asset exists
/// to draw separately, so the glyphs used at each call site are a plain-
/// Unicode-symbol approximation of the reference design's actual icons, not
/// a claim of matching them exactly). Fixed width/height, not Expanding:
/// unlike the old horizontal bar (which filled the window's full width), a
/// vertical nav column's buttons keep a natural size and leave any leftover
/// column height as blank space below the last one via the caller's own
/// trailing addStretch(1), the same convention the sidebar cards already use.
QPushButton *makeSideNavTile(const QString &icon, const QString &label, QWidget *p) {
    auto *b = new NavTileButton(icon + "\n" + label, p);
    b->setObjectName("navTile");
    b->setCheckable(true);
    b->setFixedWidth(96);
    // 84px, not 76: the Contrast Injection tile's label itself wraps to two
    // lines ("Contrast\nInjection"), so its full text is icon+2 lines versus
    // every other tile's icon+1 line - fixed height has to fit the WORST
    // case uniformly, the same lesson the horizontal bar's own two-line
    // label previously required, just in a vertical context this time.
    // Minimum, not fixed: the trailing addStretch(1) that used to sit after
    // all the tiles in the nav column dumped 100% of the sidebar's leftover
    // height into one blank gap at the bottom, the exact same class of
    // issue already fixed once for the Procedure Info/Device Status/
    // Embolic Agent cards. setSizePolicy(Expanding) + a per-tile stretch
    // factor at the call site (navVL->addWidget(tile, 1)) is what actually
    // lets each tile grow to fill the column instead.
    b->setMinimumHeight(64);
    b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    return b;
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
    // Real SID/magnification, read from CarmView's own live state, into
    // every view's Fluoroscopy HUD - previously that HUD had a second,
    // independently-hardcoded guess (25cm) that disagreed with CarmView's
    // own actual default (32cm), shown side by side in the C-Arm Status
    // card. One source of truth now, not two numbers that could drift apart.
    if (m_carmWidget)
        for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
            if (v) v->setDetectorInfo(m_carmWidget->sid(), m_carmWidget->magnification(), m_carmWidget->frameRate());
    // Embolisation Module: one shared model, handed to every view exactly
    // the way m_catheter already is - see EmbolizationModel's own class
    // comment for why this is what guarantees the two panes never disagree.
    for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
        if (v) v->setEmbolization(&m_emboli);

    // Contrast Injection: same shared-instance pattern, PLUS a link to
    // m_emboli so it can see - and be genuinely blocked by - whatever the
    // embolic cast currently occludes (ContrastInjectionModel's own class
    // comment explains why that is what makes a post-embolization angiogram
    // mechanistically show reduced/absent flow, not a scripted image swap).
    m_contrastModel.setEmbolization(&m_emboli);
    for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
        if (v) v->setContrastInjection(&m_contrastModel);

    // Ticks m_contrastModel forward in real time so contrast decays/washes
    // out on its own - a real vessel does not wait for the operator to do
    // anything before contrast starts clearing. 150 ms is frequent enough to
    // look smooth without being a meaningful frame-budget cost; tick() itself
    // is a cheap no-op while phase() is Idle.
    m_contrastTimer = new QTimer(this);
    connect(m_contrastTimer, &QTimer::timeout, this, [this]{ onContrastTick(); });
    m_contrastTimer->start(150);

    m_heart = new QTimer(this);
    m_heart->setInterval(16);           // ~60 Hz
    connect(m_heart, &QTimer::timeout, this, [this]{ onCardiacTick(); });
    m_heartClock.start();
    m_heart->start();
    setWindowTitle(tr("CathTrainVR — Endovascular Navigation Console"));
    resize(1500, 900);
}

// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// buildViewBar — Calibration, Cine Studio, the four view modes, Contrast
// Injection and Road Map as ordinary menu-bar entries.
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

    // Calibration used to be a group box in the Procedure dock. The dock is
    // gone; this opens the exact same widgets (built in buildUi(), parented
    // to m_calibrationDialog) in a small floating dialog instead. Placed
    // first, immediately after the separator, so it sits right next to Help.
    auto *calibAct = new QAction(tr("Calibration…"), this);
    connect(calibAct, &QAction::triggered, this, [this]{
        if (!m_calibrationDialog) return;
        m_calibrationDialog->show();
        m_calibrationDialog->raise();
        m_calibrationDialog->activateWindow();
    });
    bar->addAction(calibAct);

    // X-Ray Calibration — every parameter in the Fluoro projection's
    // tone-mapping pipeline (VesselView::XrayTuning). Edits are staged in
    // the dialog and only take effect on "Save / Apply" (buildUi()); this
    // action's job is just to make sure that, every time the dialog opens,
    // it shows what's CURRENTLY applied to the view — not whatever was left
    // in the spin boxes from a previous, possibly-abandoned editing session.
    auto *xrayAct = new QAction(tr("X-Ray Calibration…"), this);
    connect(xrayAct, &QAction::triggered, this, [this]{
        if (!m_xrayTuningDialog) return;
        if (VesselView *fv = m_quad ? m_quad->viewFluoro() : nullptr) {
            const auto vals = xrayTuningToArray(fv->xrayTuning());
            for (size_t i = 0; i < m_xraySpins.size() && i < vals.size(); ++i) {
                QSignalBlocker block(m_xraySpins[i]);
                m_xraySpins[i]->setValue(vals[i]);
            }
        }
        m_xrayTuningDialog->show();
        m_xrayTuningDialog->raise();
        m_xrayTuningDialog->activateWindow();
    });
    bar->addAction(xrayAct);

    // View-mode buttons (3D/Fluoro/DSA), Road Map, Cine Studio, Contrast
    // Injection and Embolisation Module moved out of the menu bar entirely —
    // they are now the bottom tile bar built in buildUi() (m_viewTiles /
    // buildBottomTileBar), matching the reference design's tile-based
    // navigation rather than flat menu-bar actions. checkViewAction() below
    // now drives that tile row's checked state instead of a QActionGroup.
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
    // Head framing on demand, so the sagittal view is reachable again after the
    // camera has been orbited or the whole tree shown.
    auto *headAct = new QAction(tr("Head — sagittal"), this);
    headAct->setShortcut(Qt::Key_F8);
    connect(headAct, &QAction::triggered, this, [this]{
        if (m_view) m_view->showHeadSagittal();
    });
    viewMenu->addAction(headAct);

    auto *resetAct = new QAction(tr("Reset camera"), this);
    connect(resetAct, &QAction::triggered, this, [this] { m_view->resetCamera(); });
    viewMenu->addAction(resetAct);

    // Re-mask — the deliberate, explicit action for refreshing a roadmap
    // mask, per requirement #4: the mask must NOT silently follow the live
    // camera/C-arm angle (that mismatch, if the operator rotates without
    // re-masking, is itself a real and clinically important behavior to
    // demonstrate) - this is the one place a fresh capture is taken
    // outside the automatic first-injection capture in onInjectContrast().
    auto *remaskAct = new QAction(tr("Re-mask (Road Map)"), this);
    connect(remaskAct, &QAction::triggered, this, [this] {
        if (m_quad && m_quad->viewFluoro()) m_quad->viewFluoro()->captureRoadmapMask();
    });
    viewMenu->addAction(remaskAct);

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
                                    "  I            advance device\n"
                                    "  K            retract device\n"
                                    "  J            take left limb at bifurcation\n"
                                    "  L            take right limb at bifurcation\n"
                                    "  Page Up      advance device\n"
                                    "  Page Down    retract device\n"
                                    "  Left arrow   take left limb at bifurcation\n"
                                    "  Right arrow  take right limb at bifurcation\n"
                                    "  Space        inflate / deflate balloon\n\n"
                                    "C-ARM\n"
                                    "  A / D        LAO / RAO 5°\n"
                                    "  W / S        CRAN / CAUD 5°\n"
                                    "  R            Reset C-arm"));
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
    connect(m_cineStudio, &CineViewStudio::backToSimulatorRequested,
            this, &MainWindow::onCineStudioBack);

    // ═══════════════════════════════════════════════════════════════════════
    // OUTER SHELL: top header bar (always visible, above both pages) + the
    // page stack below it. setCentralWidget() now targets this outer widget
    // instead of m_pageStack directly, so the header persists across the
    // Simulator ↔ Cine Studio switch rather than being part of either page.
    // ═══════════════════════════════════════════════════════════════════════
    auto *shell = new QWidget(this);
    auto *shellVL = new QVBoxLayout(shell);
    shellVL->setContentsMargins(0, 0, 0, 0);
    shellVL->setSpacing(0);

    // ── Top header bar: logo, patient/procedure identification, actions ────
    // Patient/ID/Age-Sex/Procedure are fixed demo values - this console has
    // no patient-record system to draw them from. Procedure Time is real: a
    // live QElapsedTimer started here, ticked every second. Cine/Screenshot/
    // Report/End Procedure are genuinely wired, not decorative - see
    // onScreenshot()/onGenerateReport()/onEndProcedure() below.
    auto *header = new QWidget(shell);
    header->setStyleSheet("background:#000000; border-bottom:1px solid #2A2A2E;");
    auto *headerHL = new QHBoxLayout(header);
    headerHL->setContentsMargins(14, 8, 14, 8);
    headerHL->setSpacing(24);

    // Hamburger — collapses/expands the left nav sidebar. navCol itself is
    // built further down in this same function; m_hamburgerBtn's click
    // handler is connected there, once navCol actually exists.
    m_hamburgerBtn = new QPushButton(QStringLiteral("\u2630"), header);
    m_hamburgerBtn->setStyleSheet(
        "QPushButton { background:transparent; color:#C9C9C9; border:none; font-size:18px; }"
        "QPushButton:hover { color:#E8E8E8; }");
    m_hamburgerBtn->setFixedSize(32, 32);
    m_hamburgerBtn->setCursor(Qt::PointingHandCursor);
    headerHL->addWidget(m_hamburgerBtn);

    auto *logoBox = new QVBoxLayout();
    logoBox->setSpacing(0);
    auto *logoLabel = new QLabel(tr("CathTrainVR"), header);
    logoLabel->setStyleSheet("color:#E8E8E8; font-size:16px; font-weight:bold;");
    auto *subLabel = new QLabel(tr("ENDOVASCULAR NAVIGATION SIMULATOR"), header);
    subLabel->setStyleSheet("color:#6B6B6F; font-size:8px; letter-spacing:1px;");
    logoBox->addWidget(logoLabel);
    logoBox->addWidget(subLabel);

    // SurgeonsLab logo, to the left of the CathTrainVR wordmark. Tries a
    // couple of likely filenames under the Data folder rather than assuming
    // one exact name - if none are found, this is a silent no-op (a label
    // with no pixmap simply takes no space) rather than a crash, since the
    // exact filename actually placed on disk wasn't specified.
    QPixmap surgeonsLabLogo;
    for (const QString &candidate : {"SL_logo.png", "SurgeonsLab_Logo.png", "SurgeonsLabLogo.png",
                                     "surgeonslab_logo.png", "SurgeonsLab.png"}) {
        if (surgeonsLabLogo.load(QStringLiteral("C:/SurgeonsLab/EmboNav/Data/") + candidate))
            break;
    }
    if (!surgeonsLabLogo.isNull()) {
        auto *logoImg = new QLabel(header);
        logoImg->setPixmap(surgeonsLabLogo.scaledToHeight(40, Qt::SmoothTransformation));
        headerHL->addWidget(logoImg);
    }
    headerHL->addLayout(logoBox);

    auto addHeaderField = [header, &headerHL](const QString &label, const QString &value) -> QLabel* {
        auto *box = new QVBoxLayout();
        box->setSpacing(2);
        auto *l = new QLabel(label, header);
        l->setStyleSheet("color:#6B6B6F; font-size:9px; letter-spacing:1px;");
        auto *v = new QLabel(value, header);
        v->setStyleSheet("color:#E8E8E8; font-size:12px; font-weight:bold;");
        box->addWidget(l);
        box->addWidget(v);
        headerHL->addLayout(box);
        return v;
    };
    addHeaderField(tr("PATIENT"), tr("Demo Patient"));
    addHeaderField(tr("ID"), tr("DP-2025-001"));
    addHeaderField(tr("AGE / SEX"), tr("54 / M"));
    m_activeModuleLabel = addHeaderField(tr("PROCEDURE"), tr("MMA Embolization"));
    m_headerProcedureTime = addHeaderField(tr("PROCEDURE TIME"), tr("00:00:00"));

    headerHL->addStretch(1);

    auto *cineHdrBtn = makeProcBtn(tr("Cine"), header);
    cineHdrBtn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(cineHdrBtn, &QPushButton::clicked, this, [this]{ onOpenCineStudio(); });
    headerHL->addWidget(cineHdrBtn);

    auto *shotHdrBtn = makeProcBtn(tr("Screenshot"), header);
    shotHdrBtn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(shotHdrBtn, &QPushButton::clicked, this, [this]{ onScreenshot(); });
    headerHL->addWidget(shotHdrBtn);

    auto *reportHdrBtn = makeProcBtn(tr("Report"), header);
    reportHdrBtn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(reportHdrBtn, &QPushButton::clicked, this, [this]{ onGenerateReport(); });
    headerHL->addWidget(reportHdrBtn);

    auto *endProcBtn = new QPushButton(tr("End Procedure"), header);
    endProcBtn->setStyleSheet(
        "QPushButton { background:#2A0F0F; color:#E0574A; border:1px solid #7A2A22;"
        "border-radius:4px; padding:8px 14px; font-weight:bold; }"
        "QPushButton:hover { background:#3A1512; }");
    connect(endProcBtn, &QPushButton::clicked, this, [this]{ onEndProcedure(); });
    headerHL->addWidget(endProcBtn);

    shellVL->addWidget(header);
    // Content row: [left vertical nav sidebar][page stack]. The nav sidebar
    // is inserted into this row further down (once it's actually
    // constructed, inside the simulator page's own setup code) rather than
    // being built here directly - m_contentRow is what makes it a
    // persistent SIBLING of the page stack, visible regardless of which
    // page is showing, instead of a child of just the simulator page (which
    // is what caused it to disappear entirely when switching to Cine: it
    // used to be added to the simulator page's own internal rootHL layout,
    // so hiding that page hid the nav sidebar right along with it).
    m_contentRow = new QHBoxLayout();
    m_contentRow->setContentsMargins(0, 0, 0, 0);
    m_contentRow->setSpacing(0);
    m_contentRow->addWidget(m_pageStack, 1);
    shellVL->addLayout(m_contentRow, 1);
    setCentralWidget(shell);

    // ── Bottom system status bar ─────────────────────────────────────────
    // QMainWindow's own built-in status bar (always full-width, always at
    // the very bottom, persists across the Simulator/Cine Studio page
    // switch exactly like the header does) rather than another widget
    // stitched into the page layout.
    {
        auto *sb = statusBar();
        sb->setSizeGripEnabled(false);
        auto *sbWidget = new QWidget(sb);
        auto *sbHL = new QHBoxLayout(sbWidget);
        sbHL->setContentsMargins(10, 2, 10, 2);
        sbHL->setSpacing(28);

        auto addStatusField = [sbWidget, &sbHL](const QString &label, const QString &value) -> QLabel* {
            auto *row = new QHBoxLayout();
            row->setSpacing(6);
            auto *l = new QLabel(label, sbWidget);
            l->setStyleSheet("color:#6B6B6F; font-size:10px;");
            auto *v = new QLabel(value, sbWidget);
            v->setStyleSheet("color:#E8E8E8; font-size:10px; font-weight:bold;");
            row->addWidget(l);
            row->addWidget(v);
            sbHL->addLayout(row);
            return v;
        };

        // SYSTEM STATUS — a green dot + "OK". No fault-detection model
        // exists in this build to drive anything other than a static OK.
        auto *sysRow = new QHBoxLayout();
        sysRow->setSpacing(6);
        auto *sysLabel = new QLabel(tr("SYSTEM STATUS"), sbWidget);
        sysLabel->setStyleSheet("color:#6B6B6F; font-size:10px;");
        auto *sysDot = new QLabel(sbWidget);
        sysDot->setStyleSheet("color:#2ECC71; font-size:10px;");
        sysDot->setText(QString::fromUtf8("\u25CF"));
        auto *sysVal = new QLabel(tr("OK"), sbWidget);
        sysVal->setStyleSheet("color:#E8E8E8; font-size:10px; font-weight:bold;");
        sysRow->addWidget(sysLabel);
        sysRow->addWidget(sysDot);
        sysRow->addWidget(sysVal);
        sbHL->addLayout(sysRow);

        // DOSE / AIR KERMA — fixed console defaults, same disclosed reason as
        // the Fluoroscopy HUD's kVp/mA/DAP: no generator/dosimetry model to
        // draw real numbers from. DOSE intentionally matches the HUD's own
        // DAP value (12.4 Gy·cm²) exactly, since both read out the same
        // underlying accumulated quantity rather than two independently
        // invented numbers that happen to differ for no reason.
        m_sbDose     = addStatusField(tr("DOSE"), tr("12.4 Gy·cm²"));
        m_sbAirKerma = addStatusField(tr("AIR KERMA"), tr("8.7 mGy"));

        // TOTAL FLUORO TIME — genuinely live: CarmView already tracks real
        // fluoro-active seconds (setFluoroActive()/fluoroSeconds()); this
        // just reads it every tick below.
        m_sbFluoroTime = addStatusField(tr("TOTAL FLUORO TIME"), tr("00:00:00"));

        // LAST IMAGE — genuinely live, driven by onScreenshot()'s own
        // m_lastImageClock; reads "—" until the first screenshot exists.
        m_sbLastImage = addStatusField(tr("LAST IMAGE"), tr("—"));

        sbHL->addStretch(1);

        // Speaker/brightness icons are decorative — this console has no
        // audio subsystem and no display-brightness control to actually
        // toggle, so these are shown as fixed glyphs rather than wired to
        // behaviour that does not exist.
        auto *speakerIcon = new QLabel(QString::fromUtf8("\U0001F50A"), sbWidget);
        speakerIcon->setStyleSheet("font-size:12px;");
        sbHL->addWidget(speakerIcon);
        auto *brightIcon = new QLabel(QString::fromUtf8("\u2600"), sbWidget);
        brightIcon->setStyleSheet("font-size:12px; color:#9B9B9B;");
        sbHL->addWidget(brightIcon);

        m_sbClock = new QLabel(sbWidget);
        m_sbClock->setStyleSheet("color:#E8E8E8; font-size:10px; font-weight:bold;");
        sbHL->addWidget(m_sbClock);

        sb->addWidget(sbWidget, 1);
        sb->setStyleSheet("QStatusBar { background:#000000; border-top:1px solid #2A2A2E; }"
                          "QStatusBar::item { border:none; }");
    }

    // Live procedure timer — started here (app/session start), ticked once a
    // second. A training console has no real "procedure start" moment
    // distinct from launching it, so session time is the honest answer.
    // Also refreshes the status bar's live fields (TOTAL FLUORO TIME, LAST
    // IMAGE, clock) each tick, rather than running a second timer for them.
    m_procedureClock.start();
    m_procedureTimer = new QTimer(this);
    connect(m_procedureTimer, &QTimer::timeout, this, [this]{
        const qint64 s = m_procedureClock.elapsed() / 1000;
        if (m_headerProcedureTime)
            m_headerProcedureTime->setText(QString("%1:%2:%3")
                .arg(s / 3600, 2, 10, QChar('0'))
                .arg((s / 60) % 60, 2, 10, QChar('0'))
                .arg(s % 60, 2, 10, QChar('0')));

        if (m_sbFluoroTime && m_carmWidget) {
            const qint64 fs = static_cast<qint64>(m_carmWidget->fluoroSeconds());
            m_sbFluoroTime->setText(QString("%1:%2:%3")
                .arg(fs / 3600, 2, 10, QChar('0'))
                .arg((fs / 60) % 60, 2, 10, QChar('0'))
                .arg(fs % 60, 2, 10, QChar('0')));
        }
        if (m_sbLastImage) {
            if (m_hasLastImage) {
                const qint64 ls = m_lastImageClock.elapsed() / 1000;
                m_sbLastImage->setText(tr("%1:%2:%3 ago")
                    .arg(ls / 3600, 2, 10, QChar('0'))
                    .arg((ls / 60) % 60, 2, 10, QChar('0'))
                    .arg(ls % 60, 2, 10, QChar('0')));
            } else {
                m_sbLastImage->setText(tr("—"));
            }
        }
        if (m_sbClock) m_sbClock->setText(QTime::currentTime().toString("hh:mm AP"));
    });
    m_procedureTimer->start(1000);

    m_motion = new QTimer(this);
    m_motion->setInterval(16);
    connect(m_motion, &QTimer::timeout, this, &MainWindow::onMotionTick);

    // ═══════════════════════════════════════════════════════════════════════
    // SIMULATOR PAGE: outer vertical split, matching the reference design
    //   [ rootHL: left column (C-Arm/Patient Monitor cards) | vessel view |
    //             right column (Procedure Info/Device Status cards) ]
    //   [ tileBar: bottom tile navigation - 3D/Fluoro/DSA/RoadMap/Cine/
    //             Contrast Injection/Embolization, full width ]
    // ═══════════════════════════════════════════════════════════════════════
    auto *pageVL = new QVBoxLayout(m_simulatorPage);
    pageVL->setContentsMargins(0, 0, 0, 0);
    pageVL->setSpacing(0);

    // Left column (fixed ~460px)  |  view-mode buttons col (narrow)  |  Right (vessel+procedure)
    auto *rootHL = new QHBoxLayout();
    rootHL->setContentsMargins(0, 0, 0, 0);
    rootHL->setSpacing(0);
    pageVL->addLayout(rootHL, 1);

    // ── LEFT VERTICAL NAVIGATION SIDEBAR ────────────────────────────────────
    // 3D VIEW / FLUORO / DSA / ROAD MAP / CINE / CONTRAST INJECTION /
    // EMBOLIZATION, stacked vertically as the leftmost column - moved here
    // from a horizontal bar along the bottom of the window, per the
    // reference layout. Every tile calls the exact same handler it always
    // did; only the position, orientation, and per-tile icon glyph changed.
    auto *navCol = new QWidget(this);
    navCol->setFixedWidth(120);
    navCol->setStyleSheet("background:#000000; border-right:1px solid #2A2A2E;");
    auto *navVL = new QVBoxLayout(navCol);
    navVL->setContentsMargins(8, 10, 8, 10);
    navVL->setSpacing(8);

    m_viewTileGroup = new QButtonGroup(this);
    m_viewTileGroup->setExclusive(true);

    m_viewTiles[0] = makeSideNavTile(QStringLiteral("\u25C7"), tr("3D View"), navCol);
    m_viewTiles[0]->setChecked(true);
    setNavTileActive(m_viewTiles[0], true);   // matches checkViewAction()'s own property-based styling
    connect(m_viewTiles[0], &QPushButton::clicked, this, [this]{ onViewModeChanged(0); });
    m_viewTileGroup->addButton(m_viewTiles[0]);
    navVL->addWidget(m_viewTiles[0], 1, Qt::AlignHCenter);

    m_viewTiles[1] = makeSideNavTile(QStringLiteral("\u25C9"), tr("Fluoro"), navCol);
    connect(m_viewTiles[1], &QPushButton::clicked, this, [this]{ onViewModeChanged(1); });
    m_viewTileGroup->addButton(m_viewTiles[1]);
    navVL->addWidget(m_viewTiles[1], 1, Qt::AlignHCenter);

    // Index 2 (Mesh) deliberately has no tile - see m_viewTiles' own
    // declaration comment in MainWindow.h.

    m_viewTiles[3] = makeSideNavTile(QStringLiteral("\u25C8"), tr("DSA"), navCol);
    connect(m_viewTiles[3], &QPushButton::clicked, this, [this]{ onViewModeChanged(3); });
    m_viewTileGroup->addButton(m_viewTiles[3]);
    navVL->addWidget(m_viewTiles[3], 1, Qt::AlignHCenter);

    // Road Map is a toggle, but independent of the exclusive 3D/Fluoro/DSA
    // group (it modifies the Fluoro pane rather than selecting a different
    // one) — not added to m_viewTileGroup, so it can be checked or not
    // regardless of which display mode is active.
    m_roadMapTile = makeSideNavTile(QStringLiteral("\u2B21"), tr("Road Map"), navCol);
    connect(m_roadMapTile, &QPushButton::clicked, this, [this]{ onRoadMap(); });
    navVL->addWidget(m_roadMapTile, 1, Qt::AlignHCenter);

    m_cineTile = makeSideNavTile(QStringLiteral("\u25B6"), tr("Cine"), navCol);
    m_cineTile->setCheckable(false);   // one-shot action, not a display-mode toggle
    connect(m_cineTile, &QPushButton::clicked, this, [this]{ onOpenCineStudio(); });
    navVL->addWidget(m_cineTile, 1, Qt::AlignHCenter);

    auto *contrastTile = makeSideNavTile(QStringLiteral("\u271A"), tr("Contrast\nInjection"), navCol);
    contrastTile->setCheckable(false);
    connect(contrastTile, &QPushButton::clicked, this, [this]{ showContrastInjectionDialog(); });
    navVL->addWidget(contrastTile, 1, Qt::AlignHCenter);

    auto *emboTile = makeSideNavTile(QStringLiteral("\u2733"), tr("Embolization"), navCol);
    emboTile->setProperty("accent", "embolization");
    // Qt applies a QSS property-selector change immediately on most styles,
    // but re-polishing explicitly avoids relying on that: without it, a
    // property set before the widget's first show can be missed by some
    // styles until the next explicit style/state change forces a re-evaluation.
    emboTile->style()->unpolish(emboTile);
    emboTile->style()->polish(emboTile);
    emboTile->setCheckable(false);
    connect(emboTile, &QPushButton::clicked, this, [this]{ showEmbolizationDialog(); });
    navVL->addWidget(emboTile, 1, Qt::AlignHCenter);

    // Inserted into the SHARED content row (position 0, before
    // m_pageStack, which is already sitting at index 0 there) rather than
    // into rootHL - rootHL belongs only to the simulator page, which is
    // exactly why this sidebar used to disappear entirely when switching
    // to Cine. m_contentRow is a persistent sibling of the page stack, so
    // it now stays visible regardless of which page is currently showing.
    m_contentRow->insertWidget(0, navCol);
    connect(m_hamburgerBtn, &QPushButton::clicked, this, [navCol] {
        navCol->setVisible(!navCol->isVisible());
    });

    // ── LEFT COLUMN: C-arm + angle strip + key hints + ECG ───────────────
    auto *leftCol = new QWidget(m_simulatorPage);
    leftCol->setFixedWidth(480);  // wider CarmView panel
    leftCol->setStyleSheet("background:#000000;");
    auto *leftVL = new QVBoxLayout(leftCol);
    leftVL->setContentsMargins(8,0,0,0);   // slight left inset so panel isn't flush to edge
    leftVL->setSpacing(0);

    // ── CarmView pane (top half), wrapped as a "C-Arm Status" card ────────
    QVBoxLayout *carmContent = makeCard(tr("C-Arm Status"), leftCol, leftVL, 1);
    carmContent->setContentsMargins(0, 0, 0, 0);   // CarmView fills the card edge-to-edge
    carmContent->setSpacing(0);

    m_carmWidget = new CarmView(leftCol);
    carmContent->addWidget(m_carmWidget, 1);

    // Angle labels: still created for onCarmAnglesChanged() to update,
    // but not added to any layout — angles are shown inside the CarmView HUD.
    m_laoLabel  = new QLabel("LAO 0.0°");
    m_cranLabel = new QLabel("CRAN 0.0°");
    // Key hints removed from main UI — see Help menu for controls.

    // ── ECG pane (bottom half), wrapped as a "Patient Monitor" card ────────
    QVBoxLayout *ecgContent = makeCard(tr("Patient Monitor"), leftCol, leftVL, 1);
    ecgContent->setContentsMargins(0, 0, 0, 0);   // ECGWidget fills the card edge-to-edge
    ecgContent->setSpacing(0);

    m_ecgWidget = new ECGWidget(leftCol);
    m_ecgWidget->setHeartRate(72);
    m_ecgWidget->setObjectName("ecgWidget");
    ecgContent->addWidget(m_ecgWidget, 1);

    // Equal split using stretch factors — no splitter needed.
    // Both cards were already added to leftVL above with stretch=1 each via
    // makeCard's own stretch parameter.
    m_carmWidget->setMinimumSize(10, 10);
    m_ecgWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

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

    // C-arm STL load — into BOTH the main simulator page's CarmView and
    // Cine Studio's embedded C-Arm Quick View instance. Previously this
    // only ever targeted m_carmWidget, meaning the Cine instance had zero
    // mesh parts loaded and rendered completely empty no matter what its
    // camera/zoom was set to - the actual root cause of it not being
    // visible, not a framing problem.
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
        CarmView *cineCarm = m_cineStudio ? m_cineStudio->carmView() : nullptr;
        for (const QString &base : bases) {
            const QString path = QDir::cleanPath(base) + "/" + f;
            if (m_carmWidget->loadPart(path, idx)) {
                // Same file, same index, into the embedded instance too -
                // it needs its own copy of the geometry uploaded to its
                // own OpenGL context, not a reference to the main widget's.
                if (cineCarm) cineCarm->loadPart(path, idx);
                return;
            }
        }
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

    // Explicit initial sync - onCarmAnglesChanged() (which drives Cine
    // Studio's C-Arm Quick View, among other things) was previously only
    // ever triggered via applyCarmAngles(), a path explicitly commented as
    // "legacy... normally unused" - meaning it could otherwise go uncalled
    // until the operator actually moves the C-arm for the first time. Cine
    // View needs to show the correct C-arm state from the moment it opens,
    // even before any movement has happened, per explicit request.
    onCarmAnglesChanged(m_carmCtrl->laoAngle(), m_carmCtrl->cranAngle());

    // Wire primary view signals
    connect(m_view, &VesselView::advanceRequested,       this, [this]{ onAdvance(); });
    connect(m_view, &VesselView::retractRequested,       this, [this]{ onRetract(); });
    connect(m_view, &VesselView::chooseLeftRequested,    this, [this]{ onChooseLeft(); });
    connect(m_view, &VesselView::chooseRightRequested,   this, [this]{ onChooseRight(); });
    connect(m_view, &VesselView::chooseStraightRequested, this, [this]{ onChooseStraight(); });
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

    // ── RIGHT-SIDE COLUMN: Procedure Info + Device Status ──────────────────
    // New — the reference design's sidebar. Every value shown is live app
    // state (access site, sheath/catheter identity, branch/turns/depth from
    // NavigationController, and readiness/active flags for the three device
    // stages), refreshed from updateReadouts(); nothing here is invented
    // demo data. Sheath/Catheter Selection move here from the old bottom
    // Access & devices bar, which this column replaces.
    auto *procCol = new QWidget(m_simulatorPage);
    procCol->setFixedWidth(300);
    procCol->setStyleSheet("background:#000000;");
    auto *procVL = new QVBoxLayout(procCol);
    procVL->setContentsMargins(8, 8, 8, 8);
    procVL->setSpacing(8);

    auto addInfoRow = [procCol](QVBoxLayout *lay, const QString &label, QLabel *&valueOut) {
        auto *row = new QHBoxLayout();
        auto *l = new QLabel(label, procCol);
        l->setStyleSheet("color:#9B9B9B; font-size:12px;");
        valueOut = new QLabel(tr("—"), procCol);
        valueOut->setStyleSheet("color:#E8E8E8; font-size:12px; font-weight:bold;");
        valueOut->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(l);
        row->addStretch(1);
        row->addWidget(valueOut);
        lay->addLayout(row);
        // A stretch after EVERY row, not one trailing stretch after the
        // last row - this is what spreads the card's extra height out as
        // even breathing room between each line instead of pooling all of
        // it into a single dead gap at the bottom.
        lay->addStretch(1);
    };

    // Stretch factors below (2/1/2) are what makes these three cards fill
    // the sidebar's full height instead of stacking at the top with a dead
    // black gap underneath - previously every card had stretch 0 and a
    // single addStretch(1) at the very end absorbed all the leftover space
    // as empty area rather than distributing it into the cards themselves.
    // Procedure Info and Embolic Agent get more weight than Device Status
    // (six/four rows of content vs. three short status rows), so the split
    // is proportional rather than perfectly even.
    QVBoxLayout *infoContent = makeCard(tr("Procedure Info"), procCol, procVL, 2);
    addInfoRow(infoContent, tr("Access"),   m_fAccess);
    addInfoRow(infoContent, tr("Sheath"),   m_fSheath);
    addInfoRow(infoContent, tr("Catheter"), m_fCatheter);
    addInfoRow(infoContent, tr("Branch"),   m_fBranch);
    addInfoRow(infoContent, tr("Turns"),    m_fTurns);
    addInfoRow(infoContent, tr("Depth"),    m_fDepth);
    addInfoRow(infoContent, tr("Lumen Ø"),  m_fLumen);
    addInfoRow(infoContent, tr("Device Ø"), m_fDevice);
    addInfoRow(infoContent, tr("Clear"),    m_fClear);
    m_fAccess->setText(tr("not established"));
    m_fSheath->setText(tr("no sheath"));
    m_fCatheter->setText(tr("no catheter"));

    auto addStatusRow = [procCol](QVBoxLayout *lay, const QString &label, QLabel *&valueOut) {
        auto *row = new QHBoxLayout();
        auto *l = new QLabel(label, procCol);
        l->setStyleSheet("color:#9B9B9B; font-size:12px;");
        valueOut = new QLabel(procCol);
        valueOut->setStyleSheet("font-size:12px;");
        valueOut->setTextFormat(Qt::RichText);
        valueOut->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(l);
        row->addStretch(1);
        row->addWidget(valueOut);
        lay->addLayout(row);
        // Same reasoning as addInfoRow's own per-row stretch above.
        lay->addStretch(1);
    };

    QVBoxLayout *deviceContent = makeCard(tr("Device Status"), procCol, procVL, 1);
    addStatusRow(deviceContent, tr("Sheath"),        m_deviceStatusSheath);
    addStatusRow(deviceContent, tr("Catheter"),      m_deviceStatusCatheter);
    addStatusRow(deviceContent, tr("Embolic Agent"), m_deviceStatusEmbolic);
    // Initial state — updateReadouts() keeps these live from here on.
    m_deviceStatusSheath->setText(  "<span style='color:#6B6B6F;'>&#9679;</span> NOT PLACED");
    m_deviceStatusCatheter->setText("<span style='color:#6B6B6F;'>&#9679;</span> NOT LOADED");
    m_deviceStatusEmbolic->setText( "<span style='color:#6B6B6F;'>&#9679;</span> NOT STARTED");

    // ── "Select Patient…" — directly above Sheath Selection ────────────────
    // Opens a dedicated dialog (showPatientSelectionDialog()) with the case
    // list on the left and the module categories on the right, rather than
    // an inline card in this column - matches the reference design.
    m_btnSelectPatient = makeProcBtn(tr("Select Patient…"), procCol);
    connect(m_btnSelectPatient, &QPushButton::clicked, this, [this]{ showPatientSelectionDialog(); });
    procVL->addWidget(m_btnSelectPatient);

    m_btnSheathSelect = makeProcBtn(tr("Sheath Selection…"), procCol);
    // Disabled until a patient/case is selected - sequential workflow:
    // Patient -> Sheath -> Catheter, each step gating the next.
    m_btnSheathSelect->setEnabled(false);
    connect(m_btnSheathSelect, &QPushButton::clicked, this, [this]{ onSheathSelection(); });
    procVL->addWidget(m_btnSheathSelect);

    m_btnCatheterSelect = makeProcBtn(tr("Catheter Selection…"), procCol);
    // Disabled until a sheath is in: you cannot introduce a catheter through
    // an access you have not made yet.
    m_btnCatheterSelect->setEnabled(false);
    connect(m_btnCatheterSelect, &QPushButton::clicked, this, [this]{ onCatheterSelection(); });
    procVL->addWidget(m_btnCatheterSelect);

    // ── Embedded "Embolic Agent" quick-inject panel ─────────────────────────
    // Matches the reference design directly: agent dropdown, a volume
    // read-out with +/- steppers, one big INJECT button - no separate dialog
    // needed for the common case. Drives the exact same shared m_emboli the
    // Embolisation Module dialog does (onQuickEmboInject()), so picking up
    // the full dialog later (e.g. to Reset) always reflects whatever was
    // injected from here, and vice versa.
    QVBoxLayout *quickEmboContent = makeCard(tr("Embolic Agent"), procCol, procVL, 2);

    m_quickEmboAgentBox = new QComboBox(procCol);
    for (cn::EmbolicAgent a : {cn::EmbolicAgent::Onyx, cn::EmbolicAgent::Squid,
                               cn::EmbolicAgent::Phil, cn::EmbolicAgent::Glue})
        m_quickEmboAgentBox->addItem(QString::fromLatin1(cn::embolicAgentSpec(a).name),
                                     static_cast<int>(a));
    quickEmboContent->addWidget(m_quickEmboAgentBox);

    auto *volRow = new QHBoxLayout();
    auto *volLabelText = new QLabel(tr("VOLUME"), procCol);
    volLabelText->setStyleSheet("color:#6B6B6F; font-size:12px; letter-spacing:1px;");
    auto *volMinusBtn = new QPushButton(tr("−"), procCol);
    volMinusBtn->setFixedWidth(36);
    connect(volMinusBtn, &QPushButton::clicked, this, [this]{ onQuickEmboVolumeStep(-0.02f); });
    m_quickEmboVolumeLabel = new QLabel(tr("%1 mL").arg(m_quickEmboVolumeMl, 0, 'f', 2), procCol);
    m_quickEmboVolumeLabel->setAlignment(Qt::AlignCenter);
    m_quickEmboVolumeLabel->setStyleSheet("color:#E8E8E8; font-size:12px; font-weight:bold;");
    auto *volPlusBtn = new QPushButton(tr("+"), procCol);
    volPlusBtn->setFixedWidth(36);
    connect(volPlusBtn, &QPushButton::clicked, this, [this]{ onQuickEmboVolumeStep(0.02f); });
    volRow->addWidget(volMinusBtn);
    volRow->addWidget(m_quickEmboVolumeLabel, 1);
    volRow->addWidget(volPlusBtn);
    auto *volCol = new QVBoxLayout();
    volCol->setSpacing(4);
    volCol->addWidget(volLabelText);
    volCol->addLayout(volRow);
    quickEmboContent->addLayout(volCol);

    m_quickEmboInjectBtn = new QPushButton(tr("INJECT"), procCol);
    m_quickEmboInjectBtn->setStyleSheet(
        "QPushButton { background:#0F3D22; color:#2ECC71; border:1px solid #2ECC71;"
        "border-radius:4px; padding:10px; font-weight:bold; font-size:12px; }"
        "QPushButton:hover { background:#154A2A; }");
    connect(m_quickEmboInjectBtn, &QPushButton::clicked, this, [this]{ onQuickEmboInject(); });
    quickEmboContent->addWidget(m_quickEmboInjectBtn);

    // Reset — reverses whatever has been injected (Onyx or otherwise) from
    // right here, without needing to open the Embolisation Module dialog.
    // Wired to the exact same onResetEmbolization() that dialog's own Reset
    // button calls (m_emboResetBtn) - one shared reset path for the one
    // shared m_emboli, so there is no separate "quick panel reset" logic
    // that could ever disagree with the dialog's.
    auto *quickEmboResetBtn = new QPushButton(tr("Reset"), procCol);
    quickEmboResetBtn->setStyleSheet(
        "QPushButton { background:#1A1A1E; color:#C9C9C9; border:1px solid #3A3A40;"
        "border-radius:4px; padding:8px; font-size:12px; }"
        "QPushButton:hover { background:#242429; border-color:#5A5A60; }");
    connect(quickEmboResetBtn, &QPushButton::clicked, this, [this]{ onResetEmbolization(); });
    quickEmboContent->addWidget(quickEmboResetBtn);

    // Trailing stretch INSIDE the card - without this, giving the card's
    // outer frame extra height (via makeCard's stretch factor above) would
    // spread the combo/volume/inject/reset widgets apart from each other
    // instead of keeping them compact at the top with the extra room
    // showing as blank card background beneath the buttons, which is the
    // actual look "match the balance of the space downside" is asking for.
    quickEmboContent->addStretch(1);

    rootHL->addWidget(procCol);

    // Status label
    // Status label: created but not shown in main UI (instructions moved to Help menu)
    m_statusLabel = new QLabel();
    m_statusLabel->setVisible(false);

    // The bottom horizontal tile bar that used to live here (3D VIEW/FLUORO/
    // DSA/ROADMAP/CINE/CONTRAST INJECTION/EMBOLIZATION) has moved to the left
    // vertical navigation sidebar built earlier in this function, per the
    // reference layout. m_viewTileGroup/m_viewTiles/m_roadMapTile are already
    // constructed there; nothing further is built here.

    // ── Catheter navigation — keyboard only, no on-screen buttons ──────────
    // Advance/Retract/Left/Right used to be a button box here. The buttons are
    // gone; I/K/J/L call the exact same handlers the buttons called, so the
    // movement itself — including the m_ready / decisionPending gating already
    // inside those handlers — is unchanged.
    //
    // ApplicationShortcut, same reasoning as the C-arm keys in buildMenus():
    // VesselView is a QOpenGLWidget with StrongFocus, and a plain
    // WindowShortcut can be swallowed by the focused widget before the
    // action ever sees the key.
    auto addNavKey = [this](Qt::Key k, const QString &desc, std::function<void()> fn) {
        auto *a = new QAction(desc, this);
        a->setShortcut(k);
        a->setShortcutContext(Qt::ApplicationShortcut);
        connect(a, &QAction::triggered, this, [fn]{ fn(); });
        this->addAction(a);
    };
    addNavKey(Qt::Key_I, tr("Advance catheter"),     [this]{ onAdvance(); });
    addNavKey(Qt::Key_K, tr("Retract catheter"),      [this]{ onRetract(); });
    addNavKey(Qt::Key_J, tr("Choose left limb"),      [this]{ onChooseLeft(); });
    addNavKey(Qt::Key_L, tr("Choose right limb"),     [this]{ onChooseRight(); });
    // Adjacent to I/K/J/L on the keyboard. This is the fix for a confirmed
    // gap: setRollAngle() was never called from anywhere in this file before
    // this, meaning the operator had no way to rotate the catheter at all —
    // its orientation was determined entirely by its fixed resting shape and
    // whatever path it happened to be on.
    addNavKey(Qt::Key_U, tr("Roll catheter left"),    [this]{ onRollLeft(); });
    addNavKey(Qt::Key_O, tr("Roll catheter right"),   [this]{ onRollRight(); });

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
        static const char *kSiteNames[8] = {"Right femoral","Left femoral","Right radial","Left radial",
                                            "Right popliteal","Left popliteal","Right brachial","Left brachial"};
        for (int i = 0; i < 8; ++i) {
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
    // changes.

    // ── Calibration — moved into a dialog, opened from the menu bar ────────
    // Same QFormLayout, same rows, same widgets, same connect() calls as when
    // this lived in the Procedure dock's Calibration group box; only the
    // parent container changed, from a QGroupBox docked on the right to a
    // free-floating (non-modal) QDialog. buildViewBar() wires the menu action
    // that shows it.
    m_calibrationDialog = new QDialog(this);
    m_calibrationDialog->setWindowTitle(tr("Calibration"));
    auto *calForm = new QFormLayout(m_calibrationDialog);

    // Device area: the measurements of whatever catheter is currently loaded.
    //
    // This used to be a combo listing the five Fogarty embolectomy balloons,
    // which had two problems: it offered a device class the procedure does not
    // use, and picking from it overwrote whatever Catheter Selection had just
    // loaded. The device is now chosen in one place — Catheter Selection — and
    // this area reports it.
    m_fDevModel = makeReadout(m_calibrationDialog);
    m_fDevModel->setText(tr("no catheter"));
    calForm->addRow(tr("Model"), m_fDevModel);

    // Working French size. French IS the outer diameter for a catheter —
    // OD mm = Fr / 3 — so this changes the device that has to fit the lumen,
    // not just a label. Full per-model dimensions live in the Catheter
    // Selection dialog; this is the quick size control.
    m_frCombo = new QComboBox(m_calibrationDialog);
    for (int fr = kMinFrench; fr <= kMaxFrench; ++fr)
        m_frCombo->addItem(tr("%1 Fr").arg(fr), fr);
    m_frCombo->setCurrentIndex(2 - kMinFrench);   // default 2 Fr
    connect(m_frCombo, &QComboBox::currentIndexChanged,
            this, [this](int i){ onCatheterFrenchChanged(i); });
    calForm->addRow(tr("Size"), m_frCombo);

    m_odSpin = new QDoubleSpinBox(m_calibrationDialog);
    m_odSpin->setRange(0.20, 3.00); m_odSpin->setSingleStep(0.10);
    m_odSpin->setDecimals(2); m_odSpin->setSuffix(tr(" mm"));
    m_odSpin->setValue(0.57);   // default OD; overwritten once a real catheter is loaded (applyCatheterSpec)
    connect(m_odSpin, &QDoubleSpinBox::valueChanged, this, [this](double v){ onDeviceSizeChanged(v); });
    calForm->addRow(tr("OD"), m_odSpin);

    m_stepSpin = new QDoubleSpinBox(m_calibrationDialog);
    m_stepSpin->setRange(0.5, 30.0); m_stepSpin->setValue(4.0); m_stepSpin->setSuffix(tr(" mm"));
    calForm->addRow(tr("Step"), m_stepSpin);

    m_speedSpin = new QDoubleSpinBox(m_calibrationDialog);
    m_speedSpin->setRange(1.0, 120.0); m_speedSpin->setValue(50.0); m_speedSpin->setSuffix(tr(" mm/s"));
    calForm->addRow(tr("Speed"), m_speedSpin);

    m_scaleSpin = new QDoubleSpinBox(m_calibrationDialog);
    m_scaleSpin->setRange(0.001, 10000.0); m_scaleSpin->setDecimals(3);
    m_scaleSpin->setValue(m_mmPerUnit);
    connect(m_scaleSpin, &QDoubleSpinBox::valueChanged, this, [this](double v){ onScaleChanged(v); });
    calForm->addRow(tr("mm/unit"), m_scaleSpin);

    // ── Heart rate ─────────────────────────────────────────────────────────
    // One control for the whole console: it sets the cardiac clock, which drives
    // both the ECG trace and the pulsating vessel wall.
    m_bpmSpin = new QSpinBox(m_calibrationDialog);
    m_bpmSpin->setRange(30, 220);
    m_bpmSpin->setValue(m_cardiac.heartRate());
    m_bpmSpin->setSuffix(tr(" bpm"));
    connect(m_bpmSpin, &QSpinBox::valueChanged, this, [this](int v){ onHeartRateChanged(v); });
    calForm->addRow(tr("Heart rate"), m_bpmSpin);

    m_pulseCheck = new QCheckBox(tr("Pulsatile vessel wall"), m_calibrationDialog);
    m_pulseCheck->setChecked(true);
    connect(m_pulseCheck, &QCheckBox::toggled, this, [this](bool on){
        for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
            if (v) v->setPulsationEnabled(on);
    });
    calForm->addRow(m_pulseCheck);

    m_autoCalibrate = new QCheckBox(tr("Auto-scale from trunk"), m_calibrationDialog);
    m_autoCalibrate->setChecked(true);
    calForm->addRow(m_autoCalibrate);

    // Opacity slider — matches old project
    m_opacitySlider = new QSlider(Qt::Horizontal, m_calibrationDialog);
    m_opacitySlider->setRange(5, 100);
    m_opacitySlider->setValue(68);
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int v){ onOpacityChanged(v); });
    calForm->addRow(tr("Opacity"), m_opacitySlider);

    // Close button — the dialog is non-modal (shown, not exec()'d) and never
    // deleted, so closing it just hides it; reopening from the menu shows the
    // same instance with everything exactly as it was left.
    auto *calBtns = new QDialogButtonBox(QDialogButtonBox::Close, m_calibrationDialog);
    connect(calBtns, &QDialogButtonBox::rejected, m_calibrationDialog, &QDialog::hide);
    calForm->addRow(calBtns);

    // ── X-Ray Calibration — staged edits, applied on Save / Apply ───────────
    // Every spin box is purely a staged edit while the dialog is open —
    // nothing reaches the Fluoro view until "Save / Apply" is clicked, which
    // reads all 22 of them and pushes a single VesselView::XrayTuning in one
    // call. A value the user didn't touch keeps whatever it already was
    // (its spin box was seeded with that same value), so editing one field
    // can never disturb the other 21. Reopening the dialog (buildViewBar's
    // xrayAct) re-seeds every spin box from the view's current, actually-
    // applied state, so an edit that was never Saved/Applied simply doesn't
    // persist — Close just hides the dialog and discards it.
    m_xrayTuningDialog = new QDialog(this);
    m_xrayTuningDialog->setWindowTitle(tr("X-Ray Calibration"));
    auto *xrForm = new QFormLayout(m_xrayTuningDialog);

    auto addXraySpin = [this, xrForm](const QString &label, double lo, double hi,
                                      double step, double initial) {
        auto *spin = new QDoubleSpinBox(m_xrayTuningDialog);
        spin->setRange(lo, hi);
        spin->setSingleStep(step);
        spin->setDecimals(2);
        spin->setValue(initial);
        xrForm->addRow(label, spin);
        m_xraySpins.push_back(spin);
    };
    // Three R/G/B spin boxes sharing one range/step, for the colour triples.
    auto addXrayColorRow = [&addXraySpin](const QString &prefix, double lo, double hi,
                                          double step, const QVector3D &initial) {
        addXraySpin(prefix + " R", lo, hi, step, initial.x());
        addXraySpin(prefix + " G", lo, hi, step, initial.y());
        addXraySpin(prefix + " B", lo, hi, step, initial.z());
    };

    // Defaults, purely to seed each spin box's initial displayed value —
    // the actual defaults live on VesselView::XrayTuning itself.
    const VesselView::XrayTuning xd;

    addXrayColorRow(tr("Background"), 0.0, 0.3, 0.01, xd.backgroundColor);

    addXrayColorRow(tr("Bone"), 0.5, 1.0, 0.01, xd.boneColor);
    addXraySpin(tr("Bone opacity"), 0.15, 1.0, 0.01, xd.boneOpacity);
    addXraySpin(tr("Bone ambient"), 0.0, 1.0, 0.01, xd.boneAmbient);
    addXraySpin(tr("Bone diffuse"), 0.0, 1.0, 0.01, xd.boneDiffuse);
    addXraySpin(tr("Bone specular"), 0.0, 0.5, 0.01, xd.boneSpecular);

    addXrayColorRow(tr("Muscle"), 0.1, 0.8, 0.01, xd.muscleColor);
    addXraySpin(tr("Muscle opacity"), 0.0, 0.6, 0.01, xd.muscleOpacity);
    addXraySpin(tr("Muscle ambient"), 0.0, 1.0, 0.01, xd.muscleAmbient);
    addXraySpin(tr("Muscle diffuse"), 0.0, 1.0, 0.01, xd.muscleDiffuse);
    addXraySpin(tr("Muscle specular"), 0.0, 0.5, 0.01, xd.muscleSpecular);

    addXraySpin(tr("Gamma"), 0.4, 1.6, 0.01, xd.gamma);
    addXraySpin(tr("Contrast"), 0.5, 2.5, 0.01, xd.contrast);
    addXraySpin(tr("Brightness"), 0.3, 1.5, 0.01, xd.brightness);
    addXraySpin(tr("Bone intensity multiplier"), 0.5, 2.5, 0.01, xd.boneIntensityMult);
    addXraySpin(tr("Soft-tissue multiplier"), 0.1, 1.5, 0.01, xd.softTissueMult);
    // Independent per-material brightness — distinct from the two
    // multipliers just above: each is its own scalar on only one
    // material's final value, so tuning one has zero effect on the other.
    addXraySpin(tr("Skeleton/Bone brightness"), 0.1, 3.0, 0.01, xd.boneBrightness);
    addXraySpin(tr("Muscle brightness"), 0.1, 3.0, 0.01, xd.muscleBrightness);

    addXrayColorRow(tr("Vessel"), 0.0, 1.0, 0.01, xd.vesselColor);
    addXraySpin(tr("Vessel opacity"), 0.0, 1.0, 0.01, xd.vesselOpacity);

    auto *xrBtns = new QDialogButtonBox(QDialogButtonBox::Close, m_xrayTuningDialog);
    connect(xrBtns, &QDialogButtonBox::rejected, m_xrayTuningDialog, &QDialog::hide);

    // Save / Apply — the only place any of these 24 values actually reaches
    // the Fluoro view. Reads every spin box exactly once and applies them
    // as a single VesselView::XrayTuning, so the view's own current values
    // for anything not represented here (there is nothing else) are never
    // partially overwritten mid-update.
    QPushButton *xrApply = xrBtns->addButton(tr("Save / Apply"), QDialogButtonBox::ApplyRole);
    connect(xrApply, &QPushButton::clicked, this, [this]{
        VesselView *fv = m_quad ? m_quad->viewFluoro() : nullptr;
        if (!fv) return;
        std::array<double, 28> vals{};
        for (size_t i = 0; i < m_xraySpins.size() && i < vals.size(); ++i)
            vals[i] = m_xraySpins[i]->value();
        fv->setXrayTuning(xrayArrayToTuning(vals));
    });

    QPushButton *xrReset = xrBtns->addButton(tr("Reset to defaults"), QDialogButtonBox::ResetRole);
    connect(xrReset, &QPushButton::clicked, this, [this]{
        const VesselView::XrayTuning d;
        if (VesselView *fv = m_quad ? m_quad->viewFluoro() : nullptr) fv->setXrayTuning(d);
        const auto vals = xrayTuningToArray(d);
        for (size_t i = 0; i < m_xraySpins.size() && i < vals.size(); ++i) {
            QSignalBlocker block(m_xraySpins[i]);
            m_xraySpins[i]->setValue(vals[i]);
        }
    });
    xrForm->addRow(xrBtns);

    // ── Embolisation Module ──────────────────────────────────────────────
    // Same non-modal, persistent-dialog pattern as Calibration/X-Ray
    // Calibration: built once here, shown/raised/activated from the menu
    // action, never deleted or reconstructed. Deliberately non-modal (shown,
    // not exec()'d) so the operator can watch the 3D/Fluoro panes update
    // live while clicking Inject, the same reason the other two dialogs
    // are non-modal.
    m_emboDialog = new QDialog(this);
    m_emboDialog->setWindowTitle(tr("Embolisation Module"));
    auto *emboLay = new QVBoxLayout(m_emboDialog);

    emboLay->addWidget(new QLabel(tr("Embolic agent:"), m_emboDialog));
    m_emboAgentBox = new QComboBox(m_emboDialog);
    for (cn::EmbolicAgent a : {cn::EmbolicAgent::Onyx, cn::EmbolicAgent::Squid,
                               cn::EmbolicAgent::Phil, cn::EmbolicAgent::Glue})
        m_emboAgentBox->addItem(QString::fromLatin1(cn::embolicAgentSpec(a).name),
                                static_cast<int>(a));
    emboLay->addWidget(m_emboAgentBox);

    auto *emboNote = new QLabel(m_emboDialog);
    emboNote->setWordWrap(true);
    emboNote->setStyleSheet("color:#9B9B9B;");
    emboLay->addWidget(emboNote);
    auto updateEmboNote = [this, emboNote]{
        const auto a = static_cast<cn::EmbolicAgent>(m_emboAgentBox->currentData().toInt());
        emboNote->setText(QString::fromLatin1(cn::embolicAgentSpec(a).note));
    };
    connect(m_emboAgentBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [updateEmboNote](int){ updateEmboNote(); });
    updateEmboNote();

    m_emboStatusLabel = new QLabel(tr("Not started — position the catheter, then Begin."), m_emboDialog);
    m_emboStatusLabel->setWordWrap(true);
    emboLay->addWidget(m_emboStatusLabel);

    auto *emboBtnRow = new QHBoxLayout();
    auto *emboBegin  = new QPushButton(tr("Begin at catheter tip"), m_emboDialog);
    m_emboInjectBtn  = new QPushButton(tr("Start Injection"), m_emboDialog);
    m_emboResetBtn   = new QPushButton(tr("Reset"), m_emboDialog);
    m_emboInjectBtn->setEnabled(false);
    m_emboResetBtn->setEnabled(false);
    connect(emboBegin,       &QPushButton::clicked, this, [this]{ onBeginEmbolization(); });
    connect(m_emboInjectBtn, &QPushButton::clicked, this, [this]{ onInjectEmbolic(); });
    connect(m_emboResetBtn,  &QPushButton::clicked, this, [this]{ onResetEmbolization(); });
    emboBtnRow->addWidget(emboBegin);
    emboBtnRow->addWidget(m_emboInjectBtn);
    emboBtnRow->addWidget(m_emboResetBtn);
    emboLay->addLayout(emboBtnRow);

    auto *emboRateRow = new QHBoxLayout();
    emboRateRow->addWidget(new QLabel(tr("Speed"), m_emboDialog));
    m_emboSpeedSpin = new QDoubleSpinBox(m_emboDialog);
    m_emboSpeedSpin->setRange(0.5, 3.0);
    m_emboSpeedSpin->setSingleStep(0.1);
    m_emboSpeedSpin->setValue(1.0);
    m_emboSpeedSpin->setSuffix(tr("x"));
    emboRateRow->addWidget(m_emboSpeedSpin);
    emboRateRow->addSpacing(12);
    emboRateRow->addWidget(new QLabel(tr("Volume"), m_emboDialog));
    m_emboVolumeSpin = new QDoubleSpinBox(m_emboDialog);
    m_emboVolumeSpin->setRange(1.0, 20.0);
    m_emboVolumeSpin->setSingleStep(0.5);
    m_emboVolumeSpin->setValue(6.0);
    emboRateRow->addWidget(m_emboVolumeSpin);
    emboLay->addLayout(emboRateRow);

    // Changing agent mid-cast would silently reinterpret every link already
    // grown under the old agent's resistance weighting - simplest correct
    // rule is the same one changing a catheter's own shape uses elsewhere:
    // disable the choice once a procedure step is actually underway.
    connect(m_emboAgentBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int){ m_emboAgentBox->setEnabled(!m_emboli.active()); });

    auto *emboBtns = new QDialogButtonBox(QDialogButtonBox::Close, m_emboDialog);
    connect(emboBtns, &QDialogButtonBox::rejected, m_emboDialog, &QDialog::hide);
    emboLay->addWidget(emboBtns);

    // ── Contrast Injection ────────────────────────────────────────────────
    // Three procedural phases, in the order a real diagnostic run actually
    // happens: an angiogram before embolization, a flush to clear the field,
    // then a second angiogram after — the only way to actually SEE whether
    // the embolic cast blocked flow, per the request. Same non-modal,
    // built-once pattern as the other two dialogs.
    m_contrastDialog = new QDialog(this);
    m_contrastDialog->setWindowTitle(tr("Contrast Injection"));
    auto *conLay = new QVBoxLayout(m_contrastDialog);

    m_contrastStatusLabel = new QLabel(
        tr("Idle — position the catheter, then choose a phase below."), m_contrastDialog);
    m_contrastStatusLabel->setWordWrap(true);
    conLay->addWidget(m_contrastStatusLabel);

    auto *phaseNote = new QLabel(m_contrastDialog);
    phaseNote->setWordWrap(true);
    phaseNote->setStyleSheet("color:#9B9B9B;");
    phaseNote->setText(tr(
        "Pre-Embolization Angiogram: contrast flows freely, showing the target "
        "anatomy before treatment.\n"
        "Flush: saline/DMSO clears the field — contrast drains rapidly rather "
        "than washing out on its own.\n"
        "Post-Embolization Angiogram: contrast is injected again from the same "
        "tip — any embolic cast now genuinely blocks it from reaching the "
        "target vessel, the same as a real check angiogram."));
    conLay->addWidget(phaseNote);

    auto *phaseRow = new QHBoxLayout();
    auto *preBtn   = new QPushButton(tr("Pre-Embolization\nAngiogram"), m_contrastDialog);
    auto *flushBtn = new QPushButton(tr("Flush"), m_contrastDialog);
    auto *postBtn  = new QPushButton(tr("Post-Embolization\nAngiogram"), m_contrastDialog);
    connect(preBtn,   &QPushButton::clicked, this,
           [this]{ onBeginContrastPhase(cn::ContrastPhase::PreEmbolizationAngiogram); });
    connect(flushBtn, &QPushButton::clicked, this,
           [this]{ onBeginContrastPhase(cn::ContrastPhase::Flush); });
    connect(postBtn,  &QPushButton::clicked, this,
           [this]{ onBeginContrastPhase(cn::ContrastPhase::PostEmbolizationAngiogram); });
    phaseRow->addWidget(preBtn);
    phaseRow->addWidget(flushBtn);
    phaseRow->addWidget(postBtn);
    conLay->addLayout(phaseRow);

    auto *contrastRateRow = new QHBoxLayout();
    contrastRateRow->addWidget(new QLabel(tr("Speed"), m_contrastDialog));
    m_contrastSpeedSpin = new QDoubleSpinBox(m_contrastDialog);
    m_contrastSpeedSpin->setRange(0.5, 3.0);
    m_contrastSpeedSpin->setSingleStep(0.1);
    m_contrastSpeedSpin->setValue(1.0);
    m_contrastSpeedSpin->setSuffix(tr("x"));
    contrastRateRow->addWidget(m_contrastSpeedSpin);
    contrastRateRow->addSpacing(12);
    contrastRateRow->addWidget(new QLabel(tr("Volume"), m_contrastDialog));
    m_contrastVolumeSpin = new QDoubleSpinBox(m_contrastDialog);
    m_contrastVolumeSpin->setRange(2.0, 40.0);
    m_contrastVolumeSpin->setSingleStep(1.0);
    m_contrastVolumeSpin->setValue(10.0);
    contrastRateRow->addWidget(m_contrastVolumeSpin);
    conLay->addLayout(contrastRateRow);

    auto *contrastBtnRow = new QHBoxLayout();
    m_contrastInjectBtn = new QPushButton(tr("Start Injection"), m_contrastDialog);
    m_contrastInjectBtn->setEnabled(false);
    connect(m_contrastInjectBtn, &QPushButton::clicked, this, [this]{ onInjectContrast(); });
    m_contrastResetBtn = new QPushButton(tr("Reset"), m_contrastDialog);
    m_contrastResetBtn->setEnabled(false);
    connect(m_contrastResetBtn, &QPushButton::clicked, this, [this]{ onResetContrast(); });
    contrastBtnRow->addWidget(m_contrastInjectBtn);
    contrastBtnRow->addWidget(m_contrastResetBtn);
    conLay->addLayout(contrastBtnRow);

    auto *conBtns = new QDialogButtonBox(QDialogButtonBox::Close, m_contrastDialog);
    connect(conBtns, &QDialogButtonBox::rejected, m_contrastDialog, &QDialog::hide);
    conLay->addWidget(conBtns);

    // Procedure log removed from the UI. log() is guarded on a null m_log (see
    // MainWindow::log at the bottom of this file), so every log(...) call site
    // elsewhere in this file — telemetry, sheath/catheter selection, C-arm,
    // contrast, embolic deployment, and so on — keeps running exactly as
    // before; it simply has nowhere to display now.

    // statusBar()->hide() used to sit here from when the status bar carried
    // nothing but leftover instructions text ("instructions in Help menu").
    // It now carries the real System Status bar (SYSTEM STATUS/DOSE/AIR
    // KERMA/TOTAL FLUORO TIME/LAST IMAGE/clock) built earlier in this
    // function - hiding it here was undoing that entirely, which is exactly
    // why it looked like the bar was never created at all.
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
    // Search for the vessel STL next to the executable — exactly where
    // CMakeLists.txt's post-build step copies it (its `_asset` foreach lists
    // Vessel.stl by that exact name) — and in Data/3D/ as a fallback for a
    // non-packaged dev build. Previously searched for
    // "Simplified_Vessel_2.stl"/"Vessel_Reconstructed.stl", names that never
    // matched the actual shipped asset at all: auto-load silently fell
    // through to "no vessel found" on every single launch, not just this one.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/Vessel.stl",
        appDir + "/Data/3D/Vessel.stl",
        appDir + "/../Data/3D/Vessel.stl",
        appDir + "/../../Data/3D/Vessel.stl",
        appDir + "/../../../Data/3D/Vessel.stl",
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

    // ── Try pre-computed VTP centerline first (Vessel_centerlines.vtp) ──────
    // The VTP file must sit alongside the STL or in Data/3D.
    // If found: load directly (fast path — no geodesic extraction needed).
    // If not: fall back to geodesic extraction from the STL.
    bool ok = false;
    bool usedVtp = false;

    // Build candidate VTP paths from the loaded STL location
    const QString appDir = QCoreApplication::applicationDirPath();
    // Search for VTP next to the STL and next to the executable. Previously
    // searched for "Simplified_Vessel_2_centerlines.vtp" — the same stale-
    // filename bug as tryAutoLoad()'s STL search: it never matched the
    // actual shipped asset (Vessel_centerlines.vtp), so this fast path has
    // never actually been reached; every load fell straight through to the
    // slower geodesic extraction below regardless of whether a usable VTP
    // was sitting right there.
    const QString stlDir = m_bodyMeshDir;
    const QStringList vtpCandidates = {
        stlDir  + "/Vessel_centerlines.vtp",
        appDir  + "/Vessel_centerlines.vtp",
        appDir  + "/Data/3D/Vessel_centerlines.vtp",
        appDir  + "/../Data/3D/Vessel_centerlines.vtp",
        appDir  + "/../../Data/3D/Vessel_centerlines.vtp",
        appDir  + "/../../../Data/3D/Vessel_centerlines.vtp",
        appDir  + "/../../../../Data/3D/Vessel_centerlines.vtp",
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
    // NOTE: graph cleanup (simplifyLocalCycles()) was tried here across
    // several iterations this session, attempting to fix reported "distal
    // vessel - no further advance" dead-ends by removing what were assessed
    // as redundant/artifact edges near the iliac bifurcation and in the
    // cerebral circulation (Circle of Willis). It has been REMOVED entirely.
    //
    // Direct evidence from a preserved pre-session baseline of this exact
    // codebase confirmed the RAW, completely unmodified graph topology
    // successfully navigates all the way to the Middle Meningeal Artery for
    // both femoral and radial access - the graph itself was never the
    // problem. simplifyLocalCycles() decides which of two redundant edges
    // to discard purely by comparing total edge length (a standard minimum
    // spanning tree) - a geometric criterion with no awareness of which
    // edge is anatomically meaningful to keep. If the genuinely correct
    // path toward a real target happened to be the one flagged "redundant"
    // by that length comparison, this would have silently removed a real,
    // working connection while believing it was only cleaning up an
    // artifact - undoing exactly the kind of navigation this project needs
    // to support, in the course of trying to fix something else.
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
        // Every view starts with the same hardcoded BodyAlign defaults, so
        // this is a no-op today - but only because nothing currently calls
        // VesselView::nudgeBodyLayers() (a per-instance fine-tune already
        // built into VesselView) or otherwise diverges the two. Explicitly
        // reading the alignment back from the reference view (m_view) and
        // pushing it to every view via QuadPanel::setBodyAlignment() -
        // which already exists specifically to keep this from drifting,
        // per its own comment ("has to be pushed to each of them or the 3D
        // and Fluoro views drift apart") - turns "muscular/skeletal align
        // identically everywhere" into something this code actually
        // enforces, rather than an invariant that only holds by accident of
        // nothing having touched it yet.
        if (m_view)
            m_quad->setBodyAlignment(m_view->muscularAlignment(), m_view->skeletalAlignment());
        log(tr("Body meshes: Muscular=%1  Skeletal=%2 — skeleton aligned inside vessel envelope.")
                .arg(mus.isEmpty() ? "not found" : "ok")
                .arg(skel.isEmpty() ? "not found" : "ok"));
    }
}

/// Deliberately a no-op now. A previous version of this function spliced a
/// new node partway between the resolved femoral landmark and its downward
/// neighbour via straight-line interpolation between the two - verified
/// directly against the actual dense VTP source data to be unreliable: the
/// real vessel curves significantly between these two coarse graph nodes
/// (bowing out laterally by several centimetres), and a straight line cuts
/// through the middle of that curve. Measured position error at every
/// interpolation fraction tested (5-8mm) exceeded the vessel's own radius
/// at that point (3-4mm) - meaning that version was placing the sheath
/// genuinely outside the vessel wall, not "slightly down inside it". A
/// Hermite-spline alternative (using tangent directions derived from the
/// graph's own topology) was also tested and found even less reliable,
/// since the downstream node is itself a trifurcation with no dependable
/// way to know which of its several neighbours represents this specific
/// edge's own continuing tangent.
///
/// Properly supporting "move further down while staying on the vessel"
/// requires preserving the true dense polyline shape from the source VTP
/// file, which CenterlineGraph::Link currently discards entirely during
/// graph construction (it stores only from/to node IDs and a total
/// length, nothing about the shape in between) - a legitimate, larger,
/// separately-scoped change to the loader and graph structure, not
/// something to approximate here with an interpolation method already
/// shown not to work against this project's own data.
void MainWindow::nudgeFemoralEntriesDownward() {
}

void MainWindow::resolveAccessSites() {
    log(tr("resolveAccessSites: graph nodes=%1 mmPerUnit=%2 leafNodes=%3")
            .arg(m_graph.nodeCount()).arg(m_mmPerUnit).arg(m_vtpLeafNodes.size()));
    m_sites = cn::AccessSiteResolver::resolve(m_graph, m_mmPerUnit, true, m_vtpLeafNodes);
    nudgeFemoralEntriesDownward();
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
    for (int i = 0; i < 8 && i < static_cast<int>(m_sites.size()); ++i) {
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
    // Embolisation Module casts grow over this same graph.
    m_emboli.setGraph(&m_graph);
    // Contrast Injection floods the same graph too.
    m_contrastModel.setGraph(&m_graph);

    // No auto-selection: the catheter only positions once a sheath is chosen.
    // The instruction line that used to be written to m_emboLabel here has been
    // removed; the label keeps the step text it was built with. Nothing about
    // access-site resolution or sheath selection changes — m_sites is still
    // populated above and SheathManager still holds a verified path per site.
    // Default focus/zoom on startup is the Head-Sagittal view (per explicit
    // request), for every view at once. showHeadSagittal() emits
    // cameraChanged(), and buildUi()'s camera-sync wiring (syncAllCameras)
    // is SUPPOSED to propagate that to every other linked view from this one
    // call alone - but confirmed screenshots showed Fluoro still landing on
    // a completely different, zoomed-out framing at startup despite that
    // wiring appearing correct on inspection. Rather than continue relying
    // solely on the emit reaching a connected slot (whatever subtle timing
    // issue might be preventing that in this one specific case), this now
    // ALSO explicitly, directly syncs every other view's camera right here -
    // a guarantee that does not depend on any signal/slot connection firing
    // at all, on top of whatever the emit-based path already does.
    m_view->showHeadSagittal();
    for (VesselView *v : {m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
        if (v && v != m_view) v->syncCameraFrom(*m_view);
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

namespace {
/// Builds one case row's widget - case ID (bold), name/sex/age, patient
/// size/weight, and (when non-empty) a pathology line - matching the
/// reference design's text layout. No thumbnail: no real per-case imagery
/// exists in this project, and a placeholder was tried and found to look
/// wrong (a generic glyph made no sense across every module, especially
/// non-cardiac ones), so this row is text-only rather than papering over
/// that with another invented placeholder.
QWidget *buildCaseRowWidget(const SampleCase &c, QWidget *parent) {
    auto *row = new QWidget(parent);
    auto *textCol = new QVBoxLayout(row);
    textCol->setContentsMargins(8, 6, 8, 6);
    textCol->setSpacing(2);

    auto *idLabel = new QLabel(QString::fromLatin1(c.caseId), row);
    idLabel->setStyleSheet("color:#E8E8E8; font-weight:bold; font-size:12px;");
    textCol->addWidget(idLabel);

    const QString detail = QString("%1 (%2) %3 years").arg(QString::fromLatin1(c.name),
                                                             QString::fromLatin1(c.sex)).arg(c.age);
    auto *detailLabel = new QLabel(detail, row);
    detailLabel->setStyleSheet("color:#C9C9C9; font-size:11px;");
    textCol->addWidget(detailLabel);

    const QString info = QString("Patient information: size: %1 cm, weight: %2 kg")
                              .arg(c.heightCm).arg(c.weightKg);
    auto *infoLabel = new QLabel(info, row);
    infoLabel->setStyleSheet("color:#9B9B9B; font-size:10px; font-weight:bold;");
    textCol->addWidget(infoLabel);

    if (c.pathology[0] != '\0') {
        auto *pathLabel = new QLabel(QString::fromLatin1(c.pathology), row);
        pathLabel->setStyleSheet("color:#B08A5A; font-size:10px;");
        pathLabel->setWordWrap(true);
        textCol->addWidget(pathLabel);
    }
    return row;
}

/// Fills `list` with the given cases, one rich row widget each.
void populateCaseList(QListWidget *list, const SampleCase *cases, int count) {
    list->clear();
    for (int i = 0; i < count; ++i) {
        auto *item = new QListWidgetItem(list);
        auto *w = buildCaseRowWidget(cases[i], list);
        item->setSizeHint(w->sizeHint());
        item->setData(Qt::UserRole, QString::fromLatin1(cases[i].caseId));
        list->addItem(item);
        list->setItemWidget(item, w);
    }
}
} // namespace

void MainWindow::showPatientSelectionDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Please select a patient"));
    dlg.resize(1100, 560);
    auto *rootVL = new QVBoxLayout(&dlg);

    auto *title = new QLabel(tr("Please select a patient"), &dlg);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("color:#E8E8E8; font-size:14px; font-weight:bold; padding:4px;");
    rootVL->addWidget(title);

    auto *bodyHL = new QHBoxLayout();
    rootVL->addLayout(bodyHL, 1);

    // ── Left: banner (category/module, once one is picked) + case list ────
    auto *leftVL = new QVBoxLayout();
    auto *categoryBanner = new QLabel(&dlg);
    categoryBanner->setAlignment(Qt::AlignCenter);
    categoryBanner->setStyleSheet("background:#B9C6D8; color:#1A1A1A; font-size:20px; "
                                  "font-weight:bold; padding:10px;");
    categoryBanner->hide();
    leftVL->addWidget(categoryBanner);

    auto *moduleBanner = new QLabel(&dlg);
    moduleBanner->setAlignment(Qt::AlignCenter);
    moduleBanner->setStyleSheet("background:#25407A; color:#FFFFFF; font-size:15px; "
                                "font-weight:bold; padding:8px;");
    moduleBanner->hide();
    leftVL->addWidget(moduleBanner);

    auto *caseList = new QListWidget(&dlg);
    caseList->setSpacing(2);
    leftVL->addWidget(caseList, 1);
    bodyHL->addLayout(leftVL, 3);

    populateCaseList(caseList, kDefaultCases, static_cast<int>(std::size(kDefaultCases)));

    // ── Right: one continuous list, category headers (non-selectable, blue
    // banner) interspersed with selectable module rows - matches the
    // reference design's own layout exactly, rather than a combobox plus a
    // separately-populated list. ──────────────────────────────────────────
    auto *moduleList = new QListWidget(&dlg);
    moduleList->setSpacing(0);
    bodyHL->addWidget(moduleList, 2);

    for (int catIdx = 0; catIdx < static_cast<int>(std::size(kModuleCategories)); ++catIdx) {
        const auto &cat = kModuleCategories[catIdx];
        auto *header = new QListWidgetItem(QString::fromLatin1(cat.name));
        header->setFlags(Qt::NoItemFlags);   // header row itself is not selectable
        header->setBackground(QColor("#1E3A6B"));
        header->setForeground(QColor("#FFFFFF"));
        QFont hf = header->font(); hf.setBold(true); header->setFont(hf);
        moduleList->addItem(header);

        int modIdx = 0;
        for (const auto &mod : cat.modules) {
            auto *item = new QListWidgetItem(QStringLiteral("   \u25CB  ") + QString::fromLatin1(mod.name));
            item->setData(Qt::UserRole, catIdx);
            item->setData(Qt::UserRole + 1, modIdx);
            moduleList->addItem(item);
            ++modIdx;
        }
    }

    connect(moduleList, &QListWidget::itemClicked, &dlg,
            [&](QListWidgetItem *item) {
                if (!item || item->flags() == Qt::NoItemFlags) return;   // header row
                const int catIdx = item->data(Qt::UserRole).toInt();
                const int modIdx = item->data(Qt::UserRole + 1).toInt();
                if (catIdx < 0 || catIdx >= static_cast<int>(std::size(kModuleCategories))) return;
                const auto &cat = kModuleCategories[catIdx];
                if (modIdx < 0 || modIdx >= static_cast<int>(cat.modules.size())) return;
                const ModuleInfo &mod = *(cat.modules.begin() + modIdx);

                categoryBanner->setText(QString::fromLatin1(cat.name));
                categoryBanner->show();
                moduleBanner->setText(QString::fromLatin1(mod.name));
                moduleBanner->show();
                populateCaseList(caseList, mod.cases.begin(), static_cast<int>(mod.cases.size()));
            });

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    rootVL->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;
    QListWidgetItem *chosen = caseList->currentItem();
    if (!chosen) return;

    const QString caseId = chosen->data(Qt::UserRole).toString();
    const bool caseChanged = (caseId != m_activePatientCaseId);

    m_activePatientCaseId  = caseId;
    m_activeModuleCategory = categoryBanner->isVisible() ? categoryBanner->text() : tr("General");
    m_activeModuleName     = moduleBanner->isVisible() ? moduleBanner->text() : tr("Default Case");

    // "Synchronized across all views" here means this lives in the header
    // (visible regardless of which of 3D View/Fluoro/DSA/Road Map/Cine is
    // currently active), not duplicated per-view state that could drift.
    if (m_activeModuleLabel) m_activeModuleLabel->setText(caseId);

    // Sequential workflow: Patient -> Sheath -> Catheter. Selecting a
    // patient/case is what unlocks Sheath Selection in the first place -
    // it starts disabled (see buildUi()'s own setEnabled(false) on this
    // button). A genuinely different case also discards any sheath/
    // catheter already placed for whichever case was active before,
    // rather than letting them silently carry over to a case the operator
    // has not placed anything on yet.
    if (m_btnSheathSelect) m_btnSheathSelect->setEnabled(true);
    if (caseChanged) resetDeviceState(/*alsoResetSheath=*/true);

    // Only one actual vessel dataset exists in this project - there is no
    // second, third, or thirtieth set of anatomy to swap in per case, so
    // this tracks which case is "active" (the header read-out, this log
    // line) rather than reloading different patient data. Sheath,
    // catheter, embolization, fluoro, and 3D View all keep operating on
    // the one loaded model exactly as before; nothing about their own
    // behaviour changes based on which case is selected here.
    log(tr("[%1] Patient selected: %2 (%3 / %4)")
            .arg(QTime::currentTime().toString("HH:mm:ss"))
            .arg(caseId, m_activeModuleCategory, m_activeModuleName));
}
void MainWindow::onCatheterSelection() { showCatheterSelectionMenu(); }

void MainWindow::showInsertionDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Select Access Point"));
    dlg.setFixedSize(400, 340);
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
    auto *femBtn    = new QRadioButton(tr("Femoral (Groin)"),   accGrp);
    auto *radBtn    = new QRadioButton(tr("Radial (Wrist)"),    accGrp);
    auto *poplBtn   = new QRadioButton(tr("Popliteal (Knee)"),  accGrp);
    auto *brachBtn  = new QRadioButton(tr("Brachial (Upper Arm)"), accGrp);
    femBtn->setChecked(true);
    accVL->addWidget(femBtn); accVL->addWidget(radBtn); accVL->addWidget(poplBtn); accVL->addWidget(brachBtn);
    lay->addWidget(accGrp);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;
    if (!femBtn->isChecked() && !radBtn->isChecked() && !poplBtn->isChecked() && !brachBtn->isChecked()) {
        QMessageBox::warning(this, tr("Access Point"), tr("Please select an insertion point."));
        return;
    }

    // Map dialog selection to resolved access sites and restart
    cn::AccessSite wantSite;
    bool wantLeft = leftBtn->isChecked();
    if (brachBtn->isChecked())    wantSite = wantLeft ? cn::AccessSite::LeftBrachial  : cn::AccessSite::RightBrachial;
    else if (poplBtn->isChecked())     wantSite = wantLeft ? cn::AccessSite::LeftPopliteal : cn::AccessSite::RightPopliteal;
    else if (radBtn->isChecked()) wantSite = wantLeft ? cn::AccessSite::LeftRadial    : cn::AccessSite::RightRadial;
    else                          wantSite = wantLeft ? cn::AccessSite::LeftFemoral   : cn::AccessSite::RightFemoral;

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

    // Place sheath at access point. Camera POSITION/ORIENTATION is left
    // exactly where it is - per explicit request, selecting a sheath must
    // not move, zoom, or re-focus the camera in either view. What DOES
    // still happen (true here) is enabling camera-follow mode, so the
    // catheter is trackable once it starts moving - restartFromCurrentSite()
    // now does this via setCameraFollows(true) alone, never focusOn(), so
    // it cannot jump the camera anywhere; see that function's own comment.
    restartFromCurrentSite(true);
    showDeviceSpecDialog(tr("Access Sheath"));

    const QString side = wantLeft ? tr("Left") : tr("Right");
    const QString site = brachBtn->isChecked() ? tr("Brachial")
                        : poplBtn->isChecked()  ? tr("Popliteal")
                        : radBtn->isChecked()   ? tr("Radial")
                                               : tr("Femoral");

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
bool MainWindow::applyCatheterSpec(const QString &displayName, const QString &sizeLabel,
                                   const QString &tipShapeName) {
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

    // The diagnostic-catheter body carries no fixed curve of its own (see
    // CatheterCatalog.cpp) — the shape picked in Catheter Selection is what
    // actually bends the tip. Every other device keeps the fixed curve
    // catheterParams() already read out of its own catalogue entry, so
    // tipShapeName is simply ignored for those.
    QString appliedShape;
    if (spec->devClass == cn::DeviceClass::BaseGuide) {
        const cn::TipShapeDef *shape = cn::findTipShape(tipShapeName.toUtf8().constData());
        if (!shape) {
            // Fall back to the first catalogued shape rather than leaving the
            // tip perfectly straight, which would silently look like "no
            // device selected" in the viewport.
            size_t n = 0;
            shape = &cn::tipShapeCatalog(n)[0];
        }
        p = cn::applyTipShape(p, *shape, static_cast<float>(m_mmPerUnit));
        appliedShape = QString::fromLatin1(shape->name);
    }
    m_activeTipShape = appliedShape;

    // Keep the containment settings the console already uses.
    //
    // A genuine VALUE copy, not a reference: this is read again further
    // down, after m_catheter.setParams(p) below has already overwritten
    // m_catheter's own internal params storage with the NEW device's
    // values. A reference bound here would silently start reflecting that
    // new value too, at the exact spot (the parked-device snapshot) that
    // most needs the OLD device's params - which is exactly what was
    // happening: the "frozen" guide catheter left behind on telescoping was
    // being built from the microcatheter's own radius/stiffness/tip curve
    // instead of the guide's, because CatheterModel(m_catheter.params())
    // below was reading the value AFTER it had already been overwritten.
    const cn::CatheterModel::Params old = m_catheter.params();
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
        // `old`, not m_catheter.params(): the live object's params were
        // already overwritten with the NEW device's values two lines above,
        // via setParams(p). Reading m_catheter.params() here (as this used
        // to) silently built the parked "guide" snapshot out of the
        // microcatheter's own physical properties instead of the guide's
        // own - same depth/path, but the wrong radius, stiffness, and tip
        // shape, which is exactly the kind of change that reads as "the
        // guide catheter moved/reset" even though its position value never
        // actually did.
        auto parked = std::make_unique<cn::CatheterModel>(old);
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
    const bool isMicrocatheter = (spec->devClass == cn::DeviceClass::Microcatheter);
    const QVector3D deviceColour(spec->colour[0], spec->colour[1], spec->colour[2]);
    for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()}) {
        v->setDevice(&m_catheter);
        v->setHostDevices(hosts);
        v->setDeviceIsMicrocatheter(isMicrocatheter);
        v->setDeviceColor(deviceColour);
        // Already 0.f when not telescoping, the host's own tip depth when
        // it is - see VesselView::setDeviceHiddenBehindDepth()'s own
        // comment for why the active device's shaft needs this at all.
        v->setDeviceHiddenBehindDepth(m_coaxialBaseDepth);
    }

    updateDeviceGating();
    // The new device starts parked at the host tip, not at the access point.
    m_catheter.setPath(m_nav.path(), /*keepDepth=*/true);
    m_catheter.setInsertionDepth(m_nav.depth());
    syncDevices();

    // Frontal/AP projection and framing, Fluoro view only, once the
    // catheter is actually selected - per explicit request. C-arm angle
    // change necessarily goes through the shared CarmController: the
    // Fluoro pane's own on-screen LAO/CRAN readout is driven from its
    // state (onCarmAnglesChanged()), so setting the Fluoro camera's angle
    // directly instead would leave that readout showing a stale value that
    // no longer matches the projection actually displayed. This does mean
    // the 3D view's own camera orientation moves too - both are linked to
    // one physical C-arm rig by this project's existing design - only the
    // centering/framing immediately below is scoped to Fluoro alone.
    // Camera position on sheath placement (restartFromCurrentSite()) is
    // untouched by this - that is a separate, earlier workflow step with
    // its own explicit requirement not to move the camera, and this only
    // runs here, later, at catheter selection.
    if (m_carmCtrl) {
        m_carmCtrl->rotateLAO(-m_carmCtrl->laoAngle());
        m_carmCtrl->rotateCRAN(-m_carmCtrl->cranAngle());
    }
    if (VesselView *vf = m_quad->viewFluoro()) {
        const cn::Vec3 tipPos = m_nav.path().pointAt(m_nav.depth());
        const float tipRadius = std::max(m_nav.path().radiusAt(m_nav.depth()), 1e-4f);
        vf->focusOn(QVector3D(tipPos.x, tipPos.y, tipPos.z), tipRadius);
    }

    log(tr("%1 %2 (%3, %4) — %5 Fr, ⌀%6 mm OD, %7 mm lumen, %8 cm.%9 %10")
            .arg(displayName)
            .arg(QString::fromLatin1(sz.label))
            .arg(QString::fromLatin1(spec->maker))
            .arg(QString::fromLatin1(cn::deviceClassName(spec->devClass)))
            .arg(sz.frenchOd, 0, 'f', 1)
            .arg(sz.outerDiaMm, 0, 'f', 2)
            .arg(sz.innerDiaMm, 0, 'f', 2)
            .arg(sz.workingLenCm, 0, 'f', 0)
            .arg(appliedShape.isEmpty() ? QString() : tr(" Tip shape: %1.").arg(appliedShape))
            .arg(QString::fromLatin1(spec->note)));
    return true;
}

// Clears catheter (always) and sheath (when asked) state together, so the
// UI, the device-gating labels, and what is actually attached to every view
// never drift out of sync with each other.
void MainWindow::resetDeviceState(bool alsoResetSheath) {
    // Clear the catheter unconditionally - a stale catheter must never
    // carry over to a sheath the operator has not explicitly attached it
    // to (a fresh sheath placement, or an entirely new patient/case).
    m_catheterReady = false;
    m_ready         = false;
    for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
        if (v) v->setDevice(nullptr);

    if (alsoResetSheath) {
        m_sheathReady = false;
        for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
            if (v) v->clearSheath();
    }

    updateDeviceGating();
}

// Enables navigation only when there is both an access and a device in it, and
// keeps the two status read-outs in step.
void MainWindow::updateDeviceGating() {
    // Advance/Retract/Left/Right have no buttons to enable — the I/K/J/L
    // shortcuts call handlers that already gate themselves on m_ready /
    // decisionPending, so there is nothing to toggle here for them.
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
    if (m_fDevModel)
        m_fDevModel->setText(s ? (m_activeTipShape.isEmpty()
                                     ? m_activeCatheter
                                     : tr("%1 — %2").arg(m_activeCatheter, m_activeTipShape))
                              : tr("no catheter"));

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
    // +40 px over the previous fixed height: room for the tip-shape row,
    // shown only when the diagnostic-catheter body is selected.
    dlg.setFixedSize(400, 340);
    auto *lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(tr("Select the %1:").arg(stageName), &dlg));

    auto *typeBox = new QComboBox(&dlg);
    auto *sizeBox = new QComboBox(&dlg);
    // Tip-shape picker — only meaningful for the diagnostic-catheter body
    // (DeviceClass::BaseGuide); every other device carries its own fixed
    // shape in the catalogue. Created up front and shown/hidden as the model
    // selection changes, rather than rebuilding the dialog layout each time.
    auto *shapeBox = new QComboBox(&dlg);
    auto *shapeLabel = new QLabel(tr("Tip shape:"), &dlg);
    {
        size_t n = 0;
        const cn::TipShapeDef *shapes = cn::tipShapeCatalog(n);
        for (size_t i = 0; i < n; ++i)
            shapeBox->addItem(QString::fromLatin1(shapes[i].name));
    }
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
    // Shows the shape picker only when the selected model is the diagnostic-
    // catheter body — DeviceClass::BaseGuide is what carries no fixed curve
    // of its own (see CatheterCatalog.cpp), so it is the only case where a
    // shape choice means anything. Also refreshes the shape note text so the
    // operator sees what the currently-highlighted shape actually looks like,
    // the same live read-out `describe` already gives the device dimensions.
    auto updateShapeVisibility = [shapeBox, shapeLabel](const QString &nm) {
        const cn::CatheterSpec *s = cn::findCatheter(nm.toUtf8().constData());
        const bool isDiagnostic = s && s->devClass == cn::DeviceClass::BaseGuide;
        shapeBox->setVisible(isDiagnostic);
        shapeLabel->setVisible(isDiagnostic);
    };
    connect(typeBox, &QComboBox::currentTextChanged, &dlg,
            [&](const QString &nm){ repopulateSizes(nm); describe(nm); updateShapeVisibility(nm); });
    connect(sizeBox, &QComboBox::currentTextChanged, &dlg,
            [&](const QString &){ describe(typeBox->currentText()); });
    repopulateSizes(typeBox->currentText());
    describe(typeBox->currentText());
    updateShapeVisibility(typeBox->currentText());
    lay->addWidget(specLabel);

    lay->addWidget(new QLabel(tr("Size:"), &dlg)); lay->addWidget(sizeBox);
    lay->addWidget(shapeLabel); lay->addWidget(shapeBox);

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

        // NOT shapeBox->isVisible(): QDialog::accept() has already hidden the
        // dialog and every child widget by the time dlg.exec() returns, so a
        // widget's own visibility is always false here regardless of what it
        // showed while the dialog was open — that was silently discarding
        // every shape choice and falling back to the first catalogue shape
        // every time. The devClass check below is what visibility was really
        // standing in for, and it isn't affected by the dialog having closed.
        const cn::CatheterSpec *chosen =
            cn::findCatheter(typeBox->currentText().toUtf8().constData());
        const bool isDiagnostic = chosen && chosen->devClass == cn::DeviceClass::BaseGuide;
        const QString shapeName = isDiagnostic ? shapeBox->currentText() : QString();

        // Anything that is not the access sheath becomes the live device.
        // Choosing "Sofia 6F" now actually loads a 2.11 mm catheter with a
        // 16-segment compliant tip, not just a string for the log. Choosing
        // the diagnostic-catheter body additionally bakes on whichever named
        // shape (SIM2, CK1, ...) was picked above.
        if (!stageName.contains("Sheath", Qt::CaseInsensitive))
            applyCatheterSpec(m_deviceSpec.catheterType, m_deviceSpec.catheterSizeFr, shapeName);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Road Map / DSA — same visual-mode logic as old project
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onRoadMap() {
    if (m_pageStack) m_pageStack->setCurrentIndex(0);
    m_roadMapActive = !m_roadMapActive;
    // Switch to fluoro pane and tell it what mode
    m_quad->showPane(QuadPanel::Pane::Fluoro);
    // Same explicit, direct sync as onViewModeChanged(1)'s own fix - this
    // path switches to Fluoro independently, so needs its own copy rather
    // than relying on that other call site's fix to somehow cover it too.
    // Runs unconditionally now, same reasoning as that other call site's
    // own comment: the !m_sheathReady guard this used to have only made
    // sense while restartFromCurrentSite() deliberately focused Fluoro on
    // the access site after sheath placement, which was removed in a
    // later fix - so the guard was left blocking legitimate
    // synchronization with nothing left to protect.
    if (m_quad->viewFluoro() && m_view)
        m_quad->viewFluoro()->syncCameraFrom(*m_view);
    checkViewAction(1);
    m_quad->viewFluoro()->setRoadMapActive(m_roadMapActive);
    setNavTileActive(m_roadMapTile, m_roadMapActive);
    if (m_statusLabel)

    log(tr("[%1] Road Map %2").arg(QTime::currentTime().toString("HH:mm:ss"),
                                   m_roadMapActive ? tr("ON") : tr("OFF")));
}

void MainWindow::onDSAView() {
    if (m_pageStack) m_pageStack->setCurrentIndex(0);
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
// Embolisation Module — real cast growth, shared by every view.
//
// Deliberately separate from onDeployEmbolic() above (the Contrast Injection
// dialog's own "agent" dropdown): that path is untouched by this feature,
// per the request to build on top of the existing intervention system
// without changing what is already there. This module drives
// cn::EmbolizationModel directly, and every view renders that one shared
// instance exactly the way every view already renders the one shared
// m_catheter — see EmbolizationModel's own class comment for why that
// guarantees the 3D and Fluoro panes can never disagree about the cast.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::showEmbolizationDialog() {
    if (m_pageStack) m_pageStack->setCurrentIndex(0);
    if (!m_emboDialog) return;
    if (m_emboAgentBox) m_emboAgentBox->setEnabled(!m_emboli.active());
    m_emboDialog->show();
    m_emboDialog->raise();
    m_emboDialog->activateWindow();
}

int32_t MainWindow::findProximalNeighbor(int32_t tipNode) const {
    if (tipNode < 0 || m_activeSite < 0 || m_activeSite >= static_cast<int>(m_sites.size()))
        return -1;
    const int32_t accessNode = m_sites[m_activeSite].node;
    const std::vector<float> dist = m_graph.distancesFrom(accessNode);
    if (static_cast<size_t>(tipNode) >= dist.size()) return -1;
    const float tipDist = dist[static_cast<size_t>(tipNode)];

    // Among the tip's own graph neighbors, whichever is genuinely CLOSER to
    // the access site than the tip itself is the direction the catheter
    // arrived FROM — the one segment its own body currently occupies. If
    // several qualify (a complex junction), the closest-to-access one is
    // the most direct "back the way it came" choice.
    int32_t best = -1;
    float bestDist = tipDist;
    for (int32_t n : m_graph.neighbours(tipNode)) {
        if (static_cast<size_t>(n) >= dist.size()) continue;
        if (dist[static_cast<size_t>(n)] < bestDist) {
            bestDist = dist[static_cast<size_t>(n)];
            best = n;
        }
    }
    return best;
}

void MainWindow::onBeginEmbolization() {
    // The catheter tip's current graph node — the same field the telemetry
    // read-out and branch-id display already use, so "where the tip is" is
    // never a second, independently-tracked notion of position.
    const int32_t tipNode = m_catheter.state().currentNodeId;
    if (tipNode < 0) {
        if (m_emboStatusLabel)
            m_emboStatusLabel->setText(
                tr("No catheter tip position available — advance the catheter into the vessel first."));
        return;
    }
    if (!m_emboAgentBox) return;
    const auto agent = static_cast<cn::EmbolicAgent>(m_emboAgentBox->currentData().toInt());
    m_emboli.setAgent(agent);
    m_emboli.begin(tipNode, findProximalNeighbor(tipNode));
    m_emboliInjecting = false;
    m_emboliTargetVolume = 0.f;

    if (m_emboAgentBox)  m_emboAgentBox->setEnabled(false);
    if (m_emboInjectBtn) {
        m_emboInjectBtn->setEnabled(true);
        m_emboInjectBtn->setText(tr("Start Injection"));
    }
    if (m_emboResetBtn)  m_emboResetBtn->setEnabled(true);
    if (m_quickEmboAgentBox) {
        const QSignalBlocker block(m_quickEmboAgentBox);
        const int idx = m_quickEmboAgentBox->findData(static_cast<int>(agent));
        if (idx >= 0) m_quickEmboAgentBox->setCurrentIndex(idx);
        m_quickEmboAgentBox->setEnabled(false);
    }
    updateEmboliReadout();
    refreshEmboliViews();
    log(tr("[%1] Embolisation started — %2 at node %3")
            .arg(QTime::currentTime().toString("HH:mm:ss"),
                 QString::fromLatin1(cn::embolicAgentSpec(agent).name))
            .arg(tipNode));
}

void MainWindow::onInjectEmbolic() {
    if (!m_emboli.active()) return;
    // Toggle continuous injection rather than fire one discrete step - the
    // actual per-tick injecting happens in onContrastTick() (shared timer
    // with contrast - see m_emboliInjecting's own comment), which gives a
    // visibly gradual, continuously-advancing front instead of an instant
    // jump each press.
    m_emboliInjecting = !m_emboliInjecting;
    if (m_emboliInjecting) {
        const float target = m_emboVolumeSpin ? static_cast<float>(m_emboVolumeSpin->value()) : 6.f;
        m_emboliTargetVolume = std::max(target, m_emboli.state().volumeInjected);
    }
    if (m_emboInjectBtn)
        m_emboInjectBtn->setText(m_emboliInjecting ? tr("Stop Injection") : tr("Start Injection"));
}

void MainWindow::onResetEmbolization() {
    m_emboli.reset();
    m_emboliInjecting = false;
    m_emboliTargetVolume = 0.f;
    if (m_emboAgentBox)      m_emboAgentBox->setEnabled(true);
    if (m_emboInjectBtn) {
        m_emboInjectBtn->setEnabled(false);
        m_emboInjectBtn->setText(tr("Start Injection"));
    }
    if (m_emboResetBtn)      m_emboResetBtn->setEnabled(false);
    if (m_quickEmboAgentBox) m_quickEmboAgentBox->setEnabled(true);
    if (m_emboStatusLabel)
        m_emboStatusLabel->setText(tr("Not started — position the catheter, then Begin."));
    refreshEmboliViews();
    log(tr("[%1] Embolisation reset").arg(QTime::currentTime().toString("HH:mm:ss")));
}

void MainWindow::updateEmboliReadout() {
    if (!m_emboStatusLabel) return;
    const auto &st = m_emboli.state();
    if (!st.active) {
        m_emboStatusLabel->setText(tr("Not started — position the catheter, then Begin."));
        return;
    }
    const cn::EmbolicAgentSpec &spec = cn::embolicAgentSpec(m_emboli.agent());
    m_emboStatusLabel->setText(
        tr("%1 — %2 injected, cast reach %3 mm, %4 vessel segment(s)")
            .arg(QString::fromLatin1(spec.name))
            .arg(st.volumeInjected, 0, 'f', 1)
            .arg(st.filledArcLength * m_mmPerUnit, 0, 'f', 1)
            .arg(st.links.size()));
}

void MainWindow::refreshEmboliViews() {
    // Same fan-out as every other shared-model update (setDevice, contrast,
    // road map): every view that can show anatomy gets told to redraw.
    // Mesh/DSA are aliases of the 3D view, so this costs nothing extra for
    // them - it is the Fluoro call that matters, and it is genuinely
    // necessary: Fluoro would otherwise not repaint until something else
    // (e.g. the cardiac timer) happened to trigger it.
    for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
        if (v) v->refreshEmbolization();
}

// ─────────────────────────────────────────────────────────────────────────────
// Embedded quick-inject Embolic Agent panel (sidebar) — drives the exact same
// m_emboli the Embolisation Module dialog does. See that dialog's own
// onBeginEmbolization()/onInjectEmbolic() for the "advanced" equivalents;
// this is the reference design's fast path (dropdown + volume + one button).
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onQuickEmboVolumeStep(float deltaMl) {
    m_quickEmboVolumeMl = std::clamp(m_quickEmboVolumeMl + deltaMl, 0.02f, 2.0f);
    if (m_quickEmboVolumeLabel)
        m_quickEmboVolumeLabel->setText(tr("%1 mL").arg(m_quickEmboVolumeMl, 0, 'f', 2));
}

void MainWindow::onQuickEmboInject() {
    if (!m_quickEmboAgentBox) return;
    const auto agent = static_cast<cn::EmbolicAgent>(m_quickEmboAgentBox->currentData().toInt());

    if (!m_emboli.active()) {
        // First press starts the cast, exactly like the dialog's own "Begin
        // at catheter tip" - the reference design has no separate Begin
        // button, so INJECT itself has to do both jobs on its first press.
        const int32_t tipNode = m_catheter.state().currentNodeId;
        if (tipNode < 0) {
            log(tr("[%1] Cannot inject — no catheter tip position available.")
                    .arg(QTime::currentTime().toString("HH:mm:ss")));
            return;
        }
        m_emboli.setAgent(agent);
        m_emboli.begin(tipNode, findProximalNeighbor(tipNode));

        // Keep the Embolisation Module dialog's own combo/buttons in sync,
        // since both panels drive the one shared model.
        if (m_emboAgentBox) {
            const QSignalBlocker block(m_emboAgentBox);
            const int idx = m_emboAgentBox->findData(static_cast<int>(agent));
            if (idx >= 0) m_emboAgentBox->setCurrentIndex(idx);
            m_emboAgentBox->setEnabled(false);
        }
        if (m_emboInjectBtn) m_emboInjectBtn->setEnabled(true);
        if (m_emboResetBtn)  m_emboResetBtn->setEnabled(true);
        m_quickEmboAgentBox->setEnabled(false);

        log(tr("[%1] Embolisation started — %2 at node %3")
                .arg(QTime::currentTime().toString("HH:mm:ss"),
                     QString::fromLatin1(cn::embolicAgentSpec(agent).name))
                .arg(tipNode));
    }

    // Adds to the shared target volume and (re)starts continuous injection,
    // rather than applying the whole amount as one instant jump - the
    // shared timer in onContrastTick() then drains toward this new,
    // higher target gradually, same as the dialog's own Start Injection
    // toggle, just reached by pressing INJECT repeatedly instead.
    const float addedVolume = m_quickEmboVolumeMl * 10.f;
    m_emboliTargetVolume = std::max(m_emboliTargetVolume, m_emboli.state().volumeInjected) + addedVolume;
    m_emboliInjecting = true;
    if (m_emboInjectBtn) m_emboInjectBtn->setText(tr("Stop Injection"));
    updateEmboliReadout();
    refreshEmboliViews();
}

// ─────────────────────────────────────────────────────────────────────────────
// Top header bar actions
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onScreenshot() {
    // Captures whichever pane is actually the current display mode - Fluoro
    // if that tile is checked, the 3D/Mesh/DSA view (all the same VesselView
    // instance) otherwise.
    QOpenGLWidget *target = (m_viewTiles[1] && m_viewTiles[1]->isChecked())
                                ? static_cast<QOpenGLWidget *>(m_quad->viewFluoro())
                                : static_cast<QOpenGLWidget *>(m_view);
    if (!target) return;

    const QImage img = CineViewStudio::captureWidgetFrame(target);
    if (img.isNull()) {
        log(tr("[%1] Screenshot failed — no frame captured.")
                .arg(QTime::currentTime().toString("HH:mm:ss")));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Screenshot"),
        QDir::homePath() + "/CathTrainVR_Screenshot_" +
            QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png",
        tr("PNG Image (*.png)"));
    if (path.isEmpty()) return;

    if (img.save(path)) {
        m_hasLastImage = true;
        m_lastImageClock.restart();
        log(tr("[%1] Screenshot saved to %2")
                .arg(QTime::currentTime().toString("HH:mm:ss"), path));
    } else {
        log(tr("[%1] Screenshot save failed: %2")
                .arg(QTime::currentTime().toString("HH:mm:ss"), path));
    }
}

void MainWindow::onGenerateReport() {
    // A genuine summary of real app state, not a templated mock-up - every
    // line here is read from the same fields the Procedure Info/Device
    // Status cards already display.
    QString report;
    report += tr("CathTrainVR Procedure Report\n");
    report += tr("Generated: %1\n\n").arg(QDateTime::currentDateTime().toString());
    report += tr("Access:   %1\n").arg(m_fAccess ? m_fAccess->text() : tr("not established"));
    report += tr("Sheath:   %1\n").arg(m_fSheath ? m_fSheath->text() : tr("no sheath"));
    report += tr("Catheter: %1\n").arg(m_fCatheter ? m_fCatheter->text() : tr("no catheter"));
    report += tr("Branch: %1   Turns: %2   Depth: %3\n")
                  .arg(m_fBranch ? m_fBranch->text() : tr("-"),
                       m_fTurns ? m_fTurns->text() : tr("-"),
                       m_fDepth ? m_fDepth->text() : tr("-"));

    if (m_emboli.active()) {
        const auto &st = m_emboli.state();
        report += tr("\nEmbolic Agent: %1\n")
                      .arg(QString::fromLatin1(cn::embolicAgentSpec(m_emboli.agent()).name));
        report += tr("Volume injected: %1\n").arg(st.volumeInjected, 0, 'f', 2);
        report += tr("Cast reach: %1 mm across %2 vessel segment(s)\n")
                      .arg(st.filledArcLength * m_mmPerUnit, 0, 'f', 1)
                      .arg(st.links.size());
    } else {
        report += tr("\nEmbolic Agent: not started\n");
    }

    if (m_contrastModel.phase() != cn::ContrastPhase::Idle) {
        const auto &cst = m_contrastModel.state();
        report += tr("\nContrast: last phase active, %1 injected, reach %2 mm\n")
                      .arg(cst.volumeInjected, 0, 'f', 1)
                      .arg(cst.filledArcLength * m_mmPerUnit, 0, 'f', 1);
    }

    QMessageBox::information(this, tr("Procedure Report"), report);
    log(tr("[%1] Report generated").arg(QTime::currentTime().toString("HH:mm:ss")));
}

void MainWindow::onEndProcedure() {
    if (QMessageBox::question(
            this, tr("End Procedure"),
            tr("End the current procedure? This resets embolisation and contrast state "
               "and restarts the procedure timer."),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;

    m_emboli.reset();
    m_contrastModel.reset();
    refreshEmboliViews();
    refreshContrastViews();
    updateEmboliReadout();
    updateContrastReadout();

    if (m_emboAgentBox)      m_emboAgentBox->setEnabled(true);
    if (m_emboInjectBtn)     m_emboInjectBtn->setEnabled(false);
    if (m_emboResetBtn)      m_emboResetBtn->setEnabled(false);
    if (m_quickEmboAgentBox) m_quickEmboAgentBox->setEnabled(true);

    stopCineRecording();

    // Stale mask from a previous case must never silently carry over - see
    // clearRoadmapMask()'s own comment.
    if (m_quad && m_quad->viewFluoro()) m_quad->viewFluoro()->clearRoadmapMask();

    m_procedureClock.restart();
    log(tr("[%1] Procedure ended — embolisation/contrast state reset.")
            .arg(QTime::currentTime().toString("HH:mm:ss")));
}

// ─────────────────────────────────────────────────────────────────────────────
// Contrast Injection — real 3-phase procedural simulation, sharing the same
// architecture as the Embolisation Module above (one model instance, every
// view renders that same instance). See ContrastInjectionModel's own class
// comment for why occlusion-blocking is a mechanistic graph property here,
// not a scripted before/after swap.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::showContrastInjectionDialog() {
    if (m_pageStack) m_pageStack->setCurrentIndex(0);
    if (!m_contrastDialog) return;
    m_contrastDialog->show();
    m_contrastDialog->raise();
    m_contrastDialog->activateWindow();
}

void MainWindow::onBeginContrastPhase(cn::ContrastPhase phase) {
    using cn::ContrastPhase;

    if (phase == ContrastPhase::Flush) {
        m_contrastModel.beginPhase(ContrastPhase::Flush);
        m_contrastInjecting = false;
        if (m_contrastInjectBtn) {
            m_contrastInjectBtn->setEnabled(false);
            m_contrastInjectBtn->setText(tr("Start Injection"));
        }
        if (m_contrastResetBtn) m_contrastResetBtn->setEnabled(true);
        updateContrastReadout();
        refreshContrastViews();
        log(tr("[%1] Contrast — flush started").arg(QTime::currentTime().toString("HH:mm:ss")));
        return;
    }

    // Both angiogram phases start from the catheter tip's current graph
    // node — the exact same field the Embolisation Module and the console's
    // own telemetry read-out already use as "where the tip is".
    const int32_t tipNode = m_catheter.state().currentNodeId;
    if (tipNode < 0) {
        if (m_contrastStatusLabel)
            m_contrastStatusLabel->setText(
                tr("No catheter tip position available — advance the catheter into the vessel first."));
        return;
    }

    m_contrastModel.beginPhase(phase, tipNode, findProximalNeighbor(tipNode));
    m_contrastInjecting = false;
    if (m_contrastInjectBtn) {
        m_contrastInjectBtn->setEnabled(true);
        m_contrastInjectBtn->setText(tr("Start Injection"));
    }
    if (m_contrastResetBtn) m_contrastResetBtn->setEnabled(true);
    updateContrastReadout();
    refreshContrastViews();

    // A real Cine run starts with the diagnostic angiogram, not with the
    // operator remembering to open Cine Studio separately - starting here
    // means the pre-embo -> intervention -> post-embo progression the
    // recording shows is exactly what actually happened, in order.
    if (phase == ContrastPhase::PreEmbolizationAngiogram)
        startCineRecordingIfNeeded();

    const bool isPost = (phase == ContrastPhase::PostEmbolizationAngiogram);
    log(tr("[%1] Contrast — %2 started at node %3")
            .arg(QTime::currentTime().toString("HH:mm:ss"),
                 isPost ? tr("post-embolization angiogram") : tr("pre-embolization angiogram"))
            .arg(tipNode));
}

void MainWindow::onInjectContrast() {
    using cn::ContrastPhase;
    if (m_contrastModel.phase() != ContrastPhase::PreEmbolizationAngiogram &&
        m_contrastModel.phase() != ContrastPhase::PostEmbolizationAngiogram)
        return;
    // Toggle continuous injection rather than fire one discrete step - see
    // m_contrastInjecting's own comment. onContrastTick() does the actual
    // per-tick injecting while this is true.
    m_contrastInjecting = !m_contrastInjecting;
    if (m_contrastInjectBtn)
        m_contrastInjectBtn->setText(m_contrastInjecting ? tr("Stop Injection") : tr("Start Injection"));

    // Auto-capture the FIRST roadmap mask the first time contrast injection
    // actually starts - not on every subsequent toggle, which would just be
    // a wasteful re-capture of the same anatomy at the same C-arm angle. A
    // real operator captures a mask once, at a moment of good vessel
    // opacification; from then on, "Re-mask" (see onRemaskRoadmap()) is the
    // explicit, deliberate action for refreshing it, e.g. after actually
    // rotating the C-arm.
    if (m_contrastInjecting && m_quad && m_quad->viewFluoro() &&
        m_quad->viewFluoro()->roadmapState() == VesselView::RoadmapState::NoMask)
        m_quad->viewFluoro()->captureRoadmapMask();
}

void MainWindow::onResetContrast() {
    using cn::ContrastPhase;
    m_contrastModel.beginPhase(ContrastPhase::Idle);   // beginPhase(Idle) calls reset() internally
    m_contrastInjecting = false;
    if (m_contrastInjectBtn) {
        m_contrastInjectBtn->setEnabled(false);
        m_contrastInjectBtn->setText(tr("Start Injection"));
    }
    if (m_contrastResetBtn) m_contrastResetBtn->setEnabled(false);
    updateContrastReadout();
    refreshContrastViews();
    log(tr("[%1] Contrast reset").arg(QTime::currentTime().toString("HH:mm:ss")));
}

void MainWindow::onContrastTick() {
    using cn::ContrastPhase;

    // Continuous embolic injection - no competing decay to account for
    // here (an embolic cast is a permanent deposit, unlike contrast, which
    // tick() below decays every call), so a modest, steady per-tick rate
    // alone gives smooth, gradual progress without needing to out-race
    // anything. Spreads what used to be one discrete 1.0-volume step
    // across roughly a second of ticks instead of applying it instantly.
    if (m_emboliInjecting && m_emboli.active()) {
        constexpr float kBaseVolumePerTick = 0.15f;
        const float speed = m_emboSpeedSpin ? static_cast<float>(m_emboSpeedSpin->value()) : 1.0f;
        m_emboli.inject(kBaseVolumePerTick * speed);
        updateEmboliReadout();
        refreshEmboliViews();

        const float targetVolume = m_emboliTargetVolume > 0.f ? m_emboliTargetVolume : 6.f;
        if (m_emboli.state().volumeInjected >= targetVolume) {
            m_emboliInjecting = false;
            if (m_emboInjectBtn) m_emboInjectBtn->setText(tr("Start Injection"));
        }
    }

    // tick() is a cheap no-op while Idle, but skip the readout/repaint fan-out
    // in that case too - no point repainting every view six times a second
    // for a phase that is not visibly changing.
    if (m_contrastModel.phase() == ContrastPhase::Idle) return;

    if (m_contrastInjecting &&
        (m_contrastModel.phase() == ContrastPhase::PreEmbolizationAngiogram ||
         m_contrastModel.phase() == ContrastPhase::PostEmbolizationAngiogram)) {
        // Base rate chosen to clearly exceed tick()'s own natural decay
        // (0.030 arc-length/sec = 0.0045/tick at this 150 ms interval): at
        // speed 1.0x, 0.6 volume/tick * kContrastSpreadRate(0.028, in
        // ContrastInjectionModel.cpp) = 0.0168 arc-length/tick, roughly
        // 3.7x the decay rate - clear, visible net forward progress each
        // tick rather than the front barely creeping or standing still
        // against its own washout.
        constexpr float kBaseVolumePerTick = 0.6f;
        const float speed = m_contrastSpeedSpin ? static_cast<float>(m_contrastSpeedSpin->value()) : 1.0f;
        m_contrastModel.inject(kBaseVolumePerTick * speed);

        const float targetVolume = m_contrastVolumeSpin
                                        ? static_cast<float>(m_contrastVolumeSpin->value()) : 10.f;
        if (m_contrastModel.state().volumeInjected >= targetVolume) {
            m_contrastInjecting = false;
            if (m_contrastInjectBtn) m_contrastInjectBtn->setText(tr("Start Injection"));
        }
    }

    m_contrastModel.tick(0.15f);   // matches m_contrastTimer's 150 ms interval
    updateContrastReadout();
    refreshContrastViews();
}

void MainWindow::updateContrastReadout() {
    if (!m_contrastStatusLabel) return;
    using cn::ContrastPhase;
    const auto &st = m_contrastModel.state();

    QString phaseName;
    switch (st.phase) {
    case ContrastPhase::Idle:                      phaseName = tr("Idle"); break;
    case ContrastPhase::PreEmbolizationAngiogram:  phaseName = tr("Pre-Embolization Angiogram"); break;
    case ContrastPhase::Flush:                     phaseName = tr("Flush"); break;
    case ContrastPhase::PostEmbolizationAngiogram: phaseName = tr("Post-Embolization Angiogram"); break;
    }

    if (st.phase == ContrastPhase::Idle) {
        m_contrastStatusLabel->setText(tr("Idle — position the catheter, then choose a phase below."));
        return;
    }
    m_contrastStatusLabel->setText(
        tr("%1 — %2 injected, contrast reach %3 mm, %4 vessel segment(s) opacified")
            .arg(phaseName)
            .arg(st.volumeInjected, 0, 'f', 1)
            .arg(st.filledArcLength * m_mmPerUnit, 0, 'f', 1)
            .arg(st.links.size()));
}

void MainWindow::refreshContrastViews() {
    for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
        if (v) v->refreshContrastInjection();
}

// ─────────────────────────────────────────────────────────────────────────────
// View-mode switching
// ─────────────────────────────────────────────────────────────────────────────
// Tick the menu-bar action for a display mode without re-triggering it — the
// pane is switched by the caller, so firing triggered() here would switch twice.
void MainWindow::setNavTileActive(QPushButton *tile, bool active) {
    if (!tile) return;
    tile->setProperty("active", active);
    // Forces the style to re-evaluate this widget's QSS immediately - a
    // property change alone does not automatically trigger a repaint with
    // the new rule applied.
    tile->style()->unpolish(tile);
    tile->style()->polish(tile);
    tile->update();
}

void MainWindow::checkViewAction(int id) {
    // Index 2 (Mesh) has no tile in the reference-design bottom bar - see
    // m_viewTiles' own declaration comment. A harmless no-op, not an error.
    if (id < 0 || id > 3 || !m_viewTiles[id]) return;
    m_viewTiles[id]->setChecked(true);   // keeps QButtonGroup's own exclusive-state bookkeeping correct
    for (int i = 0; i < 4; ++i)
        if (m_viewTiles[i]) setNavTileActive(m_viewTiles[i], i == id);
    // Only one tile active at a time - selecting a display mode always
    // means leaving Cine, so its own active state clears here too.
    if (m_cineTile) setNavTileActive(m_cineTile, false);
}

void MainWindow::onViewModeChanged(int id) {
    // Return to the simulator page first - clicking a nav tile from Cine
    // must actually bring the simulator back on screen, not just change a
    // pane that's sitting hidden behind the still-showing Cine page.
    if (m_pageStack) m_pageStack->setCurrentIndex(0);
    // Updates the active-property border styling to match, for id 0/1/3 -
    // a real, separate pre-existing gap: nothing previously called this on
    // a direct tile click, only Qt's own native exclusive-checked state
    // updated automatically (which governs which tile IS checked, but not
    // which one visibly shows the green border, since that moved to a
    // dynamic property specifically to sidestep a Fusion-style rendering
    // quirk with :checked - see that property's own QSS comment).
    checkViewAction(id);
    switch (id) {
    case 0: m_quad->showPane(QuadPanel::Pane::View3D); break;
    case 1:
        m_quad->showPane(QuadPanel::Pane::Fluoro);
        // Explicit, direct sync - guarantees Fluoro matches 3D View's
        // exact camera state (angle AND zoom, via syncCameraFrom()'s own
        // FOV-scaled distance) at the moment of switching, rather than
        // relying solely on the cameraChanged()-signal-based mechanism
        // that keeps them linked during normal use. Defensive: covers any
        // gap in that signal chain (e.g. Fluoro's camera changing while
        // its pane was hidden, with no repaint to catch it up) without
        // needing to fully trace which exact gap caused it.
        //
        // Runs unconditionally - views must stay synchronized at all
        // times, regardless of sheath/catheter state. This used to be
        // conditional on !m_sheathReady, back when restartFromCurrentSite()
        // deliberately focused Fluoro on the access site after sheath
        // placement and this sync would have undone that focus on every
        // pane switch; that focus behaviour was removed in a later fix
        // (per the explicit requirement that camera position must never
        // change during sheath/catheter selection), so the guard against
        // undoing it no longer has anything to guard against - it was only
        // ever silently blocking legitimate synchronization instead.
        if (m_quad->viewFluoro() && m_view)
            m_quad->viewFluoro()->syncCameraFrom(*m_view);
        break;
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

    // Better: for a genuine terminal (degree 1), the vessel leaves it in
    // exactly one direction and the hub points along the reverse. Correct
    // for any site and any patient orientation, with no axis table at all.
    const int32_t entryN = m_sheath.entryNode();
    if (entryN >= 0 && entryN < static_cast<int32_t>(m_graph.nodeCount())) {
        const auto nb = m_graph.neighbours(entryN);
        if (nb.size() == 1) {
            const cn::Vec3 inward =
                m_graph.nodes()[nb.front()].position - m_graph.nodes()[entryN].position;
            if (inward.lengthSquared() > 1e-12f) outDir = (-inward).normalized();
        } else if (nb.size() == 2) {
            // Mid-vessel entry (degree 2) - e.g. the corrected groin-level
            // femoral entry, which sits on the common femoral artery as it
            // continues both proximally into the iliac and distally toward
            // the knee, not at a dead end. A puncture here goes THROUGH the
            // vessel wall to the skin, not backward along the vessel's own
            // axis - there is no "backward" from a point the vessel
            // continues through in both directions. Approximated as the
            // direction perpendicular to the local vessel tangent that
            // points most toward the front of the body (+Y, anterior) -
            // real groin/neck punctures are approached anterolaterally,
            // never from directly behind.
            const cn::Vec3 tangent = m_graph.nodes()[nb[1]].position - m_graph.nodes()[nb[0]].position;
            if (tangent.lengthSquared() > 1e-12f) {
                const cn::Vec3 t = tangent.normalized();
                cn::Vec3 anterior(0.f, 1.f, 0.f);
                cn::Vec3 perp = anterior - t * anterior.dot(t);
                if (perp.lengthSquared() < 1e-8f) {
                    // Vessel runs nearly parallel to +Y (rare) - fall back
                    // to a lateral direction instead, still perpendicular.
                    const cn::Vec3 lateral(1.f, 0.f, 0.f);
                    perp = lateral - t * lateral.dot(t);
                }
                if (perp.lengthSquared() > 1e-12f) outDir = perp.normalized();
            }
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
    // Camera POSITION/ORIENTATION left exactly where it is; follow mode
    // still enables (true) so the catheter is trackable - same reasoning as
    // showInsertionDialog()'s own fix, see restartFromCurrentSite()'s own
    // comment for why this can no longer jump the camera anywhere.
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

    // Use the seeded overload for any non-terminal (mid-pathway) entry -
    // the plain startFrom(startNode) has a real bug for this case: its
    // very first step has no previous node to exclude any neighbour, so
    // at an entry with more than one neighbour it immediately sees
    // multiple candidates and stops as a pending junction without ever
    // extending the route at all (see NavigationController::startFrom()'s
    // own comment on the overload for the full explanation).
    //
    // "Highest Z neighbour = forward" is verified correct for femoral's
    // corrected groin-level entry (degree 2 on both sides) and for
    // brachial's lowered entry (degree 2 on the right, but degree 3 on
    // the left - the two sides are not always topologically symmetric,
    // since the graph cleanup step can affect each side's local structure
    // independently). This heuristic was also checked against a different
    // degree-3 case (carotid, since reverted) and picked the wrong
    // neighbour there, so it is applied here only because THIS specific
    // node's correct forward direction was separately verified, not
    // because degree>=2 alone is trusted to always work - a future site
    // needs its own check before relying on this.
    const auto entryNeighbours = m_graph.neighbours(startNode);
    if (entryNeighbours.size() >= 2) {
        int32_t forward = entryNeighbours[0];
        for (int32_t nb : entryNeighbours)
            if (m_graph.nodes()[nb].position.z > m_graph.nodes()[forward].position.z) forward = nb;
        m_nav.startFrom(startNode, forward);
    } else {
        m_nav.startFrom(startNode);
    }

    // Build and display sheath geometry at the entry point
    updateSheathActor();

    // Camera-follow mode only - NOT focusOn(). focusOn() bundles two things
    // together: it MOVES the camera to a given point (target/distance) AND
    // enables follow mode as a side effect. Calling it here was what used
    // to make the camera jump to the access site on every sheath placement
    // - removing that jump (per explicit earlier request) by no longer
    // calling focusOn() also accidentally removed the only place that ever
    // turned follow mode on at all, since nothing else did.
    // setCameraFollows(true) alone only sets the flag and starts the
    // easing timer (see its own implementation) - it never touches
    // target/azimuth/distance, so the camera stays exactly where the
    // operator left it.
    //
    // Camera position, zoom, and orientation are left completely
    // unchanged here - explicit, repeated requirement: "without changing
    // the current camera position," "keep the camera position unchanged,"
    // "do not reset the vessel, camera, or anatomical view during
    // access/sheath/catheter selection." setCameraFollows(true) does not
    // move the camera at all by itself - it only arms follow-mode for
    // once the catheter actually starts advancing, which is what "camera
    // follows the catheter" means; the camera's own current position,
    // target, and zoom stay exactly where the operator left them until
    // then. Applied to every view, not just m_view, so all of them start
    // following identically.
    if (focusCamera)
        for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
            if (v) v->setCameraFollows(true);

    m_pendingMm = 0.0;
    if (m_motion->isActive()) m_motion->stop();

    // Placing the sheath establishes the access and the route, nothing more.
    // The catheter is a separate device that has to be chosen and introduced,
    // so it is not attached to the views and navigation stays locked until
    // Catheter Selection runs. Previously this set m_ready and handed the
    // views a device the operator had not picked yet, so a catheter appeared
    // in the vessel the instant the sheath went in.
    //
    // resetDeviceState(false) clears any catheter left over from a
    // previous sheath first - a stale m_catheterReady=true from before
    // would otherwise silently reattach an old catheter to this brand new
    // sheath without the operator ever running Catheter Selection on it.
    // false = don't also touch the sheath itself here, since it is about
    // to be marked ready on the very next line.
    resetDeviceState(/*alsoResetSheath=*/false);
    m_sheathReady = true;
    if (m_btnCatheterSelect) m_btnCatheterSelect->setEnabled(true);
    updateDeviceGating();

    // resetDeviceState() just above already detached any device from every
    // view and guarantees m_catheterReady is false here - the catheter is
    // a separate device the operator has not chosen yet for this sheath.
    pushOverlay();
    updateReadouts();
    // status: navigation active
    m_view->setFocus();
}

void MainWindow::onFocusAccess() {
    if (m_activeSite < 0 || m_activeSite >= static_cast<int>(m_sites.size())) return;
    const cn::Vec3 &p = m_graph.nodes()[m_sites[m_activeSite].node].position;
    const float r = m_graph.nodes()[m_sites[m_activeSite].node].radius;
    // focusOn() does not emit cameraChanged() - it also flips on continuous
    // camera-follow mode (m_cameraFollows), which is per-instance ongoing
    // behaviour, not a one-shot position the syncCameraFrom() mechanism
    // could copy across even if it did emit. Call it on every view directly,
    // the same way restartFromCurrentSite() already correctly does - this
    // was the one call site that instead called it on m_view alone, which is
    // exactly why Focus Access moved the 3D camera but left Fluoro (and DSA/
    // Mesh, though those alias m_view so were never actually affected)
    // looking wherever it already was.
    for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
        if (v) v->focusOn(QVector3D(p.x, p.y, p.z), r);
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

// Rotation is a simple, immediate operation — unlike advance/retract, it does
// not interact with junction-stopping or the smoothed m_motion animation, so
// it is applied directly rather than queued. Same update()+refreshDevice()
// pattern as syncDevices(), which is what keeps every view consistent.
void MainWindow::onRollLeft() {
    if (!m_ready) return;
    constexpr float kRollStepRad = 15.f * 3.14159265f / 180.f;
    m_catheter.setRollAngle(m_catheter.state().rollAngle - kRollStepRad);
    m_catheter.update();
    for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
        v->refreshDevice();
}

void MainWindow::onRollRight() {
    if (!m_ready) return;
    constexpr float kRollStepRad = 15.f * 3.14159265f / 180.f;
    m_catheter.setRollAngle(m_catheter.state().rollAngle + kRollStepRad);
    m_catheter.update();
    for (VesselView *v : {m_view, m_quad->viewFluoro(), m_quad->viewDSA(), m_quad->viewMesh()})
        v->refreshDevice();
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

std::array<double, 28> MainWindow::xrayTuningToArray(const VesselView::XrayTuning &t) {
    return {
        t.backgroundColor.x(), t.backgroundColor.y(), t.backgroundColor.z(),
        t.boneColor.x(), t.boneColor.y(), t.boneColor.z(),
        t.boneOpacity, t.boneAmbient, t.boneDiffuse, t.boneSpecular,
        t.muscleColor.x(), t.muscleColor.y(), t.muscleColor.z(),
        t.muscleOpacity, t.muscleAmbient, t.muscleDiffuse, t.muscleSpecular,
        t.gamma, t.contrast, t.brightness,
        t.boneIntensityMult, t.softTissueMult,
        t.boneBrightness, t.muscleBrightness,
        t.vesselColor.x(), t.vesselColor.y(), t.vesselColor.z(), t.vesselOpacity
    };
}

VesselView::XrayTuning MainWindow::xrayArrayToTuning(const std::array<double, 28> &v) {
    VesselView::XrayTuning t;
    t.backgroundColor = QVector3D(float(v[0]), float(v[1]), float(v[2]));
    t.boneColor       = QVector3D(float(v[3]), float(v[4]), float(v[5]));
    t.boneOpacity  = float(v[6]);
    t.boneAmbient  = float(v[7]);
    t.boneDiffuse  = float(v[8]);
    t.boneSpecular = float(v[9]);
    t.muscleColor  = QVector3D(float(v[10]), float(v[11]), float(v[12]));
    t.muscleOpacity  = float(v[13]);
    t.muscleAmbient  = float(v[14]);
    t.muscleDiffuse  = float(v[15]);
    t.muscleSpecular = float(v[16]);
    t.gamma      = float(v[17]);
    t.contrast   = float(v[18]);
    t.brightness = float(v[19]);
    t.boneIntensityMult = float(v[20]);
    t.softTissueMult    = float(v[21]);
    t.boneBrightness    = float(v[22]);
    t.muscleBrightness  = float(v[23]);
    t.vesselColor   = QVector3D(float(v[24]), float(v[25]), float(v[26]));
    t.vesselOpacity = float(v[27]);
    return t;
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

void MainWindow::onChooseStraight() {
    if (!m_ready || !m_nav.decisionPending()) return;
    const auto &opts = m_nav.options();
    if (opts.empty()) return;
    // The option whose takeoff angle deviates least from the current
    // travel direction - not necessarily the leftmost or rightmost one on
    // screen (those are what chooseLeft()/chooseRight() already reach via
    // index 0 and size()-1). This is the only way to reach a junction's
    // middle option when it presents more than two candidates.
    int straightIdx = 0;
    float bestAngle = opts[0].takeoffAngleDeg;
    for (int i = 1; i < static_cast<int>(opts.size()); ++i) {
        if (opts[i].takeoffAngleDeg < bestAngle) {
            bestAngle = opts[i].takeoffAngleDeg;
            straightIdx = i;
        }
    }
    m_pendingMm = 0.0;
    if (m_nav.chooseOption(straightIdx)) {
        log(tr("[%1] node %2: continued STRAIGHT (option %3, %4 deg off axis)")
                .arg(QTime::currentTime().toString("HH:mm:ss"))
                .arg(m_nav.pendingJunctionNode())
                .arg(straightIdx)
                .arg(bestAngle, 0, 'f', 1));
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
    if (m_cineStudio) m_cineStudio->setCarmAngles(static_cast<float>(lao), static_cast<float>(cran));
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
    // Previously 3D-only: Fluoro never followed the tip at all, so turning
    // this on made the two views' cameras diverge every frame instead of
    // staying in lockstep. Both views read the same shared CatheterModel
    // (m_device), so each computing its own follow position from that one
    // source converges to the same effective camera - this is not a
    // duplicate/redundant call, it is what makes follow mode apply to every
    // view rather than just the one it happened to be wired to before.
    if (VesselView *fv = m_quad ? m_quad->viewFluoro() : nullptr) fv->setCameraFollows(on);
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

    // Access site — the one Procedure Info field with no prior read-out
    // anywhere in the app; accessSiteName() already exists for the log
    // messages onAccessSiteChosen() writes, reused here rather than
    // duplicating the site-name table.
    if (m_fAccess) {
        m_fAccess->setText(
            (m_activeSite >= 0 && m_activeSite < static_cast<int>(m_sites.size()))
                ? QString::fromLatin1(cn::accessSiteName(m_sites[m_activeSite].site))
                : tr("not established"));
    }

    // Device Status — a coloured bullet (rich text) + status word per stage.
    // Green once genuinely ready/active, grey beforehand; nothing here is a
    // separate tracked "status" of its own; it is read straight off the same
    // booleans that already gate navigation and the Embolisation Module.
    auto statusHtml = [](bool active, const QString &onText, const QString &offText) {
        return active ? QStringLiteral("<span style='color:#2ECC71;'>&#9679;</span> %1").arg(onText)
                      : QStringLiteral("<span style='color:#6B6B6F;'>&#9679;</span> %1").arg(offText);
    };
    if (m_deviceStatusSheath)
        m_deviceStatusSheath->setText(statusHtml(m_sheathReady, tr("PLACED"), tr("NOT PLACED")));
    if (m_deviceStatusCatheter)
        m_deviceStatusCatheter->setText(statusHtml(m_catheterReady, tr("POSITIONED"), tr("NOT LOADED")));
    if (m_deviceStatusEmbolic)
        m_deviceStatusEmbolic->setText(statusHtml(m_emboli.active(), tr("ACTIVE"), tr("NOT STARTED")));
}

// ─────────────────────────────────────────────────────────────────────────────
// Cine Studio
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onOpenCineStudio() {
    // No longer captures a frame here - Cine Studio is now purely the
    // REVIEW page for whatever startCineRecordingIfNeeded() has actually
    // been recording in the background since the procedure's first
    // Pre-Embolization Angiogram. Opening it mid-recording is fine: the
    // capture timer keeps running regardless of which page is on screen.
    m_pageStack->setCurrentIndex(1);
    setWindowTitle(tr("CathTrainVR — Cine Studio"));

    // Cine reads as the active nav tile while its page is showing, and the
    // exclusive 3D View/Fluoro/DSA group reads as none-selected - matching
    // the reference image's full-border highlight on whichever tile is
    // actually current. The exclusive QButtonGroup's own logical checked
    // state is untouched (it still remembers which of 3D/Fluoro/DSA was
    // last selected for when the operator returns), only the separate
    // "active" property driving the visual border changes here.
    for (int i = 0; i < 4; ++i)
        if (m_viewTiles[i]) setNavTileActive(m_viewTiles[i], false);
    setNavTileActive(m_cineTile, true);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cine acquisition — real continuous recording from the Fluoro view
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::startCineRecordingIfNeeded() {
    if (m_cineRecording || !m_cineStudio || !m_quad) return;
    // The same real value the Fluoroscopy HUD's own FRAME row displays
    // (CarmView::frameRate(), via VesselView::setDetectorInfo()) - not a
    // second, independently-hardcoded constant that happened to agree with
    // it. If the console's frame rate is ever changed, Cine acquisition
    // follows it automatically instead of silently capturing at a stale
    // rate no longer shown anywhere.
    const int fps = m_carmWidget ? m_carmWidget->frameRate() : 15;
    // Real C-arm angulation/detector geometry, not left at the API's own
    // defaults - the same values CarmController/CarmView already track
    // live, so a run's "Acquisition Info" panel genuinely reflects the
    // angle it was actually recorded at. DAP/Air Kerma reuse the same
    // fixed console values already shown in the status bar (this build has
    // no real per-run dose accumulation model) rather than inventing a
    // third independently-hardcoded number.
    m_cineStudio->beginRecording(
        true, tr("Run %1").arg(++m_cineRunCounter), fps,
        m_carmCtrl ? static_cast<float>(m_carmCtrl->laoAngle())  : 0.f,
        m_carmCtrl ? static_cast<float>(m_carmCtrl->cranAngle()) : 0.f,
        m_carmWidget ? m_carmWidget->sid()           : 100.f,
        m_carmWidget ? m_carmWidget->magnification() : 32.f,
        25.f,     // FOV - fixed console default, matching the Fluoroscopy HUD's own
        12.4f, 8.7f);   // DAP/Air Kerma - matching the status bar's own fixed values
    m_cineRecording = true;

    if (!m_cineCaptureTimer) {
        m_cineCaptureTimer = new QTimer(this);
        connect(m_cineCaptureTimer, &QTimer::timeout, this, [this]{ onCineCaptureTick(); });
    }
    // recordFrameFromWidget() throttles its own appends to the captureFps
    // passed to beginRecording() above, so ticking faster than that here
    // would simply mean most ticks are no-ops rather than over-capturing -
    // ticking at exactly that rate keeps this timer's own overhead in line
    // with what actually gets recorded.
    m_cineCaptureTimer->start(1000 / fps);

    log(tr("[%1] Cine recording started").arg(QTime::currentTime().toString("HH:mm:ss")));
}

void MainWindow::stopCineRecording() {
    if (!m_cineRecording) return;
    if (m_cineCaptureTimer) m_cineCaptureTimer->stop();
    if (m_cineStudio) m_cineStudio->endRecording();
    m_cineRecording = false;
    log(tr("[%1] Cine recording stopped").arg(QTime::currentTime().toString("HH:mm:ss")));
}

void MainWindow::onCineCaptureTick() {
    if (!m_cineRecording || !m_cineStudio || !m_quad) return;
    // The Fluoro view specifically, not the 3D view - a real Cine
    // acquisition IS the fluoroscopy sequence, not a 3D render of it. Same
    // capture path (QOpenGLWidget::grabFramebuffer(), via
    // CineViewStudio::captureWidgetFrame()) onScreenshot() already uses, so
    // whatever is actually on screen - catheter, contrast, embolic cast, all
    // through the same DRR pipeline - is exactly what gets recorded.
    m_cineStudio->recordFrameFromWidget(m_quad->viewFluoro());
}
void MainWindow::onCineStudioBack() {
    m_pageStack->setCurrentIndex(0);
    setWindowTitle(tr("CathTrainVR — Endovascular Navigation Console"));
    m_view->setFocus();
}

void MainWindow::log(const QString &l) { if (m_log) m_log->appendPlainText(l); }
QString MainWindow::mm(float v, int d) const {
    return QString::number(v * m_mmPerUnit, 'f', d) + " mm";
}