#pragma once

/// @file Exceptions.h
/// @brief Exception hierarchy replacing MFC CException / CMsgBoxException / CClientException.
///
/// All eMule exceptions derive from std::runtime_error.
/// The MFC pattern of CException::Delete() is replaced by normal C++ RAII.

#include "DebugUtils.h"

#include <stdexcept>
#include <string>

namespace eMule {

/// Base exception for all eMule-specific errors.
class EmuleException : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// Exception indicating an error that should be shown to the user in a message box.
class MsgBoxException : public EmuleException {
public:
    explicit MsgBoxException(const std::string& msg)
        : EmuleException(msg) {}
};

/// Exception related to a client connection (may trigger client disconnect).
class ClientException : public EmuleException {
public:
    ClientException(const std::string& msg, bool shouldDelete)
        : EmuleException(msg), m_shouldDelete(shouldDelete) {}

    /// Whether the associated client object should be deleted.
    [[nodiscard]] bool shouldDelete() const noexcept { return m_shouldDelete; }

private:
    bool m_shouldDelete;
};

/// Exception thrown on file I/O errors (also declared in SafeFile.h as FileException).
class IOException : public EmuleException {
    using EmuleException::EmuleException;
};

/// Exception thrown on protocol parsing errors.
class ProtocolException : public EmuleException {
    using EmuleException::EmuleException;
};

// ---------------------------------------------------------------------------
// Exception-safe catch macros (replacing MFC CATCH_DFLT_ALL etc.)
// ---------------------------------------------------------------------------

/// Catch and log all standard exceptions. Use in callback/event handlers.
#define EMULE_CATCH_ALL(context) \
    catch (const std::exception& ex) { \
        qCWarning(eMule::lcEmuleGeneral, "Exception in %s: %s", context, ex.what()); \
    } \
    catch (...) { \
        qCWarning(eMule::lcEmuleGeneral, "Unknown exception in %s", context); \
    }

} // namespace eMule
