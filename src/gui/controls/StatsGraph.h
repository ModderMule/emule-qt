#pragma once

/// @file StatsGraph.h
/// @brief Oscilloscope-style line chart widget — replaces MFC COScopeCtrl.
///
/// Dark navy background, colored series lines, dotted grid,
/// Y-axis labels, and a legend bar at the bottom.

#include <QDateTime>
#include <QWidget>

#include <cstdint>
#include <deque>
#include <vector>

namespace eMule {

/// Reusable oscilloscope-style line chart for the Statistics panel.
class StatsGraph : public QWidget {
    Q_OBJECT

public:
    explicit StatsGraph(int seriesCount, QWidget* parent = nullptr);

    struct SeriesInfo {
        QString label;
        QColor  color;
        bool    filled = false;
    };

    /// Configure a series (call once per series after construction).
    void setSeriesInfo(int index, const QString& label, const QColor& color,
                       bool filled = false);

    /// Restyle one series without restating its label — the options page changes
    /// colours long after setSeriesInfo() named everything.
    void setSeriesColor(int index, const QColor& color);

    /// Fill one series down to the baseline (MFC SetBarsPlot).
    void setSeriesFilled(int index, bool filled);

    [[nodiscard]] int seriesCount() const { return m_seriesCount; }
    [[nodiscard]] QString seriesLabel(int index) const { return m_series.at(static_cast<size_t>(index)).label; }
    [[nodiscard]] bool seriesFilled(int index) const { return m_series.at(static_cast<size_t>(index)).filled; }
    [[nodiscard]] QString yUnits() const { return m_yUnits; }

    /// Name of what the Y axis measures ("Download Speed"); also leads the tooltip.
    void setYUnits(const QString& units) { m_yUnits = units; update(); }

    /// Set fixed Y range; pass 0,0 for auto-scale (default).
    void setYRange(double lower, double upper);
    [[nodiscard]] double yUpper() const { return m_yUpper; }

    /// Seconds between samples — the daemon's graphsUpdateSec. One sample is one
    /// pixel, so this sets how much time the plot spans.
    void setSampleIntervalSec(double seconds)
    {
        if (seconds > 0.0 && seconds != m_sampleIntervalSec) {
            m_sampleIntervalSec = seconds;
            update();
        }
    }
    [[nodiscard]] double sampleIntervalSec() const { return m_sampleIntervalSec; }

    /// Append one data point per series (vector size must match seriesCount).
    void appendPoints(const std::vector<double>& values);

    /// Clear all data.
    void reset();

    /// Set graph background color (default: RGB(0,0,64)).
    void setBackgroundColor(const QColor& c);

    /// Set grid line color (default: RGB(192,192,255)).
    void setGridColor(const QColor& c);

    [[nodiscard]] QSize minimumSizeHint() const override { return {200, 80}; }
    [[nodiscard]] QSize sizeHint() const override { return {400, 140}; }

    // --- The time axis, MFC COScopeCtrl: one pixel per sample, newest at the right
    // edge, older ones scrolling out on the left (OScopeCtrl.cpp:137-139, 598-608).

    /// X of sample @p index out of @p count when the newest sits at @p right.
    [[nodiscard]] static int sampleX(int right, int count, int index)
    {
        return right - (count - 1 - index);
    }
    /// How long ago the sample under @p x was taken.
    [[nodiscard]] static qint64 secondsAgoAt(int x, int right, double sampleIntervalSec)
    {
        return static_cast<qint64>((right - x) * sampleIntervalSec);
    }
    /// The time the plot spans at @p plotWidth pixels: "51:12 mins"; "Stopped" when
    /// the daemon takes no samples.
    [[nodiscard]] static QString spanCaption(int plotWidth, double sampleIntervalSec);

    /// The plot rectangle inside the widget, without axis labels and legend.
    [[nodiscard]] QRect plotRect() const;
    /// "<units>: <value> @ <time> (<duration> ago)" for a point in the plot; empty outside.
    [[nodiscard]] QString tooltipAt(const QPoint& pos, const QDateTime& now) const;

signals:
    void doubleClicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    /// Round a value up to a "nice" Y-axis maximum.
    static double niceYMax(double raw);
    [[nodiscard]] double currentYMax() const;

    /// Enough for the widest plot; MFC grows its buffer with the window the same way.
    static constexpr int kMaxPoints = 4096;
    static constexpr int kGridDivisions = 5;

    int m_seriesCount;
    std::vector<SeriesInfo> m_series;
    std::vector<std::deque<double>> m_data; // per-series ring buffer

    QString m_yUnits;
    double m_yLower = 0.0;
    double m_yUpper = 0.0; // 0 = auto-scale
    double m_sampleIntervalSec = 3.0; // seconds between samples (for time labels)
    QColor m_bgColor{0, 0, 64};
    QColor m_gridColor{192, 192, 255};
};

} // namespace eMule
