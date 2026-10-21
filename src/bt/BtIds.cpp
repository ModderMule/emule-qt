#include "BtIds.h"

namespace eMule::bt {

namespace {

constexpr QLatin1StringView kV1Prefix{"bt:v1:"};
constexpr QLatin1StringView kV2Prefix{"bt:v2:"};

/// Upper-case hex of exactly `bytes` bytes, or nullopt.
std::optional<QByteArray> hexOf(QStringView text, qsizetype bytes)
{
    if (text.size() != bytes * 2)
        return std::nullopt;
    for (QChar c : text)
        if (!((c >= u'0' && c <= u'9') || (c >= u'A' && c <= u'F') || (c >= u'a' && c <= u'f')))
            return std::nullopt;
    return QByteArray::fromHex(text.toLatin1());
}

} // namespace

QString BtIds::idOf(const BtInfoHash& hash)
{
    if (hash.v1.size() == 20)
        return kV1Prefix + QString::fromLatin1(hash.v1.toHex().toUpper());
    if (hash.v2.size() == 32)
        return kV2Prefix + QString::fromLatin1(hash.v2.toHex().toUpper());
    return {};
}

bool BtIds::isTorrentId(const QString& id)
{
    return parse(id).has_value();
}

std::optional<BtInfoHash> BtIds::parse(const QString& id)
{
    if (id.startsWith(kV1Prefix)) {
        auto bytes = hexOf(QStringView{id}.sliced(kV1Prefix.size()), 20);
        if (!bytes)
            return std::nullopt;
        return BtInfoHash{*bytes, {}};
    }
    if (id.startsWith(kV2Prefix)) {
        auto bytes = hexOf(QStringView{id}.sliced(kV2Prefix.size()), 32);
        if (!bytes)
            return std::nullopt;
        return BtInfoHash{{}, *bytes};
    }
    return std::nullopt;
}

} // namespace eMule::bt
