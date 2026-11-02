#pragma once

/// @file ToolbarSkin.h
/// @brief Toolbar icons from a bitmap strip and a skin profile.
///
/// Port of CMuleToolbarCtrl::ChangeToolbarBitmap (srchybrid/MuleToolBarCtrl.cpp:520).
/// A strip holds square tiles in ToolbarTile order: MFC's 14, then optionally ours.

#include "app/ToolbarButtons.h"

#include <QIcon>
#include <QImage>
#include <QList>
#include <QString>

namespace eMule {

class SkinEngine;

class ToolbarSkin {
public:
    /// Loads a strip; a path already tried is not read again, even if it failed.
    /// @return false when the file is unreadable or not a strip (the tiles stay empty).
    bool loadStrip(const QString& path);
    void clearStrip();

    [[nodiscard]] const QString& stripPath() const { return m_stripPath; }
    [[nodiscard]] bool hasStrip() const { return !m_tiles.isEmpty(); }
    [[nodiscard]] qsizetype stripTileCount() const { return m_tiles.size(); }

    /// Strip tile, else the profile's [Icons] entry, else a null icon (built-in).
    [[nodiscard]] QIcon icon(ToolbarTile tile, const SkinEngine& profile) const;

    /// Cuts a strip into square tiles, tile size = strip height. MFC demands exactly
    /// 14 tiles; we take 14 or more so our extra tiles fit. Anything else → empty.
    /// Images without alpha get MFC's magenta (255,0,255) transparency key.
    [[nodiscard]] static QList<QImage> sliceStrip(const QImage& strip);

private:
    QString m_stripPath;
    QList<QIcon> m_tiles;
};

} // namespace eMule
