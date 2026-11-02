#pragma once

/// @file SkinIcons.h
/// @brief Skin profile [Icons] for the built-in icons of every window.
///
/// Port of CemuleApp::LoadIcon under a skin profile (srchybrid/Emule.cpp): [Icons]
/// <ResourceName>=file, keyed by the icon names of srchybrid/emule.rc. Our own icons
/// (Usenet, Torrent, Hoster...) use their file name. skinIcon() resolves the
/// override when painted, so a profile switch only needs a repaint.

#include <QHash>
#include <QIcon>
#include <QObject>
#include <QString>
#include <QStringList>

class QLabel;

namespace eMule {

/// Built-in icon (":/icons/X.ico", ":/smileys/X.ico") that follows the skin profile.
[[nodiscard]] QIcon skinIcon(const QString& resourcePath);

/// QLabel showing a built-in icon; redrawn on a profile change.
void setSkinPixmap(QLabel* label, const QString& resourcePath, int size = 16);

class SkinIcons : public QObject {
    Q_OBJECT

public:
    static SkinIcons& instance();

    /// [Icons] lower-cased resource name → readable file; empty = no skin.
    /// Emits changed() if anything changed; @return true then.
    bool setFiles(const QHash<QString, QString>& files);

    /// Skin file of a built-in icon, empty if not skinned.
    [[nodiscard]] QString file(const QString& resourcePath) const;

    /// Skinned or built-in icon.
    [[nodiscard]] QIcon resolve(const QString& resourcePath);

    /// Bumped on every change.
    [[nodiscard]] quint64 generation() const { return m_generation; }

    /// True once per change for @p seen: clear caches built from icons then.
    bool changedSince(quint64& seen) const;

    /// [Icons] keys of a built-in icon: emule.rc names, else the file's base name.
    [[nodiscard]] static QStringList resourceNames(const QString& resourcePath);

signals:
    void changed();

private:
    QHash<QString, QString> m_files;
    QHash<QString, QIcon> m_cache;   ///< resource path → resolved icon
    quint64 m_generation = 0;
};

} // namespace eMule
