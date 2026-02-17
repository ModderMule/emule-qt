#pragma once

/// @file StatisticFile.h
/// @brief Per-file transfer statistics — replaces MFC CStatisticFile.
///
/// Tracks requests, accepts, and transferred bytes for both the current
/// session and all-time totals. Decoupled from theApp: callers aggregate
/// totals externally.

#include "utils/Types.h"

namespace eMule {

class StatisticFile {
public:
    StatisticFile() = default;

    void mergeFileStats(const StatisticFile& other);

    void addRequest();
    void addAccepted();
    void addTransferred(uint64 bytes);

    [[nodiscard]] uint32 requests() const { return m_requested; }
    [[nodiscard]] uint32 accepts() const { return m_accepted; }
    [[nodiscard]] uint64 transferred() const { return m_transferred; }
    [[nodiscard]] uint32 allTimeRequests() const { return m_allTimeRequested; }
    [[nodiscard]] uint32 allTimeAccepts() const { return m_allTimeAccepted; }
    [[nodiscard]] uint64 allTimeTransferred() const { return m_allTimeTransferred; }

    void setAllTimeRequests(uint32 val) { m_allTimeRequested = val; }
    void setAllTimeAccepts(uint32 val) { m_allTimeAccepted = val; }
    void setAllTimeTransferred(uint64 val) { m_allTimeTransferred = val; }

private:
    uint64 m_allTimeTransferred = 0;
    uint64 m_transferred = 0;
    uint32 m_allTimeRequested = 0;
    uint32 m_requested = 0;
    uint32 m_allTimeAccepted = 0;
    uint32 m_accepted = 0;
};

} // namespace eMule
