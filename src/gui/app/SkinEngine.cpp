#include "pch.h"
/// @file SkinEngine.cpp
/// @brief INI-based skin profile engine implementation.
///
/// Port of the skin loading logic from srchybrid/Emule.cpp (LoadIcon, LoadImage,
/// LoadSkinColor — lines 1218-1400).

#include "app/SkinEngine.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace eMule {

// ---------------------------------------------------------------------------
// loadProfile
// ---------------------------------------------------------------------------

void SkinEngine::loadProfile(const QString& iniPath)
{
    clear();

    if (iniPath.isEmpty() || !QFileInfo::exists(iniPath))
        return;

    m_iniPath = iniPath;
    m_skinDir = QFileInfo(iniPath).absolutePath();

    QSettings ini(iniPath, QSettings::IniFormat);

    // [Icons] section — resourceName = path/to/icon.ico
    ini.beginGroup(QStringLiteral("Icons"));
    for (const auto& key : ini.childKeys()) {
        const QString value = ini.value(key).toString().trimmed();
        if (!value.isEmpty())
            m_icons.insert(key, value);
    }
    ini.endGroup();

    // [Bitmaps] section — resourceName = path/to/bitmap.bmp
    ini.beginGroup(QStringLiteral("Bitmaps"));
    for (const auto& key : ini.childKeys()) {
        const QString value = ini.value(key).toString().trimmed();
        if (!value.isEmpty())
            m_bitmaps.insert(key, value);
    }
    ini.endGroup();

    // [Colors] section — key = R,G,B  or  key = #RRGGBB
    ini.beginGroup(QStringLiteral("Colors"));
    for (const auto& key : ini.childKeys()) {
        const QString value = ini.value(key).toString().trimmed();
        if (value.isEmpty())
            continue;

        QColor color;
        if (value.startsWith(QLatin1Char('#'))) {
            color = QColor::fromString(value);
        } else {
            // Parse R,G,B format
            const auto parts = value.split(QLatin1Char(','));
            if (parts.size() >= 3) {
                color = QColor(parts[0].trimmed().toInt(),
                               parts[1].trimmed().toInt(),
                               parts[2].trimmed().toInt());
            }
        }
        if (color.isValid())
            m_colors.insert(key, color);
    }
    ini.endGroup();
}

// ---------------------------------------------------------------------------
// clear
// ---------------------------------------------------------------------------

void SkinEngine::clear()
{
    m_iniPath.clear();
    m_skinDir.clear();
    m_icons.clear();
    m_bitmaps.clear();
    m_colors.clear();
}

// ---------------------------------------------------------------------------
// icon
// ---------------------------------------------------------------------------

QIcon SkinEngine::icon(const QString& resourceName, const QIcon& fallback) const
{
    auto it = m_icons.find(resourceName);
    if (it == m_icons.end())
        return fallback;

    const QString path = resolveResourcePath(it.value());
    if (path.isEmpty() || !QFileInfo::exists(path))
        return fallback;

    return QIcon(path);
}

// ---------------------------------------------------------------------------
// pixmap
// ---------------------------------------------------------------------------

QPixmap SkinEngine::pixmap(const QString& resourceName) const
{
    auto it = m_bitmaps.find(resourceName);
    if (it == m_bitmaps.end())
        return {};

    const QString path = resolveResourcePath(it.value());
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {};

    return QPixmap(path);
}

// ---------------------------------------------------------------------------
// color
// ---------------------------------------------------------------------------

std::optional<QColor> SkinEngine::color(const QString& key) const
{
    auto it = m_colors.find(key);
    if (it == m_colors.end())
        return std::nullopt;
    return it.value();
}

// ---------------------------------------------------------------------------
// resolveResourcePath — resolve relative paths against skin directory
// ---------------------------------------------------------------------------

QString SkinEngine::resolveResourcePath(const QString& value) const
{
    if (value.isEmpty())
        return {};

    // Strip optional icon index (e.g., "file.dll,3" — only support image files)
    QString path = value;
    const int commaIdx = path.lastIndexOf(QLatin1Char(','));
    if (commaIdx > 0) {
        // Check if everything after the comma is a number (icon index)
        bool isIndex = false;
        QStringView(path).mid(commaIdx + 1).trimmed().toInt(&isIndex);
        if (isIndex)
            path = path.left(commaIdx);
    }

    // If absolute, use as-is
    if (QDir::isAbsolutePath(path))
        return path;

    // Resolve relative to skin directory
    return m_skinDir + QLatin1Char('/') + path;
}

} // namespace eMule
