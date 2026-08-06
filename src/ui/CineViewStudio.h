#pragma once

#include <QWidget>
#include <QVector>
#include <QImage>
#include <QMutex>
#include <QTimer>
#include <QString>
#include <QEvent>

class QLabel;
class QSlider;
class QPushButton;
class QCheckBox;
class QComboBox;
class QListWidget;
class QListWidgetItem;
class QOpenGLWidget;

// ─────────────────────────────────────────────────────────────────────────
// FrameBufferManager
//
// Thread-safe store for one recorded angiogram "run" (a sequence of frames
// captured from a live QOpenGLWidget). Capture happens on whatever thread
// calls captureFrame() (normally the GUI/render thread, via a QTimer); all
// public access is mutex-guarded so a background export/inversion pass
// could safely read frames while capture is still appending, without the
// two ever tearing a QImage mid-copy.
// ─────────────────────────────────────────────────────────────────────────
class FrameBufferManager
{
public:
    explicit FrameBufferManager(QString runName = QStringLiteral("Run 001"));

    // Appends a captured frame. Safe to call from a QTimer on the GUI thread
    // while other threads read via frameAt()/frameCount().
    void appendFrame(const QImage& frame);

    void clear();

    int frameCount() const;
    QImage frameAt(int index) const;   // returns a null QImage if index is out of range

    QString runName() const { return m_runName; }
    void setRunName(const QString& name) { m_runName = name; }

private:
    mutable QMutex m_mutex;
    QVector<QImage> m_frames;
    QString m_runName;
};

// ─────────────────────────────────────────────────────────────────────────
// CineViewStudio
//
// Page 1 of the MainWindow's QStackedWidget: a split-screen cine review
// tool with a reference (pre-embolization) and active (post-embolization)
// viewport, played back in lockstep from a shared timeline scrubber.
// ─────────────────────────────────────────────────────────────────────────
class CineViewStudio : public QWidget
{
    Q_OBJECT

public:
    explicit CineViewStudio(QWidget* parent = nullptr);
    ~CineViewStudio() override;

    // Capture API — call these from MainWindow while the live 3D simulator
    // is running, to record a new run into the frame buffers. captureFps
    // controls how often appendFrame() actually stores a frame; the caller
    // is expected to invoke recordFrameFromRenderWindow() on its own render
    // loop/timer, not the other way around, so this class has zero
    // dependency on how the simulator drives its own rendering.
    void beginRecording(bool intoReferenceRun, const QString& runName, int captureFps = 15);
    // Capture one frame from a QOpenGLWidget (VesselView inherits this).
    // Throttled to captureFps; call from the render loop or a QTimer.
    void recordFrameFromWidget(QOpenGLWidget* widget);
    void endRecording();

    // Adds a previously recorded run to the sidebar list without capturing
    // live (e.g. loading a saved session).
    void registerRun(const QString& runName, const QVector<QImage>& frames, bool isReferenceRun);

    // Pixel processing — public/static so MainWindow (or an export step)
    // can reuse the same inversion logic outside this widget if needed.
    static QImage invertToMonochrome(const QImage& source);
    // Grab the current framebuffer of any QOpenGLWidget as a QImage.
    // Uses grabFramebuffer() — no VTK dependency.
    static QImage captureWidgetFrame(QOpenGLWidget* widget);

protected:
    // Drives the lightweight click-drag caliper measurement overlay on
    // whichever viewport label is active, when the caliper tool is toggled on.
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void backToSimulatorRequested();

private slots:
    void onPlayClicked();
    void onPauseClicked();
    void onResetClicked();
    void onStepForwardClicked();
    void onStepBackwardClicked();
    void onSliderMoved(int value);
    void onSpeedChanged(int index);
    void onLoopToggled(bool checked);
    void onInvertToggled(bool checked);
    void onCaliperToggled(bool checked);
    void onSyncToggled(bool checked);
    void onRunSelected(QListWidgetItem* item);
    void onPlaybackTick();

private:
    void buildLayout();
    void updateViewportsForIndex(int index);
    void updateTimelineRangeFromActiveBuffers();
    QImage renderFrameForDisplay(const QImage& raw) const;

    // ---- Frame storage ----
    FrameBufferManager m_referenceBuffer;   // pre-embolization baseline
    FrameBufferManager m_activeBuffer;      // post-embolization / live
    bool m_recordingIntoReference = false;
    bool m_isRecording = false;
    QTimer m_captureThrottle;               // gates appendFrame() to the requested capture FPS

    // ---- Playback state ----
    QTimer m_playbackTimer;
    int m_currentFrameIndex = 0;
    double m_playbackSpeed = 1.0;
    bool m_loopEnabled = false;
    bool m_invertEnabled = true;            // X-ray look defaults to inverted (dark contrast on light tissue)
    bool m_syncEnabled = true;              // scrub both viewports together
    bool m_caliperActive = false;

    // ---- UI: top strip ----
    QPushButton* m_btnBack = nullptr;
    QLabel* m_hudLabel = nullptr;

    // ---- UI: left toolbox ----
    QPushButton* m_btnInvert = nullptr;
    QPushButton* m_btnCaliper = nullptr;
    QPushButton* m_btnSyncToggle = nullptr;
    QListWidget* m_runListWidget = nullptr;

    // ---- UI: central dual viewport ----
    QLabel* m_referenceViewport = nullptr;   // left: pre-embolization baseline
    QLabel* m_activeViewport = nullptr;      // right: post-embolization / live

    // ---- UI: bottom control deck ----
    QSlider* m_timelineSlider = nullptr;
    QPushButton* m_btnPlay = nullptr;
    QPushButton* m_btnPause = nullptr;
    QPushButton* m_btnReset = nullptr;
    QPushButton* m_btnStepForward = nullptr;
    QPushButton* m_btnStepBackward = nullptr;
    QCheckBox* m_loopCheckBox = nullptr;
    QComboBox* m_speedCombo = nullptr;
};