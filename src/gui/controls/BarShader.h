#pragma once

/// @file BarShader.h
/// @brief Port of MFC CBarShader (srchybrid/BarShader.cpp): byte-range spans
/// drawn into a bar, flat or with the Display page's 3D shading.
///
/// Spans narrower than a pixel are blended into one averaged column, so a
/// fragmented file still reads true at any width.

#include <QColor>
#include <QRect>

#include <cstdint>
#include <map>
#include <vector>

class QPainter;

namespace eMule {

class BarShader {
public:
    explicit BarShader(uint64_t fileSize = 1);

    /// Resets the bar to one span of the current fill colour (black).
    void setFileSize(uint64_t fileSize);
    [[nodiscard]] uint64_t fileSize() const { return m_fileSize; }

    /// Whole range to @p color.
    void fill(const QColor& color);

    /// [start, end) to @p color; later ranges overwrite earlier ones.
    void fillRange(uint64_t start, uint64_t end, const QColor& color);

    /// Draw into @p rect. @p level is the 3D depth (1..5); ignored when @p flat.
    void draw(QPainter& painter, const QRect& rect, bool flat, int level) const;

    /// Draw with the Display page's style (thePrefs.depth3D()).
    void draw(QPainter& painter, const QRect& rect) const;

    /// One solid rect, shaded unless @p flat. Black is never shaded, as in MFC.
    static void fillBarRect(QPainter& painter, const QRect& rect, const QColor& color,
                            bool flat, int level);

    /// MFC BuildModifiers(): brightness factor per row from the edge to the middle.
    [[nodiscard]] static std::vector<float> shadeModifiers(int height, int level);

private:
    uint64_t m_fileSize = 1;
    std::map<uint64_t, QColor> m_spans;  ///< span start → colour; key m_fileSize ends the bar
};

/// MFC CPreferences::UseFlatBar(): slider at 0.
[[nodiscard]] bool useFlatBar();

/// The slider value, 0 (flat) .. 5 (round).
[[nodiscard]] int barDepth3D();

} // namespace eMule
