#pragma once

/// @file MetaSearchService.h
/// @brief Daemon side of eNode meta search: metafile fetch + Meta API accounts.
///
/// Resolves which server's Meta API a search row belongs to, runs the
/// GetCaps → (token?) → GetMetaFile sequence and turns every failure into an
/// Ipc::MetaStatus plus the map the GUI's login dialog needs.

#include "IpcProtocol.h"
#include "enodemeta/MetaAccountStore.h"
#include "enodemeta/MetaApiClient.h"

#include <QCborMap>
#include <QObject>

#include <functional>
#include <memory>
#include <optional>

namespace eMule {

class SearchFile;

class MetaSearchService : public QObject {
    Q_OBJECT

public:
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

    enodemeta::MetaApiClient m_client;
    std::unique_ptr<enodemeta::MetaAccountStore> m_store;
};

} // namespace eMule
