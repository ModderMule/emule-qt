#pragma once

/// @file LocalIPv6.h
/// @brief Local IPv6 address enumeration with RFC 4941 privacy-address classification.
///
/// Qt cannot do this on its own: QNetworkAddressEntry::isTemporary() is defined as
/// !isPermanent(), and isPermanent() keys off an *infinite* lifetime. Under SLAAC both
/// the stable and the temporary address have finite lifetimes, so isTemporary() returns
/// true for both and is useless as a discriminator. Hence the platform backends below.
///
/// Why it matters: our public IPv6 is advertised to servers and peers (CT_MOD_IP_V6) and
/// is mixed into the obfuscated-UDP key (EncryptedDatagramSocket::encryptSendClient).
/// The receiver rebuilds that key from the IP it *observes*. Advertising a temporary
/// address that later rotates both expires published sources and breaks key agreement.

#include "net/Address.h"

#include <QString>
#include <QStringList>
#include <QStringView>

#include <vector>

namespace eMule {

/// How the interface identifier (low 64 bits) of a local IPv6 address was formed.
enum class IPv6AddressKind : uint8 {
    Unknown = 0,  ///< Platform could not classify — treat as usable, do not advise.
    Stable,       ///< Persistent: EUI-64, RFC 7217 stable-privacy, DHCPv6 or manual.
    Temporary,    ///< RFC 4941 temporary/anonymous — rotates; never advertise.
};

/// One global-unicast IPv6 address found on a local interface.
struct LocalIPv6Address {
    Address         address;                 ///< Never carries a scope id.
    QString         interfaceName;           ///< "en0" / "eth0" / adapter name.
    IPv6AddressKind kind = IPv6AddressKind::Unknown;
    bool            deprecated = false;      ///< Preferred lifetime expired.
    bool            tentative  = false;      ///< DAD unfinished or failed — unusable.
    uint32          validLifetimeSecs = 0;   ///< 0 = unknown, UINT32_MAX = infinite.
};

/// System-wide RFC 4941 privacy-extension policy.
enum class IPv6PrivacyPolicy : uint8 {
    Unknown = 0,       ///< Not determinable here — stay silent rather than guess.
    Disabled,          ///< Temporary addresses are not generated.
    Enabled,           ///< Generated, but the stable address is preferred as source.
    EnabledPreferred,  ///< Generated AND preferred as the outgoing source address.
};

/// Result of one scan. Cheap to copy.
struct IPv6PrivacyReport {
    IPv6PrivacyPolicy             policy = IPv6PrivacyPolicy::Unknown;
    std::vector<LocalIPv6Address> addresses;  ///< Global-unicast only, in OS order.

    [[nodiscard]] bool hasTemporaryAddress() const;
    [[nodiscard]] bool hasStableAddress() const;
};

// -- Scanning ----------------------------------------------------------------

/// Enumerate local global-unicast IPv6 addresses, classify each, and read the OS
/// privacy setting. Never blocks, never spawns a subprocess. Returns an empty report
/// with policy == Unknown where no detection path exists.
[[nodiscard]] IPv6PrivacyReport scanLocalIPv6();

// -- Selection (pure, unit-testable) -----------------------------------------

/// Pick the address to advertise as our public IPv6.
/// Tier order: Stable && !deprecated → Unknown && !deprecated → Temporary && !deprecated
///             → any !tentative → null.
/// Unknown outranks Temporary deliberately: an unclassifiable address is far more likely
/// stable, so a detection failure can never make selection worse than plain first-match.
/// Only Address::isPublicIP() candidates are considered; tentative is never selected.
[[nodiscard]] Address selectPreferredIPv6(const IPv6PrivacyReport& report);

/// Resolve the operator's publicIPv6Override preference against @p report.
/// Returns null when @p override is empty, is not an IPv6 literal, is not global-unicast,
/// or is not assigned to any local interface. @p reason receives a log-ready explanation
/// (empty when the preference was simply unset).
[[nodiscard]] Address resolveIPv6Override(const QString& overrideLiteral,
                                          const IPv6PrivacyReport& report,
                                          QString* reason = nullptr);

// -- Advisory ----------------------------------------------------------------

/// Platform command that disables privacy addresses; empty if unknown.
[[nodiscard]] QString ipv6PrivacyDisableCommand();

/// Commands that remove the temporary addresses already present in @p report. Disabling the
/// policy only stops *new* ones — those already assigned keep their SLAAC lifetime and stay
/// preferred as the outgoing source, so the disable command alone does not clear the
/// advertised-vs-source mismatch. Empty when @p report holds no temporary address, or where
/// the platform has no supported per-address removal (Windows).
[[nodiscard]] QStringList ipv6TemporaryAddressRemovalCommands(const IPv6PrivacyReport& report);

/// Emit the startup narration for @p report: how @p effective was chosen (auto-selected
/// or from publicIPv6Override, including why a bad override was rejected), followed by
/// the privacy-address advisory when temporary addressing is actually in play.
/// Call exactly once per process — CoreSession::initLocalIPv6() is the only call site.
/// All override diagnostics live here rather than in updatePublicIPv6() so a misconfigured
/// override is reported once at startup instead of on every server reconnect.
void logIPv6PrivacyAdvisory(const IPv6PrivacyReport& report, const Address& effective);

/// Select (honouring the publicIPv6Override pref) and publish into AppContext.
/// Publishes only a non-null selection, so a peer-learned publicIPv6 is never clobbered
/// on a v4-only host. Silent except when the published address actually changes, so the
/// per-reconnect refresh from ServerConnect::initLocalIP() produces no log noise.
/// Returns the address now in effect. Safe to call repeatedly.
Address updatePublicIPv6(const IPv6PrivacyReport& report);

// -- Exposed for unit tests ---------------------------------------------------

/// Parse /proc/net/if_inet6 text: "<32 hex> <ifindex> <prefixlen> <scope> <flags> <name>".
/// The kernel prints (u8)flags, so only 0x01 TEMPORARY/SECONDARY, 0x20 DEPRECATED,
/// 0x40 TENTATIVE and 0x80 PERMANENT are observable — 0x800 STABLE_PRIVACY is truncated
/// away. Only global scope (00) entries are returned.
[[nodiscard]] std::vector<LocalIPv6Address> parseProcNetIfInet6(QStringView text);

/// Windows tie-breaker: within one interface and one /64, when several addresses report
/// a random suffix the shortest-lived one is the RFC 4941 temporary address and the rest
/// are stable. Groups of one are left Unknown — with nothing to compare against there is
/// no evidence either way, and Unknown already outranks Temporary in selection.
void classifyByPrefixLifetime(std::vector<LocalIPv6Address>& addrs);

} // namespace eMule
