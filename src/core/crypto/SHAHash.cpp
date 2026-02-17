#include "SHAHash.h"
#include "utils/OtherFunctions.h"

namespace eMule {

QString ShaHasher::hashToString(const Sha1Digest* hashIn, bool urn)
{
    const QString base32 = encodeBase32(
        std::span<const uint8>(hashIn->b.data(), 20));
    return urn ? u"urn:sha1:" + base32 : base32;
}

QString ShaHasher::hashToHexString(const Sha1Digest* hashIn, bool urn)
{
    const QString hex = encodeBase16(
        std::span<const uint8>(hashIn->b.data(), 20));
    return urn ? u"urn:sha1:" + hex : hex;
}

bool ShaHasher::hashFromString(const QString& str, Sha1Digest* hashOut)
{
    if (str.length() < 32)
        return false;

    // Reject all-A hash (null in base32)
    if (str.left(32) == u"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")
        return false;

    Sha1Digest temp{};
    const std::size_t decoded = decodeBase32(str.left(32), temp.b.data(), 20);
    if (decoded != 20)
        return false;

    *hashOut = temp;
    return true;
}

bool ShaHasher::hashFromURN(const QString& str, Sha1Digest* hashOut)
{
    if (str.isEmpty())
        return false;

    const auto len = str.length();

    if (len >= 41 && str.left(9).compare(u"urn:sha1:", Qt::CaseInsensitive) == 0)
        return hashFromString(str.mid(9), hashOut);

    if (len >= 37 && str.left(5).compare(u"sha1:", Qt::CaseInsensitive) == 0)
        return hashFromString(str.mid(5), hashOut);

    // bitprint: 13 + 32 (SHA1) + 1 (.) + 39 (Tiger) = 85
    if (len >= 85 && str.left(13).compare(u"urn:bitprint:", Qt::CaseInsensitive) == 0)
        return hashFromString(str.mid(13), hashOut);

    if (len >= 81 && str.left(9).compare(u"bitprint:", Qt::CaseInsensitive) == 0)
        return hashFromString(str.mid(9), hashOut);

    return false;
}

bool ShaHasher::isNull(const Sha1Digest* hash)
{
    return *hash == Sha1Digest{};
}

} // namespace eMule
