#pragma once

/// @file AppContext.h
/// @brief Global application context — provides access to all manager services.
///
/// Follows the same pattern as `extern Preferences thePrefs`.
/// Each manager pointer is set during application startup and remains
/// valid for the lifetime of the process.

#include "utils/Types.h"
#include "utils/CorroborationTally.h"
#include "net/Address.h"

#include <QByteArray>
#include <QString>
#include <functional>
#include <vector>

namespace eMule {

// Forward declarations
class ClientCreditsList;
class ClientList;
class ClientUDPSocket;
class DownloadQueue;
class FriendList;
class IPFilter;
class KnownFileList;
class ListenSocket;
class GlobalSearchScheduler;
class HttpCacheManager;
class SearchList;
class ServerConnect;
class ServerList;
class SharedFileList;
class Statistics;
class StatsHistory;
class Scheduler;
class LastCommonRouteFinder;
class PortMapper;
class UploadBandwidthThrottler;
class UpDownClient;
class UploadQueue;

struct AppContext {
    ClientList*      clientList     = nullptr;
    ClientCreditsList* clientCredits = nullptr;
    ClientUDPSocket* clientUDP      = nullptr;
    DownloadQueue*   downloadQueue  = nullptr;
    UploadQueue*     uploadQueue    = nullptr;
    HttpCacheManager* httpCache     = nullptr;
    SharedFileList*  sharedFileList = nullptr;
    KnownFileList*   knownFileList  = nullptr;
    IPFilter*        ipFilter       = nullptr;
    ListenSocket*    listenSocket   = nullptr;
    FriendList*      friendList     = nullptr;
    ServerConnect*   serverConnect  = nullptr;
    ServerList*      serverList     = nullptr;
    SearchList*      searchList     = nullptr;
    GlobalSearchScheduler* globalSearch = nullptr;
    Statistics*      statistics     = nullptr;
    StatsHistory*    statsHistory   = nullptr;
    UploadBandwidthThrottler* uploadBandwidthThrottler = nullptr;
    LastCommonRouteFinder* lastCommonRouteFinder = nullptr;
    Scheduler*   scheduler      = nullptr;
    PortMapper*  portMapper     = nullptr;

    /// Returns our server-assigned client ID (0 if not connected).
    [[nodiscard]] uint32 getID() const;

    /// Returns true when connected to any network — an ED2K server *or* Kad.
    /// Mirrors MFC CemuleApp::IsConnected() (emule.cpp:1122).
    /// Callers that specifically mean "connected to an ED2K server" (GUI status
    /// indicators, the Connect button, connection-lost notifications) must use
    /// serverConnect->isConnected() directly instead.
    [[nodiscard]] bool isConnected() const;

    /// Returns true when we are firewalled on all connected networks (ed2k + Kad).
    [[nodiscard]] bool isFirewalled() const;

    /// Whether we can get a callback through to this peer at all.
    /// MFC CemuleApp::CanDoCallback (srchybrid/Emule.cpp:1187-1203).
    ///
    /// The Low-ID-to-Low-ID rule: with Kad unavailable a callback has to go through our
    /// server, which only works if we ourselves have a High ID; and when we are Low-ID on a
    /// server, asking that same server to call back one of its own clients breaks the
    /// protocol and gets us banned.
    [[nodiscard]] bool canDoCallback(const UpDownClient* client) const;

    /// Our public IP in ED2K byte order (first octet in the LSB), 0 if unknown.
    ///
    /// Priority is Kad -> ED2K session -> server-corroborated. Kad's IP wins whenever
    /// it has one because KadPrefs::setIPAddress() only commits a value confirmed by
    /// two independent nodes, whereas a single server's claim is one unverified
    /// assertion. Pass @p ignoreKadIP to skip the Kad source — callers that must agree
    /// with what a *specific* server sees need that.
    ///
    /// MFC: CemuleApp::GetPublicIP() — Emule.cpp:1542. Note the priority there is
    /// the other way round: the stored ED2K value wins and Kad is only a fallback,
    /// and it has no third tier at all.
    [[nodiscard]] uint32 publicIP(bool ignoreKadIP = false) const;

    /// Records an ED2K-derived public IP (HighID, server-reported IP, or a peer's
    /// OP_PUBLICIP answer). Pass 0 to clear it on server disconnect.
    /// MFC: CemuleApp::SetPublicIP() — Emule.cpp:1548.
    void setPublicIP(uint32 ip);

    /// The raw ED2K-session slot, with neither the Kad source nor the corroborated tier
    /// layered on. Callers that *feed* this slot, or that save and restore it, must read
    /// it here: going through publicIP() would let a lower tier's value be written back
    /// as if a server had told us, and the tier below could then never be superseded.
    /// Same reason publicIPv6Override()/publicIPv6Local() exist beside publicIPv6().
    [[nodiscard]] uint32 ed2kSessionIP() const noexcept { return m_publicIP; }

    /// Re-checks server UDP keys after something *other* than setPublicIP()
    /// changed our effective public IP — in practice, Kad learning a new one.
    /// MFC has no equivalent because Kad never outranks the stored value there.
    void onEffectivePublicIPChanged(uint32 newIP);

    /// Lowest public-IPv4 tier: the address a server observed us on, from the trailing
    /// field of a challenge-bound OP_GLOBSERVSTATRES (§ ServerList::processStatusResponse).
    /// @p serverKey must be the *sending server's* address — one host, one vote. Adopted
    /// once ipv4PublicServerConfirmThreshold distinct servers agree inside the window, and
    /// even then only consulted while neither Kad nor an ED2K session knows our address.
    ///
    /// There is deliberately no "must be assigned to a local interface" guard here, the way
    /// the IPv6 tiers have one: behind NAT our public IPv4 is by definition *not* on any
    /// local interface, so such a check would reject every legitimate value. What stands in
    /// its place is the challenge binding at the call site, the distinct-server threshold,
    /// and the validation ladder in ServerList.
    void recordServerObservedIP(const Address& candidate, const Address& serverKey,
                                const QString& serverLabel = {});
    /// The corroborated address in ED2K byte order, 0 until the threshold has been met.
    /// Sticky — replaced by a better-backed candidate, never dropped by vote expiry alone,
    /// because stat pings re-ask a given server at most every UDPSERVSTATREASKTIME.
    [[nodiscard]] uint32 serverCorroboratedIP() const;
    /// Forget the corroborated address and every vote behind it.
    void clearServerCorroboratedIP();

    /// Our public IPv6, chosen by descending confidence:
    ///   1. server-observed egress (CT_MOD_YOUR_IP in OP_SERVERIDENT, IPv6 session only),
    ///   2. operator override (publicIPv6Override pref),
    ///   3. peer-corroborated (>= threshold distinct peers agree within the window),
    ///   4. auto-selected local interface address.
    /// A *reflected* address (tier 1 or 3) is only ever adopted when it is actually assigned
    /// to one of our interfaces — see isLocalIPv6(). Transient state, not persisted.
    [[nodiscard]] Address publicIPv6() const;

    /// Tier 2: the operator's explicit pin. Set by LocalIPv6::updatePublicIPv6() once the
    /// publicIPv6Override literal has been confirmed present on a local interface.
    void setPublicIPv6Override(const Address& addr);
    /// Tier 4: the auto-selected stable interface address.
    void setPublicIPv6Local(const Address& addr);
    /// Raw tier slots — callers detecting a change in *their own* tier must compare against
    /// these, never against the tiered publicIPv6().
    [[nodiscard]] Address publicIPv6Override() const { return m_publicIPv6Override; }
    [[nodiscard]] Address publicIPv6Local() const { return m_publicIPv6Local; }

    /// True when publicIPv6() yields a genuine global-unicast address.
    [[nodiscard]] bool hasConfidentPublicIPv6() const;

    /// The single gate for emitting our own IPv6 (hello CT_MOD_IP_V6, Kad source publish,
    /// server login CT_MOD_IP_V6): we are confident in the address *and* no server has
    /// probed it and found it unreachable.
    [[nodiscard]] bool shouldAdvertisePublicIPv6() const
    {
        return hasConfidentPublicIPv6() && !publicIPv6ProbedUnreachable();
    }

    /// Called once whenever the address returned by publicIPv6() actually changes — after
    /// tier resolution, so a lower tier moving while a higher one wins is silent. CoreSession
    /// installs it to queue OP_CHANGE_CLIENT_IP for connected peers. A std::function hook
    /// rather than a Qt signal keeps AppContext a plain struct with no moc dependency.
    std::function<void(const Address& effective)> onPublicIPv6Changed;

    /// Tier 1: the egress IPv6 a server observed us on (CT_MOD_YOUR_IP in OP_SERVERIDENT).
    /// Callers must only pass this for a session that actually connected over IPv6; the
    /// address is rejected unless it is global-unicast *and* held on a local interface.
    void setPublicIPv6Observed(const Address& addr, const QString& serverLabel = {});
    void clearPublicIPv6Observed();

    /// Server's ST_IPV6_STATUS verdict on our advertised IPv6 (IPV6ST_* bits; 0 = unknown).
    void setPublicIPv6Status(uint8 status) { m_publicIPv6Status = status; }
    [[nodiscard]] uint8 publicIPv6Status() const { return m_publicIPv6Status; }
    /// True when the server probed our v6 and found it unreachable — suppress advertising.
    [[nodiscard]] bool publicIPv6ProbedUnreachable() const;

    /// Tier 3: record that the peer at @p peerKey observed us at @p candidate (a peer's
    /// client-to-client CT_MOD_YOUR_IP). @p peerKey must be the peer's *observed* remote
    /// address, not its self-declared user hash — one host is one vote. Adopts a candidate
    /// once >= threshold distinct peers confirm it inside the window.
    void recordPeerObservedIPv6(const Address& candidate, const QByteArray& peerKey);
    /// The current peer-corroborated address (null until it meets the threshold).
    [[nodiscard]] Address peerCorroboratedIPv6() const
    {
        return m_ipv6PeerVotes.adopted().value_or(Address{});
    }

    /// The global-unicast IPv6 addresses currently assigned to our interfaces. Refreshed
    /// from LocalIPv6::scanLocalIPv6() at startup and on every server connect; injectable
    /// so tests need no real interfaces.
    void setLocalIPv6Addresses(std::vector<Address> addrs);
    /// True when @p addr is one of those addresses. Empty set => false (fail closed): with
    /// no local global v6 there is nothing legitimate to advertise anyway.
    [[nodiscard]] bool isLocalIPv6(const Address& addr) const;

private:
    /// ED2K-derived public IP only; publicIP() layers the Kad source on top.
    /// Deliberately not persisted — it is session state, as in MFC.
    uint32 m_publicIP = 0;

    /// Emits one logInfo line whenever the *effective* (tiered) IPv6 changes, naming the
    /// tier it now comes from. Every setter funnels through this so the transition is
    /// logged once, from one place, instead of per-caller.
    void noteEffectiveIPv6Change();
    /// Human-readable name of the tier publicIPv6() currently resolves to.
    [[nodiscard]] QString publicIPv6SourceLabel() const;
    /// Distinct peers required before a candidate is adopted (>= 1).
    [[nodiscard]] std::size_t peerConfirmThreshold() const;
    /// Distinct servers required before a corroborated IPv4 is adopted (>= 1).
    [[nodiscard]] std::size_t serverConfirmThreshold() const;
    /// Expire out-of-window votes and re-elect the server-corroborated IPv4, firing the
    /// UDP-key sweep if that changed the address publicIP() resolves to. Returns the
    /// winning candidate's distinct-server count.
    std::size_t recomputeServerCorroboratedIP();
    /// Expire out-of-window reports and re-elect m_publicIPv6PeerTop, logging any change.
    /// Returns the winning candidate's distinct-peer count. Shared by recordPeerObservedIPv6()
    /// and setLocalIPv6Addresses(), which must not diverge in how they elect.
    std::size_t recomputePeerCorroboratedIPv6();

    /// Auto-selected interface public IPv6 (tier 4); transient, not persisted.
    Address m_publicIPv6Local;
    /// Operator-pinned public IPv6 (tier 2), already confirmed locally assigned.
    Address m_publicIPv6Override;
    /// Server-observed egress IPv6 (tier 1); cleared on server disconnect.
    Address m_publicIPv6Server;
    /// Server's ST_IPV6_STATUS bits for our advertised v6 (0 = unknown).
    uint8 m_publicIPv6Status = 0;
    /// Last effective address reported by noteEffectiveIPv6Change().
    Address m_publicIPv6Announced;

    /// Peer-corroboration (tier 3): a candidate v6 is adopted once the number of distinct
    /// fresh peers claiming it reaches the configured threshold. Reelect mode — peer hellos
    /// arrive continuously, so an address that stops being re-confirmed is genuinely stale.
    CorroborationTally<Address, QByteArray> m_ipv6PeerVotes{CorroborationMode::Reelect};

    /// Server-corroborated public IPv4 (lowest tier). Sticky — see recordServerObservedIP().
    CorroborationTally<Address, Address> m_ipv4ServerVotes{CorroborationMode::Sticky};

    /// Throttle for peer-reported addresses we reject as not locally assigned. Kept as a
    /// single address + tick rather than a set: a hostile peer can name unlimited distinct
    /// addresses, and per-candidate state would grow without bound.
    Address m_ipv6LastRejected;
    qint64  m_ipv6LastRejectTick = 0;

    /// Global-unicast IPv6 addresses held on our interfaces (see setLocalIPv6Addresses()).
    std::vector<Address> m_localIPv6;
};

extern AppContext theApp;

} // namespace eMule
