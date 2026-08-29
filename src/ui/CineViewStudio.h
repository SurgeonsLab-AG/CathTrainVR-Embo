#pragma once

#include <QWidget>
#include <QVector>
#include <QImage>
#include <QString>
#include <QDateTime>
#include <QTimer>
#include <QElapsedTimer>
#include <QEvent>

class QLabel;
class QSlider;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class QScrollArea;
class QHBoxLayout;
class QVBoxLayout;
class QOpenGLWidget;
class CarmView;

// ─────────────────────────────────────────────────────────────────────────
// CineRun
//
// One recorded acquisition — a sequence of frames captured from the live
// Fluoro view, plus the C-arm/dose metadata that was in effect at the
// moment recording started. Real cine acquisitions don't change C-arm angle
// mid-run (an operator doesn't rotate while the pedal is down), so a single
// snapshot taken at beginRecording() time is what a real "Acquisition Info"
// panel would actually show, not a value that needs updating per-frame.
// ─────────────────────────────────────────────────────────────────────────
struct CineRun {
    QString  name;             ///< "Run 002"
    QString  procedureLabel;   ///< "MMA Embolization", "Diagnostic", "Mask Run"
    QVector<QImage> frames;
    int      fps = 15;
    QDateTime startedAt;

    // C-arm angulation at the moment this run started.
    float lao = 0.f, cran = 0.f, sidCm = 100.f, magCm = 32.f, fovCm = 25.f;

    // Radiation dose at the moment this run started.
    float dapGyCm2 = 0.f, airKermaMGy = 0.f;
    QString referencePoint = QStringLiteral("Skin");

    QString operatorName = QStringLiteral("Dr. SurgeonsLab");

    double durationSeconds() const {
        return fps > 0 ? static_cast<double>(frames.size()) / fps : 0.0;
    }
};

// ─────────────────────────────────────────────────────────────────────────
// CineViewStudio
//
// Page 1 of the MainWindow's QStackedWidget: a clinical cine review tool.
// Every acquisition recorded during the procedure (mask run, diagnostic
// angiogram, embolization run, ...) is kept as its own independent CineRun
// in the Acquisitions list; the operator picks one to review and scrubs
// through it frame-by-frame or plays it back continuously, exactly the way
// a real angiography suite's cine review screen works.
// ─────────────────────────────────────────────────────────────────────────
class CineViewStudio : public QWidget
{
    Q_OBJECT

public:
    explicit CineViewStudio(QWidget* parent = nullptr);
    ~CineViewStudio() override;

    // ── Capture API — called from MainWindow while the live simulator is
    // running, to record a new run. `intoReferenceRun` labels the run as a
    // pre-embolization/diagnostic acquisition versus a live procedural one
    // (shown as the run's procedureLabel) - kept as the original parameter
    // name for source compatibility with existing call sites, though its
    // role changed from "which of two fixed viewport buffers" (the old
    // dual-viewport design) to "how to label this new run in the list".
    //
    // The five C-arm/dose parameters are optional or MainWindow may not
    // always have them ready at the exact call site — 0 params fall back to
    // this run's own class defaults (matching what the caller already
    // supplied last render as displayed by VesselView's own HUD) rather
    // than showing a jarring literal 0 in the review panel.
    void beginRecording(bool intoReferenceRun, const QString& runName, int captureFps = 15,
                        float lao = 0.f, float cran = 0.f, float sidCm = 100.f,
                        float magCm = 32.f, float fovCm = 25.f,
                        float dapGyCm2 = 0.f, float airKermaMGy = 0.f);
    // Capture one frame from a QOpenGLWidget (VesselView inherits this).
    // Throttled to captureFps; call from the render loop or a QTimer.
    void recordFrameFromWidget(QOpenGLWidget* widget);
    void endRecording();

    // Adds a previously recorded run to the Acquisitions list without
    // capturing live (e.g. loading a saved session).
    void registerRun(const CineRun& run);

    /// Keeps the embedded C-Arm Quick View mini-render in sync with the
    /// real C-arm - MainWindow calls this from onCarmAnglesChanged(), the
    /// one place every real angle change already funnels through
    /// regardless of source (keyboard, mouse drag on CarmView itself, a
    /// menu action, ...). This is a genuinely separate CarmView instance
    /// (its own OpenGL context, matching how every VesselView instance in
    /// this app is already independent and kept in sync the same way, not
    /// one widget shared/reparented between pages), so it needs telling
    /// explicitly rather than picking the angle up on its own.
    void setCarmAngles(float lao, float cran);
    /// Direct access to the embedded C-Arm Quick View instance - needed so
    /// MainWindow can load the same mesh part files into it that it
    /// already loads into the main simulator page's own CarmView. Without
    /// this, the embedded instance has zero mesh parts loaded and renders
    /// completely empty regardless of camera/zoom settings - this was the
    /// actual root cause of "C-Arm not visible", not a framing issue.
    CarmView* carmView() const { return m_cineCarmView; }

    // Pixel processing — public/static so MainWindow (or an export step)
    // can reuse the same inversion logic outside this widget if needed.
    static QImage invertToMonochrome(const QImage& source);
    // Grab the current framebuffer of any QOpenGLWidget as a QImage.
    // Uses grabFramebuffer() — no VTK dependency.
    static QImage captureWidgetFrame(QOpenGLWidget* widget);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void backToSimulatorRequested();

private slots:
    void onPlayPauseClicked();
    void onResetClicked();
    void onFirstClicked();
    void onLastClicked();
    void onStepForwardClicked();
    void onStepBackwardClicked();
    void onSliderMoved(int value);
    void onSpeedButtonClicked(double speed);
    void onLoopToggled();
    void onRunSelected(int runIndex);
    void onPlaybackTick();
    void onNewAcquisitionClicked();

private:
    void buildLayout();
    void rebuildAcquisitionsList();
    void rebuildThumbnailStrip();
    void updateFrameDisplay();
    void updateReviewPanel();
    void setActiveRun(int index);
    QImage renderFrameForDisplay(const QImage& raw) const;

    // ---- Run storage ----
    QVector<CineRun> m_runs;
    int  m_recordingRunIndex = -1;   ///< index into m_runs currently being appended to, or -1
    QElapsedTimer m_captureThrottle; ///< gates appendFrame() to the recording run's own fps

    // ---- Playback state ----
    QTimer m_playbackTimer;
    int    m_activeRunIndex   = -1;  ///< which run is selected for review
    int    m_currentFrameIndex = 0;
    double m_playbackSpeed    = 1.0;
    bool   m_loopEnabled      = false;
    bool   m_isPlaying        = false;
    // No invert toggle: unlike the old dual-viewport design (which
    // captured from arbitrary 3D renders and needed conversion to a
    // fluoro look), frames here are captured directly from the live
    // Fluoro view, which already renders with the correct dark-contrast-
    // on-light-tissue convention. Inverting an already-correct capture
    // again would produce a backwards image, not a toggleable style
    // choice - renderFrameForDisplay() just returns the frame as captured.

    // ---- UI: header ----
    QLabel* m_reviewTitleLabel = nullptr;

    // ---- UI: left Acquisitions sidebar ----
    QVector<QPushButton*> m_runCards;   ///< one styled card per run, parallel to m_runs
    QVBoxLayout* m_runListLayout = nullptr;
    QWidget*     m_runListContainer = nullptr;
    QScrollArea* m_runListScroll = nullptr;
    QLabel* m_carmPositionLabel = nullptr;   ///< small LAO/CRAN text readout beneath the mini-render
    /// Genuinely separate CarmView instance (own OpenGL context) - see
    /// setCarmAngles()'s own comment for why this isn't the same widget
    /// instance the main simulator page uses.
    CarmView* m_cineCarmView = nullptr;

    // ---- UI: central Fluoro display ----
    QLabel* m_frameDisplay = nullptr;   ///< the current frame, C-arm HUD baked in from capture time

    // ---- UI: right Cine Review panel ----
    QLabel* m_frameCounterLabel = nullptr;      ///< "Frame 18 / 60"
    QSlider* m_frameSlider = nullptr;
    QLabel* m_acqFrameRateValue = nullptr;
    QLabel* m_acqDurationValue = nullptr;
    QLabel* m_acqAcquiredValue = nullptr;
    QLabel* m_acqOperatorValue = nullptr;
    QLabel* m_angLaoValue = nullptr;
    QLabel* m_angCranValue = nullptr;
    QLabel* m_angSidValue = nullptr;
    QLabel* m_angMagValue = nullptr;
    QLabel* m_angFovValue = nullptr;
    QLabel* m_doseDapValue = nullptr;
    QLabel* m_doseAirKermaValue = nullptr;
    QLabel* m_doseRefPointValue = nullptr;

    // ---- UI: bottom frame thumbnail timeline ----
    QScrollArea* m_thumbnailScroll = nullptr;
    QWidget*     m_thumbnailContainer = nullptr;
    QHBoxLayout* m_thumbnailLayout = nullptr;
    QVector<QLabel*> m_thumbnailLabels;   ///< parallel to the active run's frames
    QVector<QWidget*> m_thumbnailWrappers; ///< owns each [image, number] group; deleting these cascades to their children
    QLabel* m_thumbnailCountLabel = nullptr;
    /// Wide scrub bar spanning the full width beneath the thumbnail strip,
    /// per the reference - kept in sync with m_frameSlider (the one in the
    /// Cine Review panel) via a bidirectional signal connection rather than
    /// updating both explicitly at every one of the many call sites that
    /// change the current frame; Qt's own setValue() only emits
    /// valueChanged when the value genuinely changes, so this cannot loop.
    QSlider* m_timelineScrubber = nullptr;

    // ---- UI: bottom playback control deck ----
    QPushButton* m_btnFirst = nullptr;
    QPushButton* m_btnPrev = nullptr;
    QPushButton* m_btnPlayPause = nullptr;
    QPushButton* m_btnNext = nullptr;
    QPushButton* m_btnLast = nullptr;
    QPushButton* m_btnLoop = nullptr;
    QVector<QPushButton*> m_speedButtons;
    QPushButton* m_btnReset = nullptr;
};