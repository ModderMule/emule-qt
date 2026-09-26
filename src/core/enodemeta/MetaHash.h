#pragma once

/// @file MetaHash.h
/// @brief eNode meta pseudo-hash — port of enodemeta/metahash (Go).
///
/// A torrent or Usenet row in an eD2K search answer carries a 16-byte
/// pseudo-hash instead of an MD4:
///
///   offset size field
///        0    2 magic 0xED 0x2B
///        2    1 version (1)
///        3    1 kind (high nibble) | flags (low nibble)
///        4    2 file index, uint16 LE (0xFFFF = whole release)
///        6   10 fold10 of the release identity
///
/// The network of a row is decided by this hash, never by its name.
/// Test vectors: external/enodemeta/testdata/meta-hash-vectors.json.

#include "utils/Types.h"

#include <QByteArray>
#include <QString>

#include <array>
#include <expected>
#include <span>

namespace eMule::enodemeta {

inline constexpr int kMetaHashSize = 16;
inline constexpr int kDigestSize   = 10;
inline constexpr uint8 kMagic0     = 0xED;
inline constexpr uint8 kMagic1     = 0x2B;
inline constexpr uint8 kVersion1   = 0x01;

inline constexpr uint16 kFileIndexWholeSet   = 0xFFFF;
inline constexpr uint32 kFileIndexWholeSet32 = 0xFFFFFFFF;

/// Release kind (hash high nibble, FT_META_KIND).
enum class Kind : uint8 {
    Unspecified = 0,
    BtV1        = 1,   ///< BitTorrent v1 or hybrid, identity = 20-byte v1 infohash
    BtV2        = 2,   ///< BitTorrent v2 only, identity = 32-byte v2 infohash
    Nzb         = 3,   ///< Usenet, identity = 32-byte canonical NZB digest
};

/// Hash flag bits (low nibble). Not the FT_META_FLAGS bitfield.
enum HashFlag : uint8 {
    FlagMultiFile         = 0x1,
    FlagPathAuthoritative = 0x2,
    FlagProtected         = 0x4,
    FlagReserved          = 0x8,   ///< must be 0
};

enum class ParseError : uint8 {
    Length,
    Magic,
    Version,
    Kind,
    ReservedFlag,
};

using Digest = std::array<uint8, kDigestSize>;

struct ParsedHash {
    uint8  version = 0;
    Kind   kind = Kind::Unspecified;
    uint8  flags = 0;
    uint16 fileIndex = 0;
    Digest digest{};

    [[nodiscard]] bool isTorrent() const { return kind == Kind::BtV1 || kind == Kind::BtV2; }
    [[nodiscard]] bool isNzb() const { return kind == Kind::Nzb; }
};

[[nodiscard]] constexpr bool isValidKind(Kind k)
{
    return k == Kind::BtV1 || k == Kind::BtV2 || k == Kind::Nzb;
}

/// Identity length a kind takes (20 or 32), 0 for an invalid kind.
[[nodiscard]] int identityLength(Kind k);

/// XOR-fold @p data into 10 bytes: out[i % 10] ^= data[i].
[[nodiscard]] Digest fold10(std::span<const uint8> data);
[[nodiscard]] Digest fold10(const QByteArray& data);

/// Take a meta hash apart; fails for anything that is not one.
[[nodiscard]] std::expected<ParsedHash, ParseError> parse(std::span<const uint8> hash);
[[nodiscard]] std::expected<ParsedHash, ParseError> parse(const uint8* hash16);

/// Marker test only — the row's tags decide (see crossCheck).
[[nodiscard]] bool isMetaHash(const uint8* hash16);

/// Mint a hash (tests and tools; clients never mint).
[[nodiscard]] std::expected<std::array<uint8, kMetaHashSize>, ParseError>
build(Kind kind, uint8 flags, uint32 fileIndex, const QByteArray& identity);

/// Compare the row's FT_META_* tags against its hash. Hash flags have no tag,
/// so only version, kind and the low 16 index bits are compared.
[[nodiscard]] bool crossCheck(const ParsedHash& parsed, uint8 tagKind, uint8 tagVersion,
                              uint32 tagFileIndex);

/// Same release: digests equal (different files of one torrent match).
[[nodiscard]] bool sameRelease(const uint8* a16, const uint8* b16);

/// Does @p identity fold to the hash's digest?
[[nodiscard]] bool verifyIdentity(const uint8* hash16, const QByteArray& identity);

[[nodiscard]] QString kindName(Kind k);
[[nodiscard]] QString parseErrorText(ParseError e);

} // namespace eMule::enodemeta
