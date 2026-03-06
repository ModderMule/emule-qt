#pragma once

/// @file config_win.h
/// @brief Platform feature detection for Windows (qmake builds).
///
/// Static equivalent of the CMake-generated config.h for .pro file builds.

// ---------------------------------------------------------------------------
// Project version
// ---------------------------------------------------------------------------
#define EMULE_VERSION_MAJOR  0
#define EMULE_VERSION_MINOR  1
#define EMULE_VERSION_PATCH  0
#define EMULE_VERSION_STRING "0.1.0"

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------
#define EMULE_OS_WINDOWS 1

// ---------------------------------------------------------------------------
// Endianness — x86/x64 Windows is always little-endian
// ---------------------------------------------------------------------------
#define EMULE_BIG_ENDIAN 0

// ---------------------------------------------------------------------------
// System header availability — none of these exist on Windows
// ---------------------------------------------------------------------------
/* #undef HAVE_SYS_SOCKET_H */
/* #undef HAVE_NETINET_IN_H */
/* #undef HAVE_ARPA_INET_H */
/* #undef HAVE_UNISTD_H */
/* #undef HAVE_ENDIAN_H */
/* #undef HAVE_SYS_ENDIAN_H */
/* #undef HAVE_EXECINFO_H */

// ---------------------------------------------------------------------------
// Function availability
// ---------------------------------------------------------------------------
/* #undef HAVE_HTOLE32 */
/* #undef HAVE_BE64TOH */

// ---------------------------------------------------------------------------
// Feature flags
// ---------------------------------------------------------------------------
#define SUPPORT_LARGE_FILES 1