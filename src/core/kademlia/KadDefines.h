#pragma once

/// @file KadDefines.h
/// @brief Kademlia protocol constants (ported from kademlia/kademlia/Defines.h).

#include <cstdint>

namespace eMule::kad {

// ---------------------------------------------------------------------------
// Core Kademlia parameters
// ---------------------------------------------------------------------------
inline constexpr uint32_t kK                = 10;   // K-bucket size
inline constexpr uint32_t kKBase            = 4;
inline constexpr uint32_t kKK               = 5;
inline constexpr uint32_t kAlphaQuery       = 3;
inline constexpr uint32_t kLogBaseExponent  = 5;
inline constexpr uint32_t kHelloTimeout     = 20;   // seconds

// ---------------------------------------------------------------------------
// Search timeouts (seconds)
// ---------------------------------------------------------------------------
inline constexpr uint32_t kSearchTolerance              = 0x1000000u;
inline constexpr uint32_t kSearchJumpstart              = 1;
inline constexpr uint32_t kSearchJumpstartCooldown      = 3;    // seconds without response before jumpstart sends again (MFC: SEC(3))
inline constexpr uint32_t kJumpstartMaxSend             = 1;    // packets per jumpstart (MFC sends 1; go() sends kAlphaQuery)
inline constexpr uint32_t kSearchLifetime               = 45;
inline constexpr uint32_t kSearchFileLifetime           = 45;
inline constexpr uint32_t kSearchKeywordLifetime        = 45;
inline constexpr uint32_t kSearchNotesLifetime          = 45;
inline constexpr uint32_t kSearchNodeLifetime           = 45;
inline constexpr uint32_t kSearchNodeCompLifetime       = 10;
inline constexpr uint32_t kSearchStoreFileLifetime      = 140;
inline constexpr uint32_t kSearchStoreKeywordLifetime   = 140;
inline constexpr uint32_t kSearchStoreNotesLifetime     = 100;
inline constexpr uint32_t kSearchFindBuddyLifetime      = 100;
inline constexpr uint32_t kSearchFindSourceLifetime     = 45;

// ---------------------------------------------------------------------------
// Search totals (max number of responses/contacts)
// ---------------------------------------------------------------------------
inline constexpr uint32_t kSearchFileTotal              = 300;
inline constexpr uint32_t kSearchKeywordTotal           = 300;
inline constexpr uint32_t kSearchNotesTotal             = 50;
inline constexpr uint32_t kSearchStoreFileTotal         = 10;
inline constexpr uint32_t kSearchStoreKeywordTotal      = 10;
inline constexpr uint32_t kSearchStoreNotesTotal        = 10;
inline constexpr uint32_t kSearchNodeCompTotal          = 10;
inline constexpr uint32_t kSearchFindBuddyTotal         = 10;
inline constexpr uint32_t kSearchFindSourceTotal        = 20;

// ---------------------------------------------------------------------------
// Zone timer intervals (seconds) — matches MFC Kademlia.cpp Process() loop
// ---------------------------------------------------------------------------
inline constexpr uint32_t kBigTimerGlobal               = 10;   // seconds between processing any two zones
inline constexpr uint32_t kBigTimerPerZone              = 3600; // per-zone repeat interval (1 hour)
inline constexpr uint32_t kSmallTimerInterval           = 60;   // per-zone small timer (1 minute)

// ---------------------------------------------------------------------------
// External port consensus
// ---------------------------------------------------------------------------
inline constexpr uint32_t kExternalPortAskIPs = 3;

// ---------------------------------------------------------------------------
// Bootstrap readiness — minimum HELLO_RES packets before file operations
// ---------------------------------------------------------------------------
inline constexpr uint32_t kMinPacketsForReady = 3;

} // namespace eMule::kad
