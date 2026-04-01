#pragma once

/// @file PreviewLauncher.h
/// @brief Launch a media player for preview streaming.

#include <QString>

namespace eMule {

/// Launch the configured video player with the given streaming URL.
/// If VLC is detected, reuses an existing instance instead of spawning a new one.
void launchPreview(const QString& url);

} // namespace eMule
