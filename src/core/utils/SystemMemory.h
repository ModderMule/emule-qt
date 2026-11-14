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

/// RAM of this process: what Task Manager / Activity Monitor show as its memory.
struct ProcessMemory {
    std::uint64_t current = 0;    ///< private bytes (Windows), footprint (macOS), RSS (Linux)
    std::uint64_t peak = 0;       ///< highest of it so far; 0 when unknown
};

/// RAM now; nullopt: the platform did not tell.
[[nodiscard]] std::optional<SystemMemory> systemMemory();

/// This process now; nullopt: the platform did not tell.
[[nodiscard]] std::optional<ProcessMemory> processMemory();

/// Available RAM below @p percent of the total. false when unknown or @p percent <= 0.
[[nodiscard]] bool systemMemoryLow(int percent);

} // namespace eMule
