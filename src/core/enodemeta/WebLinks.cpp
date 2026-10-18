#include "pch.h"
/// @file WebLinks.cpp
/// @brief Web link sets — port of enodemeta/weblinks (Go).

#include "enodemeta/WebLinks.h"

#include <QCryptographicHash>

#include <algorithm>
#include <set>

namespace eMule::enodemeta {

namespace {

/// ASCII space, tab and CR from both ends — not QByteArray::trimmed(), which takes more.
QByteArray trim(const QByteArray& s)
{
    qsizetype b = 0;
    qsizetype e = s.size();
    const auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (b < e && ws(s.at(b)))
        ++b;
    while (e > b && ws(s.at(e - 1)))
        --e;
    return s.mid(b, e - b);
}

/// A to Z only.
QByteArray lowerAscii(QByteArray s)
{
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    return s;
}

/// ":80" on http, ":443" on https, or an empty port goes. IPv6 colons are no port.
QByteArray stripDefaultPort(const QByteArray& authority, const QByteArray& scheme)
{
    const qsizetype colon = authority.lastIndexOf(':');
    if (colon < 0 || colon < authority.lastIndexOf(']'))
        return authority;
    const QByteArray port = authority.mid(colon + 1);
    if (port.isEmpty() || (scheme == "http" && port == "80") || (scheme == "https" && port == "443"))
        return authority.left(colon);
    return authority;
}

/// Decimal hint, 0 when it is none: a bad hint does not refuse the links.
uint64 parseUint(const QByteArray& value, int bits)
{
    if (value.isEmpty() || !std::ranges::all_of(value, [](char c) { return c >= '0' && c <= '9'; }))
        return 0;
    bool ok = false;
    const qulonglong n = value.toULongLong(&ok);
    if (!ok || (bits == 32 && n > 0xFFFFFFFFull))
        return 0;
    return n;
}

} // namespace

int WebLinkSet::fileCount() const
{
    std::set<uint32> files;
    for (const WebLink& l : links)
        files.insert(l.file);
    return static_cast<int>(files.size());
}

std::expected<QByteArray, QString> canonicalLink(const QByteArray& raw)
{
    const QByteArray link = trim(raw);
    for (qsizetype i = 0; i < link.size(); ++i) {
        const auto c = static_cast<uint8>(link.at(i));
        if (c <= ' ' || c == 0x7F)
            return std::unexpected(QStringLiteral("a control character or space at byte %1").arg(i));
    }

    const qsizetype sep = link.indexOf("://");
    if (sep < 0)
        return std::unexpected(QStringLiteral("no scheme"));
    const QByteArray scheme = lowerAscii(link.left(sep));
    if (scheme != "http" && scheme != "https")
        return std::unexpected(QStringLiteral("scheme \"%1\" is not http or https")
                                   .arg(QString::fromUtf8(scheme)));
    QByteArray rest = link.mid(sep + 3);

    // an empty fragment says nothing; any other is kept (some hosters keep the key there)
    if (const qsizetype hash = rest.indexOf('#'); hash >= 0 && hash == rest.size() - 1)
        rest.chop(1);

    QByteArray authority = rest;
    QByteArray tail;
    for (qsizetype i = 0; i < rest.size(); ++i) {
        const char c = rest.at(i);
        if (c == '/' || c == '?' || c == '#') {
            authority = rest.left(i);
            tail = rest.mid(i);
            break;
        }
    }
    if (authority.contains('@'))
        return std::unexpected(QStringLiteral("a link with credentials"));
    authority = stripDefaultPort(lowerAscii(authority), scheme);
    if (authority.isEmpty())
        return std::unexpected(QStringLiteral("no host"));
    if (!tail.startsWith('/'))
        tail.prepend('/');

    return scheme + "://" + authority + tail;
}

std::expected<WebLinkSet, QString> parseWebLinks(const QByteArray& raw)
{
    WebLinkSet set;
    WebLink next;
    const QList<QByteArray> lines = raw.split('\n');
    for (qsizetype n = 0; n < lines.size(); ++n) {
        const QByteArray text = trim(lines.at(n));
        if (text.isEmpty())
            continue;

        if (text.startsWith('#')) {
            const qsizetype colon = text.indexOf(':');
            if (colon < 0)
                continue;
            const QByteArray key = lowerAscii(trim(text.mid(1, colon - 1)));
            if (key.isEmpty() || key.contains(' ') || key.contains('\t'))
                continue;
            const QByteArray value = trim(text.mid(colon + 1));
            if (key == "title")
                set.title = QString::fromUtf8(value);
            else if (key == "protected")
                set.isProtected = value == "1";
            else if (key == "file")
                next.file = static_cast<uint32>(parseUint(value, 32));
            else if (key == "name")
                next.name = QString::fromUtf8(value);
            else if (key == "size")
                next.size = parseUint(value, 64);
            else if (key == "hoster")
                next.hoster = QString::fromUtf8(value);
            continue;
        }

        auto canonical = canonicalLink(text);
        if (!canonical)
            return std::unexpected(QStringLiteral("line %1: %2").arg(n + 1).arg(canonical.error()));
        if (set.links.size() == kWebMaxLinks)
            return std::unexpected(QStringLiteral("more than %1 links").arg(kWebMaxLinks));
        next.url = std::move(*canonical);
        set.links.append(std::exchange(next, {}));
    }

    if (set.links.isEmpty())
        return std::unexpected(QStringLiteral("the container holds no link"));
    return set;
}

std::expected<QByteArray, QString> webIdentityPreimage(const WebLinkSet& set)
{
    std::set<QByteArray> links;   // distinct, ascending by bytes
    for (const WebLink& l : set.links) {
        auto canonical = canonicalLink(l.url);
        if (!canonical)
            return std::unexpected(canonical.error());
        links.insert(std::move(*canonical));
    }
    if (links.empty())
        return std::unexpected(QStringLiteral("the container holds no link"));

    QByteArray pre(kWebIdentityPrefix);
    for (const QByteArray& l : links) {
        pre += l;
        pre += '\n';
    }
    return pre;
}

std::expected<QByteArray, QString> webIdentity(const QByteArray& raw)
{
    const auto set = parseWebLinks(raw);
    if (!set)
        return std::unexpected(set.error());
    const auto pre = webIdentityPreimage(*set);
    if (!pre)
        return std::unexpected(pre.error());
    return QCryptographicHash::hash(*pre, QCryptographicHash::Sha256);
}

} // namespace eMule::enodemeta
