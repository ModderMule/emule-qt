#include "Ed3kDecline.h"

#include <QFile>
#include <QSet>

#include <algorithm>
#include <array>
#include <set>

namespace eMule::ed3k {

namespace {

/// proto Kind names (ed3k.proto `enum Kind`), index = number; NONE (0) is never listed
constexpr std::array<const char*, 16> kKindNames{"NONE", "SOURCE", "KEYWORD", "NOTE",   "HEAD", "MEMBER", "LIVE", "SUB",
                                                 "HIDDEN", "PART", "FRIEND", "ANON", "GATE", "ALIAS",  "NAME", "CLAIM"};

/// @p text exactly N bytes of hex; nullopt otherwise
template <std::size_t N>
std::optional<std::array<std::uint8_t, N>> hexBytes(const QString& text)
{
    if (text.size() != qsizetype(2 * N))
        return std::nullopt;
    const QByteArray bytes = QByteArray::fromHex(text.toLatin1());
    // fromHex skips bad characters: only when every one was hex
    if (bytes.size() != qsizetype(N) || QString::fromLatin1(bytes.toHex()).compare(text, Qt::CaseInsensitive) != 0)
        return std::nullopt;
    std::array<std::uint8_t, N> out{};
    std::copy(bytes.cbegin(), bytes.cend(), out.begin());
    return out;
}

/// The hash of `ed2k://|file|name|size|hash|/...`
std::optional<::ed3k::Ed2kHash> ed2kLinkHash(const QString& link)
{
    const QStringList parts = link.split(QLatin1Char('|'));
    if (parts.size() < 5 || parts.at(1).compare(QLatin1String("file"), Qt::CaseInsensitive) != 0)
        return std::nullopt;
    return hexBytes<16>(parts.at(4).trimmed());
}

bool takePrefix(const QString& line, QLatin1String prefix, QString& rest)
{
    if (!line.startsWith(prefix, Qt::CaseInsensitive))
        return false;
    rest = line.mid(prefix.size()).trimmed();
    return true;
}

} // namespace

bool DeclineList::listsRoot(const ::ed3k::Root& root) const
{
    return std::ranges::find(roots, root) != roots.end();
}

bool DeclineList::listsEd2k(const ::ed3k::Ed2kHash& hash) const
{
    return std::ranges::find(ed2k, hash) != ed2k.end();
}

bool DeclineList::listsEd2k(const QByteArray& md4) const
{
    if (md4.size() != 16)
        return false;
    ::ed3k::Ed2kHash h{};
    std::copy(md4.cbegin(), md4.cend(), h.begin());
    return listsEd2k(h);
}

bool DeclineList::listsFile(const ::ed3k::Root& root, const QByteArray& md4) const
{
    return listsRoot(root) || listsEd2k(md4);
}

::ed3k::DeclineLists DeclineList::toFfi() const
{
    ::ed3k::DeclineLists l;
    l.roots = roots;
    l.ed2k = ed2k;
    l.keys = keys;
    l.kinds = kinds;
    l.keywords.reserve(std::size_t(keywords.size()));
    for (const QString& k : keywords)
        l.keywords.push_back(k.toStdString());
    return l;
}

void DeclineList::merge(const DeclineList& other)
{
    // URL lists may be long: no linear find per entry
    const auto add = [](auto& to, const auto& from) {
        std::set seen(to.begin(), to.end());
        for (const auto& x : from)
            if (seen.insert(x).second)
                to.push_back(x);
    };
    add(roots, other.roots);
    add(ed2k, other.ed2k);
    add(keys, other.keys);
    add(kinds, other.kinds);
    QSet<QString> words(keywords.cbegin(), keywords.cend());
    for (const QString& k : other.keywords)
        if (!words.contains(k)) {
            words.insert(k);
            keywords.append(k);
        }
}

DeclineList parseDeclineList(const QString& text)
{
    DeclineList out;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        QString rest;
        // ed2k:// before ed2k: (both start so)
        if (line.startsWith(QLatin1String("ed2k://"), Qt::CaseInsensitive)) {
            if (const auto h = ed2kLinkHash(line))
                out.ed2k.push_back(*h);
            else
                out.invalid.append(line);
            continue;
        }
        if (takePrefix(line, QLatin1String("ed2k:"), rest)) {
            if (const auto h = hexBytes<16>(rest))
                out.ed2k.push_back(*h);
            else
                out.invalid.append(line);
            continue;
        }
        if (takePrefix(line, QLatin1String("key:"), rest)) {
            if (const auto k = hexBytes<32>(rest))
                out.keys.push_back(*k);
            else
                out.invalid.append(line);
            continue;
        }
        if (takePrefix(line, QLatin1String("kind:"), rest)) {
            if (const auto k = declineKindOf(rest))
                out.kinds.push_back(*k);
            else
                out.invalid.append(line);
            continue;
        }
        // was: if (line.size() == 64) { fromHex, compare, root } (now hexBytes)
        if (const auto r = hexBytes<32>(line)) {
            out.roots.push_back(::ed3k::Root{*r});
            continue;
        }
        out.keywords.append(line);
    }
    return out;
}

DeclineList loadDeclineList(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return parseDeclineList(QString::fromUtf8(file.readAll()));
}

std::optional<std::uint32_t> declineKindOf(const QString& name)
{
    bool number = false;
    const uint n = name.toUInt(&number);
    if (number)
        return n > 0 ? std::optional<std::uint32_t>(n) : std::nullopt;
    for (std::size_t i = 1; i < kKindNames.size(); ++i)
        if (name.compare(QLatin1String(kKindNames.at(i)), Qt::CaseInsensitive) == 0)
            return std::uint32_t(i);
    return std::nullopt;
}

} // namespace eMule::ed3k
