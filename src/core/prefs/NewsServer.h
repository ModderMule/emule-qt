#pragma once

/// @file NewsServer.h
/// @brief One configured Usenet provider account.
///
/// Lives in core/prefs rather than in src/usenet because it is a stored
/// preference first and a protocol object second — the same call made for
/// HttpCacheServerConfig, and for the same reason: the file format, the
/// encryption of the credential and the identity rules are Preferences'
/// business, and core may not depend on the usenet module.
/// src/usenet/nntp/NewsServer.h aliases these names back into eMule::usenet.
///
/// Field set ported from NZBGet's `daemon/nntp/NewsServer.h`, not from ngPost's
/// flat POD. The difference matters: ngPost is a poster and has no failover, so
/// it carries only host/port/auth/connections. A downloader needs the tier
/// fields — `level`, `group`, `optional`, `retention` — because those are what
/// make block-account and fill-server setups work. An article missing on a
/// level-0 server is retried at level 1 with the tried servers excluded; that
/// escalation is the whole of Usenet fault tolerance.

#include <QString>
#include <QtTypes>

namespace eMule {

/// How TLS is established.
///
/// Deliberately an enum rather than SmtpClient's hardcoded `port == 465` test:
/// NNTP's implicit-TLS port is 563, providers also offer 443 to get through
/// restrictive firewalls, and some run STARTTLS on the cleartext port 119.
/// A magic number cannot express that.
enum class NntpTlsMode : quint8 {
    None = 0,      ///< Cleartext. Most providers still accept it on 119.
    Implicit = 1,  ///< TLS from the first byte (563, 443).
    StartTls = 2,  ///< Connect cleartext, then upgrade with STARTTLS (RFC 4642).
};

/// How hard to check the provider's certificate.
/// Mirrors NZBGet's `certVerificationLevel` so a user migrating a working
/// configuration finds the same three choices.
enum class NntpCertVerification : quint8 {
    None = 0,     ///< Accept anything. Needed for a few providers with broken chains.
    Minimal = 1,  ///< Chain must validate; hostname mismatch tolerated.
    Strict = 2,   ///< Chain and hostname must both match. The default.
};

/// Default ports, by TLS mode.
inline constexpr quint16 kDefaultNntpPort = 119;
inline constexpr quint16 kDefaultNntpTlsPort = 563;

/// Default simultaneous connections to one provider.
///
/// The connection count *is* the download rate: a provider caps a single
/// connection at a fraction of the line, so 8 connections measured 1.2 MB/s
/// where 40 measured 4.4-4.9 on the same post (docs/usenet-module.md, "Why a
/// download is slow"). Every plan sold today allows far more than 8. Above what
/// the plan allows the answer is 502 and a backed-off server, which is why the
/// spin box still caps at 100 and still carries its warning.
inline constexpr int kDefaultMaxConnections = 40;

/// A provider account. Plain value type — copied freely, no identity.
struct NewsServer {
    QString name;                  ///< Display only.
    QString host;
    quint16 port = kDefaultNntpTlsPort;
    NntpTlsMode tlsMode = NntpTlsMode::Implicit;
    QString user;
    QString pass;                  ///< Plaintext in memory, AES-encrypted in the YAML.

    /// Priority tier. Lower is tried first. Servers on the same level are
    /// equals and rotate; a higher level is only reached when every server on
    /// the level below reported the article missing.
    int level = 0;

    /// Servers sharing a group id count as one account for connection limits —
    /// the same provider reached through two hostnames, say.  0 = ungrouped.
    int group = 0;

    /// An optional server never fails a download on its own: if it is down,
    /// the article is simply tried elsewhere. Block accounts are optional.
    bool optional = false;

    /// Retention in days, 0 = unknown/unlimited. Used to skip a server for an
    /// article older than it can possibly hold, rather than paying a round trip
    /// to be told 430.
    int retention = 0;

    /// Whether to issue GROUP before each article fetch. Almost no provider
    /// needs it for message-id access, and it costs a round trip, but a few
    /// old servers refuse BODY <msgid> without it.
    bool joinGroup = false;

    int maxConnections = kDefaultMaxConnections;
    NntpCertVerification certVerification = NntpCertVerification::Strict;
    bool enabled = true;

    /// Whether this entry is usable at all. An empty host is the one hard
    /// requirement; everything else has a working default.
    [[nodiscard]] bool isValid() const { return !host.isEmpty() && port != 0; }

    /// What to show in a list when the user did not name the account.
    [[nodiscard]] QString displayName() const { return name.isEmpty() ? host : name; }

    /// Stable identity for the connection pool and the "servers already tried"
    /// exclusion list. Deliberately derived from the connection parameters
    /// rather than a list index: the index changes when the user reorders the
    /// list, and an article half way through a failover ladder would then be
    /// excluded from the wrong server.
    [[nodiscard]] QString key() const
    {
        return QStringLiteral("%1:%2/%3").arg(host).arg(port).arg(user);
    }
};

} // namespace eMule
