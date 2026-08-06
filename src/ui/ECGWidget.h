#pragma once
#include <QWidget>
#include <QTimer>
#include <QVector>
#include <QPainter>

#include "core/CardiacCycle.h"

class ECGWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ECGWidget(QWidget* parent = nullptr);

    /// Share the console's cardiac clock. The trace is then a view of the same
    /// phase that drives the pulsating vessel wall, so the two can never drift:
    /// previously this widget ran its own 16 ms timer and its own phase, and any
    /// difference in rounding accumulated over a session.
    void setCardiacCycle(const cn::CardiacCycle *c) { m_cycle = c; update(); }

    /// Only used when no shared cycle is attached (standalone use).
    void setHeartRate(int bpm);
    void triggerEvent(const QString& label, int durationMs = 3000);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTick();

private:
    void rebuildBuffer();

    const cn::CardiacCycle *m_cycle = nullptr;
    cn::CardiacCycle m_ownCycle;   ///< fallback when no shared clock is set

    QTimer m_timer;
    QVector<double> m_ecgBuffer, m_abpBuffer;
    int m_writePos = 0;
    // Phase and respiratory wander moved into cn::CardiacCycle, which is the
    // single source of cardiac timing for the whole console.
    QString m_eventLabel;
    bool m_inEvent = false;

    // Displayed pressures. Held here rather than printed inline so the numerals
    // and the trace cannot disagree.
    static constexpr double kSystolicMmHg  = 118.0;
    static constexpr double kDiastolicMmHg =  76.0;
    /// Fixed pressure axis. Not auto-scaled: a trace that silently rescales
    /// itself between beats is unreadable as a measurement.
    static constexpr double kScaleTopMmHg  = 180.0;
    static constexpr double kScaleBotMmHg  =  40.0;
    /// ECG amplitude as a fraction of the field height. Small on purpose — it
    /// shares the field with the pressure trace rather than owning a lane.
    static constexpr double kEcgHeightFraction = 0.17;

    const QColor BG_COLOR   = QColor(0, 0, 0);
    // GRID_COLOR removed with the grid — it was the only non-black element
    // in the panel. Re-add as a neutral dark grey if the ruling is wanted back.
    const QColor ECG_COLOR  = QColor(0, 255, 100);
    const QColor ABP_COLOR  = QColor(255, 60, 60);
    const QColor RESP_COLOR = QColor(230, 230, 230);
    // The plethysmograph lane and its PLETH_COLOR were removed. cn::CardiacCycle
    // still provides pleth(); only this display is gone.
};