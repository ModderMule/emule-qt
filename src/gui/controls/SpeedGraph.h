#pragma once

/// @file SpeedGraph.h
/// @brief Compact dual-pane speed graph for the toolbar — upload top, download bottom.

#include <QTimer>
#include <QWidget>

#include <deque>

namespace eMule {

/// Compact speed graph widget displayed in the main toolbar.
/// Two halves: upload (red, top) and download (green, bottom),
/// each with independent auto-scaling, arrow indicators, and a scrolling grid.
class SpeedGraph : public QWidget {
    Q_OBJECT

public:
    explicit SpeedGraph(QWidget* parent = nullptr);

    /// Append one sample (called every second from the rate polling timer).
    void appendSample(double downKBs, double upKBs);

    /// Set the visible time range; resizes the internal ring buffer.
    void setTimeRangeMinutes(int minutes);

    [[nodiscard]] QSize sizeHint() const override { return {250, 50}; }
    [[nodiscard]] QSize minimumSizeHint() const override { return {180, 40}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /// Round a value up to a "nice" Y-axis maximum.
    static double niceYMax(double raw);

    /// Draw grid lines over a rectangular area.
    void drawGrid(QPainter& p, const QRect& area);

    /// Draw one half of the graph (upload or download).
    void drawHalf(QPainter& p, const QRect& plotArea, const std::deque<double>& data,
                  double yMax, const QColor& color, bool isUpload,
                  const QString& rateText);

    std::deque<double> m_downData;
    std::deque<double> m_upData;
    int m_maxPoints = 900; // 15 min * 60s

    double m_minUpScale = 100.0;   // minimum Y scale for upload (KB/s)
    double m_minDownScale = 300.0; // minimum Y scale for download (KB/s)

    // Scrolling grid
    QTimer m_gridTimer;
    int m_gridXOffset = 0;
};

} // namespace eMule
