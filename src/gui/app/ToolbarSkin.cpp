#include "pch.h"
/// @file ToolbarSkin.cpp
/// @brief Toolbar strip slicing and icon lookup.

#include "app/ToolbarSkin.h"

#include "app/SkinEngine.h"

#include <QPixmap>

namespace eMule {

bool ToolbarSkin::loadStrip(const QString& path)
{
    if (path == m_stripPath)
        return hasStrip();

    clearStrip();
    m_stripPath = path;

    const QList<QImage> tiles = sliceStrip(QImage(path));
    for (const QImage& tile : tiles)
        m_tiles.append(QIcon(QPixmap::fromImage(tile)));
    return hasStrip();
}

void ToolbarSkin::clearStrip()
{
    m_stripPath.clear();
    m_tiles.clear();
}

QIcon ToolbarSkin::icon(ToolbarTile tile, const SkinEngine& profile) const
{
    const auto index = static_cast<qsizetype>(tile);
    if (index < m_tiles.size())
        return m_tiles[index];
    if (index < kToolbarTileCount)
        return profile.icon(QLatin1String(kToolbarTiles[index].skinKey));
    return {};
}

QList<QImage> ToolbarSkin::sliceStrip(const QImage& strip)
{
    const int size = strip.height();
    if (strip.isNull() || size <= 0 || strip.width() % size != 0)
        return {};
    const int count = strip.width() / size;
    if (count < kOfficialToolbarTiles)
        return {};

    QImage image = strip.convertToFormat(QImage::Format_ARGB32);

    // MFC keys out magenta unless the bitmap is 32 bpp. A 32 bpp BMP with an all-zero
    // alpha byte is really 24 bpp in disguise: Qt would draw it fully transparent.
    bool useAlpha = strip.hasAlphaChannel();
    if (useAlpha) {
        useAlpha = false;
        for (int y = 0; y < image.height() && !useAlpha; ++y) {
            const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                if (qAlpha(line[x]) != 0) {
                    useAlpha = true;
                    break;
                }
            }
        }
    }
    if (!useAlpha) {
        for (int y = 0; y < image.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const QRgb px = line[x] | 0xFF000000u;
                line[x] = (px & 0x00FFFFFFu) == 0x00FF00FFu ? 0u : px;
            }
        }
    }

    // tiles past our last known one belong to a newer layout: ignore them
    const int used = std::min(count, kToolbarTileCount);
    QList<QImage> tiles;
    tiles.reserve(used);
    for (int i = 0; i < used; ++i)
        tiles.append(image.copy(i * size, 0, size, size));
    return tiles;
}

} // namespace eMule
