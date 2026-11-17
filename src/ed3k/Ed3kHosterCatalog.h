#pragma once

/// @file Ed3kHosterCatalog.h
/// @brief What the Add account dialogs offer, as JDownloader's: every resolver plugin that
/// takes a premium account (hosters, multi-hosters, captcha solvers) and the sites' favicons.
///
/// Both come from the resolver and are kept in Config/ed3k/hoster/: accounthosts.json (asked
/// again once per run, the plugins may have changed) and favicons/<host>.png (.none: the site
/// has none, asked again after kNoneDays). The server country of a site: its host's A/AAAA
/// records; behind a CDN its subdomains and aliases the plugin names (countries.json, kCountryDays).
/// The servers downloads of a site really came from go first. sites.json per site: those servers
/// with their last seen time (kept for good) and each login's last check (Options status).
/// Newest asks go first: the rows in view. Daemon thread only.

#include "Ed3kResolver.h"

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

#include <deque>
#include <functional>

namespace eMule::ed3k {

class Ed3kHosterCatalog : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxFetches = 10;     ///< favicon tasks at once
    static constexpr int kFetchTimeoutMs = 20000;   ///< a dead site: none, the slot freed
    static constexpr int kMaxLookups = 8;      ///< sites resolved at once
    static constexpr int kCountryDays = 7;     ///< a country is looked up again after
    static constexpr int kNoneDays = 7;        ///< a site without an icon is asked again after
    static constexpr int kRefreshDays = 30;    ///< an icon is fetched again after
    static constexpr int kServerNames = 4;     ///< newest download servers resolved for a flag
    static constexpr qint64 kAccountWriteS = 3600;   ///< the same check result: its time written after

    explicit Ed3kHosterCatalog(const QString& configDir, QObject* parent = nullptr);
    ~Ed3kHosterCatalog() override;

    void setResolver(Ed3kResolver* resolver);

    /// The plugins that take an account, as last known (empty: none yet). Asks the
    /// resolver once per run; *asking: an accountHostsChanged follows.
    [[nodiscard]] QList<AccountHost> accountHosts(bool* asking = nullptr);

    /// The icons of @p hosts there are (PNG); the others are fetched, iconReady follows
    /// for each. Hosts that are no domain are skipped.
    [[nodiscard]] QHash<QString, QByteArray> icons(const QStringList& hosts);

    /// IP2Country: @p ready false = no database (no lookups), @p countryOf an address's code.
    void setGeo(std::function<bool()> ready, std::function<QString(const QHostAddress&)> countryOf);
    /// A name's addresses (A and AAAA); empty when it has none. Default: the system resolver.
    using DnsLookup = std::function<void(const QString& name, QObject* context, std::function<void(QList<QHostAddress>)> done)>;
    void setDnsLookup(DnsLookup lookup);
    /// false: no DNS (a proxy is set, names must not reach the local resolver)
    void setDnsAllowed(bool allowed) { m_dnsAllowed = allowed; }

    /// The server countries of @p hosts known (ISO code, "" = none: CDN only); the others are
    /// looked up, countryReady follows for each.
    [[nodiscard]] QHash<QString, QString> countries(const QStringList& hosts);

    /// A download of @p site (a link's host) got its bytes from @p server: resolved first for
    /// the site's flag from now on; a new name looks the country up again.
    void noteServer(const QString& site, const QString& server);
    /// The download servers known for @p host's site, newest first.
    [[nodiscard]] QStringList servers(const QString& host);

    /// The last check of login @p user at @p host (hoster, multi-hoster, solver): kept with a
    /// "checked" time (s) across restarts.
    void noteAccount(const QString& host, const QString& user, QJsonObject status);
    /// empty: never checked
    [[nodiscard]] QJsonObject account(const QString& host, const QString& user);
    /// Forgets the checks of logins not in @p logins (host, user): removed ones.
    void keepAccounts(const QList<std::pair<QString, QString>>& logins);

    /// An address of a CDN or DDoS shield (isCdnAddress): says nothing about the site's country.
    [[nodiscard]] static bool behindCdn(const QHostAddress& address);

    /// Whether @p host can be a file name and a domain: [a-z0-9.-], a dot, no "..".
    [[nodiscard]] static bool validHost(const QString& host);

signals:
    void accountHostsChanged(const QList<eMule::ed3k::AccountHost>& hosts);
    /// @p png empty: the site has none (or it could not be fetched now)
    void iconReady(const QString& host, const QByteArray& png);
    /// @p cc empty: none (CDN only)
    void countryReady(const QString& host, const QString& cc);

private:
    void onTaskEvent(Ed3kResolver::TaskId task, const ResolverEvent& ev);
    void onStateChanged();
    void askHosts();
    void pump();
    void finishFetch(Ed3kResolver::TaskId task, const ResolverEvent& ev);
    void pumpLookups();
    void resolveStage(const QString& host, const QStringList& names);
    void onResolved(const QString& host, int index, const QList<QHostAddress>& addresses);
    void finishLookup(const QString& host, const QString& cc, bool known);
    void loadCountries();
    void saveCountries();
    [[nodiscard]] QStringList subsOf(const QString& host);
    /// The plugin host @p host belongs to (its own, an alias, a subdomain); else @p host without "www."
    [[nodiscard]] QString siteOf(const QString& host);
    [[nodiscard]] static bool validServer(const QString& name);
    void loadSites();
    void saveSites();   ///< batched
    void writeSites();
    /// @p hosts (in view order) to the front of @p queue
    static void toFront(std::deque<QString>& queue, QSet<QString>& queued, const QStringList& hosts);
    void loadHosts();
    void saveHosts();
    [[nodiscard]] QString dir() const;
    [[nodiscard]] QString iconFile(const QString& host, bool none) const;

    QString m_configDir;
    QPointer<Ed3kResolver> m_resolver;
    QList<AccountHost> m_hosts;
    bool m_hostsLoaded = false;
    bool m_hostsAsked = false;         ///< this run: the resolver answered with a list
    Ed3kResolver::TaskId m_hostsTask = 0;
    bool m_waitAvailable = false;      ///< asked while the resolver was not ready: ask again
    std::deque<QString> m_queue;       ///< favicons to fetch
    QSet<QString> m_queued;            ///< in m_queue or running
    QSet<QString> m_failedRun;         ///< no icon this run (network, no resolver)
    QHash<Ed3kResolver::TaskId, QString> m_fetches;

    struct Lookup {
        bool subs = false;                    ///< second stage: the plugin's other names
        QStringList names;
        QList<QList<QHostAddress>> results;   ///< per name
        int pending = 0;
        bool sawCdn = false;
    };
    std::function<bool()> m_geoReady;
    std::function<QString(const QHostAddress&)> m_countryOf;
    DnsLookup m_dns;
    bool m_dnsAllowed = true;
    bool m_countriesLoaded = false;
    bool m_countriesDirty = false;
    QHash<QString, std::pair<QString, qint64>> m_countries;   ///< host -> code ("~" CDN only), when (s)
    std::deque<QString> m_lookupQueue;
    QSet<QString> m_lookupQueued;      ///< in m_lookupQueue or running
    QSet<QString> m_lookupFailedRun;   ///< no answer this run
    QHash<QString, Lookup> m_lookups;  ///< running
    struct Site {
        QHash<QString, qint64> servers;        ///< download server -> last seen (s)
        QHash<QString, QJsonObject> accounts;  ///< user -> last check
    };
    bool m_sitesLoaded = false;
    bool m_sitesDirty = false;
    QHash<QString, Site> m_sites;   ///< by siteOf()
};

} // namespace eMule::ed3k
