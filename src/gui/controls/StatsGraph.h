#pragma once

/// @file StatsGraph.h
/// @brief Oscilloscope-style line chart widget — replaces MFC COScopeCtrl.
///
/// Dark navy background, colored series lines, dotted grid,
/// Y-axis labels, and a legend bar at the bottom.

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

    /// Set the Y-axis unit label (e.g. "KB/s" or "").
    void setYUnits(const QString& units) { m_yUnits = units; update(); }

    /// Set fixed Y range; pass 0,0 for auto-scale (default).
    void setYRange(double lower, double upper);

    /// Append one data point per series (vector size must match seriesCount).
    void appendPoints(const std::vector<double>& values);

    /// Clear all data.
    void reset();

    /// Set graph background color (default: RGB(0,0,64)).
    void setBackgroundColor(const QColor& c);

    /// Set grid line color (default: RGB(192,192,255)).
    void setGridColor(const QColor& c);

    /// When true, all series are drawn filled regardless of per-series flag.
    void setFillAll(bool fill);

    [[nodiscard]] QSize minimumSizeHint() const override { return {200, 80}; }
    [[nodiscard]] QSize sizeHint() const override { return {400, 140}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /// Round a value up to a "nice" Y-axis maximum.
    static double niceYMax(double raw);

    static constexpr int kMaxPoints = 1024; // ~51 min at 3s/sample (matches MFC)
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
    bool m_fillAll = false;
};

} // namespace eMule
