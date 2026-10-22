#include "pch.h"
/// @file TorrentPieceGrid.cpp
/// @brief Square piece grid of the torrent inspector — implementation.

#include "controls/TorrentPieceGrid.h"

#include <QPainter>

#include <cmath>

namespace eMule {

namespace {

QColor mix(const QColor& from, const QColor& to, double share)
{
    auto m = [share](int a, int b) { return static_cast<int>(a + (b - a) * share); };
    return {m(from.red(), to.red()), m(from.green(), to.green()), m(from.blue(), to.blue())};
}

/// availability: this many peers per piece reads as "plenty"
constexpr int kAvailableFull = 10;

} // namespace

TorrentPieceGrid::TorrentPieceGrid(QWidget* parent)
    : QWidget(parent)
{
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setMinimumSize(120, 120);
}

void TorrentPieceGrid::setProgressMap(const QByteArray& map)
{
    if (map == m_progress)
        return;
    m_progress = map;
    if (m_mode == Mode::Progress)
        update();
}

void TorrentPieceGrid::setAvailabilityMap(const QByteArray& map)
{
    if (map == m_availability)
        return;
    m_availability = map;
    if (m_mode == Mode::Available)
        update();
}

void TorrentPieceGrid::setMode(Mode mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;
    update();
}

int TorrentPieceGrid::gridSide(int buckets)
{
    return buckets <= 0 ? 0 : static_cast<int>(std::ceil(std::sqrt(static_cast<double>(buckets))));
}

QColor TorrentPieceGrid::cellColor(quint8 value) const
{
    // the active group: an inactive window's highlight is grey on macOS
    const QColor empty = palette().color(QPalette::Active, QPalette::Base);
    const QColor accent = palette().color(QPalette::Active, QPalette::Highlight);
    if (m_mode == Mode::Progress)
        return value == 0 ? empty : mix(empty, accent, 0.25 + 0.75 * value / 255.0);
    // nobody has it: red, as the part bars draw a missing part
    if (value == 0)
        return {220, 60, 60};
    return mix(empty, accent, 0.25 + 0.75 * std::min<int>(value, kAvailableFull) / kAvailableFull);
}

QSize TorrentPieceGrid::sizeHint() const
{
    return {180, 180};
}

void TorrentPieceGrid::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const int side = std::min(width(), height());
    const QRect area((width() - side) / 2, 0, side, side);
    const QColor grid = palette().color(QPalette::Mid);
    p.fillRect(area, grid);

    const QByteArray& map = shown();
    const int n = gridSide(static_cast<int>(map.size()));
    if (n == 0) {
        p.fillRect(area.adjusted(1, 1, -1, -1), palette().color(QPalette::Base));
        return;
    }
    // integer cell edges: no gaps of rounding between cells
    for (int i = 0; i < n * n; ++i) {
        const int row = i / n;
        const int col = i % n;
        const int x0 = area.left() + 1 + col * (side - 1) / n;
        const int x1 = area.left() + 1 + (col + 1) * (side - 1) / n;
        const int y0 = area.top() + 1 + row * (side - 1) / n;
        const int y1 = area.top() + 1 + (row + 1) * (side - 1) / n;
        const QRect cell(x0, y0, std::max(1, x1 - x0 - 1), std::max(1, y1 - y0 - 1));
        const QColor c = i < map.size() ? cellColor(static_cast<quint8>(map.at(i)))
                                        : palette().color(QPalette::Window);
        p.fillRect(cell, c);
    }
}

const QByteArray& TorrentPieceGrid::shown() const
{
    return m_mode == Mode::Progress ? m_progress : m_availability;
}

} // namespace eMule
