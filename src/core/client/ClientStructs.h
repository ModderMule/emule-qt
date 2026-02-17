#pragma once

/// @file ClientStructs.h
/// @brief Supporting structs for download/upload block management.
///
/// Ported from MFC struct definitions used in DownloadClient.cpp and UploadClient.cpp.

#include "utils/Types.h"

#include <array>
#include <cstdint>

// Forward declare zlib stream
struct z_stream_s;

namespace eMule {

/// A block of file data requested from a peer (upload side tracks these).
struct Requested_Block_Struct {
    uint64 startOffset = 0;
    uint64 endOffset = 0;
    std::array<uint8, 16> fileID{};
    uint32 transferredByClient = 0;
};

/// A pending download block with optional zlib decompression state.
struct Pending_Block_Struct {
    Requested_Block_Struct* block = nullptr;
    z_stream_s* zStream = nullptr;
    uint32 totalUnzipped = 0;
    bool zStreamError = false;
    bool recovered = false;
    uint8 queued = 0;
};

/// Tracks a file we've requested from this client (upload request tracking).
struct Requested_File_Struct {
    std::array<uint8, 16> fileID{};
    uint32 lastAsked = 0;
    uint8 badRequests = 0;
};

} // namespace eMule
