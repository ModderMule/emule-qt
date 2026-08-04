#include "pch.h"
/// @file ED2KLink.cpp
/// @brief ED2K link parsing implementation — port of CED2KLink from MFC.

#include "ED2KLink.h"

#include "app/AppContext.h"
#include "prefs/Preferences.h"

#include <QHostAddress>
#include <QUrl>

#include <algorithm>

namespace eMule {

namespace {

/// Textual form of one source hint, bracketing an IPv6 literal.
QString sourceToken(const ED2KLinkSource& src)
{
    const QString host = src.address.isNull() ? src.hostname : src.address.toString();
    return formatHostPort(host, src.port);
}

/// Parse a comma-separated `host:port` list into @p link.
/// @param ipv6Only  true for the `s6=` parameter, which may only carry IPv6 literals.
/// A malformed entry is skipped without discarding the rest of the list.
void appendLinkSources(ED2KFileLink& link, QStringView list, bool ipv6Only)
{
    for (const auto token : list.split(QChar(u','), Qt::SkipEmptyParts)) {
        if (static_cast<int>(link.hostnameSources.size()) >= kMaxLinkSources)
            return;

        const auto hp = parseHostPort(token, 4662);
        if (!hp)
            continue;

        ED2KLinkSource src;
        src.hostname = hp->host;
        src.port = hp->port;
        src.address = Address::fromString(hp->host);   // null => DNS name

        if (ipv6Only && !src.address.isIPv6())
            continue;   // a v4 or hostname entry in s6= is a malformed link

        // MFC drops *.*.*.0 sources as LowIDs (srchybrid/ED2KLink.cpp:305). That test
        // only means anything for IPv4 — an IPv6 literal has no ED2K ID, and running
        // it through toNetworkUint32() (which is 0 for IPv6) is what silently dropped
        // every IPv6 source before.
        if (src.address.isIPv4() && isLowID(src.address.toNetworkUint32()))
            continue;

        link.hostnameSources.push_back(std::move(src));
    }
}

} // namespace

// ---------------------------------------------------------------------------
// toLink() — reconstruct ed2k:// URLs from parsed data
// ---------------------------------------------------------------------------

QString ED2KServerLink::toLink() const
{
    // Bracket an IPv6 literal so our own parser round-trips it unambiguously. A
    // legacy client runs inet_addr() on the address and falls back to treating the
    // failure as a dynIP hostname, so a bracketed literal degrades to an
    // unresolvable server entry rather than a mis-dialed one.
    const bool isIPv6Literal = Address::fromString(address).isIPv6();
    return QStringLiteral("ed2k://|server|%1|%2|/")
        .arg(isIPv6Literal ? QStringLiteral("[%1]").arg(address) : address)
        .arg(port);
}

QString ED2KFileLink::toLink(const ED2KLinkFormat& fmt) const
{
    QString out;
    if (fmt.html)
        out = QStringLiteral("<a href=\"");

    out += QStringLiteral("ed2k://|file|%1|%2|%3|")
               .arg(urlEncode(stripInvalidFilenameChars(name)))
               .arg(size)
               .arg(md4str(hash.data()));

    if (fmt.partHashes && !partHashes.empty()) {
        out += QStringLiteral("p=");
        for (std::size_t i = 0; i < partHashes.size(); ++i) {
            if (i > 0)
                out += QChar(u':');
            out += encodeBase16({partHashes[i].data(), 16});
        }
        out += QChar(u'|');
    }

    if (fmt.aichHash && hasValidAICHHash)
        out += QStringLiteral("h=%1|").arg(aichHash.getString());

    // The '/' terminates the ed2k parameter section. MFC emits it BEFORE any source
    // block (srchybrid/AbstractFile.cpp:439-443) and its tokenizer stops at the first
    // empty token (srchybrid/ED2KLink.cpp:365), so omitting it here would make every
    // link carrying a source hint unparseable to stock eMule.
    out += QChar(u'/');

    if (fmt.sources && !hostnameSources.empty()) {
        QStringList v4Tokens;
        QStringList v6Tokens;
        for (const auto& src : hostnameSources) {
            // URL sources are not re-emitted: `s=` is an inbound-only hint here, since
            // we never publish ourselves as an HTTP source.
            if (src.port == 0 || !src.url.isEmpty())
                continue;
            if (src.address.isIPv6())
                v6Tokens << sourceToken(src);
            else
                v4Tokens << sourceToken(src);
        }

        // IPv6 goes into its own `s6=` token, placed BEFORE the classic block: a
        // legacy parser starts scanning at the first "sources" occurrence, so
        // anything earlier is never tokenized and our v6 hints stay invisible to it.
        // Inside `sources,` they would not be: MFC splits at the FIRST colon, which
        // can yield a valid-looking port and an inet_addr()-acceptable first segment,
        // i.e. a bogus source at the wrong address.
        if (!v6Tokens.isEmpty())
            out += QStringLiteral("|s6=%1").arg(v6Tokens.join(QChar(u',')));
        if (!v4Tokens.isEmpty())
            out += QStringLiteral("|sources,%1").arg(v4Tokens.join(QChar(u',')));
        if (!v6Tokens.isEmpty() || !v4Tokens.isEmpty())
            out += QStringLiteral("|/");
    }

    if (fmt.html)
        out += QStringLiteral("\">%1</a>").arg(stripInvalidFilenameChars(name));

    return out;
}

QString ED2KServerListLink::toLink() const
{
    return QStringLiteral("ed2k://|serverlist|%1|/").arg(address);
}

QString ED2KNodesListLink::toLink() const
{
    return QStringLiteral("ed2k://|nodeslist|%1|/").arg(address);
}

QString ED2KSearchLink::toLink() const
{
    return QStringLiteral("ed2k://|search|%1|/").arg(searchTerm);
}

// ---------------------------------------------------------------------------
// linkType()
// ---------------------------------------------------------------------------

ED2KLinkType linkType(const ED2KLink& link)
{
    return std::visit([](const auto& l) -> ED2KLinkType {
        using T = std::decay_t<decltype(l)>;
        if constexpr (std::is_same_v<T, ED2KFileLink>)
            return ED2KLinkType::File;
        else if constexpr (std::is_same_v<T, ED2KServerLink>)
            return ED2KLinkType::Server;
        else if constexpr (std::is_same_v<T, ED2KServerListLink>)
            return ED2KLinkType::ServerList;
        else if constexpr (std::is_same_v<T, ED2KNodesListLink>)
            return ED2KLinkType::NodesList;
        else
            return ED2KLinkType::Search;
    }, link);
}

// ---------------------------------------------------------------------------
// File link parameter parsing helpers
// ---------------------------------------------------------------------------

static std::optional<ED2KFileLink> parseFileLink(const QStringList& parts)
{
    // ed2k://|file|<name>|<size>|<hash>|/  (minimum 5 pipe-separated parts)
    // parts[0] = "file", parts[1] = name, parts[2] = size, parts[3] = hash
    // Additional parts are optional parameters or source info
    if (parts.size() < 4)
        return std::nullopt;

    ED2KFileLink link;

    // Name (URL-decoded)
    link.name = urlDecode(parts[1]);
    if (link.name.isEmpty())
        return std::nullopt;

    // Size
    bool sizeOk = false;
    link.size = parts[2].toULongLong(&sizeOk);
    if (!sizeOk)
        return std::nullopt;

    // Hash (32 hex chars = 16 bytes MD4)
    if (parts[3].size() != 32)
        return std::nullopt;
    if (!strmd4(parts[3], link.hash.data()))
        return std::nullopt;

    // Parse optional parameters after the hash
    for (int i = 4; i < parts.size(); ++i) {
        const QString& param = parts[i];
        if (param.isEmpty() || param == QStringLiteral("/"))
            continue;

        // Part hashes: p=hash1:hash2:...
        if (param.startsWith(QStringLiteral("p="), Qt::CaseInsensitive)) {
            const QString hashStr = param.mid(2);
            const QStringList hashes = hashStr.split(QChar(u':'), Qt::SkipEmptyParts);

            // Collect the valid hashes first: writing the declared count before
            // validating would leave a hashset claiming more parts than it holds,
            // and loadMD4HashsetFromFile() would then read past the data.
            link.partHashes.clear();
            for (const auto& h : hashes) {
                std::array<uint8, 16> partHash{};
                if (h.size() == 32 && strmd4(h, partHash.data()))
                    link.partHashes.push_back(partHash);
            }

            if (!link.partHashes.empty()) {
                link.hashset = std::make_unique<SafeMemFile>();
                link.hashset->writeHash16(link.hash.data());
                link.hashset->writeUInt16(static_cast<uint16>(link.partHashes.size()));
                for (const auto& partHash : link.partHashes)
                    link.hashset->writeHash16(partHash.data());
                link.hashset->seek(0, 0);
            }
        }
        // AICH hash: h=<base32 hash>
        else if (param.startsWith(QStringLiteral("h="), Qt::CaseInsensitive)) {
            const QString hashB32 = param.mid(2);
            std::array<uint8, 20> aichRaw{};
            const auto decoded = decodeBase32(hashB32, aichRaw.data(), aichRaw.size());
            if (decoded == kAICHHashSize) {
                link.aichHash = AICHHash(aichRaw.data());
                link.hasValidAICHHash = true;
            }
        }
        // HTTP source: s=url
        else if (param.startsWith(QStringLiteral("s="), Qt::CaseInsensitive)) {
            if (static_cast<int>(link.hostnameSources.size()) >= kMaxLinkSources)
                continue;
            const QString sourceUrl = param.mid(2);
            const QUrl url(sourceUrl);
            const QString scheme = url.scheme().toLower();
            // URLClient only speaks HTTP; anything else is not a usable source.
            if (url.isValid() && !url.host().isEmpty()
                && (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")))
            {
                ED2KLinkSource src;
                src.hostname = url.host();      // QUrl already strips [] from an IPv6 host
                src.port = static_cast<uint16>(url.port(4662));
                src.address = Address::fromString(src.hostname);
                src.url = sourceUrl;
                link.hostnameSources.push_back(std::move(src));
            }
        }
        // IPv6 sources: s6=[v6]:port,[v6]:port,...
        else if (param.startsWith(QStringLiteral("s6="), Qt::CaseInsensitive)) {
            appendLinkSources(link, param.mid(3), /*ipv6Only=*/true);
        }
        // IPv4 / hostname sources: sources,ip:port,ip:port,...
        else if (param.startsWith(QStringLiteral("sources"), Qt::CaseInsensitive)) {
            // Format: sources@YYMMDD,ip:port,ip:port,...  or  sources,ip:port,...
            // Find the first comma to skip past the "sources" or "sources@date" part
            const auto commaPos = param.indexOf(QChar(u','));
            if (commaPos >= 0)
                appendLinkSources(link, param.mid(commaPos + 1), /*ipv6Only=*/false);
        }
    }

    return link;
}

// ---------------------------------------------------------------------------
// Server link parsing
// ---------------------------------------------------------------------------

static std::optional<ED2KServerLink> parseServerLink(const QStringList& parts)
{
    // ed2k://|server|<address>|<port>|/
    if (parts.size() < 3)
        return std::nullopt;

    ED2KServerLink link;
    link.address = parts[1].trimmed();

    // Accept a bracketed IPv6 literal and store the bare form — Server keeps the
    // unbracketed string as its dedup key and display address.
    if (link.address.size() > 2 && link.address.startsWith(u'[') && link.address.endsWith(u']'))
        link.address = link.address.mid(1, link.address.size() - 2);

    if (link.address.isEmpty())
        return std::nullopt;

    // No hostname or IPv4 literal contains a colon, so an unbracketed address with one
    // is either an IPv6 literal or a "host:port" pasted into the address field.
    if (link.address.contains(u':') && !Address::fromString(link.address).isIPv6())
        return std::nullopt;

    bool portOk = false;
    link.port = static_cast<uint16>(parts[2].toUInt(&portOk));
    if (!portOk || link.port == 0)
        return std::nullopt;

    return link;
}

// ---------------------------------------------------------------------------
// Magnet link parsing
// ---------------------------------------------------------------------------

static std::optional<ED2KLink> parseMagnetLink(const QString& uri)
{
    // magnet:?xt=urn:ed2k:HASH&dn=name&xl=size
    const auto queryStart = uri.indexOf(QChar(u'?'));
    if (queryStart < 0)
        return std::nullopt;

    const QString query = uri.mid(queryStart + 1);
    const QStringList params = query.split(QChar(u'&'), Qt::SkipEmptyParts);

    QString hash;
    QString name;
    uint64 size = 0;

    for (const auto& param : params) {
        const auto eqPos = param.indexOf(QChar(u'='));
        if (eqPos < 0)
            continue;
        const QString key = param.left(eqPos).toLower();
        const QString val = param.mid(eqPos + 1);

        if (key == QStringLiteral("xt")) {
            // xt=urn:ed2k:HASH or xt=urn:ed2khash:HASH
            const auto ed2kPrefix = QStringLiteral("urn:ed2k:");
            const auto ed2kHashPrefix = QStringLiteral("urn:ed2khash:");
            if (val.startsWith(ed2kPrefix, Qt::CaseInsensitive))
                hash = val.mid(ed2kPrefix.size());
            else if (val.startsWith(ed2kHashPrefix, Qt::CaseInsensitive))
                hash = val.mid(ed2kHashPrefix.size());
        } else if (key == QStringLiteral("dn")) {
            name = urlDecode(val);
        } else if (key == QStringLiteral("xl")) {
            bool ok = false;
            size = val.toULongLong(&ok);
            if (!ok) size = 0;
        } else if (key == QStringLiteral("as") || key == QStringLiteral("xs")) {
            // Alternative/exact source — parse as ed2k link if present
            if (val.startsWith(QStringLiteral("ed2k://"), Qt::CaseInsensitive) ||
                val.startsWith(QStringLiteral("ed2k%3A%2F%2F"), Qt::CaseInsensitive)) {
                // Try to extract file link info from embedded ed2k URL
                const QString decoded = urlDecode(val);
                auto result = parseED2KLink(decoded);
                if (result && std::holds_alternative<ED2KFileLink>(*result))
                    return result;
            }
        }
    }

    // Need at least the hash to create a file link
    if (hash.size() != 32)
        return std::nullopt;

    ED2KFileLink link;
    if (!strmd4(hash, link.hash.data()))
        return std::nullopt;

    link.name = name.isEmpty() ? hash : name;
    link.size = size;

    return ED2KLink{std::move(link)};
}

// ---------------------------------------------------------------------------
// parseED2KLink — main entry point
// ---------------------------------------------------------------------------

std::optional<ED2KLink> parseED2KLink(const QString& uri)
{
    if (uri.isEmpty())
        return std::nullopt;

    // Handle magnet links
    if (uri.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive))
        return parseMagnetLink(uri);

    // Must start with ed2k://|
    if (!uri.startsWith(QStringLiteral("ed2k://|"), Qt::CaseInsensitive))
        return std::nullopt;

    // Strip the "ed2k://|" prefix and split by "|"
    const QString body = uri.mid(8);  // skip "ed2k://|"
    const QStringList parts = body.split(QChar(u'|'), Qt::KeepEmptyParts);
    if (parts.isEmpty())
        return std::nullopt;

    const QString type = parts[0].toLower();

    if (type == QStringLiteral("file")) {
        auto result = parseFileLink(parts);
        if (result)
            return ED2KLink{std::move(*result)};
    } else if (type == QStringLiteral("server")) {
        auto result = parseServerLink(parts);
        if (result)
            return ED2KLink{std::move(*result)};
    } else if (type == QStringLiteral("serverlist")) {
        if (parts.size() >= 2 && !parts[1].isEmpty()) {
            ED2KServerListLink link;
            link.address = parts[1];
            return ED2KLink{std::move(link)};
        }
    } else if (type == QStringLiteral("nodeslist")) {
        if (parts.size() >= 2 && !parts[1].isEmpty()) {
            ED2KNodesListLink link;
            link.address = parts[1];
            return ED2KLink{std::move(link)};
        }
    } else if (type == QStringLiteral("search")) {
        if (parts.size() >= 2 && !parts[1].isEmpty()) {
            ED2KSearchLink link;
            link.searchTerm = urlDecode(parts[1]);
            return ED2KLink{std::move(link)};
        }
    }

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// ownLinkSourceHints
// ---------------------------------------------------------------------------

std::vector<ED2KLinkSource> ownLinkSourceHints()
{
    std::vector<ED2KLinkSource> hints;
    const uint16 port = thePrefs.port();
    if (port == 0)
        return hints;

    // Configured hostname: an IP literal of either family, or a DNS name — which still
    // has to look like one, as MFC's `Find('.') >= 0` test did.
    const QString configured = thePrefs.ed2kHostname().trimmed();
    if (!configured.isEmpty()) {
        const Address literal = Address::fromString(configured);
        if (!literal.isNull() || configured.contains(u'.'))
            hints.push_back({configured, port, literal, {}});
    }

    // Our public IPv6, but only when the advertise gate is satisfied: the same gate
    // every other IPv6 advertisement uses, so a probed-unreachable address is never
    // published. Unlike a pref literal this cannot go stale on prefix rotation.
    if (thePrefs.ed2kLinkAdvertiseIPv6() && theApp.shouldAdvertisePublicIPv6()) {
        const Address v6 = theApp.publicIPv6();
        const bool alreadyListed = std::any_of(hints.begin(), hints.end(),
            [&v6](const ED2KLinkSource& src) { return src.address == v6; });
        if (v6.isIPv6() && !alreadyListed)
            hints.push_back({v6.toString(), port, v6, {}});
    }

    return hints;
}

} // namespace eMule
