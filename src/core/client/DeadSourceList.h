#pragma once

/// @file DeadSourceList.h
/// @brief Dead source tracking with expiry — replaces MFC CDeadSource + CDeadSourceList.
///
/// Decoupled from CUpDownClient: takes explicit parameters (hash, IPs, ports)
/// instead of a client reference.

#include "utils/Types.h"
#include "utils/OtherFunctions.h"

#include <array>
#include <chrono>
#include <functional>
#include <unordered_map>

namespace eMule {

using SteadyTimePoint = std::chrono::steady_clock::time_point;

/// Key identifying a dead source, replaces CDeadSource.
struct DeadSourceKey {
    std::array<uint8, 16> hash{};  // user hash (for low-ID clients without server)
    uint32 serverIP = 0;           // server IP (for low-ID identification)
    uint32 userID   = 0;           // user ID (high-ID = IP, low-ID = server-assigned)
    uint16 port     = 0;           // user TCP port
    uint16 kadPort  = 0;           // Kademlia UDP port

    friend bool operator==(const DeadSourceKey& a, const DeadSourceKey& b) noexcept;
};

} // namespace eMule

// Hash specialization must appear before any unordered_map<DeadSourceKey> usage
template <>
struct std::hash<eMule::DeadSourceKey> {
    std::size_t operator()(const eMule::DeadSourceKey& ds) const noexcept
    {
        std::size_t h = ds.userID;
        if (h != 0) {
            if (eMule::isLowID(static_cast<eMule::uint32>(h)))
                h ^= ds.serverIP;
            return h;
        }
        // Low-ID without server: hash from user hash
        std::size_t result = 0;
        for (std::size_t i = 0; i < 16; ++i)
            result += static_cast<std::size_t>(ds.hash[i] + 1) * (i * i + 1);
        return result + 1;
    }
};

namespace eMule {

/// Dead source tracking with time-based expiry.
class DeadSourceList {
public:
    DeadSourceList() = default;

    void init(bool globalList);

    void addDeadSource(const DeadSourceKey& key, bool lowID);
    void removeDeadSource(const DeadSourceKey& key);
    [[nodiscard]] bool isDeadSource(const DeadSourceKey& key) const;
    [[nodiscard]] std::size_t count() const { return m_sources.size(); }

private:
    void cleanUp();

    std::unordered_map<DeadSourceKey, SteadyTimePoint> m_sources;
    SteadyTimePoint m_lastCleanUp{};
    bool m_globalList = false;
};

} // namespace eMule
