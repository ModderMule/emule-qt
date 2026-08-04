#pragma once

/// @file ED2KLink.h
/// @brief ED2K link parsing — modern C++23 replacement for MFC CED2KLink hierarchy.
///
/// Replaces the CED2KLink / CED2KFileLink / CED2KServerLink class hierarchy
/// with value-type structs and a std::variant-based ED2KLink type.
/// Parsing returns std::optional instead of throwing exceptions.

#include "net/Address.h"
#include "utils/SafeFile.h"
#include "utils/Types.h"
#include "crypto/AICHData.h"

#include <QString>

#include <array>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace eMule {

enum class ED2KLinkType { File, Server, ServerList, NodesList, Search };

/// Upper bound on the sources one link may carry. Link text is untrusted input, and
/// each entry can cost a DNS lookup plus a dial attempt.
inline constexpr int kMaxLinkSources = 64;

struct ED2KServerLink {
    QString address;    ///< IPv4/IPv6 literal (never bracketed) or a hostname
    uint16 port = 0;
    [[nodiscard]] QString toLink() const;
};

/// One source hint carried by a file link.
struct ED2KLinkSource {
    QString hostname;   ///< As written, brackets stripped: DNS name or IP literal
    uint16  port = 0;
    Address address;    ///< Non-null when @a hostname is a literal — no DNS needed
    QString url;        ///< Non-empty when this came from `s=<url>` (an HTTP source)
};

/// Which optional parts ED2KFileLink::toLink() emits. All false reproduces the
/// canonical `ed2k://|file|NAME|SIZE|HASH|/` form.
struct ED2KLinkFormat {
    bool partHashes = false;   ///< `p=hash:hash:…`
    bool aichHash   = false;   ///< `h=<base32>`
    bool sources    = false;   ///< `s6=[v6]:port` and/or `sources,host:port`
    bool html       = false;   ///< wrap in `<a href="…">name</a>`
};

struct ED2KFileLink {
    /// Kept as a member type so existing call sites keep compiling.
    using HostnameSource = ED2KLinkSource;

    QString name;
    uint64 size = 0;
    std::array<uint8, 16> hash{};
    AICHHash aichHash;
    bool hasValidAICHHash = false;
    std::unique_ptr<SafeMemFile> hashset;
    std::vector<std::array<uint8, 16>> partHashes;   ///< Parsed `p=`, for re-emission
    std::vector<ED2KLinkSource> hostnameSources;
    [[nodiscard]] QString toLink(const ED2KLinkFormat& fmt = {}) const;
};

struct ED2KServerListLink {
    QString address;
    [[nodiscard]] QString toLink() const;
};

struct ED2KNodesListLink {
    QString address;
    [[nodiscard]] QString toLink() const;
};

struct ED2KSearchLink {
    QString searchTerm;
    [[nodiscard]] QString toLink() const;
};

using ED2KLink = std::variant<ED2KFileLink, ED2KServerLink,
                               ED2KServerListLink, ED2KNodesListLink,
                               ED2KSearchLink>;

/// Parse an ed2k:// or magnet: URI. Returns std::nullopt on failure.
[[nodiscard]] std::optional<ED2KLink> parseED2KLink(const QString& uri);

/// The source hints this client advertises in its own links: the configured
/// eD2K hostname (a DNS name or an IPv6 literal) plus our public IPv6 when the
/// advertise gate allows it. Empty when nothing is publishable.
[[nodiscard]] std::vector<ED2KLinkSource> ownLinkSourceHints();

/// Determine the type of an ED2KLink variant.
[[nodiscard]] ED2KLinkType linkType(const ED2KLink& link);

} // namespace eMule
