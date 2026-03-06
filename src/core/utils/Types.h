#pragma once

/// @file Types.h
/// @brief Cross-platform type definitions replacing Windows DWORD/BYTE/etc.
///
/// This header is the central place for portable integer typedefs used
/// throughout the eMule codebase.  It replaces the Windows SDK types
/// (DWORD, BYTE, WORD, UINT, BOOL, etc.) with standard <cstdint> types.

#include <cstdint>
#include <cstddef>

#if __has_include(<config.h>)
    #include <config.h>
#elif defined(_WIN32)
    #include "config_win.h"
#else
    #error "config.h not found — run CMake configure first"
#endif

namespace eMule {

// Fixed-width unsigned integers (replacing Windows SDK types)
using uint8  = std::uint8_t;   // BYTE
using uint16 = std::uint16_t;  // WORD
using uint32 = std::uint32_t;  // DWORD
using uint64 = std::uint64_t;  // ULONGLONG
using uchar  = std::uint8_t;   // unsigned char (191 MFC uses)

// Fixed-width signed integers
using int8   = std::int8_t;
using int16  = std::int16_t;
using int32  = std::int32_t;   // LONG
using int64  = std::int64_t;   // LONGLONG

// Signed aliases matching MFC types.h convention
using sint8  = std::int8_t;
using sint16 = std::int16_t;
using sint32 = std::int32_t;
using sint64 = std::int64_t;

// Size types
using usize  = std::size_t;
using isize  = std::ptrdiff_t;

// File-size type — replaces MFC EMFileSize debug wrapper class
using EMFileSize = uint64;

} // namespace eMule