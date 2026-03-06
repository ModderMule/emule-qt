#include "pch.h"
/// @file SettingsUtils.cpp
/// @brief Settings class implementation.

#include "SettingsUtils.h"
#include "PathUtils.h"

#include <QDir>

namespace eMule {

Settings::Settings()
    : m_settings(QDir(appDirectory(AppDir::Config)).filePath(QStringLiteral("emule.ini")),
                 QSettings::IniFormat)
{
}

Settings::Settings(const QString& filePath)
    : m_settings(filePath, QSettings::IniFormat)
{
}

bool Settings::contains(const QString& key) const
{
    return m_settings.contains(key);
}

void Settings::remove(const QString& key)
{
    m_settings.remove(key);
}

void Settings::sync()
{
    m_settings.sync();
}

QString Settings::filePath() const
{
    return m_settings.fileName();
}

} // namespace eMule
