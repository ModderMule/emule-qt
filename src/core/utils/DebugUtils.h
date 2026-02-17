#pragma once

/// @file DebugUtils.h
/// @brief Assertion macros and logging categories replacing MFC ASSERT/TRACE.
///
/// Replaces:
///   ASSERT(cond)        → EMULE_ASSERT(cond)        (2215 uses)
///   VERIFY(expr)        → EMULE_VERIFY(expr)
///   ASSERT_VALID(ptr)   → EMULE_ASSERT_VALID(ptr)
///   TRACE(fmt, ...)     → qCDebug(lcEmuleGeneral, ...) etc.

#include <QLoggingCategory>

namespace eMule {

// ---- Logging categories -----------------------------------------------------
// Usage:  qCDebug(lcEmuleNet) << "connected to" << host;

Q_DECLARE_LOGGING_CATEGORY(lcEmuleGeneral)
Q_DECLARE_LOGGING_CATEGORY(lcEmuleNet)
Q_DECLARE_LOGGING_CATEGORY(lcEmuleFile)
Q_DECLARE_LOGGING_CATEGORY(lcEmuleKad)
Q_DECLARE_LOGGING_CATEGORY(lcEmuleServer)
Q_DECLARE_LOGGING_CATEGORY(lcEmuleCrypto)

} // namespace eMule

// ---- Assertion macros -------------------------------------------------------

/// Direct replacement for MFC ASSERT — disabled in release builds.
#define EMULE_ASSERT(cond) Q_ASSERT(cond)

/// Replacement for MFC VERIFY — always evaluates @p expr, asserts only in debug.
#ifdef QT_NO_DEBUG
    #define EMULE_VERIFY(expr) static_cast<void>(expr)
#else
    #define EMULE_VERIFY(expr) Q_ASSERT(expr)
#endif

/// Replacement for ASSERT_VALID — checks pointer is not null.
#define EMULE_ASSERT_VALID(ptr) Q_ASSERT((ptr) != nullptr)
