/// @file Ed3kPlumbing.cpp
/// @brief Preferences → ed3k engine helpers — implementation.

#include "Ed3kPlumbing.h"

#include "net/Address.h"
#include "net/IPv6SourcePin.h"

#include <QUrl>

#include <array>
#include <utility>

namespace eMule::ed3k {

namespace {

/// the optional extensions of version 1 by the names of their documents (ext::switch)
constexpr std::array<std::pair<const char*, std::uint64_t>, 23> kExtensions{{
    {"alias", ED3K_EXT_ALIAS},
    {"bootstrap", ED3K_EXT_BOOTSTRAP},
    {"browse", ED3K_EXT_BROWSE},
    {"chaintube", ED3K_EXT_CHAINTUBE},
    {"chat", ED3K_EXT_CHAT},
    {"credit", ED3K_EXT_CREDIT},
    {"file-set", ED3K_EXT_FILE_SET},
    {"filter-list", ED3K_EXT_FILTER_LIST},
    {"flood-protection", ED3K_EXT_FLOOD_PROTECTION},
    {"friend", ED3K_EXT_FRIEND},
    {"hidden-set", ED3K_EXT_HIDDEN_SET},
    {"hops", ED3K_EXT_HOPS},
    {"hoster", ED3K_EXT_HOSTER},
    {"http-source", ED3K_EXT_HTTP_SOURCE},
    {"lan-discovery", ED3K_EXT_LAN},
    {"live", ED3K_EXT_LIVE},
    {"multi-author-set", ED3K_EXT_MULTI_AUTHOR_SET},
    {"pubsub", ED3K_EXT_PUBSUB},
    {"subscription", ED3K_EXT_SUBSCRIPTION},
    {"torrent", ED3K_EXT_TORRENT},
    {"usenet", ED3K_EXT_USENET},
    {"room", ED3K_EXT_ROOM},
    {"name", ED3K_EXT_NAMES},
}};

constexpr char kBase32[] = "abcdefghijklmnopqrstuvwxyz234567";

} // namespace

std::uint64_t extBit(const QString& name)
{
    const QString n = name.trimmed().toLower();
    if (n == QLatin1StringView("lan"))   // as packmule's --ext-off
        return ED3K_EXT_LAN;
    for (const auto& [key, bit] : kExtensions)
        if (n == QLatin1StringView(key))
            return bit;
    return 0;
}

std::uint64_t extOffBits(const QStringList& names, QStringList* unknown)
{
    std::uint64_t bits = 0;
    for (const QString& name : names) {
        if (name.trimmed().isEmpty())
            continue;
        if (const std::uint64_t bit = extBit(name))
            bits |= bit;
        else if (unknown)
            unknown->append(name.trimmed());
    }
    return bits;
}

std::optional<NodesSource> parseNodesLink(const QString& link)
{
    const QString text = link.trimmed();
    static const QLatin1StringView kPrefix("ed3k:nodes?");
    if (!text.startsWith(kPrefix, Qt::CaseInsensitive))
        return std::nullopt;
    std::optional<QString> url;
    std::optional<QString> key;
    for (const QString& part : text.mid(kPrefix.size()).split(QLatin1Char('&'))) {
        const qsizetype eq = part.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const QString name = part.left(eq);
        const QString value = QUrl::fromPercentEncoding(part.mid(eq + 1).toUtf8());
        auto* slot = name == QLatin1StringView("url") ? &url : name == QLatin1StringView("key") ? &key : nullptr;
        if (!slot)
            continue;   // another parameter: not ours to judge
        if (*slot)
            return std::nullopt;   // repeated
        *slot = value;
    }
    if (!url || !key)
        return std::nullopt;
    const QUrl u(*url, QUrl::StrictMode);
    if (!u.isValid() || u.isRelative() || u.scheme() != QLatin1StringView("https") || u.host().isEmpty())
        return std::nullopt;
    const auto raw = fromBase32(*key);
    if (!raw || raw->size() != 32)
        return std::nullopt;
    NodesSource out;
    out.url = *url;
    std::copy(raw->begin(), raw->end(), out.key.begin());
    return out;
}

QString nodesLink(const QString& url, const ::ed3k::Key32& key)
{
    return QStringLiteral("ed3k:nodes?url=%1&key=%2")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(url)), base32(key));
}

QString base32(std::span<const std::uint8_t> bytes)
{
    QString out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const std::uint8_t b : bytes) {
        buffer = (buffer << 8) | b;
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out.append(QLatin1Char(kBase32[(buffer >> bits) & 0x1f]));
        }
    }
    if (bits > 0)
        out.append(QLatin1Char(kBase32[(buffer << (5 - bits)) & 0x1f]));
    return out;
}

std::optional<QByteArray> fromBase32(const QString& text)
{
    QByteArray out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const QChar c : text) {
        const char l = char(c.toLower().unicode());
        int v = -1;
        if (l >= 'a' && l <= 'z')
            v = l - 'a';
        else if (l >= '2' && l <= '7')
            v = 26 + (l - '2');
        if (v < 0 || c.unicode() > 0x7f)
            return std::nullopt;
        buffer = (buffer << 5) | std::uint32_t(v);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.append(char((buffer >> bits) & 0xff));
        }
    }
    // left-over bits must be zero padding of the last byte
    if (bits >= 5 || (buffer & ((1u << bits) - 1)) != 0)
        return std::nullopt;
    return out;
}

::ed3k::Address toAddress(const QHostAddress& ip, std::uint16_t port)
{
    bool v4 = false;
    const quint32 n = ip.toIPv4Address(&v4);   // also ::ffff:a.b.c.d
    if (v4)
        return ::ed3k::Address::v4(std::uint8_t(n >> 24), std::uint8_t(n >> 16), std::uint8_t(n >> 8), std::uint8_t(n), port);
    ::ed3k::Address a;
    const Q_IPV6ADDR v6 = ip.toIPv6Address();
    std::copy(std::begin(v6.c), std::end(v6.c), a.ip.begin());
    a.port = port;
    return a;
}

QHostAddress toQHostAddress(const ::ed3k::Address& a)
{
    if (a.isV4())
        return QHostAddress((quint32(a.ip[12]) << 24) | (quint32(a.ip[13]) << 16) | (quint32(a.ip[14]) << 8)
                            | quint32(a.ip[15]));
    Q_IPV6ADDR v6;
    std::copy(a.ip.begin(), a.ip.end(), v6.c);
    return QHostAddress(v6);
}

QHostAddress advertisedIPv6(const QHostAddress& bind, const QHostAddress& pin)
{
    if (bind != QHostAddress::Any && bind != QHostAddress::AnyIPv6)
        return {};
    if (pin.isNull() || pin.protocol() != QAbstractSocket::IPv6Protocol)
        return {};
    return pin;
}

QHostAddress ipv6Pin()
{
    // sourceFor() applies the pin to IPv6 peers only, and not with the privacy address or a
    // proxy; any global address asks it (2001:db8::/32, documentation only)
    static const Address kProbe = Address::fromString(QStringLiteral("2001:db8::1"));
    const Address pin = IPv6SourcePin::sourceFor(kProbe);
    return pin.isNull() ? QHostAddress() : pin.toQHostAddress();
}

} // namespace eMule::ed3k
