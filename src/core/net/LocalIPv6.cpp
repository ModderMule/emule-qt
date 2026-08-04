#include "pch.h"
/// @file LocalIPv6.cpp
/// @brief Platform backends for local IPv6 enumeration and privacy-address detection.
///
/// Layout: each platform contributes only raw fact-gathering (flags, lifetimes, the OS
/// privacy setting). Every decision — filtering, ranking, override resolution, advisory
/// wording — lives in the pure functions at the bottom and is unit-tested without any
/// OS involvement. One .cpp rather than three because src/core/CMakeLists.txt:21 globs
/// net/*.cpp, so per-platform files would all be compiled on every platform.

#include "net/LocalIPv6.h"
#include "app/AppContext.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QFile>
#include <QStringList>

#include <algorithm>
#include <cstring>

#ifdef Q_OS_MACOS
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <QSettings>
#else
#include <QFileInfo>
#endif

namespace eMule {

namespace {

/// Common post-processing: drop anything that is not a usable global-unicast address.
void filterToGlobalUnicast(std::vector<LocalIPv6Address>& addrs)
{
    std::erase_if(addrs, [](const LocalIPv6Address& a) {
        return !a.address.isIPv6() || !a.address.isPublicIP();
    });
}

} // namespace

// ============================================================================
// IPv6PrivacyReport
// ============================================================================

bool IPv6PrivacyReport::hasTemporaryAddress() const
{
    return std::any_of(addresses.begin(), addresses.end(), [](const LocalIPv6Address& a) {
        return a.kind == IPv6AddressKind::Temporary;
    });
}

bool IPv6PrivacyReport::hasStableAddress() const
{
    return std::any_of(addresses.begin(), addresses.end(), [](const LocalIPv6Address& a) {
        return a.kind == IPv6AddressKind::Stable && !a.deprecated && !a.tentative;
    });
}

// ============================================================================
// Platform backend — macOS
// ============================================================================

#ifdef Q_OS_MACOS

IPv6PrivacyReport scanLocalIPv6()
{
    IPv6PrivacyReport report;

    // Policy via sysctl — a direct libc call, no subprocess.
    int useTemp = 0;
    std::size_t len = sizeof(useTemp);
    if (::sysctlbyname("net.inet6.ip6.use_tempaddr", &useTemp, &len, nullptr, 0) == 0) {
        if (useTemp == 0) {
            report.policy = IPv6PrivacyPolicy::Disabled;
        } else {
            int preferTemp = 0;
            len = sizeof(preferTemp);
            const bool havePrefer =
                ::sysctlbyname("net.inet6.ip6.prefer_tempaddr", &preferTemp, &len, nullptr, 0) == 0;
            report.policy = (havePrefer && preferTemp != 0) ? IPv6PrivacyPolicy::EnabledPreferred
                                                            : IPv6PrivacyPolicy::Enabled;
        }
    }

    ifaddrs* ifList = nullptr;
    if (::getifaddrs(&ifList) != 0)
        return report;

    // One socket for the whole scan; SIOCGIFAFLAG_IN6 needs an AF_INET6 handle.
    const int sock = ::socket(AF_INET6, SOCK_DGRAM, 0);

    for (const ifaddrs* ifa = ifList; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET6)
            continue;

        const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(ifa->ifa_addr);

        LocalIPv6Address entry;
        entry.address       = Address::fromIPv6Bytes(
            reinterpret_cast<const uint8*>(&sin6->sin6_addr));
        entry.interfaceName = QString::fromUtf8(ifa->ifa_name);

        if (sock >= 0) {
            in6_ifreq ifr{};
            std::strncpy(ifr.ifr_name, ifa->ifa_name, sizeof(ifr.ifr_name) - 1);
            std::memcpy(&ifr.ifr_ifru.ifru_addr, sin6, sizeof(sockaddr_in6));

            if (::ioctl(sock, SIOCGIFAFLAG_IN6, &ifr) == 0) {
                const int flags = ifr.ifr_ifru.ifru_flags6;
                // IN6_IFF_TEMPORARY is exactly what `ifconfig` prints as "temporary";
                // IN6_IFF_SECURED is the RFC 7217 stable-privacy address ("secured").
                if (flags & IN6_IFF_TEMPORARY)
                    entry.kind = IPv6AddressKind::Temporary;
                else
                    entry.kind = IPv6AddressKind::Stable;

                entry.deprecated = (flags & IN6_IFF_DEPRECATED) != 0;
                entry.tentative  = (flags & IN6_IFF_NOTREADY) != 0;
            }
            // ioctl failure leaves kind == Unknown, which selection handles safely.
        }

        report.addresses.push_back(std::move(entry));
    }

    if (sock >= 0)
        ::close(sock);
    ::freeifaddrs(ifList);

    filterToGlobalUnicast(report.addresses);
    return report;
}

QString ipv6PrivacyDisableCommand()
{
    // prefer_tempaddr must go too: use_tempaddr=0 only stops *new* temporary addresses, so
    // any address already assigned stays preferred as the outgoing source and the
    // advertised-vs-source mismatch persists. Measured on macOS 15 — see
    // docs/protocol/ipv6-direct-connect.local.md section 4.2.
    return QStringLiteral("sudo sysctl -w net.inet6.ip6.use_tempaddr=0"
                          " net.inet6.ip6.prefer_tempaddr=0"
                          "   (also switch off Wi-Fi > \"Limit IP Address Tracking\", which"
                          " drives this sysctl; persist in /etc/sysctl.conf)");
}

QStringList ipv6TemporaryAddressRemovalCommands(const IPv6PrivacyReport& report)
{
    QStringList cmds;
    for (const auto& a : report.addresses) {
        if (a.kind != IPv6AddressKind::Temporary || a.interfaceName.isEmpty())
            continue;
        cmds << QStringLiteral("sudo ifconfig %1 inet6 %2 -alias")
                    .arg(a.interfaceName, a.address.toString());
    }
    return cmds;
}

// ============================================================================
// Platform backend — Windows
// ============================================================================

#elif defined(Q_OS_WIN)

IPv6PrivacyReport scanLocalIPv6()
{
    IPv6PrivacyReport report;

    // Policy via registry. Absent means ENABLED — that is the Windows default.
    {
        QSettings reg(QStringLiteral(
                          "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters"),
                      QSettings::NativeFormat);
        const QVariant v = reg.value(QStringLiteral("UseTemporaryAddresses"));
        if (!v.isValid()) {
            report.policy = IPv6PrivacyPolicy::EnabledPreferred;
        } else {
            switch (v.toInt()) {
            case 0:  report.policy = IPv6PrivacyPolicy::Disabled;         break;
            case 1:  report.policy = IPv6PrivacyPolicy::Enabled;          break;
            default: report.policy = IPv6PrivacyPolicy::EnabledPreferred; break;
            }
        }
    }

    // Two-pass GetAdaptersAddresses — the first call reports the needed size.
    ULONG size = 0;
    const ULONG gaaFlags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
                         | GAA_FLAG_SKIP_DNS_SERVER;
    if (::GetAdaptersAddresses(AF_INET6, gaaFlags, nullptr, nullptr, &size) != ERROR_BUFFER_OVERFLOW)
        return report;

    std::vector<uint8> buffer(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    if (::GetAdaptersAddresses(AF_INET6, gaaFlags, nullptr, adapters, &size) != NO_ERROR)
        return report;

    for (const IP_ADAPTER_ADDRESSES* ad = adapters; ad != nullptr; ad = ad->Next) {
        if (ad->IfType == IF_TYPE_SOFTWARE_LOOPBACK || ad->OperStatus != IfOperStatusUp)
            continue;

        const QString ifName = QString::fromWCharArray(ad->FriendlyName);

        for (const IP_ADAPTER_UNICAST_ADDRESS* ua = ad->FirstUnicastAddress;
             ua != nullptr; ua = ua->Next)
        {
            if (ua->Address.lpSockaddr == nullptr
                || ua->Address.lpSockaddr->sa_family != AF_INET6)
                continue;

            const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(ua->Address.lpSockaddr);

            LocalIPv6Address entry;
            entry.address       = Address::fromIPv6Bytes(
                reinterpret_cast<const uint8*>(&sin6->sin6_addr));
            entry.interfaceName = ifName;
            entry.validLifetimeSecs = ua->ValidLifetime;
            entry.deprecated = (ua->DadState == IpDadStateDeprecated);
            entry.tentative  = (ua->DadState == IpDadStateTentative
                                || ua->DadState == IpDadStateDuplicate
                                || ua->DadState == IpDadStateInvalid);

            // A non-random suffix is unambiguously stable. IpSuffixOriginRandom is NOT
            // a discriminator on its own: with randomizeidentifiers enabled (the Win10+
            // default) the stable SLAAC address also reports Random. Those stay Unknown
            // here and are resolved by lifetime below.
            switch (ua->SuffixOrigin) {
            case IpSuffixOriginLinkLayerAddress:
            case IpSuffixOriginManual:
            case IpSuffixOriginDhcp:
            case IpSuffixOriginWellKnown:
                entry.kind = IPv6AddressKind::Stable;
                break;
            default:
                break; // leave Unknown for the lifetime pass
            }

            report.addresses.push_back(std::move(entry));
        }
    }

    filterToGlobalUnicast(report.addresses);

    // With privacy off there are no temporary addresses at all, so anything still
    // Unknown must be stable. Otherwise disambiguate by lifetime within each /64.
    if (report.policy == IPv6PrivacyPolicy::Disabled) {
        for (auto& a : report.addresses) {
            if (a.kind == IPv6AddressKind::Unknown)
                a.kind = IPv6AddressKind::Stable;
        }
    } else {
        classifyByPrefixLifetime(report.addresses);
    }

    return report;
}

QString ipv6PrivacyDisableCommand()
{
    return QStringLiteral("netsh interface ipv6 set privacy state=disabled store=persistent"
                          "   (run as Administrator, then restart eMuleQt)");
}

QStringList ipv6TemporaryAddressRemovalCommands(const IPv6PrivacyReport&)
{
    // No supported per-address delete for an autoconfigured temporary address: netsh's
    // store=persistent change takes effect on the next interface reset. Deliberately empty
    // so the advisory says nothing rather than something unactionable.
    return {};
}

// ============================================================================
// Platform backend — Linux and other Unix
// ============================================================================

#else

IPv6PrivacyReport scanLocalIPv6()
{
    IPv6PrivacyReport report;

    // Policy: /proc/sys/net/ipv6/conf/all/use_tempaddr, falling back to default/.
    for (const auto* path : {"/proc/sys/net/ipv6/conf/all/use_tempaddr",
                             "/proc/sys/net/ipv6/conf/default/use_tempaddr"}) {
        QFile f(QString::fromLatin1(path));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        bool ok = false;
        const int v = f.readAll().trimmed().toInt(&ok);
        if (!ok)
            continue;
        report.policy = (v == 0) ? IPv6PrivacyPolicy::Disabled
                      : (v == 1) ? IPv6PrivacyPolicy::Enabled
                                 : IPv6PrivacyPolicy::EnabledPreferred;
        break;
    }

    // Addresses: /proc/net/if_inet6 carries per-address flags; getifaddrs() does not
    // (glibc copies the *link* flags into ifa_flags, which say nothing about RFC 4941).
    QFile f(QStringLiteral("/proc/net/if_inet6"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        report.addresses = parseProcNetIfInet6(QString::fromLatin1(f.readAll()));

    filterToGlobalUnicast(report.addresses);
    return report;
}

QString ipv6PrivacyDisableCommand()
{
    // No prefer_tempaddr knob here: on Linux use_tempaddr itself carries the preference
    // (0 = off, 1 = generate but prefer the stable address, 2 = generate and prefer).
    // NetworkManager's per-connection ipv6.ip6-privacy overrides the sysctl on managed links.
    return QStringLiteral("sudo sysctl -w net.ipv6.conf.all.use_tempaddr=0"
                          "   (persist in /etc/sysctl.d/99-emuleqt.conf; under NetworkManager"
                          " also set the connection's ipv6.ip6-privacy=0)");
}

QStringList ipv6TemporaryAddressRemovalCommands(const IPv6PrivacyReport& report)
{
    // One flush per interface, not per address: `ip addr flush` filters by the temporary flag.
    QStringList ifaces;
    for (const auto& a : report.addresses) {
        if (a.kind == IPv6AddressKind::Temporary && !a.interfaceName.isEmpty()
            && !ifaces.contains(a.interfaceName))
            ifaces << a.interfaceName;
    }
    QStringList cmds;
    for (const QString& name : ifaces)
        cmds << QStringLiteral("sudo ip -6 addr flush dev %1 temporary").arg(name);
    return cmds;
}

#endif // platform backends

// ============================================================================
// Pure helpers — no OS involvement, fully unit-testable
// ============================================================================

std::vector<LocalIPv6Address> parseProcNetIfInet6(QStringView text)
{
    // Format: <32 hex addr> <ifindex> <prefixlen> <scope> <flags> <devname>
    std::vector<LocalIPv6Address> out;

    for (const auto lineView : text.split(u'\n', Qt::SkipEmptyParts)) {
        const QString line = lineView.toString().simplified();
        const QStringList cols = line.split(u' ', Qt::SkipEmptyParts);
        if (cols.size() < 6)
            continue;

        const QString hex = cols[0];
        if (hex.size() != 32)
            continue;

        bool scopeOk = false;
        const int scope = cols[3].toInt(&scopeOk, 16);
        if (!scopeOk || scope != 0)   // 0 == global
            continue;

        bool flagsOk = false;
        const uint32 flags = cols[4].toUInt(&flagsOk, 16);
        if (!flagsOk)
            continue;

        uint8 bytes[16];
        bool bytesOk = true;
        for (int i = 0; i < 16; ++i) {
            bool byteOk = false;
            bytes[i] = static_cast<uint8>(hex.mid(i * 2, 2).toUInt(&byteOk, 16));
            if (!byteOk) { bytesOk = false; break; }
        }
        if (!bytesOk)
            continue;

        LocalIPv6Address entry;
        entry.address       = Address::fromIPv6Bytes(bytes);
        entry.interfaceName = cols[5];

        // The kernel prints (u8)flags, so IFA_F_STABLE_PRIVACY (0x800) is truncated away
        // and cannot be observed here. IFA_F_TEMPORARY and IFA_F_SECONDARY share bit 0,
        // so require PERMANENT (0x80) to be clear before calling it temporary — a
        // manually added secondary address is permanent and must stay Stable.
        constexpr uint32 kTemporary  = 0x01;
        constexpr uint32 kDeprecated = 0x20;
        constexpr uint32 kTentative  = 0x40;
        constexpr uint32 kPermanent  = 0x80;

        if ((flags & kTemporary) && !(flags & kPermanent))
            entry.kind = IPv6AddressKind::Temporary;
        else
            entry.kind = IPv6AddressKind::Stable;

        entry.deprecated = (flags & kDeprecated) != 0;
        entry.tentative  = (flags & kTentative) != 0;

        out.push_back(std::move(entry));
    }

    return out;
}

void classifyByPrefixLifetime(std::vector<LocalIPv6Address>& addrs)
{
    // Group by interface + /64. Within a group of two or more still-Unknown addresses,
    // the shortest-lived is the RFC 4941 temporary one (bounded by TempValidLifetime,
    // 7 days by default) and the rest are stable (RA lifetimes are typically 30 days).
    for (std::size_t i = 0; i < addrs.size(); ++i) {
        if (addrs[i].kind != IPv6AddressKind::Unknown)
            continue;

        std::vector<std::size_t> group{i};
        for (std::size_t j = i + 1; j < addrs.size(); ++j) {
            if (addrs[j].kind != IPv6AddressKind::Unknown)
                continue;
            if (addrs[j].interfaceName != addrs[i].interfaceName)
                continue;
            if (std::memcmp(addrs[j].address.ipv6Bytes().data(),
                            addrs[i].address.ipv6Bytes().data(), 8) != 0)
                continue;
            group.push_back(j);
        }

        if (group.size() < 2)
            continue;   // nothing to compare against — leave Unknown

        std::size_t shortest = group.front();
        bool tie = false;
        for (const std::size_t idx : group) {
            if (idx == shortest)
                continue;
            if (addrs[idx].validLifetimeSecs < addrs[shortest].validLifetimeSecs) {
                shortest = idx;
                tie = false;
            } else if (addrs[idx].validLifetimeSecs == addrs[shortest].validLifetimeSecs) {
                tie = true;
            }
        }
        if (tie)
            continue;   // equal lifetimes carry no signal — do not guess

        for (const std::size_t idx : group)
            addrs[idx].kind = (idx == shortest) ? IPv6AddressKind::Temporary
                                                : IPv6AddressKind::Stable;
    }
}

Address selectPreferredIPv6(const IPv6PrivacyReport& report)
{
    const auto pick = [&report](auto&& predicate) -> Address {
        for (const auto& a : report.addresses) {
            if (a.tentative || !a.address.isPublicIP())
                continue;
            if (predicate(a))
                return a.address;
        }
        return {};
    };

    // Tier 1 — the goal.
    if (const Address a = pick([](const LocalIPv6Address& e) {
            return e.kind == IPv6AddressKind::Stable && !e.deprecated; });
        !a.isNull())
        return a;

    // Tier 2 — detection unavailable; identical to the old first-match behaviour.
    if (const Address a = pick([](const LocalIPv6Address& e) {
            return e.kind == IPv6AddressKind::Unknown && !e.deprecated; });
        !a.isNull())
        return a;

    // Tier 3 — rotating beats nothing; the advisory explains the cost.
    if (const Address a = pick([](const LocalIPv6Address& e) {
            return e.kind == IPv6AddressKind::Temporary && !e.deprecated; });
        !a.isNull())
        return a;

    // Tier 4 — deprecated is still routable for existing flows.
    return pick([](const LocalIPv6Address&) { return true; });
}

Address resolveIPv6Override(const QString& overrideLiteral,
                            const IPv6PrivacyReport& report,
                            QString* reason)
{
    const auto fail = [reason](const QString& why) -> Address {
        if (reason != nullptr)
            *reason = why;
        return {};
    };

    if (reason != nullptr)
        reason->clear();

    const QString trimmed = overrideLiteral.trimmed();
    if (trimmed.isEmpty())
        return {};

    const Address wanted = Address::fromString(trimmed);
    if (wanted.isNull() || !wanted.isIPv6())
        return fail(QStringLiteral("publicIPv6Override '%1' is not a valid IPv6 literal — ignoring")
                        .arg(trimmed));

    if (!wanted.isPublicIP())
        return fail(QStringLiteral("publicIPv6Override %1 is not a global-unicast address — ignoring")
                        .arg(wanted.toString()));

    const auto it = std::find_if(report.addresses.begin(), report.addresses.end(),
                                 [&wanted](const LocalIPv6Address& a) {
                                     return a.address == wanted && !a.tentative;
                                 });
    if (it == report.addresses.end())
        return fail(QStringLiteral("publicIPv6Override %1 is not assigned to any local "
                                   "interface — falling back to auto-selection")
                        .arg(wanted.toString()));

    if (reason != nullptr) {
        *reason = QStringLiteral("using configured publicIPv6Override %1 on %2")
                      .arg(wanted.toString(), it->interfaceName);
        if (it->kind == IPv6AddressKind::Temporary)
            reason->append(QStringLiteral(" (this is a temporary privacy address and will rotate)"));
    }
    return wanted;
}

// ============================================================================
// Advisory and publication
// ============================================================================

void logIPv6PrivacyAdvisory(const IPv6PrivacyReport& report, const Address& effective)
{
    // Override outcome first — this is the only place it is reported, so a typo is
    // surfaced once at startup rather than on every reconnect through updatePublicIPv6().
    QString reason;
    const Address pinned = resolveIPv6Override(thePrefs.publicIPv6Override(), report, &reason);
    if (!reason.isEmpty()) {
        if (pinned.isNull())
            logError(QStringLiteral("IPv6: %1").arg(reason));   // logWarning is hidden
        else
            logInfo(QStringLiteral("IPv6: %1").arg(reason));
    }

    if (effective.isNull()) {
        logDebug(QStringLiteral("IPv6: no global-unicast address on any interface"));
        return;
    }

    // Interface and kind of the address actually in effect.
    QString ifName;
    IPv6AddressKind kind = IPv6AddressKind::Unknown;
    for (const auto& a : report.addresses) {
        if (a.address == effective) {
            ifName = a.interfaceName;
            kind   = a.kind;
            break;
        }
    }
    const Address selected = effective;

    const bool privacyOn = (report.policy == IPv6PrivacyPolicy::Enabled
                            || report.policy == IPv6PrivacyPolicy::EnabledPreferred);

    // Line 1 — what we will advertise. Do not claim a stable address is unavailable:
    // this branch is also reached when the operator pinned a temporary one on purpose.
    if (kind == IPv6AddressKind::Temporary) {
        logInfo(QStringLiteral("IPv6: advertising temporary privacy address %1 on %2 — "
                               "it will rotate and published sources will expire%3")
                    .arg(selected.toString(), ifName,
                         report.hasStableAddress()
                             ? QStringLiteral(" (a stable address is available)")
                             : QString()));
    } else {
        logInfo(QStringLiteral("IPv6: public address %1 (%2%3)")
                    .arg(selected.toString(),
                         kind == IPv6AddressKind::Stable ? QStringLiteral("stable")
                                                         : QStringLiteral("unclassified"),
                         ifName.isEmpty() ? QString() : QStringLiteral(", ") + ifName));
    }

    if (report.policy == IPv6PrivacyPolicy::Unknown) {
        logDebug(QStringLiteral("IPv6: privacy-extension state could not be determined "
                                "on this platform"));
        return;
    }
    if (!privacyOn || !report.hasTemporaryAddress())
        return;

    // Lines 2-3 — only when privacy addressing is actually in play.
    QString why = QStringLiteral(
        "IPv6: this system also holds a temporary privacy address (RFC 4941)");
    if (report.policy == IPv6PrivacyPolicy::EnabledPreferred) {
        why += QStringLiteral(" and prefers it as the outgoing source. Peers then see a source IP "
                              "different from the one we advertise, which breaks obfuscated IPv6 "
                              "UDP and expires published sources when the address rotates.");
    } else {
        why += QStringLiteral(". Published sources expire when it rotates.");
    }
    logInfo(why);

    const QString cmd = ipv6PrivacyDisableCommand();
    if (!cmd.isEmpty())
        logInfo(QStringLiteral("IPv6: to disable privacy addresses run:  %1").arg(cmd));

    // The policy change only stops *new* temporary addresses. Ones already assigned keep
    // their SLAAC lifetime — up to a week — and stay preferred as the outgoing source until
    // removed, so the advisory is incomplete without these. Naming the actual addresses
    // costs nothing: they are already in the report.
    for (const QString& removal : ipv6TemporaryAddressRemovalCommands(report))
        logInfo(QStringLiteral("IPv6: the policy change does not remove addresses already "
                               "assigned — also run:  %1").arg(removal));
}

Address updatePublicIPv6(const IPv6PrivacyReport& report)
{
    // The set of addresses we actually hold gates every *reflected* address (server- or
    // peer-reported), so publish it first — this call also re-validates any reflection
    // already adopted, which is how a prefix renumber gets noticed.
    std::vector<Address> local;
    local.reserve(report.addresses.size());
    for (const LocalIPv6Address& a : report.addresses) {
        if (!a.tentative && a.address.isPublicIP())
            local.push_back(a.address);
    }
    theApp.setLocalIPv6Addresses(std::move(local));

    // Silent on purpose: this runs again on every server reconnect. Override diagnostics are
    // emitted once at startup by logIPv6PrivacyAdvisory(), and AppContext logs the effective
    // address whenever the winning tier changes.
    const Address override = resolveIPv6Override(thePrefs.publicIPv6Override(), report, nullptr);
    theApp.setPublicIPv6Override(override);

    // Feed the auto-selection its own slot. Comparing against theApp.publicIPv6() instead
    // would compare a tier-4 candidate with whatever higher tier currently wins.
    const Address auto_ = selectPreferredIPv6(report);
    if (!auto_.isNull())
        theApp.setPublicIPv6Local(auto_);

    return theApp.publicIPv6();
}

} // namespace eMule
