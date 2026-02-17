#pragma once

/// @file KadError.h
/// @brief Kademlia error code constants (replaces MFC #defines).

#include "utils/Types.h"

#include <cstdint>

namespace eMule::kad {

enum class KadError : uint16 {
    Success        = 0,
    ReadOnly       = 4,
    WriteOnly      = 5,
    EndOfFile      = 6,
    BufferTooSmall = 7,
    NoContacts     = 0x0D,
    Unknown        = 0xFFFF
};

} // namespace eMule::kad
