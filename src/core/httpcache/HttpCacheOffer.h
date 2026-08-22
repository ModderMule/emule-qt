#pragma once

/// @file HttpCacheOffer.h
/// @brief OP_HTTPCACHE packet codec — pure serialization, no I/O and no state.
///
/// Every HTTP Cache message shares one frame:
///
///     <version 1><subop 1><tagcount 1>[new-style eD2K tags]
///
/// Keeping the codec free of sockets, files and preferences is what makes the
/// hostile-input surface testable on its own: HttpCacheOffer::parse() is the
/// only place a remote peer's bytes are interpreted, and tst_HttpCacheOffer
/// covers it without standing up a client.

#include "crypto/AesCbc.h"
#include "httpcache/HttpCacheTypes.h"
#include "utils/Types.h"

#include <QByteArray>
#include <QMetaType>
#include <QString>

#include <array>
#include <memory>

namespace eMule {

class Packet;

/// An uploader's "this part is in the cache, go and get it" message.
struct HttpCacheOffer {
    std::array<uint8, 16> fileHash{};
    uint32 partIndex = 0;
    uint64 plainLength = 0;      ///< plaintext bytes of this part
    uint64 cipherLength = 0;     ///< bytes the server will serve
    QString url;                 ///< absolute, as returned by the cache server
    QByteArray key;              ///< kAesKeySize bytes
    QByteArray iv;               ///< kAesIvSize bytes
    QByteArray cipherSha256;     ///< 32 bytes, over the ciphertext
    uint32 expiresAt = 0;        ///< unix time; 0 = unknown

    /// Structural sanity, applied before anything else looks at the offer.
    ///
    /// Deliberately does not check the URL host or whether we want the part —
    /// those need the IP filter and the download queue, so they live in
    /// HttpCacheClient. This is the part that can be tested in isolation.
    [[nodiscard]] bool isWellFormed() const;

    /// Human-readable reason isWellFormed() failed, for the log. Empty when it
    /// did not.
    [[nodiscard]] QString malformedReason() const;
};

/// A downloader's report on what happened to an offer.
struct HttpCacheReport {
    std::array<uint8, 16> fileHash{};
    uint32 partIndex = 0;
    HttpCacheResult result = HttpCacheResult::Ok;
    uint64 bytesFetched = 0;
};

/// Builders and parsers for OP_HTTPCACHE.
namespace HttpCacheCodec {

/// HCOP_OFFER.
[[nodiscard]] std::unique_ptr<Packet> buildOffer(const HttpCacheOffer& offer);

/// HCOP_RESULT, or HCOP_NONE when @p report declines without having tried.
[[nodiscard]] std::unique_ptr<Packet> buildReport(const HttpCacheReport& report, bool declined);

/// HCOP_CANCEL — the chunk is gone; stop waiting for it.
[[nodiscard]] std::unique_ptr<Packet> buildCancel(const std::array<uint8, 16>& fileHash,
                                                  uint32 partIndex);

/// What a parsed packet turned out to be.
enum class Kind : uint8 {
    Invalid,   ///< unusable: bad version, truncated, or a tag with the wrong type
    Offer,
    Report,
    Cancel,
};

/// Result of parsing one OP_HTTPCACHE payload.
struct Parsed {
    Kind kind = Kind::Invalid;
    HttpCacheOffer offer;
    HttpCacheReport report;
    QString error;    ///< set when kind == Invalid
};

/// Parse an OP_HTTPCACHE payload.
///
/// Never throws and never trusts a length in the packet. Unknown tag ids are
/// skipped so the tag set can grow, but an unknown *sub-opcode* is Invalid —
/// silently ignoring one would leave the sender waiting for a reply forever.
[[nodiscard]] Parsed parse(const uint8* data, uint32 size);

} // namespace HttpCacheCodec

} // namespace eMule

// Queued signal delivery and QSignalSpy both need these in the metatype system.
Q_DECLARE_METATYPE(eMule::HttpCacheOffer)
Q_DECLARE_METATYPE(eMule::HttpCacheReport)
