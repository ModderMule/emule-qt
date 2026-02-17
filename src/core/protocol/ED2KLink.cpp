/// @file ED2KLink.cpp
/// @brief ED2K link parsing implementation — port of CED2KLink from MFC.

#include "ED2KLink.h"
#include "utils/OtherFunctions.h"

#include <QHostAddress>
#include <QUrl>

namespace eMule {

// ---------------------------------------------------------------------------
// toLink() — reconstruct ed2k:// URLs from parsed data
// ---------------------------------------------------------------------------

QString ED2KServerLink::toLink() const
{
    return QStringLiteral("ed2k://|server|%1|%2|/").arg(address).arg(port);
}

QString ED2KFileLink::toLink() const
{
    return QStringLiteral("ed2k://|file|%1|%2|%3|/")
        .arg(name)
        .arg(size)
        .arg(md4str(hash.data()));
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

            if (!hashes.isEmpty()) {
                link.hashset = std::make_unique<SafeMemFile>();
                // Write file hash first
                link.hashset->writeHash16(link.hash.data());
                // Write part count
                link.hashset->writeUInt16(static_cast<uint16>(hashes.size()));
                // Write each part hash
                for (const auto& h : hashes) {
                    std::array<uint8, 16> partHash{};
                    if (h.size() == 32 && strmd4(h, partHash.data())) {
                        link.hashset->writeHash16(partHash.data());
                    }
                }
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
        // Hostname sources: s=url
        else if (param.startsWith(QStringLiteral("s="), Qt::CaseInsensitive)) {
            const QString sourceUrl = param.mid(2);
            QUrl url(sourceUrl);
            if (url.isValid() && !url.host().isEmpty()) {
                ED2KFileLink::HostnameSource src;
                src.hostname = url.host();
                src.port = static_cast<uint16>(url.port(4662));
                link.hostnameSources.push_back(std::move(src));
            }
        }
        // IP sources: sources,ip:port,ip:port,...
        else if (param.startsWith(QStringLiteral("sources"), Qt::CaseInsensitive)) {
            // Format: sources@YYMMDD,ip:port,ip:port,...  or  sources,ip:port,...
            // Find the first comma to skip past the "sources" or "sources@date" part
            const auto commaPos = param.indexOf(QChar(u','));
            if (commaPos >= 0) {
                const QString sourceList = param.mid(commaPos + 1);
                const QStringList sources = sourceList.split(QChar(u','), Qt::SkipEmptyParts);
                for (const auto& src : sources) {
                    const auto colonPos = src.lastIndexOf(QChar(u':'));
                    if (colonPos > 0) {
                        const QString ipStr = src.left(colonPos);
                        const QString portStr = src.mid(colonPos + 1);
                        bool portOk = false;
                        const auto srcPort = static_cast<uint16>(portStr.toUInt(&portOk));
                        if (portOk && srcPort > 0) {
                            // Check if it's a hostname or IP
                            QHostAddress addr(ipStr);
                            if (!addr.isNull()) {
                                // Valid IP — check if it's not a LowID
                                const auto ip = static_cast<uint32>(addr.toIPv4Address());
                                if (!isLowID(ip)) {
                                    ED2KFileLink::HostnameSource hs;
                                    hs.hostname = ipStr;
                                    hs.port = srcPort;
                                    link.hostnameSources.push_back(std::move(hs));
                                }
                            } else {
                                // Treat as hostname
                                ED2KFileLink::HostnameSource hs;
                                hs.hostname = ipStr;
                                hs.port = srcPort;
                                link.hostnameSources.push_back(std::move(hs));
                            }
                        }
                    }
                }
            }
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
    link.address = parts[1];
    if (link.address.isEmpty())
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

} // namespace eMule
