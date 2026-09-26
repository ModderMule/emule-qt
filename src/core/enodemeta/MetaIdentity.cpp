#include "pch.h"
/// @file MetaIdentity.cpp
/// @brief Release identities behind a meta hash, and the fetched-file check.

#include "enodemeta/MetaIdentity.h"

#include <QCryptographicHash>
#include <QXmlStreamReader>

#include <algorithm>
#include <cctype>
#include <optional>

namespace eMule::enodemeta {

namespace {

// --- bencode span scanning ---------------------------------------------------

constexpr int kMaxBencodeDepth = 64;

/// End offset of the value starting at @p pos, or -1 when malformed.
qsizetype skipValue(const QByteArray& d, qsizetype pos, int depth = 0)
{
    if (pos >= d.size() || depth > kMaxBencodeDepth)
        return -1;
    const char c = d.at(pos);
    if (c == 'i') {
        const qsizetype e = d.indexOf('e', pos + 1);
        return e < 0 ? -1 : e + 1;
    }
    if (c == 'l' || c == 'd') {
        ++pos;
        while (pos < d.size() && d.at(pos) != 'e') {
            pos = skipValue(d, pos, depth + 1);
            if (pos < 0)
                return -1;
        }
        return pos < d.size() ? pos + 1 : -1;
    }
    if (c >= '0' && c <= '9') {
        const qsizetype colon = d.indexOf(':', pos);
        if (colon < 0)
            return -1;
        bool ok = false;
        const qint64 len = d.mid(pos, colon - pos).toLongLong(&ok);
        if (!ok || len < 0 || colon + 1 + len > d.size())
            return -1;
        return colon + 1 + len;
    }
    return -1;
}

/// A byte string at @p pos ("<len>:<bytes>"), or nullopt.
std::optional<QByteArray> readString(const QByteArray& d, qsizetype pos, qsizetype& end)
{
    end = skipValue(d, pos);
    if (end < 0 || d.at(pos) < '0' || d.at(pos) > '9')
        return std::nullopt;
    const qsizetype colon = d.indexOf(':', pos);
    return d.mid(colon + 1, end - colon - 1);
}

/// Raw spans of each top-level dict key's value. Empty when @p d is not a dict.
struct DictEntry {
    QByteArray key;
    qsizetype begin = 0;
    qsizetype end = 0;
};

std::optional<std::vector<DictEntry>> dictEntries(const QByteArray& d, qsizetype begin, qsizetype end)
{
    if (begin >= end || d.at(begin) != 'd')
        return std::nullopt;
    std::vector<DictEntry> out;
    qsizetype pos = begin + 1;
    while (pos < end - 1) {
        qsizetype keyEnd = 0;
        auto key = readString(d, pos, keyEnd);
        if (!key)
            return std::nullopt;
        const qsizetype valEnd = skipValue(d, keyEnd);
        if (valEnd < 0 || valEnd > end - 1)
            return std::nullopt;
        out.push_back({*key, keyEnd, valEnd});
        pos = valEnd;
    }
    return out;
}

const DictEntry* findKey(const std::vector<DictEntry>& entries, const char* key)
{
    for (const auto& e : entries)
        if (e.key == key)
            return &e;
    return nullptr;
}

// --- NZB charset -------------------------------------------------------------

QString declaredEncoding(const QByteArray& raw)
{
    const QByteArray head = raw.left(256);
    QByteArray trimmed = head;
    while (!trimmed.isEmpty() && (trimmed.at(0) == ' ' || trimmed.at(0) == '\t'
                                  || trimmed.at(0) == '\r' || trimmed.at(0) == '\n'))
        trimmed.remove(0, 1);
    if (trimmed.startsWith("\xEF\xBB\xBF"))
        trimmed.remove(0, 3);
    if (!trimmed.startsWith("<?xml"))
        return {};
    const qsizetype end = head.indexOf("?>");
    if (end < 0)
        return {};
    const QByteArray decl = head.left(end);
    const qsizetype idx = decl.indexOf("encoding");
    if (idx < 0)
        return {};
    QByteArray rest = decl.mid(idx + 8);
    qsizetype q = -1;
    for (qsizetype i = 0; i < rest.size(); ++i)
        if (rest.at(i) == '"' || rest.at(i) == '\'') { q = i; break; }
    if (q < 0)
        return {};
    rest = rest.mid(q + 1);
    for (qsizetype i = 0; i < rest.size(); ++i)
        if (rest.at(i) == '"' || rest.at(i) == '\'')
            return QString::fromLatin1(rest.left(i)).trimmed();
    return {};
}

bool isUtf8Name(const QString& n)
{
    const QString l = n.trimmed().toLower();
    return l.isEmpty() || l == u"utf-8" || l == u"utf8" || l == u"us-ascii" || l == u"ascii";
}

bool isLatin1Name(const QString& n)
{
    const QString l = n.trimmed().toLower();
    return l == u"iso-8859-1" || l == u"iso8859-1" || l == u"latin1" || l == u"latin-1"
        || l == u"windows-1252" || l == u"cp1252";
}

/// One leading '<' and one trailing '>', independently (not idempotent).
QString normalizeMessageId(QString id)
{
    id = id.trimmed();
    if (id.startsWith(u'<'))
        id.remove(0, 1);
    if (id.endsWith(u'>'))
        id.chop(1);
    return id;
}

struct NzbSeg {
    int number = 0;
    QString id;
};

} // namespace

std::expected<TorrentIdentity, QString> torrentIdentity(const QByteArray& metafile)
{
    const auto top = dictEntries(metafile, 0, metafile.size());
    if (!top || skipValue(metafile, 0) != metafile.size())
        return std::unexpected(QStringLiteral("not a bencoded dictionary"));

    // whole .torrent → its "info"; otherwise the buffer is the info dict itself
    qsizetype infoBegin = 0;
    qsizetype infoEnd = metafile.size();
    std::vector<DictEntry> info = *top;
    if (const auto* e = findKey(*top, "info")) {
        const auto inner = dictEntries(metafile, e->begin, e->end);
        if (!inner)
            return std::unexpected(QStringLiteral("the info value is not a dictionary"));
        infoBegin = e->begin;
        infoEnd = e->end;
        info = *inner;
    }

    const QByteArray raw = metafile.mid(infoBegin, infoEnd - infoBegin);

    int metaVersion = 0;
    if (const auto* e = findKey(info, "meta version"); e && metafile.at(e->begin) == 'i')
        metaVersion = metafile.mid(e->begin + 1, e->end - e->begin - 2).toInt();
    const auto* fileTree = findKey(info, "file tree");
    const bool hasFileTree = fileTree && metafile.at(fileTree->begin) == 'd';
    const auto* pieces = findKey(info, "pieces");
    const auto* files = findKey(info, "files");
    const auto* length = findKey(info, "length");
    const bool hasV1Layout = (pieces && std::isdigit(static_cast<unsigned char>(metafile.at(pieces->begin))))
                          || (files && metafile.at(files->begin) == 'l')
                          || (length && metafile.at(length->begin) == 'i');

    const bool v2 = hasFileTree && metaVersion >= 2;
    const bool v1 = !v2 || hasV1Layout;   // hybrid carries both

    TorrentIdentity out;
    if (v1)
        out.infoHashV1 = QCryptographicHash::hash(raw, QCryptographicHash::Sha1);
    if (v2)
        out.infoHashV2 = QCryptographicHash::hash(raw, QCryptographicHash::Sha256);
    if (v1) {
        out.kind = Kind::BtV1;
        out.identity = out.infoHashV1;
    } else {
        out.kind = Kind::BtV2;
        out.identity = out.infoHashV2;
    }
    return out;
}

std::expected<QByteArray, QString> nzbIdentityPreimage(const QByteArray& nzb)
{
    if (nzb.isEmpty())
        return std::unexpected(QStringLiteral("empty NZB"));
    if (nzb.size() > (64 << 20))
        return std::unexpected(QStringLiteral("NZB too large"));

    // decode ourselves so the reader sees text, not a charset to guess
    const QString declared = declaredEncoding(nzb);
    QString text;
    if (isUtf8Name(declared))
        text = QString::fromUtf8(nzb);   // invalid bytes become U+FFFD, like Go's repair
    else if (isLatin1Name(declared))
        text = QString::fromLatin1(nzb);
    else
        return std::unexpected(QStringLiteral("unsupported NZB charset \"%1\"").arg(declared));

    QXmlStreamReader xml(text);
    std::vector<std::vector<NzbSeg>> files;
    std::vector<NzbSeg> current;
    bool inFile = false;

    while (!xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const auto name = xml.name();
            if (name == u"file") {
                current.clear();
                inFile = true;
            } else if (name == u"segment") {
                const QString numAttr = xml.attributes().value(u"number").toString().trimmed();
                const QString raw = xml.readElementText(QXmlStreamReader::SkipChildElements);
                if (xml.hasError())
                    break;
                if (!inFile)
                    continue;
                const QString id = normalizeMessageId(raw);
                if (id.isEmpty())
                    continue;   // rule 6
                bool ok = false;
                const int n = numAttr.toInt(&ok);
                current.push_back({ok ? std::max(n, 0) : 0, id});
            }
        } else if (tok == QXmlStreamReader::EndElement && xml.name() == u"file" && inFile) {
            inFile = false;
            if (!current.empty())
                files.push_back(std::move(current));
            current = {};
        }
    }
    if (xml.hasError())
        return std::unexpected(QStringLiteral("malformed NZB: %1").arg(xml.errorString()));
    if (files.empty())
        return std::unexpected(QStringLiteral("not an NZB (no file with articles)"));

    QByteArray pre("nzb1\n");
    for (auto& segs : files) {
        std::stable_sort(segs.begin(), segs.end(),
                         [](const NzbSeg& a, const NzbSeg& b) { return a.number < b.number; });
        for (const auto& s : segs) {
            pre += QByteArray::number(s.number);
            pre += ':';
            pre += s.id.toUtf8();
            pre += '\n';
        }
    }
    return pre;
}

std::expected<QByteArray, QString> nzbIdentity(const QByteArray& nzb)
{
    auto pre = nzbIdentityPreimage(nzb);
    if (!pre)
        return std::unexpected(pre.error());
    return QCryptographicHash::hash(*pre, QCryptographicHash::Sha256);
}

std::expected<QByteArray, QString> identityOf(Kind kind, const QByteArray& metafile)
{
    switch (kind) {
    case Kind::BtV1:
    case Kind::BtV2: {
        auto t = torrentIdentity(metafile);
        if (!t)
            return std::unexpected(t.error());
        if (t->kind != kind)
            return std::unexpected(QStringLiteral("the row says %1, the metafile is %2")
                                       .arg(kindName(kind), kindName(t->kind)));
        return t->identity;
    }
    case Kind::Nzb:
        return nzbIdentity(metafile);
    default:
        return std::unexpected(QStringLiteral("no identity function for kind %1")
                                   .arg(static_cast<int>(kind)));
    }
}

std::expected<void, QString> verifyMetaFile(const uint8* hash16, const QByteArray& metafile)
{
    const auto parsed = parse(hash16);
    if (!parsed)
        return std::unexpected(QStringLiteral("not a meta hash: %1").arg(parseErrorText(parsed.error())));
    const auto identity = identityOf(parsed->kind, metafile);
    if (!identity)
        return std::unexpected(identity.error());
    if (!verifyIdentity(hash16, *identity))
        return std::unexpected(QStringLiteral("the metafile does not match its meta hash"));
    return {};
}

} // namespace eMule::enodemeta
