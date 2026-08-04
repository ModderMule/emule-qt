#pragma once

/// @file HostResolver.h
/// @brief Single family-agnostic asynchronous DNS resolver.
///
/// Every other DNS site in the tree used to roll its own lookup, and all of them
/// were IPv4-only (`QDnsLookup::A`, or `QHostInfo` filtered to `IPv4Protocol`), so a
/// hostname with only an AAAA record could never be reached.
///
/// This wraps `QHostInfo::lookupHost`, which returns A *and* AAAA records in one
/// query and honours the hosts file, search domains and NAT64 synthesis — none of
/// which `QDnsLookup` does, since it talks to a nameserver directly. On top of that
/// it adds what `QHostInfo` lacks: a timeout, cancellation (explicit or when a
/// context object dies), an IP-literal short-circuit, in-flight de-duplication and a
/// small positive/negative cache.

#include "net/Address.h"
#include "utils/Types.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

class QTimer;

namespace eMule {

/// Asynchronous host resolution for both address families.
class HostResolver : public QObject {
    Q_OBJECT

public:
    /// Which families to return, and in what order.
    enum class Preference : uint8 {
        Any,            ///< Both families, in the order the resolver returned them
        PreferIPv4,     ///< Both families, IPv4 entries first
        PreferIPv6,     ///< Both families, IPv6 entries first
        IPv4Only,       ///< Drop IPv6 results
        IPv6Only        ///< Drop IPv4 results
    };

    struct Result {
        QString              host;          ///< The name that was looked up
        std::vector<Address> addresses;     ///< Ordered per the requested Preference
        QString              errorString;   ///< Empty on success

        [[nodiscard]] bool ok() const { return !addresses.empty(); }
        [[nodiscard]] Address first() const;
        [[nodiscard]] Address firstIPv4() const;
        [[nodiscard]] Address firstIPv6() const;
    };

    using Callback = std::function<void(const Result&)>;

    static constexpr int kDefaultTimeoutMs = 10'000;

    explicit HostResolver(QObject* parent = nullptr);
    ~HostResolver() override;

    HostResolver(const HostResolver&) = delete;
    HostResolver& operator=(const HostResolver&) = delete;

    /// Resolve @p host and invoke @p cb with the result.
    ///
    /// The callback is *always* delivered asynchronously — including for an IP literal
    /// and for a cache hit — so callers have a single invariant. It is never invoked
    /// after cancel(), after @p context is destroyed, or after this resolver dies.
    ///
    /// @return a token for cancel(), or 0 when @p host is empty.
    int resolve(const QString& host, Preference pref, QObject* context, Callback cb,
                int timeoutMs = kDefaultTimeoutMs);

    /// Overload without a context object: the request lives until it completes,
    /// times out, is cancelled, or this resolver is destroyed.
    int resolve(const QString& host, Preference pref, Callback cb,
                int timeoutMs = kDefaultTimeoutMs);

    /// Drop a pending request. The callback will not be invoked.
    void cancel(int token);

    /// Drop every pending request without invoking any callback.
    void cancelAll();

    [[nodiscard]] bool hasPendingRequests() const { return !m_pending.empty(); }

    /// Clear the address cache (test seam; also useful after a network change).
    void clearCache();

    /// Order/filter resolved addresses per @p pref, removing duplicates.
    /// Exposed for testing — the resolution path calls this internally.
    [[nodiscard]] static std::vector<Address> orderByPreference(std::vector<Address> addresses,
                                                                Preference pref);

private:
    struct Pending {
        QString                 host;
        Preference              pref = Preference::Any;
        Callback                cb;
        std::unique_ptr<QTimer> timeout;
        QMetaObject::Connection contextConnection;
    };

    struct CacheEntry {
        std::vector<Address> addresses;   ///< Empty for a negative entry
        QString              errorString;
        qint64               expiresAtMs = 0;
    };

    /// Start (or join) a lookup for @p host.
    void startLookup(const QString& host);

    /// Deliver results for every token waiting on @p host.
    void completeHost(const QString& host, const std::vector<Address>& addresses,
                      const QString& errorString);

    /// Deliver one result and drop the token. Safe to call from a callback.
    void finish(int token, const std::vector<Address>& addresses, const QString& errorString);

    /// Remove @p token from the waiter list of its host, aborting the underlying
    /// lookup when no waiter is left.
    void detachFromLookup(int token, const QString& host);

    [[nodiscard]] const CacheEntry* cachedEntry(const QString& host) const;
    void cacheResult(const QString& host, const std::vector<Address>& addresses,
                     const QString& errorString);

    std::unordered_map<int, Pending>              m_pending;      ///< token -> request
    std::unordered_map<QString, std::vector<int>> m_waiters;      ///< host -> tokens
    std::unordered_map<QString, int>              m_lookupIds;   ///< host -> QHostInfo id
    std::unordered_map<QString, CacheEntry>       m_cache;
    QElapsedTimer                                 m_clock;
    int                                           m_nextToken = 1;
};

} // namespace eMule
