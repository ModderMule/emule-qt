#pragma once

/// @file SkinInstaller.h
/// @brief Downloads published skin bundles (manifest "skins") into <config>/skins/<id>/.
///
/// A bundle is a zip with one *.eMuleSkin.ini at its top or in one top folder
/// (scripts/publish-skin.py checks the same). Installed bundles carry
/// .skinrelease.json with the manifest sha256/date; an update check that lists
/// another sha256 for an installed id re-installs it (updateInstalled).

#include "app/VersionManifest.h"   // src/core

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace eMule {

class SkinInstaller : public QObject {
    Q_OBJECT

public:
    /// What .skinrelease.json of an installed bundle records.
    struct Installed {
        QString id;
        QString name;
        QString sha256;
        QDateTime date;
    };

    explicit SkinInstaller(QObject* parent = nullptr);

    /// <config>/skins; may not exist yet.
    [[nodiscard]] static QString skinsRoot();
    /// <config>/skins/<id>
    [[nodiscard]] static QString skinDir(const QString& id);
    /// Installed release of @p id, nullopt when not installed from the manifest.
    [[nodiscard]] static std::optional<Installed> installed(const QString& id);
    /// *.eMuleSkin.ini of installed @p id, empty if none.
    [[nodiscard]] static QString profilePath(const QString& id);
    /// First *.eMuleToolbar.kad02.{png,bmp,gif} in @p dir or one level below, empty if none.
    [[nodiscard]] static QString toolbarStripPath(const QString& dir);
    /// Id of the installed bundle holding @p path, empty when it is no bundle file.
    [[nodiscard]] static QString bundleIdOf(const QString& path);

    [[nodiscard]] bool isBusy(const QString& id) const { return m_busy.contains(id); }

    /// Download, verify and unpack @p release; replaces an installed copy. Ends in finished().
    void install(const SkinRelease& release);

    /// Re-install every installed bundle whose sha256 differs from @p releases.
    void updateInstalled(const QList<SkinRelease>& releases);

signals:
    void progress(const QString& id, qint64 received, qint64 total);
    void finished(const QString& id, bool ok, const QString& error);

private:
    void onDownloaded(QNetworkReply* reply, const SkinRelease& release);
    /// Unpack @p zipPath into skinDir(id). @return error text, empty on success.
    [[nodiscard]] QString unpack(const SkinRelease& release, const QString& zipPath);
    void fail(const QString& id, const QString& error);

    QNetworkAccessManager* m_nam = nullptr;
    QSet<QString> m_busy;
};

} // namespace eMule
