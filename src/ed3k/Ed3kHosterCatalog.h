#pragma once

/// @file Ed3kHosterCatalog.h
/// @brief What the Add account dialogs offer, as JDownloader's: every resolver plugin that
/// takes a premium account (hosters, multi-hosters, captcha solvers) and the sites' favicons.
///
/// Both come from the resolver and are kept in Config/ed3k/hoster/: accounthosts.json (asked
/// again once per run, the plugins may have changed) and favicons/<host>.png (.none: the site
/// has none, asked again after kNoneDays). Daemon thread only.

#include "Ed3kResolver.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

#include <deque>

namespace eMule::ed3k {

class Ed3kHosterCatalog : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxFetches = 4;      ///< favicon tasks at once
    static constexpr int kNoneDays = 7;        ///< a site without an icon is asked again after
    static constexpr int kRefreshDays = 30;    ///< an icon is fetched again after

    explicit Ed3kHosterCatalog(const QString& configDir, QObject* parent = nullptr);

    void setResolver(Ed3kResolver* resolver);

    /// The plugins that take an account, as last known (empty: none yet). Asks the
    /// resolver once per run; *asking: an accountHostsChanged follows.
    [[nodiscard]] QList<AccountHost> accountHosts(bool* asking = nullptr);

    /// The icons of @p hosts there are (PNG); the others are fetched, iconReady follows
    /// for each. Hosts that are no domain are skipped.
    [[nodiscard]] QHash<QString, QByteArray> icons(const QStringList& hosts);

    /// Whether @p host can be a file name and a domain: [a-z0-9.-], a dot, no "..".
    [[nodiscard]] static bool validHost(const QString& host);

signals:
    void accountHostsChanged(const QList<eMule::ed3k::AccountHost>& hosts);
    /// @p png empty: the site has none (or it could not be fetched now)
    void iconReady(const QString& host, const QByteArray& png);

private:
    void onTaskEvent(Ed3kResolver::TaskId task, const ResolverEvent& ev);
    void onStateChanged();
    void askHosts();
    void pump();
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
};

} // namespace eMule::ed3k
