#pragma once

/// @file Ed3kBootstrap.h
/// @brief Node lists for the first contact (bootstrap.md §3, host-io.md "Node lists").
///
/// - The sources are the pref ed3k.nodeLists (`ed3k:nodes?url=…&key=…` links), handed to
///   the engine with bootstrapConfigure each time the node starts, with the automatic fetch
///   on and the start of the last first contact (BootstrapStarted, kept in
///   Config/ed3k/bootstrap.dat). The fetches are HTTP requests of the engine (Ed3kHttp).
/// - A link the user opened (openLink → bootstrapLink) is never used before the user said
///   yes (§3 "MUST ask"): BootstrapAsk goes to the asker (the daemon pushes it to the GUI);
///   no asker, or no GUI to ask, refuses it. An ask not answered in 5 minutes is refused.
/// - BootstrapAdded: sourceAdded() with the link, which the daemon adds to the pref.
/// - The shipped list (bootstrap.md §1 source 5): `nodes.ed3knod` of the config folder, a
///   bundled file refreshed with each build; handed over at each start, used once before any
///   fetch. The age limit and the automatic fetch are prefs (setPolicy).
/// - eD2K peers with a derived user hash (offerEd2kPeer, §1 source 4) seed Kad while it has
///   few contacts; host-side limit: one a second, each address once in 10 minutes.
/// - exportList: the own contacts as a signed list file (§3 "By hand").

#include "Ed3kFfi.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <set>

namespace eMule::ed3k {

class Ed3kSession;

class Ed3kBootstrap : public QObject {
    Q_OBJECT

public:
    /// Shows the user an ask (the host name of @p url); false: nobody to ask (refused).
    using Asker = std::function<bool(std::uint64_t ask, const QString& host, const QString& url)>;

    explicit Ed3kBootstrap(Ed3kSession& session, QObject* parent = nullptr);
    ~Ed3kBootstrap() override;

    /// The pref ed3k.nodeLists; invalid links are logged and left out. Applied at once while
    /// the node runs.
    void setSources(const QStringList& links);
    [[nodiscard]] QStringList sources() const { return m_links; }
    void setAsker(Asker asker) { m_asker = std::move(asker); }
    /// An ed3k:nodes link the user opened: the ask follows. nullopt: refused (invalid, off).
    std::optional<std::uint64_t> openLink(const QString& link);
    /// The user's answer to an ask. false: no such ask (answered, timed out).
    bool answer(std::uint64_t ask, bool yes);
    /// Fetches node lists now (bootstrapFetch).
    bool fetchNow();
    /// The prefs of the fetch: lists fetched by themselves, the age in days a list may have
    /// (0: no limit). Applied at once while the node runs.
    void setPolicy(bool autoFetch, int maxAgeDays);
    /// The shipped list file; default: `nodes.ed3knod` in the config folder. Empty: none.
    void setShippedFile(const QString& path);
    [[nodiscard]] QString shippedFile() const { return m_shippedPath; }
    /// Seeding Kad from eD2K peers on or off.
    void setSeedFromEd2k(bool on) { m_seedFromEd2k = on; }
    /// An eD2K peer: its user hash, address and eD2K TCP port. Dialled over ed3k when the
    /// hash is a derived one (§14.2) and Kad has fewer than 10 contacts. true: dialled.
    bool offerEd2kPeer(const std::array<std::uint8_t, 16>& userHash, const QHostAddress& ip, std::uint16_t tcpPort);
    /// The own contacts as a node list file signed with the node key; the error text on
    /// failure (no node, no contacts).
    [[nodiscard]] std::expected<QByteArray, QString> exportList(const QString& comment) const;
    /// The same without a running node: the contacts the node of @p configDir saved
    /// (Config/ed3k/kad.dat), signed with its node.key, for the public network. What a
    /// release ships as data/config/nodes.ed3knod. Never creates a key.
    [[nodiscard]] static std::expected<QByteArray, QString> exportSaved(const QString& configDir, const QString& comment);
    /// Asks waiting for the user (tests).
    [[nodiscard]] std::size_t pendingAsks() const { return m_asks.size(); }
    /// The start of the last first contact (Unix s); nullopt: none known.
    [[nodiscard]] std::optional<std::uint64_t> lastStart() const { return m_last; }
    [[nodiscard]] QString lastFile() const;

signals:
    /// The user said yes to a link and the engine took the source: add it to the pref.
    void sourceAdded(const QString& link);
    /// A first contact fetched @p lists lists with @p nodes candidates.
    void listsReceived(int lists, int nodes);

private:
    void onEventPolled(const ::ed3k::Event& event, bool* handled);
    void onStatus();
    void configure();
    void loadLast();
    void saveLast();
    void applyPolicy();
    void applyShipped();

    Ed3kSession& m_session;
    QStringList m_links;
    Asker m_asker;
    std::set<std::uint64_t> m_asks;
    std::optional<std::uint64_t> m_last;
    bool m_running = false;   ///< configured for this run of the node
    bool m_autoFetch = true;
    int m_maxAgeDays = 30;
    QString m_shippedPath;
    bool m_seedFromEd2k = true;
    QElapsedTimer m_seedClock;          ///< clock of the eD2K seeds
    QHash<QString, qint64> m_seedTried; ///< address → when (ms of m_seedClock)
};

} // namespace eMule::ed3k
