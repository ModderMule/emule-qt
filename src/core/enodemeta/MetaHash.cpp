#include "pch.h"
/// @file MetaHash.cpp
/// @brief eNode meta pseudo-hash — port of enodemeta/metahash (Go).

#include "enodemeta/MetaHash.h"

#include <cstring>

namespace eMule::enodemeta {

namespace {

constexpr uint8 kFlagMask = 0x0F;

// uint32 index → uint16 hash field; a wide index needs PathAuthoritative
std::expected<uint16, ParseError> indexFor(uint32 fileIndex, uint8 flags)
{
    if (fileIndex == kFileIndexWholeSet32)
        return kFileIndexWholeSet;
    if (fileIndex == kFileIndexWholeSet)
        return std::unexpected(ParseError::Length);   // low bits collide with whole-set marker
    if (fileIndex > 0xFFFF && !(flags & FlagPathAuthoritative))
        return std::unexpected(ParseError::Length);
    return static_cast<uint16>(fileIndex & 0xFFFF);
}

} // namespace

int identityLength(Kind k)
{
    switch (k) {
    case Kind::BtV1: return 20;
    case Kind::BtV2: return 32;
    case Kind::Nzb:  return 32;
    default:         return 0;
    }
}

Digest fold10(std::span<const uint8> data)
{
    Digest out{};
    for (size_t i = 0; i < data.size(); ++i)
        out[i % kDigestSize] ^= data[i];
    return out;
}

Digest fold10(const QByteArray& data)
{
    return fold10(std::span(reinterpret_cast<const uint8*>(data.constData()),
                            static_cast<size_t>(data.size())));
}

std::expected<ParsedHash, ParseError> parse(std::span<const uint8> b)
{
    if (b.size() != kMetaHashSize)
        return std::unexpected(ParseError::Length);
    if (b[0] != kMagic0 || b[1] != kMagic1)
        return std::unexpected(ParseError::Magic);
    if (b[2] != kVersion1)
        return std::unexpected(ParseError::Version);

    const auto kind = static_cast<Kind>(b[3] >> 4);
    if (!isValidKind(kind))
        return std::unexpected(ParseError::Kind);

    const uint8 flags = b[3] & kFlagMask;
    if (flags & FlagReserved)
        return std::unexpected(ParseError::ReservedFlag);

    ParsedHash p;
    p.version = b[2];
    p.kind = kind;
    p.flags = flags;
    p.fileIndex = static_cast<uint16>(b[4] | (b[5] << 8));
    std::memcpy(p.digest.data(), b.data() + 6, kDigestSize);
    return p;
}

std::expected<ParsedHash, ParseError> parse(const uint8* hash16)
{
    return parse(std::span<const uint8>(hash16, kMetaHashSize));
}

bool isMetaHash(const uint8* hash16)
{
    return parse(hash16).has_value();
}

std::expected<std::array<uint8, kMetaHashSize>, ParseError>
build(Kind kind, uint8 flags, uint32 fileIndex, const QByteArray& identity)
{
    if (!isValidKind(kind))
        return std::unexpected(ParseError::Kind);
    if (identity.size() != identityLength(kind))
        return std::unexpected(ParseError::Length);
    if (flags & FlagReserved)
        return std::unexpected(ParseError::ReservedFlag);

    const auto index16 = indexFor(fileIndex, flags);
    if (!index16)
        return std::unexpected(index16.error());

    std::array<uint8, kMetaHashSize> h{};
    h[0] = kMagic0;
    h[1] = kMagic1;
    h[2] = kVersion1;
    h[3] = static_cast<uint8>((static_cast<uint8>(kind) << 4) | (flags & kFlagMask));
    h[4] = static_cast<uint8>(*index16 & 0xFF);
    h[5] = static_cast<uint8>(*index16 >> 8);
    const Digest d = fold10(identity);
    std::memcpy(h.data() + 6, d.data(), kDigestSize);
    return h;
}

bool crossCheck(const ParsedHash& parsed, uint8 tagKind, uint8 tagVersion, uint32 tagFileIndex)
{
    return tagVersion == parsed.version
        && tagKind == static_cast<uint8>(parsed.kind)
        && static_cast<uint16>(tagFileIndex & 0xFFFF) == parsed.fileIndex;
}

bool sameRelease(const uint8* a16, const uint8* b16)
{
    return std::memcmp(a16 + 6, b16 + 6, kDigestSize) == 0;
}

bool verifyIdentity(const uint8* hash16, const QByteArray& identity)
{
    const Digest d = fold10(identity);
    return std::memcmp(hash16 + 6, d.data(), kDigestSize) == 0;
}

QString kindName(Kind k)
{
    switch (k) {
    case Kind::BtV1: return QStringLiteral("bt-v1");
    case Kind::BtV2: return QStringLiteral("bt-v2");
    case Kind::Nzb:  return QStringLiteral("nzb");
    default:         return QStringLiteral("unspecified");
    }
}

QString parseErrorText(ParseError e)
{
    switch (e) {
    case ParseError::Length:       return QStringLiteral("bad length");
    case ParseError::Magic:        return QStringLiteral("no meta magic");
    case ParseError::Version:      return QStringLiteral("unknown version");
    case ParseError::Kind:         return QStringLiteral("unknown kind");
    case ParseError::ReservedFlag: return QStringLiteral("reserved flag set");
    }
    return {};
}

} // namespace eMule::enodemeta
