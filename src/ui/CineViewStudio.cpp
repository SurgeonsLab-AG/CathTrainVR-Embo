#include "CineViewStudio.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QFrame>
#include <QMutexLocker>

#include <QOpenGLWidget>

#include <algorithm>
#include <cmath>
#include <cstring>

// ═════════════════════════════════════════════════════════════════════════
// FrameBufferManager
// ═════════════════════════════════════════════════════════════════════════

FrameBufferManager::FrameBufferManager(QString runName)
    : m_runName(std::move(runName))
{
}

void FrameBufferManager::appendFrame(const QImage& frame)
{
    QMutexLocker lock(&m_mutex);
    // QImage is implicitly shared/copy-on-write, so this append is cheap and
    // safe to do while another thread holds an earlier frameAt() copy.
    m_frames.append(frame);
}

void FrameBufferManager::clear()
{
    QMutexLocker lock(&m_mutex);
    m_frames.clear();
}

int FrameBufferManager::frameCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_frames.size();
}

QImage FrameBufferManager::frameAt(int index) const
{
    QMutexLocker lock(&m_mutex);
    if (index < 0 || index >= m_frames.size()) return QImage();
    return m_frames.at(index);
}

// ═════════════════════════════════════════════════════════════════════════
// CineViewStudio
// ═════════════════════════════════════════════════════════════════════════

CineViewStudio::CineViewStudio(QWidget* parent)
    : QWidget(parent)
    , m_referenceBuffer(QStringLiteral("Run 001 (Reference)"))
    , m_activeBuffer(QStringLiteral("Run 002 (Active)"))
{
    buildLayout();

    // Playback ticks at a nominal 30fps; actual advance-per-tick is scaled
    // by m_playbackSpeed so 0.5x/1.0x/2.0x all share one QTimer.
    m_playbackTimer.setInterval(33);
    connect(&m_playbackTimer, &QTimer::timeout, this, &CineViewStudio::onPlaybackTick);

    // Capture throttle: appendFrame() only actually fires when this timer
    // has elapsed, so recordFrameFromRenderWindow() can be called as often
    // as the simulator likes (e.g. every VTK Render()) without flooding the
    // buffer faster than the requested capture FPS.
    m_captureThrottle.setSingleShot(false);
}

CineViewStudio::~CineViewStudio() = default;

// ─────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────

void CineViewStudio::buildLayout()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ---- 1. Top menu strip ----
    auto* topStrip = new QWidget(this);
    topStrip->setObjectName("cineTopStrip");
    topStrip->setStyleSheet("#cineTopStrip { background:#1c1c1f; border-bottom:1px solid #333; }");
    auto* topLayout = new QHBoxLayout(topStrip);
    topLayout->setContentsMargins(10, 6, 10, 6);

    m_btnBack = new QPushButton(QStringLiteral("\u25C4 Back to Simulator"), topStrip);
    connect(m_btnBack, &QPushButton::clicked, this, &CineViewStudio::backToSimulatorRequested);
    topLayout->addWidget(m_btnBack);

    topLayout->addStretch();

    m_hudLabel = new QLabel(QStringLiteral("Patient: — | C-Arm: LAO 0° / CRAN 0° | Studio: Cine Review"), topStrip);
    m_hudLabel->setStyleSheet("color:#ccc; font-weight:bold;");
    topLayout->addWidget(m_hudLabel);

    rootLayout->addWidget(topStrip);

    // ---- Middle: sidebar (20%) + central monitor hub (80%) ----
    auto* middleSplitter = new QSplitter(Qt::Horizontal, this);
    middleSplitter->setChildrenCollapsible(false);

    // ---- 2. Left-side toolbox sidebar ----
    auto* sidebar = new QWidget(middleSplitter);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(8, 8, 8, 8);

    m_btnInvert = new QPushButton(QStringLiteral("Invert X-Ray View"), sidebar);
    m_btnInvert->setCheckable(true);
    m_btnInvert->setChecked(m_invertEnabled);
    connect(m_btnInvert, &QPushButton::toggled, this, &CineViewStudio::onInvertToggled);
    sidebarLayout->addWidget(m_btnInvert);

    m_btnCaliper = new QPushButton(QStringLiteral("Activate Caliper Tool"), sidebar);
    m_btnCaliper->setCheckable(true);
    connect(m_btnCaliper, &QPushButton::toggled, this, &CineViewStudio::onCaliperToggled);
    sidebarLayout->addWidget(m_btnCaliper);

    m_btnSyncToggle = new QPushButton(QStringLiteral("Toggle Playback Sync"), sidebar);
    m_btnSyncToggle->setCheckable(true);
    m_btnSyncToggle->setChecked(m_syncEnabled);
    connect(m_btnSyncToggle, &QPushButton::toggled, this, &CineViewStudio::onSyncToggled);
    sidebarLayout->addWidget(m_btnSyncToggle);

    sidebarLayout->addSpacing(12);
    sidebarLayout->addWidget(new QLabel(QStringLiteral("Recorded Runs"), sidebar));

    m_runListWidget = new QListWidget(sidebar);
    m_runListWidget->addItem(m_referenceBuffer.runName());
    m_runListWidget->addItem(m_activeBuffer.runName());
    connect(m_runListWidget, &QListWidget::itemClicked, this, &CineViewStudio::onRunSelected);
    sidebarLayout->addWidget(m_runListWidget, /*stretch=*/1);

    middleSplitter->addWidget(sidebar);

    // ---- 3. Central monitor hub: dual viewport ----
    auto* monitorHub = new QWidget(middleSplitter);
    auto* monitorLayout = new QHBoxLayout(monitorHub);
    monitorLayout->setContentsMargins(4, 4, 4, 4);
    monitorLayout->setSpacing(4);

    auto makeViewportLabel = [this](const QString& caption) -> QLabel* {
        auto* frame = new QLabel(this);
        frame->setAlignment(Qt::AlignCenter);
        frame->setMinimumSize(320, 320);
        frame->setStyleSheet("background:#000; color:#888; border:1px solid #333;");
        frame->setText(caption);
        frame->installEventFilter(this);
        return frame;
    };

    m_referenceViewport = makeViewportLabel(QStringLiteral("Reference (Pre-Embolization)"));
    m_activeViewport     = makeViewportLabel(QStringLiteral("Active (Post-Embolization)"));

    monitorLayout->addWidget(m_referenceViewport, 1);
    monitorLayout->addWidget(m_activeViewport, 1);

    middleSplitter->addWidget(monitorHub);

    // 20% / 80% split, as specified.
    middleSplitter->setStretchFactor(0, 20);
    middleSplitter->setStretchFactor(1, 80);

    rootLayout->addWidget(middleSplitter, /*stretch=*/1);

    // ---- 4. Bottom control deck ----
    auto* bottomDeck = new QWidget(this);
    bottomDeck->setObjectName("cineBottomDeck");
    bottomDeck->setStyleSheet("#cineBottomDeck { background:#1c1c1f; border-top:1px solid #333; }");
    auto* bottomLayout = new QVBoxLayout(bottomDeck);
    bottomLayout->setContentsMargins(10, 6, 10, 6);

    m_timelineSlider = new QSlider(Qt::Horizontal, bottomDeck);
    m_timelineSlider->setMinimum(0);
    m_timelineSlider->setMaximum(0);
    connect(m_timelineSlider, &QSlider::sliderMoved, this, &CineViewStudio::onSliderMoved);
    bottomLayout->addWidget(m_timelineSlider);

    auto* controlsLayout = new QHBoxLayout();

    m_btnStepBackward = new QPushButton(QStringLiteral("\u23EA"), bottomDeck);
    m_btnPlay         = new QPushButton(QStringLiteral("\u25B6 Play"), bottomDeck);
    m_btnPause        = new QPushButton(QStringLiteral("\u23F8 Pause"), bottomDeck);
    m_btnStepForward  = new QPushButton(QStringLiteral("\u23E9"), bottomDeck);
    m_btnReset        = new QPushButton(QStringLiteral("\u23F9 Reset"), bottomDeck);

    connect(m_btnStepBackward, &QPushButton::clicked, this, &CineViewStudio::onStepBackwardClicked);
    connect(m_btnPlay,         &QPushButton::clicked, this, &CineViewStudio::onPlayClicked);
    connect(m_btnPause,        &QPushButton::clicked, this, &CineViewStudio::onPauseClicked);
    connect(m_btnStepForward,  &QPushButton::clicked, this, &CineViewStudio::onStepForwardClicked);
    connect(m_btnReset,        &QPushButton::clicked, this, &CineViewStudio::onResetClicked);

    controlsLayout->addWidget(m_btnStepBackward);
    controlsLayout->addWidget(m_btnPlay);
    controlsLayout->addWidget(m_btnPause);
    controlsLayout->addWidget(m_btnStepForward);
    controlsLayout->addWidget(m_btnReset);

    controlsLayout->addSpacing(20);

    m_loopCheckBox = new QCheckBox(QStringLiteral("Loop"), bottomDeck);
    connect(m_loopCheckBox, &QCheckBox::toggled, this, &CineViewStudio::onLoopToggled);
    controlsLayout->addWidget(m_loopCheckBox);

    controlsLayout->addSpacing(20);
    controlsLayout->addWidget(new QLabel(QStringLiteral("Speed:"), bottomDeck));

    m_speedCombo = new QComboBox(bottomDeck);
    m_speedCombo->addItem(QStringLiteral("0.5x"), 0.5);
    m_speedCombo->addItem(QStringLiteral("1.0x"), 1.0);
    m_speedCombo->addItem(QStringLiteral("2.0x"), 2.0);
    m_speedCombo->setCurrentIndex(1);   // default 1.0x
    connect(m_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CineViewStudio::onSpeedChanged);
    controlsLayout->addWidget(m_speedCombo);

    controlsLayout->addStretch();
    bottomLayout->addLayout(controlsLayout);

    rootLayout->addWidget(bottomDeck);

    setStyleSheet(styleSheet() + "QPushButton { padding:6px 10px; } "
                                 "QPushButton:checked { background:#3a6ea5; color:white; }");
}

// ─────────────────────────────────────────────────────────────────────────
// Capture API
// ─────────────────────────────────────────────────────────────────────────

void CineViewStudio::beginRecording(bool intoReferenceRun, const QString& runName, int captureFps)
{
    m_recordingIntoReference = intoReferenceRun;
    FrameBufferManager& target = intoReferenceRun ? m_referenceBuffer : m_activeBuffer;
    target.clear();
    target.setRunName(runName);

    if (m_runListWidget) {
        int row = intoReferenceRun ? 0 : 1;
        if (auto* item = m_runListWidget->item(row)) item->setText(runName);
    }

    captureFps = std::max(1, captureFps);
    m_captureThrottle.setInterval(1000 / captureFps);
    m_captureThrottle.start();   // just gates timing; readiness checked in recordFrameFromRenderWindow()

    m_isRecording = true;
}

void CineViewStudio::recordFrameFromWidget(QOpenGLWidget* widget)
{
    if (!m_isRecording || !widget) return;

    // Throttle: only append a frame once the capture-FPS interval has elapsed.
    if (m_captureThrottle.remainingTime() > 0) return;
    m_captureThrottle.start();

    QImage frame = captureWidgetFrame(widget);
    if (frame.isNull()) return;

    FrameBufferManager& target = m_recordingIntoReference ? m_referenceBuffer : m_activeBuffer;
    target.appendFrame(frame);

    updateTimelineRangeFromActiveBuffers();
}

void CineViewStudio::endRecording()
{
    m_isRecording = false;
    m_captureThrottle.stop();
}

void CineViewStudio::registerRun(const QString& runName, const QVector<QImage>& frames, bool isReferenceRun)
{
    FrameBufferManager& target = isReferenceRun ? m_referenceBuffer : m_activeBuffer;
    target.clear();
    target.setRunName(runName);
    for (const QImage& f : frames) target.appendFrame(f);

    if (m_runListWidget) {
        int row = isReferenceRun ? 0 : 1;
        if (auto* item = m_runListWidget->item(row)) item->setText(runName);
    }

    updateTimelineRangeFromActiveBuffers();
}

// ─────────────────────────────────────────────────────────────────────────
// Pixel processing
// ─────────────────────────────────────────────────────────────────────────

QImage CineViewStudio::captureWidgetFrame(QOpenGLWidget* widget)
{
    if (!widget) return QImage();

    // grabFramebuffer() reads the back buffer of the OpenGL context that owns
    // the widget and returns it as a QImage (Format_ARGB32 on most platforms).
    // Qt-native capture: no third-party dependency, no front/back-buffer
    // confusion, no row-flip required
    // because Qt already returns the image in top-left origin convention.
    QImage frame = widget->grabFramebuffer();
    if (frame.isNull()) return frame;

    // Normalise to RGB888 for consistent downstream processing (inversion,
    // saving). Conversion is a no-op if the format is already compatible.
    return frame.convertToFormat(QImage::Format_RGB888);
}

QImage CineViewStudio::invertToMonochrome(const QImage& source)
{
    if (source.isNull()) return source;

    QImage gray = source.convertToFormat(QImage::Format_Grayscale8);
    QImage result(gray.size(), QImage::Format_Grayscale8);

    const int w = gray.width();
    const int h = gray.height();
    for (int y = 0; y < h; ++y) {
        const uchar* srcLine = gray.constScanLine(y);
        uchar* dstLine = result.scanLine(y);
        for (int x = 0; x < w; ++x) {
            // High-contrast inversion: dark contrast media on light tissue
            // becomes bright contrast on dark field, or vice versa,
            // matching the classic fluoro "invert" toggle.
            dstLine[x] = static_cast<uchar>(255 - srcLine[x]);
        }
    }
    return result.convertToFormat(QImage::Format_RGB888);
}

QImage CineViewStudio::renderFrameForDisplay(const QImage& raw) const
{
    return m_invertEnabled ? invertToMonochrome(raw) : raw;
}

// ─────────────────────────────────────────────────────────────────────────
// Playback / timeline
// ─────────────────────────────────────────────────────────────────────────

void CineViewStudio::updateTimelineRangeFromActiveBuffers()
{
    int maxCount = std::max(m_referenceBuffer.frameCount(), m_activeBuffer.frameCount());
    if (m_timelineSlider) {
        m_timelineSlider->setMaximum(std::max(0, maxCount - 1));
    }
}

void CineViewStudio::updateViewportsForIndex(int index)
{
    m_currentFrameIndex = index;

    QImage refFrame = m_referenceBuffer.frameAt(index);
    QImage activeFrame = m_syncEnabled ? m_activeBuffer.frameAt(index)
                                       : m_activeBuffer.frameAt(m_currentFrameIndex);

    if (!refFrame.isNull() && m_referenceViewport) {
        m_referenceViewport->setPixmap(QPixmap::fromImage(renderFrameForDisplay(refFrame))
                                           .scaled(m_referenceViewport->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    if (!activeFrame.isNull() && m_activeViewport) {
        m_activeViewport->setPixmap(QPixmap::fromImage(renderFrameForDisplay(activeFrame))
                                        .scaled(m_activeViewport->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    if (m_timelineSlider && m_timelineSlider->value() != index) {
        QSignalBlocker blocker(m_timelineSlider);
        m_timelineSlider->setValue(index);
    }
}

void CineViewStudio::onPlaybackTick()
{
    int maxCount = std::max(m_referenceBuffer.frameCount(), m_activeBuffer.frameCount());
    if (maxCount <= 0) { m_playbackTimer.stop(); return; }

    // 33ms base tick x speed scalar => effective frames-per-tick advance.
    // Accumulate fractional advance so 0.5x genuinely plays at half rate
    // rather than rounding to "advance every other tick."
    static double accumulator = 0.0;
    accumulator += m_playbackSpeed;
    int advance = static_cast<int>(accumulator);
    accumulator -= advance;
    if (advance <= 0) return;

    int next = m_currentFrameIndex + advance;
    if (next >= maxCount) {
        if (m_loopEnabled) {
            next = next % maxCount;
        } else {
            next = maxCount - 1;
            m_playbackTimer.stop();
        }
    }
    updateViewportsForIndex(next);
}

void CineViewStudio::onPlayClicked()      { m_playbackTimer.start(); }
void CineViewStudio::onPauseClicked()     { m_playbackTimer.stop(); }

void CineViewStudio::onResetClicked()
{
    m_playbackTimer.stop();
    updateViewportsForIndex(0);
}

void CineViewStudio::onStepForwardClicked()
{
    int maxCount = std::max(m_referenceBuffer.frameCount(), m_activeBuffer.frameCount());
    if (maxCount <= 0) return;
    int next = std::min(m_currentFrameIndex + 1, maxCount - 1);
    updateViewportsForIndex(next);
}

void CineViewStudio::onStepBackwardClicked()
{
    int prev = std::max(m_currentFrameIndex - 1, 0);
    updateViewportsForIndex(prev);
}

void CineViewStudio::onSliderMoved(int value)
{
    // This is the "double-screen chronological sync" requirement: dragging
    // the single master slider scrubs BOTH viewports to the same relative
    // index in one call.
    updateViewportsForIndex(value);
}

void CineViewStudio::onSpeedChanged(int index)
{
    if (!m_speedCombo) return;
    m_playbackSpeed = m_speedCombo->itemData(index).toDouble();
}

void CineViewStudio::onLoopToggled(bool checked)   { m_loopEnabled = checked; }

void CineViewStudio::onInvertToggled(bool checked)
{
    m_invertEnabled = checked;
    updateViewportsForIndex(m_currentFrameIndex);   // re-render current frame with new inversion state
}

void CineViewStudio::onSyncToggled(bool checked)
{
    m_syncEnabled = checked;
    m_btnSyncToggle->setText(checked ? QStringLiteral("Toggle Playback Sync (ON)")
                                     : QStringLiteral("Toggle Playback Sync (OFF)"));
}

void CineViewStudio::onCaliperToggled(bool checked)
{
    m_caliperActive = checked;
}

void CineViewStudio::onRunSelected(QListWidgetItem* item)
{
    if (!item || !m_runListWidget) return;
    // Row 0 = reference run, row 1 = active run, matching registerRun()'s
    // convention above. Selecting either just brings its buffer's current
    // frame into view; both viewports always show whichever run occupies
    // their slot, this only affects which label text is highlighted.
    int row = m_runListWidget->row(item);
    m_hudLabel->setText(QStringLiteral("Patient: — | C-Arm: LAO 0° / CRAN 0° | Viewing: %1").arg(item->text()));
    Q_UNUSED(row);
}

// ─────────────────────────────────────────────────────────────────────────
// Caliper tool — lightweight click-drag pixel-distance measurement,
// implemented via an event filter on the viewport QLabels so it doesn't
// require a custom QGraphicsView just for this one feature.
// ─────────────────────────────────────────────────────────────────────────
bool CineViewStudio::eventFilter(QObject* watched, QEvent* event)
{
    if (!m_caliperActive) return QWidget::eventFilter(watched, event);

    auto* label = qobject_cast<QLabel*>(watched);
    if (!label || (label != m_referenceViewport && label != m_activeViewport))
        return QWidget::eventFilter(watched, event);

    static QPoint dragStart;
    static bool dragging = false;

    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        dragStart = me->pos();
        dragging = true;
        return true;
    }
    if (event->type() == QEvent::MouseMove && dragging) {
        auto* me = static_cast<QMouseEvent*>(event);
        QPixmap base = label->pixmap(Qt::ReturnByValue);
        if (!base.isNull()) {
            QPixmap overlay = base.copy();
            QPainter p(&overlay);
            p.setPen(QPen(Qt::yellow, 2));
            p.drawLine(dragStart, me->pos());
            double dist = std::hypot(me->pos().x() - dragStart.x(), me->pos().y() - dragStart.y());
            p.drawText(me->pos() + QPoint(8, -8), QStringLiteral("%1 px").arg(dist, 0, 'f', 1));
            label->setPixmap(overlay);
        }
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        dragging = false;
        return true;
    }
    return QWidget::eventFilter(watched, event);
}