#pragma once

/// @file SettingsUtils.h
/// @brief Portable settings storage replacing Windows registry access.
///
/// Wraps QSettings with INI format for cross-platform persistence.
/// Replaces ~18 Windows registry calls across 8 files.

#include <QSettings>
#include <QString>
#include <QVariant>

#include <optional>

namespace eMule {

/// Cross-platform settings store backed by a QSettings INI file.
/// Replaces direct Windows registry access (RegOpenKeyEx / RegSetValueEx).
class Settings {
public:
    /// Construct with default INI path (inside appDirectory(AppDir::Config)).
    Settings();

    /// Construct with an explicit INI file path.
    explicit Settings(const QString& filePath);

    /// Read a typed value.  Returns @p defaultValue if key does not exist.
    template <typename T>
    [[nodiscard]] T value(const QString& key, const T& defaultValue = {}) const
    {
        return m_settings.value(key, QVariant::fromValue(defaultValue)).template value<T>();
    }

    /// Read a typed value, returning std::nullopt if the key does not exist.
    template <typename T>
    [[nodiscard]] std::optional<T> optionalValue(const QString& key) const
    {
        if (!m_settings.contains(key))
            return std::nullopt;
        return m_settings.value(key).template value<T>();
    }

    /// Write a typed value.
    template <typename T>
    void setValue(const QString& key, const T& val)
    {
        m_settings.setValue(key, QVariant::fromValue(val));
    }

    /// Check whether a key exists.
    [[nodiscard]] bool contains(const QString& key) const;

    /// Remove a key.
    void remove(const QString& key);

    /// Flush pending writes to disk.
    void sync();

    /// Return the underlying file path.
    [[nodiscard]] QString filePath() const;

private:
    mutable QSettings m_settings;
};

} // namespace eMule
