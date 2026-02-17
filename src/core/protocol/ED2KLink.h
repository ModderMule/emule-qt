#pragma once

/// @file ED2KLink.h
/// @brief ED2K link parsing — modern C++23 replacement for MFC CED2KLink hierarchy.
///
/// Replaces the CED2KLink / CED2KFileLink / CED2KServerLink class hierarchy
/// with value-type structs and a std::variant-based ED2KLink type.
/// Parsing returns std::optional instead of throwing exceptions.

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

struct ED2KServerLink {
    QString address;
    uint16 port = 0;
    [[nodiscard]] QString toLink() const;
};

struct ED2KFileLink {
    QString name;
    uint64 size = 0;
    std::array<uint8, 16> hash{};
    AICHHash aichHash;
    bool hasValidAICHHash = false;
    std::unique_ptr<SafeMemFile> hashset;
    struct HostnameSource {
        QString hostname;
        uint16 port = 0;
    };
    std::vector<HostnameSource> hostnameSources;
    [[nodiscard]] QString toLink() const;
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

/// Determine the type of an ED2KLink variant.
[[nodiscard]] ED2KLinkType linkType(const ED2KLink& link);

} // namespace eMule
