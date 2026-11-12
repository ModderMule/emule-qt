#pragma once

/// @file SystemMemory.h
/// @brief Physical RAM of the machine: total and available (free + reclaimable cache).

#include <cstdint>
#include <optional>

namespace eMule {

struct SystemMemory {
    std::uint64_t total = 0;      ///< physical RAM, bytes
    std::uint64_t available = 0;  ///< usable without swapping, bytes
};

/// RAM now; nullopt: the platform did not tell.
[[nodiscard]] std::optional<SystemMemory> systemMemory();

/// Available RAM below @p percent of the total. false when unknown or @p percent <= 0.
[[nodiscard]] bool systemMemoryLow(int percent);

} // namespace eMule
