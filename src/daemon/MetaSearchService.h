#pragma once

/// @file MetaSearchService.h
/// @brief Daemon side of eNode meta search: search, metafile fetch + Meta API accounts.
///
/// Resolves which server's Meta API a search row belongs to, runs the
/// GetCaps → (token?) → GetMetaFile sequence and turns every failure into an
/// Ipc::MetaStatus plus the map the GUI's login dialog needs. Also runs the
/// Usenet / torrent searches of the search queue (MetaSearchRunner).

#include "IpcProtocol.h"
#include "enodemeta/MetaAccountStore.h"
#include "enodemeta/MetaApiClient.h"
#include "search/MetaSearchRunner.h"
#include "search/SearchFile.h"

#include <QCborMap>
#include <QHash>
#include <QObject>

#include <functional>
#include <memory>
#include <optional>

namespace eMule {

class MetaSearchService : public QObject, public MetaSearchRunner {
    Q_OBJECT

public:
    /// Rows of one search over all its pages; past it no further page is offered.
    static constexpr int kMaxMetaRows = 2500;

    ~MetaSearchService() override;

    /// A server's Meta API, and how the GUI names that server.
    struct Target {
        enodemeta::MetaEndpoint endpoint;
        QString serverAddr;   ///< "addr:port", the key GUI requests use
    };

    /// Result of a fetch: status + meta map for the GUI, and the file on success.
    using FetchCallback = std::function<void(Ipc::MetaStatus status, const QString& error,
                                             const QCborMap& meta,
                                             const enodemeta::pb::MetaFile& file)>;
    using MapCallback = std::function<void(bool ok, const QString& error, const QCborMap& meta)>;

    /// Lazily created, parented to the application.
    static MetaSearchService& instance();

    /// The Meta API of the server that returned @p file, else the connected one's.
    [[nodiscard]] std::optional<Target> targetForResult(const SearchFile& file) const;
    /// Same, from the answering servers alone — a restored row has no SearchFile.
    [[nodiscard]] std::optional<Target> targetForServers(const std::list<SearchFile::SServer>& servers) const;
    /// By "addr:port" from a GUI request.
    [[nodiscard]] std::optional<Target> targetForServer(const QString& serverAddr) const;

    /// Fetch and verify the metafile behind a meta row. @p maxBytes 0 = caps limit.
    void fetch(const Target& target, const QByteArray& hash16, const QString& catalogId,
               quint32 maxBytes, FetchCallback cb);

    void authStatus(const Target& target, MapCallback cb);
    void login(const Target& target, const QString& username, const QString& password, MapCallback cb);
    void logout(const Target& target, MapCallback cb);

    /// Meta map carrying only a status (e.g. NoMetaApi before any call).
    [[nodiscard]] static QCborMap statusMap(Ipc::MetaStatus status, const QString& serverAddr = {},
                                            const QString& serverName = {});

    // -- MetaSearchRunner --

    void startMetaSearch(uint32 searchID, SearchType type, const SearchParams& params,
                         const enodemeta::pb::SearchRequest& request,
                         std::vector<MetaSearchCandidate> candidates) override;
    void continueMetaSearch(uint32 searchID) override;
    void cancelMetaSearch(uint32 searchID) override;

private:
    explicit MetaSearchService(QObject* parent);

    enodemeta::MetaAccountStore& store();
    void fetchWithToken(const Target& target, const enodemeta::pb::Caps& caps, const QByteArray& hash16,
                        const QString& catalogId, quint32 maxBytes, FetchCallback cb);

    [[nodiscard]] static QCborMap baseMap(Ipc::MetaStatus status, const Target& target);
    static void addCaps(QCborMap& m, const enodemeta::pb::Caps& caps);
    static void addErrorInfo(QCborMap& m, const enodemeta::pb::ErrorInfo& info);
    static void addAuthStatus(QCborMap& m, const enodemeta::pb::AuthStatus& st);
    [[nodiscard]] static Ipc::MetaStatus statusFor(const enodemeta::CallResult& r);
    [[nodiscard]] static QString errorText(const enodemeta::CallResult& r, Ipc::MetaStatus status);

    /// A search on its way through the candidates.
    struct SearchRun {
        SearchType type = SearchType::MetaUsenet;
        SearchParams params;
        enodemeta::pb::SearchRequest request;
        std::vector<MetaSearchCandidate> candidates;
        size_t next = 0;
        int rows = 0;
        /// Set while the search is finished with a page to come (continueMetaSearch).
        bool parked = false;
        MetaSearchCandidate server;   ///< the one that answered
        QString token;
        quint32 nextOffset = 0;
        QString authError;   ///< a server that would answer to an account
        QString lastError;
    };

    void askNextServer(uint32 searchID);
    void askPage(uint32 searchID, const MetaSearchCandidate& server, const QString& token, quint32 offset);
    void finishSearch(uint32 searchID, const QString& error);
    /// Keep what Caps says the server searches on its list entry (and in server.met).
    static void noteNetworks(const MetaSearchCandidate& server, const enodemeta::pb::Caps& caps);
    [[nodiscard]] static Target targetOf(const MetaSearchCandidate& server);

    QHash<uint32, SearchRun> m_searches;
    /// Search → the server that answered it, where its rows cannot say (IPv6-only).
    QHash<uint32, Target> m_searchTargets;

    enodemeta::MetaApiClient m_client;
    std::unique_ptr<enodemeta::MetaAccountStore> m_store;
};

} // namespace eMule
