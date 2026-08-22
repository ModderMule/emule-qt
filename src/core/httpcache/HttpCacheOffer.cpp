#include "pch.h"
/// @file HttpCacheOffer.cpp
/// @brief OP_HTTPCACHE packet codec — implementation.

#include "httpcache/HttpCacheOffer.h"

#include "net/Packet.h"
#include "protocol/Tag.h"
#include "utils/SafeFile.h"

#include <QUrl>

namespace eMule {

namespace {

/// SHA-256 digest length. Not in Opcodes.h because it is a property of the hash,
/// not of the protocol.
constexpr int kSha256Size = 32;

/// Combined length of the HCTAG_KEYIV blob.
constexpr int kKeyIvSize = kAesKeySize + kAesIvSize;

bool isHashNull(const std::array<uint8, 16>& hash)
{
    return std::all_of(hash.begin(), hash.end(), [](uint8 b) { return b == 0; });
}

/// Read and discard one tag. Used for ids we do not know: the value has already
/// been consumed by Tag's deserializing constructor, so there is nothing to do
/// beyond not caring about it.
void skip(const Tag&)
{
}

} // namespace

// ---------------------------------------------------------------------------
// HttpCacheOffer
// ---------------------------------------------------------------------------

QString HttpCacheOffer::malformedReason() const
{
    if (isHashNull(fileHash))
        return QStringLiteral("null file hash");

    if (plainLength == 0 || plainLength > kHttpCachePlainMax)
        return QStringLiteral("plaintext length %1 out of range").arg(plainLength);

    if (cipherLength == 0 || cipherLength > kHttpCacheCipherMax)
        return QStringLiteral("ciphertext length %1 out of range").arg(cipherLength);

    // The padding rule is fixed, so the two lengths must agree exactly. A peer
    // that gets this wrong is either buggy or trying to make us over-allocate.
    if (cipherLength != AesCbcEncryptor::cipherLengthFor(plainLength))
        return QStringLiteral("ciphertext length %1 does not match plaintext %2")
            .arg(cipherLength).arg(plainLength);

    if (key.size() != kAesKeySize || iv.size() != kAesIvSize)
        return QStringLiteral("bad key/iv size");

    if (cipherSha256.size() != kSha256Size)
        return QStringLiteral("bad ciphertext digest size");

    if (url.isEmpty() || url.size() > HTTPCACHE_MAX_URL_LEN)
        return QStringLiteral("url empty or too long");

    const QUrl parsed(url, QUrl::StrictMode);
    if (!parsed.isValid() || parsed.host().isEmpty())
        return QStringLiteral("url is not a valid absolute URL");

    const QString scheme = parsed.scheme();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return QStringLiteral("url scheme '%1' is not http(s)").arg(scheme);

    return {};
}

bool HttpCacheOffer::isWellFormed() const
{
    return malformedReason().isEmpty();
}

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------

namespace HttpCacheCodec {

std::unique_ptr<Packet> buildOffer(const HttpCacheOffer& offer)
{
    if (!offer.isWellFormed())
        return nullptr;

    QByteArray keyIv = offer.key;
    keyIv.append(offer.iv);

    SafeMemFile data;
    data.writeUInt8(HCPCK_VERSION);
    data.writeUInt8(HCOP_OFFER);
    data.writeUInt8(8);

    Tag(HCTAG_FILEID, offer.fileHash.data()).writeNewEd2kTag(data);
    Tag(HCTAG_PARTINDEX, offer.partIndex).writeNewEd2kTag(data);
    Tag(HCTAG_PLAINLEN, static_cast<uint32>(offer.plainLength)).writeNewEd2kTag(data);
    Tag(HCTAG_CIPHERLEN, static_cast<uint32>(offer.cipherLength)).writeNewEd2kTag(data);
    Tag(HCTAG_URL, offer.url).writeNewEd2kTag(data);
    Tag(HCTAG_KEYIV, keyIv).writeNewEd2kTag(data);
    Tag(HCTAG_CIPHERSHA, offer.cipherSha256).writeNewEd2kTag(data);
    Tag(HCTAG_EXPIRES, offer.expiresAt).writeNewEd2kTag(data);

    return std::make_unique<Packet>(data, OP_EMULEPROT, OP_HTTPCACHE);
}

std::unique_ptr<Packet> buildReport(const HttpCacheReport& report, bool declined)
{
    SafeMemFile data;
    data.writeUInt8(HCPCK_VERSION);
    data.writeUInt8(declined ? HCOP_NONE : HCOP_RESULT);
    data.writeUInt8(4);

    Tag(HCTAG_FILEID, report.fileHash.data()).writeNewEd2kTag(data);
    Tag(HCTAG_PARTINDEX, report.partIndex).writeNewEd2kTag(data);
    Tag(HCTAG_RESULT, static_cast<uint32>(report.result)).writeNewEd2kTag(data);
    Tag(HCTAG_BYTES, static_cast<uint32>(report.bytesFetched)).writeNewEd2kTag(data);

    return std::make_unique<Packet>(data, OP_EMULEPROT, OP_HTTPCACHE);
}

std::unique_ptr<Packet> buildCancel(const std::array<uint8, 16>& fileHash, uint32 partIndex)
{
    SafeMemFile data;
    data.writeUInt8(HCPCK_VERSION);
    data.writeUInt8(HCOP_CANCEL);
    data.writeUInt8(2);

    Tag(HCTAG_FILEID, fileHash.data()).writeNewEd2kTag(data);
    Tag(HCTAG_PARTINDEX, partIndex).writeNewEd2kTag(data);

    return std::make_unique<Packet>(data, OP_EMULEPROT, OP_HTTPCACHE);
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

Parsed parse(const uint8* data, uint32 size)
{
    Parsed out;

    // version + subop + tagcount
    if (!data || size < 3) {
        out.error = QStringLiteral("packet too short (%1 bytes)").arg(size);
        return out;
    }

    SafeMemFile in(data, size);

    try {
        const uint8 version = in.readUInt8();
        if (version != HCPCK_VERSION) {
            out.error = QStringLiteral("unsupported version 0x%1")
                            .arg(version, 2, 16, QLatin1Char('0'));
            return out;
        }

        const uint8 subOp = in.readUInt8();
        const uint8 tagCount = in.readUInt8();

        bool haveHash = false;
        bool havePart = false;
        std::array<uint8, 16> fileHash{};
        uint32 partIndex = 0;
        uint32 plainLength = 0;
        uint32 cipherLength = 0;
        uint32 expiresAt = 0;
        uint32 resultCode = 0;
        uint32 bytesFetched = 0;
        QString url;
        QByteArray keyIv;
        QByteArray cipherSha;

        for (uint8 i = 0; i < tagCount; ++i) {
            const Tag tag(in, false);

            switch (tag.nameId()) {
            case HCTAG_FILEID:
                if (tag.isHash()) {
                    std::memcpy(fileHash.data(), tag.hashValue(), fileHash.size());
                    haveHash = true;
                }
                break;
            case HCTAG_PARTINDEX:
                if (tag.isInt()) {
                    partIndex = tag.intValue();
                    havePart = true;
                }
                break;
            case HCTAG_PLAINLEN:
                if (tag.isInt())
                    plainLength = tag.intValue();
                break;
            case HCTAG_CIPHERLEN:
                if (tag.isInt())
                    cipherLength = tag.intValue();
                break;
            case HCTAG_URL:
                // Cap before copying: an oversized URL is rejected, never stored.
                if (tag.isStr() && tag.strValue().size() <= HTTPCACHE_MAX_URL_LEN)
                    url = tag.strValue();
                break;
            case HCTAG_KEYIV:
                if (tag.isBlob() && tag.blobValue().size() == kKeyIvSize)
                    keyIv = tag.blobValue();
                break;
            case HCTAG_CIPHERSHA:
                if (tag.isBlob() && tag.blobValue().size() == kSha256Size)
                    cipherSha = tag.blobValue();
                break;
            case HCTAG_EXPIRES:
                if (tag.isInt())
                    expiresAt = tag.intValue();
                break;
            case HCTAG_RESULT:
                if (tag.isInt())
                    resultCode = tag.intValue();
                break;
            case HCTAG_BYTES:
                if (tag.isInt())
                    bytesFetched = tag.intValue();
                break;
            default:
                // Forward compatibility: a tag we do not know is not an error.
                skip(tag);
                break;
            }
        }

        if (!haveHash || !havePart) {
            out.error = QStringLiteral("missing file hash or part index");
            return out;
        }

        switch (subOp) {
        case HCOP_OFFER:
            out.offer.fileHash = fileHash;
            out.offer.partIndex = partIndex;
            out.offer.plainLength = plainLength;
            out.offer.cipherLength = cipherLength;
            out.offer.url = url;
            out.offer.key = keyIv.left(kAesKeySize);
            out.offer.iv = keyIv.mid(kAesKeySize);
            out.offer.cipherSha256 = cipherSha;
            out.offer.expiresAt = expiresAt;

            if (!out.offer.isWellFormed()) {
                out.error = out.offer.malformedReason();
                out.offer = {};
                return out;
            }

            out.kind = Kind::Offer;
            return out;

        case HCOP_RESULT:
        case HCOP_NONE:
            out.report.fileHash = fileHash;
            out.report.partIndex = partIndex;
            out.report.bytesFetched = bytesFetched;
            out.report.result = (resultCode <= static_cast<uint32>(HttpCacheResult::Corrupt))
                                    ? static_cast<HttpCacheResult>(resultCode)
                                    : HttpCacheResult::HttpFailed;
            out.kind = Kind::Report;
            return out;

        case HCOP_CANCEL:
            out.report.fileHash = fileHash;
            out.report.partIndex = partIndex;
            out.kind = Kind::Cancel;
            return out;

        default:
            // Not skippable like an unknown tag: the sender is waiting on us.
            out.error = QStringLiteral("unknown sub-opcode 0x%1")
                            .arg(subOp, 2, 16, QLatin1Char('0'));
            return out;
        }
    } catch (const FileException&) {
        // Truncated packet: the tag block is variable length and carries no size
        // field, so this is the only way to notice.
        out.kind = Kind::Invalid;
        out.error = QStringLiteral("truncated packet");
        return out;
    }
}

} // namespace HttpCacheCodec

} // namespace eMule
