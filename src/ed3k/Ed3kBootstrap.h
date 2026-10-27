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

#include "Ed3kFfi.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
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

    Ed3kSession& m_session;
    QStringList m_links;
    Asker m_asker;
    std::set<std::uint64_t> m_asks;
    std::optional<std::uint64_t> m_last;
    bool m_running = false;   ///< configured for this run of the node
};

} // namespace eMule::ed3k
