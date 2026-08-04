#include "pch.h"
/// @file AppContext.cpp
/// @brief Global application context definition.

#include "app/AppContext.h"
#include "kademlia/Kademlia.h"
#include "prefs/Preferences.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/TimeUtils.h"

#include <QStringList>
#include <algorithm>

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace eMule {

AppContext theApp;

uint32 AppContext::getID() const
{
    if (serverConnect && serverConnect->isConnected())
        return serverConnect->clientID();
    return 0;
}

bool AppContext::isConnected() const
{
    // MFC: CemuleApp::IsConnected() — Emule.cpp:1122
    // Connected to *any* network: ED2K server or Kad.
    if (serverConnect && serverConnect->isConnected())
        return true;

    auto* kadInst = kad::Kademlia::instance();
    return kadInst && kadInst->isConnected();
}

bool AppContext::isFirewalled() const
{
    // MFC: CemuleApp::IsFirewalled() — Emule.cpp:1176
    // Not firewalled if we have an ed2k High ID
    if (serverConnect && serverConnect->isConnected() && !serverConnect->isLowID())
        return false;

    // Not firewalled if Kad says we're open
    auto* kadInst = kad::Kademlia::instance();
    if (kadInst && kadInst->isConnected() && !kadInst->isFirewalled())
        return false;

    return true;
}

uint32 AppContext::publicIP(bool ignoreKadIP) const
{
    // MFC: CemuleApp::GetPublicIP() — Emule.cpp:1542, with the priority inverted:
    // Kad outranks the ED2K value here. KadPrefs::setIPAddress() only commits an
    // address two independent nodes agreed on, while a server's claim is one
    // unverified assertion from a single host.
    if (!ignoreKadIP) {
        // Kad holds IPs with the first octet in the MSB; ED2K wants it in the LSB.
        // Same direction as the ipFilter calls in KadRoutingZone.cpp.
        auto* kadInst = kad::Kademlia::instance();
        if (kadInst && kadInst->isConnected() && kadInst->getIPAddress() != 0)
            return htonl(kadInst->getIPAddress());
    }
    if (m_publicIP != 0)
        return m_publicIP;

    // Lowest tier — several servers independently reflected the same address back at us.
    // Only reachable when neither Kad nor an ED2K session knows our address, which is
    // exactly the state where this function used to return 0.
    return serverCorroboratedIP();
}

void AppContext::setPublicIP(uint32 ip)
{
    // MFC: CemuleApp::SetPublicIP() — Emule.cpp:1548.
    if (ip != 0) {
        // MFC logs this same disagreement (Emule.cpp:1557). Under our Kad-first
        // priority it is more than a curiosity: the server issues its UDP key for
        // the IP *it* sees us on, so if these two ever diverge, the key is stamped
        // and validated against Kad's view and can never match. This log is how
        // that shows up.
        auto* kadInst = kad::Kademlia::instance();
        if (kadInst && kadInst->isConnected() && kadInst->getIPAddress() != 0) {
            const uint32 kadIP = htonl(kadInst->getIPAddress());
            if (kadIP != ip)
                logDebug(QStringLiteral("Public IP reported by Kad (%1) differs from the "
                                        "ED2K-reported one (%2); Kad's takes precedence")
                             .arg(ipstr(kadIP), ipstr(ip)));
        }
    }

    // Server UDP keys are bound to the IP they were issued for, so a real change
    // invalidates every one of them and those servers must be re-pinged.
    // Compares the stored value, not publicIP(): against the Kad-backed getter
    // this check would never fire while Kad is supplying the effective IP.
    const uint32 oldIP = m_publicIP;
    const uint32 effectiveBefore = publicIP();
    m_publicIP = ip;
    if (ip != 0 && ip != oldIP && serverList) {
        serverList->checkForExpiredUDPKeys(publicIP());
    } else if (ip == 0 && oldIP != 0 && serverList) {
        // Clearing the session value on disconnect used to mean "we no longer know our
        // address", so there was nothing to re-key against. Now a corroborated tier may
        // sit underneath and supply a *different* one, which kills every key stamped for
        // oldIP. With no corroborated address this stays a no-op, as before.
        const uint32 effectiveAfter = publicIP();
        if (effectiveAfter != 0 && effectiveAfter != effectiveBefore)
            serverList->checkForExpiredUDPKeys(effectiveAfter);
    }
}

void AppContext::onEffectivePublicIPChanged(uint32 newIP)
{
    // Kad outranks m_publicIP, so a change to *its* address changes our effective
    // IP and kills every server UDP key — with no call to setPublicIP() to notice.
    // Without this hook the Kad-first priority silently reintroduces stale keys.
    if (newIP != 0 && serverList)
        serverList->checkForExpiredUDPKeys(publicIP());
}

uint32 AppContext::serverCorroboratedIP() const
{
    return m_ipv4ServerVotes.adopted().value_or(Address{}).toNetworkUint32();
}

void AppContext::clearServerCorroboratedIP()
{
    if (m_ipv4ServerVotes.adopted().has_value())
        logDebug(QStringLiteral("Public IPv4: dropping the server-corroborated address"));
    m_ipv4ServerVotes.clear();
}

void AppContext::recordServerObservedIP(const Address& candidate, const Address& serverKey,
                                        const QString& serverLabel)
{
    // Validation lives in ServerList::processStatusResponse, which has the packet context
    // needed to say *why* a value was refused. Guard the invariants this class depends on.
    if (!candidate.isIPv4() || !candidate.isPublicIP() || serverKey.isNull())
        return;

    const QString who = serverLabel.isEmpty() ? serverKey.toString() : serverLabel;

    m_ipv4ServerVotes.record(candidate, serverKey,
                             static_cast<std::int64_t>(getTickCount()));

    const std::size_t bestCount = recomputeServerCorroboratedIP();
    const std::size_t threshold = serverConfirmThreshold();
    logDebug(QStringLiteral("Public IPv4: server %1 observes us at %2 (%3/%4 distinct servers)")
                 .arg(who, candidate.toString())
                 .arg(m_ipv4ServerVotes.voterCount(candidate))
                 .arg(threshold));

    // Note once per candidate when a server's view disagrees with the address we are
    // actually using. This is the CGNAT / multi-homed-egress signal: on such a host no
    // candidate ever reaches the threshold and this line is the only way to see why.
    const uint32 chosen = publicIP();
    if (chosen != 0 && chosen != candidate.toNetworkUint32()
        && m_ipv4ServerVotes.markWarnedOnce(candidate)) {
        logDebug(QStringLiteral("Public IPv4: a server reports our address as %1, which differs "
                                "from our chosen %2 (%3 server(s) agree; need %4 to adopt)")
                     .arg(candidate.toString(), ipstr(chosen))
                     .arg(bestCount).arg(threshold));
    }
}

namespace {

/// A value is only usable as our own public IPv6 if it is genuinely global-unicast.
[[nodiscard]] bool isUsablePublicIPv6(const Address& a)
{
    return a.isIPv6() && a.isPublicIP();
}

} // namespace

Address AppContext::publicIPv6() const
{
    // Descending confidence. The server sees our actual egress, so it outranks even the
    // operator's pin; the pin in turn outranks what peers claim, which outranks a guess
    // made purely from the interface list.
    if (isUsablePublicIPv6(m_publicIPv6Server))
        return m_publicIPv6Server;
    if (isUsablePublicIPv6(m_publicIPv6Override))
        return m_publicIPv6Override;
    if (isUsablePublicIPv6(peerCorroboratedIPv6()))
        return peerCorroboratedIPv6();
    if (isUsablePublicIPv6(m_publicIPv6Local))
        return m_publicIPv6Local;
    return Address{};
}

QString AppContext::publicIPv6SourceLabel() const
{
    if (isUsablePublicIPv6(m_publicIPv6Server))
        return QStringLiteral("server-observed");
    if (isUsablePublicIPv6(m_publicIPv6Override))
        return QStringLiteral("operator override");
    if (const Address top = peerCorroboratedIPv6(); isUsablePublicIPv6(top)) {
        return QStringLiteral("peer-corroborated, %1 peer(s)")
            .arg(m_ipv6PeerVotes.voterCount(top));
    }
    if (isUsablePublicIPv6(m_publicIPv6Local))
        return QStringLiteral("local interface");
    return QStringLiteral("none");
}

void AppContext::noteEffectiveIPv6Change()
{
    const Address effective = publicIPv6();
    if (effective == m_publicIPv6Announced)
        return;

    const Address previous = m_publicIPv6Announced;
    m_publicIPv6Announced = effective;

    if (effective.isNull()) {
        logInfo(QStringLiteral("IPv6: no public address available — was %1")
                    .arg(previous.toString()));
    } else {
        logInfo(QStringLiteral("IPv6: now advertising %1 (source: %2)%3")
                    .arg(effective.toString(), publicIPv6SourceLabel(),
                         previous.isNull() ? QString()
                                           : QStringLiteral(", was %1").arg(previous.toString())));
    }

    // Notify last, so an observer sees the new address already published. Fires for the
    // null case too — peers that were told an address deserve to learn it is gone.
    if (onPublicIPv6Changed)
        onPublicIPv6Changed(effective);
}

void AppContext::setPublicIPv6Override(const Address& addr)
{
    if (addr == m_publicIPv6Override)
        return;
    m_publicIPv6Override = addr;
    noteEffectiveIPv6Change();
}

void AppContext::setPublicIPv6Local(const Address& addr)
{
    if (addr == m_publicIPv6Local)
        return;
    m_publicIPv6Local = addr;
    noteEffectiveIPv6Change();
}

void AppContext::setLocalIPv6Addresses(std::vector<Address> addrs)
{
    m_localIPv6 = std::move(addrs);

    QStringList names;
    names.reserve(static_cast<qsizetype>(m_localIPv6.size()));
    for (const Address& a : m_localIPv6)
        names.append(a.toString());
    logDebug(QStringLiteral("IPv6: local interface set refreshed: %1 global address(es)%2")
                 .arg(m_localIPv6.size())
                 .arg(names.isEmpty() ? QString()
                                      : QStringLiteral(" (%1)").arg(names.join(QStringLiteral(", ")))));

    // An address we no longer hold cannot still be our egress — drop reflections that the
    // refreshed interface list no longer backs (prefix renumber, interface down).
    if (!m_publicIPv6Server.isNull() && !isLocalIPv6(m_publicIPv6Server)) {
        logInfo(QStringLiteral("IPv6: server-observed %1 is no longer assigned to any local "
                               "interface — dropping it")
                    .arg(m_publicIPv6Server.toString()));
        m_publicIPv6Server = Address{};
    }

    // Discard the *votes* too, not just the adopted value: leaving them in place would let
    // an address we no longer hold keep winning the "most distinct peers" comparison and
    // suppress a genuine new candidate until the window expired. The per-candidate warn
    // latch dies with the entry, so no separate cleanup is needed.
    m_ipv6PeerVotes.eraseCandidatesIf([this](const Address& cand, std::size_t peers) {
        if (isLocalIPv6(cand))
            return false;
        logDebug(QStringLiteral("IPv6: discarding %1 peer report(s) for %2 — no longer assigned "
                                "to any local interface")
                     .arg(peers).arg(cand.toString()));
        return true;
    });

    recomputePeerCorroboratedIPv6();
    noteEffectiveIPv6Change();
}

bool AppContext::isLocalIPv6(const Address& addr) const
{
    if (!addr.isIPv6())
        return false;
    return std::find(m_localIPv6.begin(), m_localIPv6.end(), addr) != m_localIPv6.end();
}

std::size_t AppContext::peerConfirmThreshold() const
{
    const uint32 thr = thePrefs.ipv6PublicPeerConfirmThreshold();
    // A 0 threshold would defeat corroboration entirely — never single-peer adopt.
    return thr < 1 ? 1u : static_cast<std::size_t>(thr);
}

std::size_t AppContext::serverConfirmThreshold() const
{
    const uint32 thr = thePrefs.ipv4PublicServerConfirmThreshold();
    // A 0 threshold would defeat corroboration entirely — never single-server adopt.
    return thr < 1 ? 1u : static_cast<std::size_t>(thr);
}

std::size_t AppContext::recomputeServerCorroboratedIP()
{
    const auto now = static_cast<std::int64_t>(getTickCount());
    const auto windowMs =
        static_cast<std::int64_t>(thePrefs.ipv4PublicServerConfirmWindowSecs()) * 1000;

    // Bracket the election with the *effective* address: while Kad or an ED2K session
    // outranks this tier, adopting changes nothing observable and must stay silent.
    const uint32 before = publicIP();
    const auto out = m_ipv4ServerVotes.recompute(now, serverConfirmThreshold(), windowMs);
    if (out.adoptedChanged) {
        const uint32 after = publicIP();
        if (after != before) {
            logInfo(QStringLiteral("Public IPv4: now %1 (source: server-corroborated, "
                                   "%2 server(s))%3")
                        .arg(after != 0 ? ipstr(after) : QStringLiteral("unknown"))
                        .arg(out.bestCount)
                        .arg(before != 0 ? QStringLiteral(", was %1").arg(ipstr(before))
                                         : QString()));
            // Every server UDP key is stamped with the address it was issued for, so a
            // change to the effective one kills them all. Same path Kad uses.
            onEffectivePublicIPChanged(after);
        }
    }
    return out.bestCount;
}

std::size_t AppContext::recomputePeerCorroboratedIPv6()
{
    const auto now = static_cast<std::int64_t>(getTickCount());
    const auto windowMs =
        static_cast<std::int64_t>(thePrefs.ipv6PublicPeerConfirmWindowSecs()) * 1000;

    const auto out = m_ipv6PeerVotes.recompute(now, peerConfirmThreshold(), windowMs);
    if (out.adoptedChanged)
        noteEffectiveIPv6Change();
    return out.bestCount;
}

bool AppContext::hasConfidentPublicIPv6() const
{
    return isUsablePublicIPv6(publicIPv6());
}

void AppContext::setPublicIPv6Observed(const Address& addr, const QString& serverLabel)
{
    const QString who = serverLabel.isEmpty() ? QStringLiteral("server") : serverLabel;

    if (!isUsablePublicIPv6(addr)) {
        logDebug(QStringLiteral("IPv6: ignoring egress %1 reported by %2 — not a global-unicast "
                                "IPv6 address")
                     .arg(addr.toString(), who));
        return;
    }

    // A reflected address we do not actually hold cannot receive traffic for us. Advertising
    // it would kill inbound IPv6 and diverge our v6 UDP obfuscation keys, so keep whatever we
    // already chose. A NAT66 / prefix-translating router legitimately lands here — the log
    // says so explicitly rather than silently degrading.
    if (!isLocalIPv6(addr)) {
        logInfo(QStringLiteral("IPv6: %1 reports our egress as %2, which is NOT assigned to any "
                               "local interface — ignoring, keeping %3")
                    .arg(who, addr.toString(),
                         publicIPv6().isNull() ? QStringLiteral("none") : publicIPv6().toString()));
        return;
    }

    if (addr == m_publicIPv6Server)
        return;

    m_publicIPv6Server = addr;
    logDebug(QStringLiteral("IPv6: %1 observed our egress as %2 on an IPv6 session — accepted "
                            "(held locally)")
                 .arg(who, addr.toString()));
    noteEffectiveIPv6Change();
}

void AppContext::clearPublicIPv6Observed()
{
    m_publicIPv6Status = 0;
    if (m_publicIPv6Server.isNull())
        return;
    logDebug(QStringLiteral("IPv6: dropping server-observed egress %1 (session ended)")
                 .arg(m_publicIPv6Server.toString()));
    m_publicIPv6Server = Address{};
    noteEffectiveIPv6Change();
}

bool AppContext::publicIPv6ProbedUnreachable() const
{
    // The server actually dial-back-probed our advertised v6 and it failed — do not
    // present ourselves as v6-reachable. An unset/absent status (0) means "unknown".
    return (m_publicIPv6Status & IPV6ST_PROBED) && !(m_publicIPv6Status & IPV6ST_REACHABLE);
}

void AppContext::recordPeerObservedIPv6(const Address& candidate, const QByteArray& peerKey)
{
    // Only a genuine global-unicast address can be ours; a peer claiming anything else is
    // noise. Peer corroboration is the weakest confident tier and is used only while no
    // server has observed our egress and no operator pin exists (see publicIPv6()).
    if (!isUsablePublicIPv6(candidate) || peerKey.isEmpty())
        return;

    // Peers are unauthenticated, so corroboration alone must never be able to make us
    // advertise a foreign address. Requiring the candidate to be one we actually hold bounds
    // the damage to "picks the wrong one of our own addresses", which is exactly what this
    // tier exists to disambiguate.
    if (!isLocalIPv6(candidate)) {
        // Throttled by address *and* time — a hostile peer can name unlimited distinct
        // addresses, so this must not accumulate per-candidate state.
        const qint64 tick = static_cast<qint64>(getTickCount());
        if (!(candidate == m_ipv6LastRejected) || tick - m_ipv6LastRejectTick > 60 * 1000) {
            m_ipv6LastRejected = candidate;
            m_ipv6LastRejectTick = tick;
            logDebug(QStringLiteral("IPv6: rejecting peer-reported %1 — not assigned to any "
                                    "local interface")
                         .arg(candidate.toString()));
        }
        return;
    }

    const auto now = static_cast<std::int64_t>(getTickCount());
    m_ipv6PeerVotes.record(candidate, peerKey, now);

    const std::size_t bestCount = recomputePeerCorroboratedIPv6();
    const std::size_t threshold = peerConfirmThreshold();

    const std::size_t candCount = m_ipv6PeerVotes.voterCount(candidate);
    logDebug(QStringLiteral("IPv6: peer %1 reports our IPv6 as %2 (%3/%4 distinct peers)")
                 .arg(QString::fromUtf8(peerKey), candidate.toString())
                 .arg(candCount).arg(threshold));

    // Note once per distinct candidate when a peer reports an address that differs from the
    // one we currently present. Bounded: only locally-held addresses reach this point, and
    // the latch is erased with its vote entry when the window expires. markWarnedOnce()
    // mutates, so it stays last in the chain — the latch must only be set when the two
    // conditions before it hold.
    const Address chosen = publicIPv6();
    if (!chosen.isNull() && !(candidate == chosen) && m_ipv6PeerVotes.markWarnedOnce(candidate)) {
        logDebug(QStringLiteral("IPv6: a peer reports our public IPv6 as %1, which differs from "
                                "our chosen %2 (%3 peer(s) agree; need %4 to adopt)")
                     .arg(candidate.toString(), chosen.toString())
                     .arg(bestCount).arg(threshold));
    }
}

} // namespace eMule
