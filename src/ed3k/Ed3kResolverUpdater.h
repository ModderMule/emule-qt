#pragma once

/// @file Ed3kResolverUpdater.h
/// @brief Fetches newer hoster resolver plugins (resolver.zip) that the update manifest names
/// (docs/hoster.md "Plugin updates").
///
/// - Every `ed3k: hoster: autoUpdateHours` (YAML only, default 24) the manifest's
///   "hosterResolver" entry is read. It is taken only when its "api" is the sidecar protocol this
///   daemon speaks (Ed3kJdResolver::kProtocol) and its jdRevision is newer than the bundled zip
///   and the one fetched before. No "api": never taken.
/// - The zip goes to <dir>/resolver-r<rev>.zip, checked by size and SHA-256, then by its own
///   resolver/api.txt, revision.txt and layout. update.json keeps what was fetched and when.
/// - zipToUse() picks the fetched zip while it is newer than the bundled one and of our api,
///   else the bundled one. Swapping it in is the resolver's job: it waits until its sidecar
///   ends, so running hoster downloads and checks are never cut (Ed3kJdResolver::setConfig).
/// - No zip at all (release bundles ship none): nothing at start; fetchFirst() once a hoster
///   link wants plugins (Ed3kJdResolver::pluginsNeeded), also with autoUpdate off.
/// - Off (pref, or hoster downloads off): nothing is checked, a running fetch is stopped.
/// - Shared (`sharedPluginDir`): <dir> is <sharedPluginDir>/update for all instances. One
///   fetches at a time (.fetch.lock); the others wait, then take its zip from update.json, and
///   each tick reads update.json again, so one check per interval serves every instance.

#ifdef EMULE_HAVE_HOSTER_JD

#include "app/VersionManifest.h"
#include "net/ScheduledUpdater.h"

#include <QDateTime>
#include <QLockFile>
#include <QNetworkProxy>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace eMule {
class GuardedNetworkAccessManager;
class HttpStreamDownload;
}

namespace eMule::ed3k {

class Ed3kResolverUpdater : public ScheduledUpdater {
    Q_OBJECT

public:
    static constexpr qint64 kMaxZipBytes = 256ll * 1024 * 1024;
    static constexpr qint64 kFirstRetrySecs = 10 * 60;
    /// another instance holds the fetch lock: try again after this
    static constexpr int kLockRetryMs = 30 * 1000;

    /// @p dir: where fetched zips and update.json live (<config>/ed3k/resolver/update)
    explicit Ed3kResolverUpdater(QString dir, QObject* parent = nullptr);
    ~Ed3kResolverUpdater() override;

    /// Another folder (shared on/off): a running fetch stops. A fetched zip of the old folder
    /// newer than any in the new one is copied along; the old folder is left to the caller
    void setDir(const QString& dir);
    [[nodiscard]] QString dir() const { return m_dir; }
    /// @p on: autoUpdate and hoster downloads on. @p hours: check interval
    void setSchedule(bool on, int hours);
    void setProxy(const QNetworkProxy& proxy) { m_proxy = proxy; }
    /// The zip that ships with the application; empty: none
    void setBundledZip(const QString& zip);
    /// Tests: a local manifest; default VersionManifest::manifestUrl()
    void setManifestUrl(const QUrl& url) { m_manifestUrl = url; }
    /// Tests: wait for another instance's fetch this long; default kLockRetryMs
    void setLockRetryMs(int ms) { m_lockRetry.setInterval(ms); }

    /// The fetched zip when newer than the bundled one and of our api, else the bundled one
    [[nodiscard]] QString zipToUse() const;
    /// The fetched zip and its revision; empty / 0: none
    [[nodiscard]] QString fetchedZip() const;
    [[nodiscard]] int fetchedRevision() const { return m_fetchedRevision; }
    [[nodiscard]] QDateTime lastChecked() const { return m_checked; }
    [[nodiscard]] bool busy() const;

    /// Check now: ignores the interval and the back-off (still nothing while off)
    void checkNow();
    void checkSchedule() override;
    /// No zip yet and a link wants one: fetch now, whatever the schedule. False: not fetching
    /// (a zip is there, or the last try was under kFirstRetrySecs ago)
    bool fetchFirst();
    void stop() override;

    /// Whether @p r is worth fetching over @p haveRevision; @p why: the reason it is not (log)
    [[nodiscard]] static bool wanted(const HosterResolverRelease& r, int haveRevision, QString* why = nullptr);

signals:
    /// A new zip is fetched and checked: zipToUse() changed
    void zipReady();
    /// One check ended (tests, log); @p ok false: the manifest or the zip failed
    void checked(bool ok, const QString& message);

private:
    void fetchManifest();
    void onManifest(const QByteArray& json);
    void startZip(const HosterResolverRelease& r);
    void onZipDone(bool ok);
    void finish(bool ok, const QString& message);
    void loadState();
    void saveState() const;
    /// update.json again (another instance may have fetched). True: zipToUse() changed
    bool reloadState();
    /// False: another instance fetches; retried after kLockRetryMs
    bool takeFetchLock();
    void clearFetched();
    /// Removes fetched zips but @p keep and the previous one (the resolver may still use it)
    void pruneZips(const QString& keep) const;
    [[nodiscard]] int haveRevision() const;

    QString m_dir;
    bool m_on = false;
    bool m_started = false;
    int m_hours = 24;
    QNetworkProxy m_proxy{QNetworkProxy::DefaultProxy};
    QUrl m_manifestUrl;
    QString m_bundled;
    int m_bundledRevision = 0;
    QDateTime m_checked;          ///< last good check
    QString m_fetched;            ///< file name in m_dir; empty: none
    int m_fetchedRevision = 0;
    int m_fetchedApi = 0;
    QString m_fetchedSha;
    QDateTime m_firstTried;           ///< last fetchFirst()
    HosterResolverRelease m_wanted;   ///< while the zip downloads
    std::unique_ptr<QLockFile> m_fetchLock;   ///< held from manifest to zip done
    QTimer m_lockRetry;                       ///< waiting for another instance's fetch
    std::unique_ptr<GuardedNetworkAccessManager> m_nam;
    std::unique_ptr<HttpStreamDownload> m_dl;
};

} // namespace eMule::ed3k

#endif // EMULE_HAVE_HOSTER_JD
