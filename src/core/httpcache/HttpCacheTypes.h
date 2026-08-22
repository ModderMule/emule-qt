#pragma once

/// @file HttpCacheTypes.h
/// @brief Shared vocabulary for the HTTP Cache feature.
///
/// See docs/protocol/http-cache-spec.md for the wire format and
/// data/../emule-http-cache-php/README.md for the server REST contract.

#include "utils/Opcodes.h"
#include "utils/Types.h"

namespace eMule {

/// Outcome a downloader reports back in HCOP_RESULT / HCOP_NONE.
///
/// The uploader uses these to decide whether the chunk is suspect (Corrupt,
/// SizeMismatch) or the peer simply could not take it right now (Busy,
/// Disabled) — only the former says anything about the blob.
enum class HttpCacheResult : uint8 {
    Ok = 0,             ///< fetched, decrypted and hash-verified
    Disabled = 1,       ///< peer has HTTP Cache downloads turned off
    Busy = 2,           ///< peer is already at its concurrent-fetch limit
    NotWanted = 3,      ///< peer does not need that part (any more)
    BadOffer = 4,       ///< offer failed validation: bad url, bad sizes, unknown file
    HttpFailed = 5,     ///< transport failure: connect, timeout, non-2xx status
    SizeMismatch = 6,   ///< server delivered a different number of bytes than promised
    Corrupt = 7,        ///< SHA-256 or AES padding check failed — the blob is wrong
};

/// Largest plaintext an offer may describe: one full eMule part.
inline constexpr uint64 kHttpCachePlainMax = PARTSIZE;

/// Largest ciphertext, i.e. a full part plus its PKCS#7 pad block.
inline constexpr uint64 kHttpCacheCipherMax = PARTSIZE + 16;

/// Only parts of exactly this size are published. A file's short tail part stays
/// on ed2k: it is by definition the least shared part of the file, and special
/// casing it would buy nothing.
inline constexpr uint64 kHttpCachePublishPartSize = PARTSIZE;

} // namespace eMule
