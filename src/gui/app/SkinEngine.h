#pragma once

/// @file SkinEngine.h
/// @brief INI-based skin profile engine — icon, bitmap, and color overrides.
///
/// Parses MFC-compatible skin profile INI files with [Icons], [Bitmaps],
/// and [Colors] sections. Used by MainWindow::applySkinProfile().
/// Port of the skin loading code in srchybrid/Emule.cpp.

#include <QColor>
#include <QIcon>
#include <QMap>
#include <QPixmap>
#include <QString>

#include <optional>

namespace eMule {

class SkinEngine {
public:
    SkinEngine() = default;

    /// Load a skin profile from an INI file.
    /// Reads [Icons], [Bitmaps], and [Colors] sections.
    void loadProfile(const QString& iniPath);

    /// Clear all overrides (return to default resources).
    void clear();

    /// @return true if a skin profile is currently loaded.
    [[nodiscard]] bool isActive() const { return !m_iniPath.isEmpty(); }

    /// Get an icon override. Returns @p fallback if no override exists.
    [[nodiscard]] QIcon icon(const QString& resourceName, const QIcon& fallback = {}) const;

    /// Get a pixmap/bitmap override. Returns a null QPixmap if no override exists.
    [[nodiscard]] QPixmap pixmap(const QString& resourceName) const;

    /// Get a color override. Returns std::nullopt if no override exists.
    [[nodiscard]] std::optional<QColor> color(const QString& key) const;

    /// @return The directory containing the skin profile INI (for relative paths).
    [[nodiscard]] const QString& skinDir() const { return m_skinDir; }

private:
    QString m_iniPath;
    QString m_skinDir;
    QMap<QString, QString> m_icons;    ///< [Icons] resourceName → file path
    QMap<QString, QString> m_bitmaps;  ///< [Bitmaps] resourceName → file path
    QMap<QString, QColor>  m_colors;   ///< [Colors] key → QColor

    [[nodiscard]] QString resolveResourcePath(const QString& value) const;
};

} // namespace eMule
