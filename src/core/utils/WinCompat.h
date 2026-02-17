#pragma once

/// @file WinCompat.h
/// @brief Zero-cost type aliases for common Windows SDK types.
///
/// These aliases let downstream ported code keep the original Windows type
/// names so that porting diffs stay small.  They map directly to the
/// fixed-width types already defined in Types.h.

#include "Types.h"

namespace eMule {

// --- Integer aliases matching Windows SDK names ---
using DWORD     = uint32;
using UINT      = uint32;
using BYTE      = uint8;
using WORD      = uint16;
using LONG      = int32;
using LONGLONG  = int64;
using ULONGLONG = uint64;
using COLORREF  = uint32;

// BOOL is intentionally mapped to C++ bool (not int).
// MFC code that relies on BOOL being an int should be fixed at the call site.
using BOOL = bool;

// Pointer-sized types — match native pointer width on all platforms
using LPARAM  = std::intptr_t;
using WPARAM  = std::uintptr_t;
using LRESULT = std::intptr_t;

} // namespace eMule
