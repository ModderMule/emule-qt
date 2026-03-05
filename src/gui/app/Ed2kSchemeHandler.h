#pragma once

/// @file Ed2kSchemeHandler.h
/// @brief Platform-specific ed2k:// URL scheme registration.

namespace eMule {

/// Register this application as the handler for ed2k:// URLs.
void registerEd2kUrlScheme();

/// Check whether this application is currently registered as the ed2k:// handler.
[[nodiscard]] bool isEd2kSchemeRegistered();

} // namespace eMule
