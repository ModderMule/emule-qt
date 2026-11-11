#pragma once

/// @file SkinEngine.h
/// @brief INI-based skin profile engine — icon, bitmap, and color overrides.
///
/// Parses MFC skin profiles (*.eMuleSkin.ini) with [Icons], [Bitmaps] and [Colors]
/// sections. Lookups are case-insensitive; relative paths resolve against the
/// profile's folder. Port of the skin loading code in srchybrid/Emule.cpp.
/// Used for the toolbar, its MainToolBarBk background, [Icons] of every window
/// (SkinIcons), [Colors] and the list watermarks <key>BkImg (SkinColors).

#include <QColor>
#include <QHash>
#include <QIcon>
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

    /// All readable [Icons] entries: lower-cased resource name → file.
    [[nodiscard]] QHash<QString, QString> iconFiles() const;

    /// Get a pixmap/bitmap override. Returns a null QPixmap if no override exists.
    [[nodiscard]] QPixmap pixmap(const QString& resourceName) const;

    /// Resolved file of a [Bitmaps] entry, empty if none or it cannot be read.
    [[nodiscard]] QString bitmapPath(const QString& resourceName) const;

    /// Get a color override. Returns std::nullopt if no override exists.
    [[nodiscard]] std::optional<QColor> color(const QString& key) const;

    /// All [Colors] entries, keys lower-cased.
    [[nodiscard]] const QHash<QString, QColor>& colors() const { return m_colors; }

    /// [Colors] <key>BkImg list watermarks: lower-cased key → readable file.
    [[nodiscard]] const QHash<QString, QString>& listImages() const { return m_listImages; }

    /// [Skin] @p key of the profile at @p iniPath, else that key in any section; empty if none.
    [[nodiscard]] static QString profileValue(const QString& iniPath, const QString& key);

    /// Preview image of a skin bundle: preview/overview.png next to the profile, else
    /// its [Skin] Preview= file. Empty when neither is a readable image.
    [[nodiscard]] static QString previewImage(const QString& iniPath);

    /// @return The directory containing the skin profile INI (for relative paths).
    [[nodiscard]] const QString& skinDir() const { return m_skinDir; }

private:
    QString m_iniPath;
    QString m_skinDir;
    QHash<QString, QString> m_icons;    ///< [Icons] lower-case resourceName → file spec
    QHash<QString, QString> m_bitmaps;  ///< [Bitmaps] lower-case resourceName → file spec
    QHash<QString, QColor>  m_colors;   ///< [Colors] lower-case key → QColor
    QHash<QString, QString> m_listImages;   ///< [Colors] lower-case <key>BkImg → file

    /// section → key → value, both lower-cased
    [[nodiscard]] static QHash<QString, QHash<QString, QString>> parseIni(const QString& text);
    [[nodiscard]] static QString decodeIni(const QByteArray& raw);
    [[nodiscard]] QString resolveResourcePath(const QString& value) const;
};

} // namespace eMule
