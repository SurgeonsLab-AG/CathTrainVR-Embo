#include "CineViewStudio.h"
#include "CarmView.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QScrollArea>
#include <QOpenGLWidget>
#include <QFrame>
#include <QScrollBar>
#include <QStyle>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────
// Local styling helpers — mirrors MainWindow's own makeCard()/card QSS
// pattern for visual consistency ("maintain the existing dark clinical
// interface style"), reimplemented locally since that helper lives in an
// anonymous namespace private to MainWindow.cpp and isn't shared across
// files.
// ─────────────────────────────────────────────────────────────────────────
namespace {

const char *kCineStyle = R"(
QWidget#cineRoot { background:#000000; }
QFrame#cineCard {
    background:#0B0B0E; border:1px solid #2A2A2E; border-radius:6px;
}
QLabel#cineCardHeader {
    color:#6B6B6F; font-size:11px; font-weight:bold; letter-spacing:1px;
    padding-bottom:4px; border-bottom:1px solid #222226;
}
QPushButton#runCard {
    background:#0B0B0E; color:#C9C9C9; border:1px solid #2A2A2E; border-radius:6px;
    text-align:left; padding:10px 12px; font-size:12px;
}
QPushButton#runCard:checked {
    background:#0F1A12; border-color:#2ECC71; color:#2ECC71;
}
QPushButton#runCard:hover { border-color:#3A3A40; }
QPushButton#deckBtn {
    background:#0B0B0E; color:#C9C9C9; border:1px solid #2A2A2E; border-radius:5px;
    padding:8px 14px; font-size:12px; font-weight:bold;
}
QPushButton#deckBtn:hover  { background:#151519; border-color:#3A3A40; }
QPushButton#deckBtn:checked { background:#0F1A12; border-color:#2ECC71; color:#2ECC71; }
QPushButton#speedBtn {
    background:#0B0B0E; color:#C9C9C9; border:1px solid #2A2A2E; border-radius:5px;
    padding:8px 10px; font-size:12px; font-weight:bold; min-width:36px;
}
QPushButton#speedBtn:checked { background:#0F1A12; border-color:#2ECC71; color:#2ECC71; }
QSlider::groove:horizontal { background:#1A1A1E; height:4px; border-radius:2px; }
QSlider::handle:horizontal {
    background:#2ECC71; width:14px; height:14px; margin:-5px 0; border-radius:7px;
}
QSlider::sub-page:horizontal { background:#2ECC71; border-radius:2px; }
QLabel#thumbFrame {
    border:2px solid transparent; border-radius:3px;
}
QLabel#thumbFrame[active="true"] { border-color:#2ECC71; }
)";

QFrame *makeCineCard(const QString &title, QWidget *parent, QVBoxLayout *outerLayout,
                     const QString &headerIcon = QString()) {
    auto *card = new QFrame(parent);
    card->setObjectName("cineCard");
    auto *vl = new QVBoxLayout(card);
    vl->setContentsMargins(12, 10, 12, 10);
    vl->setSpacing(8);
    auto *headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    auto *header = new QLabel(title.toUpper(), card);
    header->setObjectName("cineCardHeader");
    headerRow->addWidget(header, 1);
    if (!headerIcon.isEmpty()) {
        auto *icon = new QLabel(headerIcon, card);
        icon->setStyleSheet("color:#6B6B6F; font-size:14px;");
        headerRow->addWidget(icon);
    }
    vl->addLayout(headerRow);
    if (outerLayout) outerLayout->addWidget(card);
    return card;
}

QLabel *addInfoRow(QVBoxLayout *cardLayout, const QString &label, QWidget *parent) {
    auto *row = new QHBoxLayout();
    auto *l = new QLabel(label, parent);
    l->setStyleSheet("color:#9B9B9B; font-size:11px;");
    auto *value = new QLabel(QStringLiteral("—"), parent);
    value->setStyleSheet("color:#E8E8E8; font-size:12px; font-weight:bold;");
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(l);
    row->addStretch(1);
    row->addWidget(value);
    cardLayout->addLayout(row);
    return value;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────
CineViewStudio::CineViewStudio(QWidget *parent) : QWidget(parent) {
    setObjectName("cineRoot");
    setStyleSheet(kCineStyle);
    buildLayout();

    m_playbackTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_playbackTimer, &QTimer::timeout, this, &CineViewStudio::onPlaybackTick);
}

CineViewStudio::~CineViewStudio() = default;

// ─────────────────────────────────────────────────────────────────────────
// Pixel processing / capture
// ─────────────────────────────────────────────────────────────────────────
QImage CineViewStudio::captureWidgetFrame(QOpenGLWidget *widget) {
    if (!widget) return QImage();
    // grabFramebuffer() reads the back buffer of the OpenGL context that
    // owns the widget and returns it as a QImage - Qt-native capture, no
    // third-party dependency, no front/back-buffer confusion, no row-flip
    // required because Qt already returns top-left-origin convention.
    QImage frame = widget->grabFramebuffer();
    if (frame.isNull()) return frame;
    return frame.convertToFormat(QImage::Format_RGB888);
}

QImage CineViewStudio::invertToMonochrome(const QImage &source) {
    // Retained as a public utility (an export step or a future feature
    // might still want it) even though the normal capture/display path no
    // longer calls it — see the header's own comment on why: frames are
    // captured directly from the Fluoro view, which already renders with
    // the correct dark-contrast-on-light-tissue convention, so inverting
    // again here would produce a backwards image, not a style choice.
    if (source.isNull()) return source;
    QImage gray = source.convertToFormat(QImage::Format_Grayscale8);
    QImage result(gray.size(), QImage::Format_Grayscale8);
    const int w = gray.width(), h = gray.height();
    for (int y = 0; y < h; ++y) {
        const uchar *srcLine = gray.constScanLine(y);
        uchar *dstLine = result.scanLine(y);
        for (int x = 0; x < w; ++x)
            dstLine[x] = static_cast<uchar>(255 - srcLine[x]);
    }
    return result.convertToFormat(QImage::Format_RGB888);
}

QImage CineViewStudio::renderFrameForDisplay(const QImage &raw) const {
    // See invertToMonochrome()'s own comment - no conversion applied, the
    // captured frame IS the display frame.
    return raw;
}

void CineViewStudio::beginRecording(bool intoReferenceRun, const QString &runName, int captureFps,
                                    float lao, float cran, float sidCm, float magCm, float fovCm,
                                    float dapGyCm2, float airKermaMGy) {
    CineRun run;
    run.name = runName;
    run.procedureLabel = intoReferenceRun ? QStringLiteral("Diagnostic") : QStringLiteral("Procedure");
    run.fps = std::max(1, captureFps);
    run.startedAt = QDateTime::currentDateTime();
    run.lao = lao; run.cran = cran; run.sidCm = sidCm; run.magCm = magCm; run.fovCm = fovCm;
    run.dapGyCm2 = dapGyCm2; run.airKermaMGy = airKermaMGy;

    m_runs.push_back(run);
    m_recordingRunIndex = m_runs.size() - 1;

    m_captureThrottle.restart();

    rebuildAcquisitionsList();
    // A run being recorded is also the one under review, live, the same
    // way a real console shows the acquisition currently in progress.
    setActiveRun(m_recordingRunIndex);
}

void CineViewStudio::recordFrameFromWidget(QOpenGLWidget *widget) {
    if (m_recordingRunIndex < 0 || m_recordingRunIndex >= m_runs.size()) return;
    CineRun &run = m_runs[m_recordingRunIndex];
    // Throttle appends to the run's own fps, regardless of how often the
    // caller's own timer actually ticks - see the header's own comment on
    // beginRecording() for why the caller is expected to tick faster than
    // (or exactly at) this rate rather than the other way around.
    const int intervalMs = 1000 / std::max(1, run.fps);
    if (m_captureThrottle.isValid() && m_captureThrottle.elapsed() < intervalMs) return;
    m_captureThrottle.restart();

    const QImage frame = captureWidgetFrame(widget);
    if (frame.isNull()) return;
    run.frames.push_back(frame);

    // Live-updating the review UI only while this run is the one actually
    // being reviewed, and only every few frames worth of UI work (the
    // thumbnail strip rebuild is the expensive part) - matching how a real
    // console's live acquisition preview updates without re-laying-out
    // dozens of thumbnail widgets on every single frame.
    if (m_activeRunIndex == m_recordingRunIndex) {
        m_currentFrameIndex = run.frames.size() - 1;
        updateFrameDisplay();
        if (m_frameSlider) {
            m_frameSlider->blockSignals(true);
            m_frameSlider->setMaximum(std::max(0, static_cast<int>(run.frames.size()) - 1));
            m_frameSlider->setValue(m_currentFrameIndex);
            m_frameSlider->blockSignals(false);
        }
        if (m_timelineScrubber) {
            m_timelineScrubber->blockSignals(true);
            m_timelineScrubber->setMaximum(std::max(0, static_cast<int>(run.frames.size()) - 1));
            m_timelineScrubber->setValue(m_currentFrameIndex);
            m_timelineScrubber->blockSignals(false);
        }
        rebuildThumbnailStrip();
        updateReviewPanel();
    }
}

void CineViewStudio::endRecording() {
    m_recordingRunIndex = -1;
    rebuildAcquisitionsList();
}

void CineViewStudio::registerRun(const CineRun &run) {
    m_runs.push_back(run);
    rebuildAcquisitionsList();
    setActiveRun(m_runs.size() - 1);
}

// ─────────────────────────────────────────────────────────────────────────
// Layout construction
// ─────────────────────────────────────────────────────────────────────────
void CineViewStudio::buildLayout() {
    // Everything below is built inside contentWidget, wrapped in a scroll
    // area, rather than directly on `this` - see this function's own
    // opening comment for why. Any widget constructed with `this` as its
    // parent argument throughout the rest of this function still ends up
    // correctly parented to contentWidget once it's added to a layout that
    // is part of contentWidget's own hierarchy (root, or anything nested
    // under it) - Qt reparents a widget to whatever owns the layout it was
    // added to, regardless of what parent its constructor was given, so
    // none of the existing widget construction calls below needed to
    // change for this.
    auto *outerVL = new QVBoxLayout(this);
    outerVL->setContentsMargins(0, 0, 0, 0);
    outerVL->setSpacing(0);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    outerVL->addWidget(scrollArea);

    auto *contentWidget = new QWidget(scrollArea);
    scrollArea->setWidget(contentWidget);

    auto *root = new QVBoxLayout(contentWidget);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(6);

    // ── Three-column body: [C-Arm Quick View + Acquisitions, stacked] |
    //    Fluoroscopy | Cine Review ─────────────────────────────────────────
    auto *bodyRow = new QHBoxLayout();
    bodyRow->setSpacing(0);
    root->addLayout(bodyRow, 1);

    // ── FIRST: C-Arm Quick View + Acquisitions, one shared column ───────
    // Stacked vertically, C-Arm Quick View on top: this is the reverse
    // order of the original stacked design (which had Acquisitions on top
    // and C-Arm Quick View below it), swapped per explicit request. It
    // also happens to avoid the original stacking bug on its own: the
    // ORIGINAL bug was Acquisitions' own stretch factor filling all
    // leftover height BEFORE C-Arm Quick View got a chance to claim its
    // own compact size, pushing it down past a large empty gap. With C-Arm
    // Quick View first (fixed, no stretch) and Acquisitions second
    // (stretch=1), the expanding card is now the one filling whatever
    // space is left AFTER the fixed one, which is the natural, correct
    // order regardless.
    auto *leftCol = new QWidget(this);
    leftCol->setFixedWidth(480);
    leftCol->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto *leftVL = new QVBoxLayout(leftCol);
    leftVL->setContentsMargins(8, 0, 0, 0);
    leftVL->setSpacing(0);

    // Built directly here (not via makeCineCard()) specifically to mirror
    // makeCard()'s exact structure from the main 3D View's own "C-Arm
    // Status" card: an outer bordered frame + header, then a SEPARATE
    // inner content widget with zero margins, so CarmView fills it fully
    // edge-to-edge. makeCineCard() puts header and content in one shared,
    // padded layout instead - structurally different from the pattern
    // already proven to work correctly for this exact same CarmView class
    // elsewhere in this app, which is what this construction replicates
    // directly rather than reusing the differently-structured helper.
    auto *carmCard = new QFrame(leftCol);
    carmCard->setObjectName("cineCard");
    auto *carmOuter = new QVBoxLayout(carmCard);
    carmOuter->setContentsMargins(0, 0, 0, 0);
    carmOuter->setSpacing(0);

    auto *carmHeader = new QLabel(tr("C-ARM QUICK VIEW"), carmCard);
    carmHeader->setObjectName("cineCardHeader");
    carmOuter->addWidget(carmHeader);

    auto *carmContent = new QWidget(carmCard);
    auto *carmContentLay = new QVBoxLayout(carmContent);
    carmContentLay->setContentsMargins(0, 0, 0, 0);
    carmContentLay->setSpacing(0);
    carmOuter->addWidget(carmContent, 1);

    // Genuinely the same rendered C-arm rig the main simulator page's own
    // "C-Arm Status" card shows - a separate CarmView instance, its own
    // OpenGL context, kept in sync via setCarmAngles() rather than sharing
    // one widget between pages (matching how every VesselView instance in
    // this app is already independent and synced the same way).
    m_cineCarmView = new CarmView(carmContent);
    // No setZoomFactor() call - the column this sits in is now 480px wide,
    // exactly matching the main 3D View's own leftCol width (previously
    // 195px, which is why a 2.0x zoom-out was needed to fit the model in
    // frame at all). The main view's own m_carmWidget never calls
    // setZoomFactor() either, staying at the class default of 1.0 - not
    // calling it here at all guarantees this instance frames the model
    // identically to how it actually looks in 3D View, rather than
    // guessing at a new compensating value.
    m_cineCarmView->setHudFontScale(0.65f);   // 20pt/8pt -> ~13pt/5pt - compact but still legible, not shrunk past readability
    m_cineCarmView->setMinimumSize(10, 10);
    carmContentLay->addWidget(m_cineCarmView, 1);

    m_carmPositionLabel = new QLabel(tr("LAO 0°   CRAN 0°"), carmContent);
    m_carmPositionLabel->setStyleSheet("color:#E8E8E8; font-size:10px; font-weight:bold; padding:6px;");
    carmContentLay->addWidget(m_carmPositionLabel);
    leftVL->addWidget(carmCard, 1);   // stretch=1 - equal split with Acquisitions below, matching the main simulator's C-Arm/ECG 50/50 split

    auto *acqCard = makeCineCard(tr("Acquisitions"), leftCol, nullptr, QStringLiteral("\u2630"));
    auto *acqCardVL = qobject_cast<QVBoxLayout *>(acqCard->layout());
    leftVL->addWidget(acqCard, 1);   // stretch=1 - fills the remaining column height below C-Arm Quick View

    m_runListScroll = new QScrollArea(acqCard);
    m_runListScroll->setWidgetResizable(true);
    m_runListScroll->setFrameShape(QFrame::NoFrame);
    m_runListScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_runListContainer = new QWidget(m_runListScroll);
    m_runListLayout = new QVBoxLayout(m_runListContainer);
    m_runListLayout->setContentsMargins(0, 0, 0, 0);
    m_runListLayout->setSpacing(6);
    m_runListLayout->addStretch(1);
    m_runListScroll->setWidget(m_runListContainer);
    acqCardVL->addWidget(m_runListScroll, 1);

    auto *newAcqBtn = new QPushButton(tr("+  New Acquisition"), acqCard);
    newAcqBtn->setObjectName("deckBtn");
    connect(newAcqBtn, &QPushButton::clicked, this, &CineViewStudio::onNewAcquisitionClicked);
    acqCardVL->addWidget(newAcqBtn);

    bodyRow->addWidget(leftCol);

    // ── SECOND: Fluoroscopy frame display ───────────────────────────────
    auto *centerCol = new QVBoxLayout();
    centerCol->setSpacing(6);
    auto *fluoroTitle = new QLabel(tr("● FLUOROSCOPY"), this);
    fluoroTitle->setStyleSheet("color:#2ECC71; font-size:13px; font-weight:bold;");
    centerCol->addWidget(fluoroTitle);

    m_frameDisplay = new QLabel(this);
    m_frameDisplay->setAlignment(Qt::AlignCenter);
    m_frameDisplay->setStyleSheet("background:#000000; border:1px solid #2A2A2E; border-radius:6px;");
    m_frameDisplay->setMinimumSize(480, 400);
    // No maximum width cap - per explicit request to match the main
    // simulator's Vessel View column exactly, which has no width cap
    // either (m_quad simply expands via stretch=1, uncapped). This
    // directly supersedes an earlier "slightly narrower" request from a
    // previous turn, since exact-measurement parity with 3D View is now
    // the explicit, stated goal. The displayed pixmap still always
    // preserves its own aspect ratio regardless of container size
    // (Qt::KeepAspectRatio in updateFrameDisplay()), so it never
    // stretches or distorts even at a wider container size.
    m_frameDisplay->setText(tr("No frames recorded yet"));
    m_frameDisplay->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // Catches QEvent::Resize (see eventFilter()) so the displayed pixmap
    // stays rescaled to the widget's actual current size - without this,
    // a frame rendered while this widget was still small (e.g. before the
    // layout has settled to its final size, or before the window is
    // maximized) stays permanently stuck at that small scale, since
    // QLabel does not auto-rescale an already-set pixmap on its own when
    // the widget is later resized.
    m_frameDisplay->installEventFilter(this);
    centerCol->addWidget(m_frameDisplay, 1);

    // Wide scrub bar spanning exactly the Fluoroscopy viewport's own
    // width - a second view onto the exact same position m_frameSlider (in
    // the Cine Review panel) represents, not an independent piece of state.
    // The bidirectional sync connect() calls are made later, right after
    // m_frameSlider is actually constructed (below, in the Cine Review
    // panel's own section) - connecting here would reference it before it
    // exists, since this scrubber is built first in the layout but
    // m_frameSlider isn't constructed until rightCol's section runs.
    //
    // The FRAMES thumbnail strip (header label + scrollable row of frame
    // thumbnails) that used to sit above this scrubber has been removed
    // entirely per explicit request - this scrubber is now the primary way
    // to jump to an arbitrary frame directly, alongside the small slider
    // in the Cine Review panel. m_thumbnailLayout/m_thumbnailContainer/
    // m_thumbnailScroll/m_thumbnailCountLabel are deliberately left
    // unconstructed (staying null) rather than removed as members -
    // rebuildThumbnailStrip() already guards on
    // `if (!m_thumbnailLayout) return;`, so every existing call to it
    // elsewhere in this file becomes a safe no-op automatically, without
    // needing to hunt down and remove each call site individually.
    m_timelineScrubber = new QSlider(Qt::Horizontal, this);
    m_timelineScrubber->setMinimum(0);
    m_timelineScrubber->setMaximum(0);
    centerCol->addWidget(m_timelineScrubber);

    // ── Playback control deck — same column, no width cap ──────────────
    auto *deckContainer = new QWidget(this);
    auto *deckRow = new QHBoxLayout(deckContainer);
    deckRow->setContentsMargins(0, 0, 0, 0);
    deckRow->setSpacing(8);

    deckRow->addStretch(1);

    m_btnFirst = new QPushButton(tr("|◀◀  First"), this);
    m_btnFirst->setObjectName("deckBtn");
    connect(m_btnFirst, &QPushButton::clicked, this, &CineViewStudio::onFirstClicked);
    deckRow->addWidget(m_btnFirst);

    m_btnPrev = new QPushButton(tr("◀  Prev"), this);
    m_btnPrev->setObjectName("deckBtn");
    connect(m_btnPrev, &QPushButton::clicked, this, &CineViewStudio::onStepBackwardClicked);
    deckRow->addWidget(m_btnPrev);

    m_btnPlayPause = new QPushButton(tr("▶ ‖  Play"), this);
    m_btnPlayPause->setObjectName("deckBtn");
    m_btnPlayPause->setCheckable(true);
    connect(m_btnPlayPause, &QPushButton::clicked, this, &CineViewStudio::onPlayPauseClicked);
    deckRow->addWidget(m_btnPlayPause);

    m_btnNext = new QPushButton(tr("▶  Next"), this);
    m_btnNext->setObjectName("deckBtn");
    connect(m_btnNext, &QPushButton::clicked, this, &CineViewStudio::onStepForwardClicked);
    deckRow->addWidget(m_btnNext);

    m_btnLast = new QPushButton(tr("Last  ▶▶|"), this);
    m_btnLast->setObjectName("deckBtn");
    connect(m_btnLast, &QPushButton::clicked, this, &CineViewStudio::onLastClicked);
    deckRow->addWidget(m_btnLast);

    deckRow->addSpacing(12);

    m_btnLoop = new QPushButton(tr("↻  Loop"), this);
    m_btnLoop->setObjectName("deckBtn");
    m_btnLoop->setCheckable(true);
    connect(m_btnLoop, &QPushButton::clicked, this, &CineViewStudio::onLoopToggled);
    deckRow->addWidget(m_btnLoop);

    deckRow->addSpacing(12);
    auto *speedLbl = new QLabel(tr("Speed"), this);
    speedLbl->setStyleSheet("color:#9B9B9B; font-size:12px;");
    deckRow->addWidget(speedLbl);

    const double speeds[] = {0.25, 0.5, 1.0, 2.0, 4.0};
    for (double s : speeds) {
        auto *b = new QPushButton(QString::number(s, 'g', 3) + "x", this);
        b->setObjectName("speedBtn");
        b->setCheckable(true);
        if (s == 1.0) b->setChecked(true);
        connect(b, &QPushButton::clicked, this, [this, s] { onSpeedButtonClicked(s); });
        m_speedButtons.push_back(b);
        deckRow->addWidget(b);
    }

    deckRow->addStretch(1);

    m_btnReset = new QPushButton(tr("↺  Reset"), this);
    m_btnReset->setObjectName("deckBtn");
    connect(m_btnReset, &QPushButton::clicked, this, &CineViewStudio::onResetClicked);
    deckRow->addWidget(m_btnReset);

    centerCol->addWidget(deckContainer);
    bodyRow->addLayout(centerCol, 1);

    // ── THIRD: Cine Review info panel ───────────────────────────────────
    auto *rightCol = new QWidget(this);
    rightCol->setFixedWidth(300);
    rightCol->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto *rightVL = new QVBoxLayout(rightCol);
    rightVL->setContentsMargins(8, 8, 8, 8);
    rightVL->setSpacing(8);

    m_reviewTitleLabel = new QLabel(tr("CINE REVIEW"), rightCol);
    m_reviewTitleLabel->setStyleSheet("color:#2ECC71; font-size:14px; font-weight:bold;");
    rightVL->addWidget(m_reviewTitleLabel);

    auto *frameCard = makeCineCard(tr(""), rightCol, nullptr);
    auto *frameCardVL = qobject_cast<QVBoxLayout *>(frameCard->layout());
    auto *frameRow = new QHBoxLayout();
    auto *frameLbl = new QLabel(tr("Frame"), frameCard);
    frameLbl->setStyleSheet("color:#9B9B9B; font-size:12px;");
    m_frameCounterLabel = new QLabel(tr("0 / 0"), frameCard);
    m_frameCounterLabel->setStyleSheet("color:#2ECC71; font-size:18px; font-weight:bold;");
    frameRow->addWidget(frameLbl);
    frameRow->addStretch(1);
    frameRow->addWidget(m_frameCounterLabel);
    frameCardVL->addLayout(frameRow);
    m_frameSlider = new QSlider(Qt::Horizontal, frameCard);
    m_frameSlider->setMinimum(0);
    m_frameSlider->setMaximum(0);
    connect(m_frameSlider, &QSlider::valueChanged, this, &CineViewStudio::onSliderMoved);
    frameCardVL->addWidget(m_frameSlider);
    rightVL->addWidget(frameCard);

    auto *acqInfoCard = makeCineCard(tr("Acquisition Info"), rightCol, nullptr);
    auto *acqInfoVL = qobject_cast<QVBoxLayout *>(acqInfoCard->layout());
    m_acqFrameRateValue = addInfoRow(acqInfoVL, tr("Frame Rate"), acqInfoCard);
    m_acqDurationValue  = addInfoRow(acqInfoVL, tr("Duration"), acqInfoCard);
    m_acqAcquiredValue  = addInfoRow(acqInfoVL, tr("Acquired"), acqInfoCard);
    m_acqOperatorValue  = addInfoRow(acqInfoVL, tr("Operator"), acqInfoCard);
    rightVL->addWidget(acqInfoCard);

    auto *angCard = makeCineCard(tr("C-Arm Angulation"), rightCol, nullptr);
    auto *angVL = qobject_cast<QVBoxLayout *>(angCard->layout());
    m_angLaoValue = addInfoRow(angVL, tr("LAO"), angCard);
    m_angCranValue = addInfoRow(angVL, tr("CRAN"), angCard);
    m_angSidValue = addInfoRow(angVL, tr("SID"), angCard);
    m_angMagValue = addInfoRow(angVL, tr("MAG"), angCard);
    m_angFovValue = addInfoRow(angVL, tr("FOV"), angCard);
    rightVL->addWidget(angCard);

    auto *doseCard = makeCineCard(tr("Radiation Dose"), rightCol, nullptr);
    auto *doseVL = qobject_cast<QVBoxLayout *>(doseCard->layout());
    m_doseDapValue = addInfoRow(doseVL, tr("DAP"), doseCard);
    m_doseAirKermaValue = addInfoRow(doseVL, tr("Air Kerma"), doseCard);
    m_doseRefPointValue = addInfoRow(doseVL, tr("Reference Point"), doseCard);
    rightVL->addWidget(doseCard);

    rightVL->addStretch(1);
    bodyRow->addWidget(rightCol);
}

// ─────────────────────────────────────────────────────────────────────────
// Acquisitions list
// ─────────────────────────────────────────────────────────────────────────
void CineViewStudio::rebuildAcquisitionsList() {
    // Clear existing cards (everything except the trailing stretch).
    for (QPushButton *card : m_runCards) card->deleteLater();
    m_runCards.clear();
    while (m_runListLayout->count() > 1) {
        QLayoutItem *item = m_runListLayout->takeAt(0);
        delete item;
    }

    for (int i = 0; i < m_runs.size(); ++i) {
        const CineRun &run = m_runs[i];
        const bool isRecording = (i == m_recordingRunIndex);

        const QString circle = (i == m_activeRunIndex) ? QStringLiteral("\u25CF") : QStringLiteral("\u25CB");
        QString nameLine = run.name + (isRecording ? tr("  (Active)") : QString());
        QString text = circle + "  " + nameLine + "\n" + run.procedureLabel + "\n" +
                       tr("%1 frames  •  %2 fps").arg(run.frames.size()).arg(run.fps) + "\n" +
                       tr("%1 sec  •  %2").arg(run.durationSeconds(), 0, 'f', 1)
                           .arg(run.startedAt.toString("hh:mm:ss AP"));

        auto *card = new QPushButton(text, m_runListContainer);
        card->setObjectName("runCard");
        card->setCheckable(true);
        card->setChecked(i == m_activeRunIndex);
        connect(card, &QPushButton::clicked, this, [this, i] { onRunSelected(i); });

        m_runCards.push_back(card);
        m_runListLayout->insertWidget(m_runListLayout->count() - 1, card);
    }
}

void CineViewStudio::onRunSelected(int runIndex) { setActiveRun(runIndex); }

void CineViewStudio::setActiveRun(int index) {
    if (index < 0 || index >= m_runs.size()) return;
    m_activeRunIndex = index;
    m_currentFrameIndex = std::max(0, static_cast<int>(m_runs[index].frames.size()) - 1);
    m_isPlaying = false;
    m_playbackTimer.stop();
    if (m_btnPlayPause) { m_btnPlayPause->setChecked(false); m_btnPlayPause->setText(tr("▶ ‖  Play")); }

    for (int i = 0; i < m_runCards.size(); ++i)
        m_runCards[i]->setChecked(i == m_activeRunIndex);

    if (m_frameSlider) {
        m_frameSlider->blockSignals(true);
        m_frameSlider->setMaximum(std::max(0, static_cast<int>(m_runs[index].frames.size()) - 1));
        m_frameSlider->setValue(m_currentFrameIndex);
        m_frameSlider->blockSignals(false);
    }
    if (m_timelineScrubber) {
        m_timelineScrubber->blockSignals(true);
        m_timelineScrubber->setMaximum(std::max(0, static_cast<int>(m_runs[index].frames.size()) - 1));
        m_timelineScrubber->setValue(m_currentFrameIndex);
        m_timelineScrubber->blockSignals(false);
    }

    updateFrameDisplay();
    rebuildThumbnailStrip();
    updateReviewPanel();
}

// ─────────────────────────────────────────────────────────────────────────
// Frame display / thumbnails / review panel
// ─────────────────────────────────────────────────────────────────────────
void CineViewStudio::updateFrameDisplay() {
    if (!m_frameDisplay) return;
    if (m_activeRunIndex < 0 || m_activeRunIndex >= m_runs.size()) {
        m_frameDisplay->setText(tr("No frames recorded yet"));
        return;
    }
    const CineRun &run = m_runs[m_activeRunIndex];
    if (run.frames.isEmpty() || m_currentFrameIndex < 0 || m_currentFrameIndex >= run.frames.size()) {
        m_frameDisplay->setText(tr("No frames recorded yet"));
        return;
    }
    const QImage frame = renderFrameForDisplay(run.frames[m_currentFrameIndex]);
    const QPixmap pix = QPixmap::fromImage(frame).scaled(
        m_frameDisplay->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_frameDisplay->setPixmap(pix);

    if (m_frameCounterLabel)
        m_frameCounterLabel->setText(tr("%1 / %2").arg(m_currentFrameIndex + 1).arg(run.frames.size()));
    // C-Arm Quick View (m_cineCarmView/m_carmPositionLabel) deliberately
    // NOT updated here - per explicit request, that panel is a live
    // indicator of the main simulator's actual current C-arm state
    // (driven solely by setCarmAngles(), called from MainWindow whenever
    // the real C-arm moves), not a per-run historical snapshot. The
    // run's own captured angle still shows in the separate "C-Arm
    // Angulation" info card, populated below in updateReviewPanel().
}

void CineViewStudio::setCarmAngles(float lao, float cran) {
    // Always applies, unconditionally - per explicit request, C-Arm Quick
    // View is a live indicator of the main simulator's actual current
    // C-arm state, not a per-run historical snapshot. It must update
    // immediately whenever the real C-arm moves, and stay visible/correct
    // even when no runs have been recorded at all - the run-specific
    // "C-Arm Angulation" info card on the right (driven from
    // updateFrameDisplay() below) is where a completed run's own captured
    // angle still belongs; this panel is deliberately a different thing.
    if (m_cineCarmView) m_cineCarmView->setAngles(lao, cran);
    if (m_carmPositionLabel)
        m_carmPositionLabel->setText(tr("LAO %1°   CRAN %2°").arg(lao, 0, 'f', 0).arg(cran, 0, 'f', 0));
}

void CineViewStudio::rebuildThumbnailStrip() {
    if (!m_thumbnailLayout) return;
    for (QWidget *w : m_thumbnailWrappers) w->deleteLater();
    m_thumbnailWrappers.clear();
    m_thumbnailLabels.clear();
    while (m_thumbnailLayout->count() > 1) {
        QLayoutItem *item = m_thumbnailLayout->takeAt(0);
        delete item;
    }

    if (m_activeRunIndex < 0 || m_activeRunIndex >= m_runs.size()) {
        if (m_thumbnailCountLabel) m_thumbnailCountLabel->setText(tr("FRAMES (0)"));
        return;
    }
    const CineRun &run = m_runs[m_activeRunIndex];
    if (m_thumbnailCountLabel) m_thumbnailCountLabel->setText(tr("FRAMES (%1)").arg(run.frames.size()));

    // A sliding window of ~22 frames centered on the current position, not
    // an even sample spread across the whole run - a run with hundreds of
    // frames should show the thumbnails immediately around where the
    // operator currently is, the same way a real cine review workstation's
    // filmstrip does, not a compressed overview of the entire acquisition.
    constexpr int kVisibleThumbnails = 22;
    const int total = run.frames.size();
    int startIdx = std::max(0, m_currentFrameIndex - kVisibleThumbnails / 2);
    int endIdx = std::min(total, startIdx + kVisibleThumbnails);
    startIdx = std::max(0, endIdx - kVisibleThumbnails);   // clamp back near the end of a short run

    for (int i = startIdx; i < endIdx; ++i) {
        // Small vertical group: thumbnail image, frame number underneath -
        // matching the reference's numbered filmstrip rather than a bare
        // row of images with no way to tell which frame number is which.
        auto *wrapper = new QWidget(m_thumbnailContainer);
        auto *wrapperVL = new QVBoxLayout(wrapper);
        wrapperVL->setContentsMargins(0, 0, 0, 0);
        wrapperVL->setSpacing(2);

        auto *thumb = new QLabel(wrapper);
        thumb->setObjectName("thumbFrame");
        thumb->setFixedSize(72, 64);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setScaledContents(false);
        const QPixmap pix = QPixmap::fromImage(run.frames[i]).scaled(
            68, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        thumb->setPixmap(pix);
        thumb->setProperty("active", i == m_currentFrameIndex);
        thumb->setCursor(Qt::PointingHandCursor);
        // installEventFilter lets eventFilter() route a click on this
        // specific thumbnail back to its frame index (see eventFilter()'s
        // own implementation) - QLabel has no native clicked() signal.
        thumb->installEventFilter(this);
        thumb->setProperty("frameIndex", i);
        wrapperVL->addWidget(thumb);

        auto *numberLbl = new QLabel(QString::number(i + 1), wrapper);
        numberLbl->setAlignment(Qt::AlignCenter);
        numberLbl->setStyleSheet(i == m_currentFrameIndex
                                     ? "color:#2ECC71; font-size:10px; font-weight:bold;"
                                     : "color:#6B6B6F; font-size:10px;");
        numberLbl->setProperty("isFrameNumberLabel", true);
        wrapperVL->addWidget(numberLbl);

        m_thumbnailLabels.push_back(thumb);
        m_thumbnailWrappers.push_back(wrapper);
        m_thumbnailLayout->insertWidget(m_thumbnailLayout->count() - 1, wrapper);
    }
}

void CineViewStudio::updateReviewPanel() {
    if (m_activeRunIndex < 0 || m_activeRunIndex >= m_runs.size()) return;
    const CineRun &run = m_runs[m_activeRunIndex];

    if (m_acqFrameRateValue) m_acqFrameRateValue->setText(tr("%1 fps").arg(run.fps));
    if (m_acqDurationValue)  m_acqDurationValue->setText(tr("%1 sec").arg(run.durationSeconds(), 0, 'f', 1));
    if (m_acqAcquiredValue)  m_acqAcquiredValue->setText(run.startedAt.toString("hh:mm:ss AP"));
    if (m_acqOperatorValue)  m_acqOperatorValue->setText(run.operatorName);

    if (m_angLaoValue) m_angLaoValue->setText(tr("%1°").arg(run.lao, 0, 'f', 0));
    if (m_angCranValue) m_angCranValue->setText(tr("%1°").arg(run.cran, 0, 'f', 0));
    if (m_angSidValue) m_angSidValue->setText(tr("%1 cm").arg(run.sidCm, 0, 'f', 0));
    if (m_angMagValue) m_angMagValue->setText(tr("%1 cm").arg(run.magCm, 0, 'f', 0));
    if (m_angFovValue) m_angFovValue->setText(tr("%1 cm").arg(run.fovCm, 0, 'f', 0));

    if (m_doseDapValue) m_doseDapValue->setText(tr("%1 Gy·cm²").arg(run.dapGyCm2, 0, 'f', 1));
    if (m_doseAirKermaValue) m_doseAirKermaValue->setText(tr("%1 mGy").arg(run.airKermaMGy, 0, 'f', 1));
    if (m_doseRefPointValue) m_doseRefPointValue->setText(run.referencePoint);
}

// ─────────────────────────────────────────────────────────────────────────
// Playback controls
// ─────────────────────────────────────────────────────────────────────────
void CineViewStudio::onPlayPauseClicked() {
    if (m_activeRunIndex < 0 || m_activeRunIndex >= m_runs.size()) {
        if (m_btnPlayPause) m_btnPlayPause->setChecked(false);
        return;
    }
    m_isPlaying = !m_isPlaying;
    if (m_btnPlayPause) m_btnPlayPause->setText(m_isPlaying ? tr("‖  Pause") : tr("▶ ‖  Play"));

    if (m_isPlaying) {
        const CineRun &run = m_runs[m_activeRunIndex];
        // Interval derived from the RUN's own recorded fps and the current
        // playback speed multiplier - a run recorded at 15fps played at
        // 2.0x genuinely plays back frame-for-frame at 30fps, not just a
        // vague "faster" feeling.
        const int baseIntervalMs = 1000 / std::max(1, run.fps);
        const int intervalMs = std::max(1, static_cast<int>(baseIntervalMs / std::max(0.01, m_playbackSpeed)));
        m_playbackTimer.start(intervalMs);
    } else {
        m_playbackTimer.stop();
    }
}

void CineViewStudio::onPlaybackTick() {
    if (m_activeRunIndex < 0 || m_activeRunIndex >= m_runs.size()) { m_playbackTimer.stop(); return; }
    const CineRun &run = m_runs[m_activeRunIndex];
    if (run.frames.isEmpty()) { m_playbackTimer.stop(); return; }

    ++m_currentFrameIndex;
    if (m_currentFrameIndex >= run.frames.size()) {
        if (m_loopEnabled) {
            m_currentFrameIndex = 0;
        } else {
            m_currentFrameIndex = run.frames.size() - 1;
            m_isPlaying = false;
            m_playbackTimer.stop();
            if (m_btnPlayPause) { m_btnPlayPause->setChecked(false); m_btnPlayPause->setText(tr("▶ ‖  Play")); }
        }
    }

    updateFrameDisplay();
    if (m_frameSlider) {
        m_frameSlider->blockSignals(true);
        m_frameSlider->setValue(m_currentFrameIndex);
        m_frameSlider->blockSignals(false);
    }
    // Full rebuild, not a highlight-only pass - the visible thumbnail
    // window now slides to stay centered on the current frame (see
    // rebuildThumbnailStrip()'s own comment), so which frames are even
    // shown changes as playback advances, not just which one is
    // highlighted. Only ~22 widgets now (down from up to 120 before), so
    // this is cheap enough to do every tick.
    rebuildThumbnailStrip();
}

void CineViewStudio::onResetClicked() {
    m_currentFrameIndex = 0;
    m_isPlaying = false;
    m_playbackTimer.stop();
    if (m_btnPlayPause) { m_btnPlayPause->setChecked(false); m_btnPlayPause->setText(tr("▶ ‖  Play")); }
    updateFrameDisplay();
    if (m_frameSlider) { m_frameSlider->blockSignals(true); m_frameSlider->setValue(0); m_frameSlider->blockSignals(false); }
}

void CineViewStudio::onFirstClicked() {
    m_currentFrameIndex = 0;
    updateFrameDisplay();
    if (m_frameSlider) { m_frameSlider->blockSignals(true); m_frameSlider->setValue(0); m_frameSlider->blockSignals(false); }
    rebuildThumbnailStrip();
}

void CineViewStudio::onLastClicked() {
    if (m_activeRunIndex < 0 || m_activeRunIndex >= m_runs.size()) return;
    m_currentFrameIndex = std::max(0, static_cast<int>(m_runs[m_activeRunIndex].frames.size()) - 1);
    updateFrameDisplay();
    if (m_frameSlider) {
        m_frameSlider->blockSignals(true);
        m_frameSlider->setValue(m_currentFrameIndex);
        m_frameSlider->blockSignals(false);
    }
    rebuildThumbnailStrip();
}

void CineViewStudio::onStepForwardClicked() {
    if (m_activeRunIndex < 0 || m_activeRunIndex >= m_runs.size()) return;
    m_currentFrameIndex = std::min(static_cast<int>(m_runs[m_activeRunIndex].frames.size()) - 1, m_currentFrameIndex + 1);
    updateFrameDisplay();
    if (m_frameSlider) { m_frameSlider->blockSignals(true); m_frameSlider->setValue(m_currentFrameIndex); m_frameSlider->blockSignals(false); }
    rebuildThumbnailStrip();
}

void CineViewStudio::onStepBackwardClicked() {
    m_currentFrameIndex = std::max(0, m_currentFrameIndex - 1);
    updateFrameDisplay();
    if (m_frameSlider) { m_frameSlider->blockSignals(true); m_frameSlider->setValue(m_currentFrameIndex); m_frameSlider->blockSignals(false); }
    rebuildThumbnailStrip();
}

void CineViewStudio::onSliderMoved(int value) {
    m_currentFrameIndex = value;
    updateFrameDisplay();
    rebuildThumbnailStrip();
}

void CineViewStudio::onSpeedButtonClicked(double speed) {
    m_playbackSpeed = speed;
    for (QPushButton *b : m_speedButtons)
        b->setChecked(qFuzzyCompare(b->text().chopped(1).toDouble() + 1.0, speed + 1.0));
    // If already playing, restart the timer directly at the new interval
    // rather than waiting for the current one to finish.
    if (m_isPlaying && m_activeRunIndex >= 0 && m_activeRunIndex < m_runs.size()) {
        const CineRun &run = m_runs[m_activeRunIndex];
        const int baseIntervalMs = 1000 / std::max(1, run.fps);
        const int intervalMs = std::max(1, static_cast<int>(baseIntervalMs / std::max(0.01, m_playbackSpeed)));
        m_playbackTimer.start(intervalMs);
    }
}

void CineViewStudio::onLoopToggled() {
    m_loopEnabled = m_btnLoop && m_btnLoop->isChecked();
}

void CineViewStudio::onNewAcquisitionClicked() {
    // A manual placeholder run — actual recording is driven by MainWindow
    // calling beginRecording()/recordFrameFromWidget()/endRecording() as
    // the procedure actually happens (see MainWindow::startCineRecordingIfNeeded()).
    // This button exists for the case an operator wants to explicitly mark
    // a new acquisition boundary rather than relying only on the automatic
    // phase-based trigger.
    emit backToSimulatorRequested();
}

bool CineViewStudio::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Resize && watched == m_frameDisplay) {
        // Re-scales the currently displayed frame to the widget's new
        // actual size - see m_frameDisplay's own installEventFilter()
        // comment for why this is needed at all: a pixmap set while this
        // widget was smaller (e.g. before layout settles, or before the
        // window is maximized) otherwise stays visibly stuck at that
        // smaller scale forever, since QLabel never rescales an
        // already-set pixmap on its own just because its own size changed.
        updateFrameDisplay();
        return false;   // don't consume the resize - QLabel's own layout handling still needs it
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto *thumb = qobject_cast<QLabel *>(watched);
        if (thumb && m_thumbnailLabels.contains(thumb)) {
            m_currentFrameIndex = thumb->property("frameIndex").toInt();
            updateFrameDisplay();
            if (m_frameSlider) {
                m_frameSlider->blockSignals(true);
                m_frameSlider->setValue(m_currentFrameIndex);
                m_frameSlider->blockSignals(false);
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}