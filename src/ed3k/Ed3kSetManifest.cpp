/// @file Ed3kSetManifest.cpp
/// @brief A set manifest read for the GUI and the downloads — implementation.

#include "Ed3kSetManifest.h"

#include <QDir>
#include <QRegularExpression>

#include <cstring>

namespace eMule::ed3k {

namespace {

// protobuf wire reading, just enough for SetFile / FileSet / Member
class Reader {
public:
    explicit Reader(QByteArrayView data) : m_data(data) {}

    [[nodiscard]] bool atEnd() const { return m_pos >= m_data.size(); }

    bool varint(std::uint64_t& out)
    {
        out = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (m_pos >= m_data.size())
                return false;
            const auto b = std::uint8_t(m_data[m_pos++]);
            out |= std::uint64_t(b & 0x7F) << shift;
            if ((b & 0x80) == 0)
                return true;
        }
        return false;
    }

    /// field number and wire type of the next field
    bool tag(std::uint32_t& field, std::uint32_t& wire)
    {
        std::uint64_t t = 0;
        if (!varint(t) || (t >> 3) == 0 || (t >> 3) > 0x1FFFFFFF)
            return false;
        field = std::uint32_t(t >> 3);
        wire = std::uint32_t(t & 7);
        return true;
    }

    bool bytes(QByteArrayView& out)
    {
        std::uint64_t len = 0;
        if (!varint(len) || len > std::uint64_t(m_data.size() - m_pos))
            return false;
        out = m_data.sliced(m_pos, qsizetype(len));
        m_pos += qsizetype(len);
        return true;
    }

    /// skips a field of wire type @p wire
    bool skip(std::uint32_t wire)
    {
        std::uint64_t v = 0;
        QByteArrayView b;
        switch (wire) {
        case 0: return varint(v);
        case 1: return advance(8);
        case 2: return bytes(b);
        case 5: return advance(4);
        default: return false;
        }
    }

private:
    bool advance(qsizetype n)
    {
        if (n > m_data.size() - m_pos)
            return false;
        m_pos += n;
        return true;
    }

    QByteArrayView m_data;
    qsizetype m_pos = 0;
};

struct RawMember {
    Ed3kSetEntry entry;
    QString name;
    std::uint32_t dir = 0;
};

std::optional<RawMember> parseMember(QByteArrayView data)
{
    Reader r(data);
    RawMember m;
    while (!r.atEnd()) {
        std::uint32_t field = 0;
        std::uint32_t wire = 0;
        if (!r.tag(field, wire))
            return std::nullopt;
        QByteArrayView b;
        std::uint64_t v = 0;
        if (wire == 2 && (field == 1 || field == 3 || field == 5 || field == 10)) {
            if (!r.bytes(b))
                return std::nullopt;
            if (field == 1) {
                if (b.size() != qsizetype(m.entry.root.bytes.size()))
                    return std::nullopt;
                std::memcpy(m.entry.root.bytes.data(), b.data(), m.entry.root.bytes.size());
            } else if (field == 3) {
                m.name = QString::fromUtf8(b);
            } else if (field == 5) {
                m.entry.ed2k = b.toByteArray();
            } else {
                m.entry.mount = true;
            }
        } else if (wire == 0 && (field == 2 || field == 4 || field == 6 || field == 7 || field == 8)) {
            if (!r.varint(v))
                return std::nullopt;
            if (field == 2)
                m.entry.size = v;
            else if (field == 4)
                m.dir = std::uint32_t(v);
            else if (field == 6)
                m.entry.isSet = v != 0;
            else if (field == 7)
                m.entry.setFiles = std::uint32_t(v);
            else
                m.entry.setSize = v;
        } else if (!r.skip(wire)) {
            return std::nullopt;
        }
    }
    if (m.name.isEmpty())
        return std::nullopt;
    return m;
}

bool parseBody(QByteArrayView body, Ed3kSetManifest& out)
{
    Reader r(body);
    std::vector<RawMember> members;
    while (!r.atEnd()) {
        std::uint32_t field = 0;
        std::uint32_t wire = 0;
        if (!r.tag(field, wire))
            return false;
        QByteArrayView b;
        std::uint64_t v = 0;
        if (wire == 2 && (field == 1 || field == 2 || field == 3 || field == 6)) {
            if (!r.bytes(b))
                return false;
            if (field == 1) {
                out.title = QString::fromUtf8(b);
            } else if (field == 2) {
                out.dirs.append(QString::fromUtf8(b));
            } else if (field == 3) {
                auto m = parseMember(b);
                if (!m)
                    return false;
                members.push_back(std::move(*m));
            } else {
                out.id = b.toByteArray();
            }
        } else if (wire == 0 && field == 7) {
            if (!r.varint(v))
                return false;
            out.seq = v;
        } else if (!r.skip(wire)) {
            return false;
        }
    }
    for (RawMember& m : members) {
        if (m.dir > std::uint32_t(out.dirs.size()))
            return false;
        m.entry.path = m.dir == 0 ? m.name : out.dirs.at(qsizetype(m.dir) - 1) + QLatin1Char('/') + m.name;
        out.files.push_back(std::move(m.entry));
    }
    return true;
}

} // namespace

std::optional<Ed3kSetManifest> Ed3kSetManifest::parse(const QByteArray& file)
{
    static const QByteArray kMagic("ed3kset\x01", 8);
    if (!file.startsWith(kMagic))
        return std::nullopt;
    Reader r(QByteArrayView(file).sliced(kMagic.size()));
    Ed3kSetManifest out;
    bool body = false;
    while (!r.atEnd()) {
        std::uint32_t field = 0;
        std::uint32_t wire = 0;
        if (!r.tag(field, wire))
            return std::nullopt;
        QByteArrayView b;
        if (wire == 2 && (field == 1 || field == 2)) {
            if (!r.bytes(b))
                return std::nullopt;
            if (field == 2) {
                out.key = b.toByteArray();
            } else {
                if (!parseBody(b, out))
                    return std::nullopt;
                body = true;
            }
        } else if (!r.skip(wire)) {
            return std::nullopt;
        }
    }
    if (!body)
        return std::nullopt;
    return out;
}

QString ed3kSafeName(const QString& name)
{
    QString n = name;
    for (QChar& c : n)
        if (c.unicode() < 0x20 || QStringLiteral("<>:\"/\\|?*").contains(c))
            c = QLatin1Char('_');
    // a trailing dot or space is dropped by Windows
    while (n.endsWith(QLatin1Char('.')) || n.endsWith(QLatin1Char(' ')))
        n.chop(1);
    if (n.isEmpty() || n == QLatin1String(".") || n == QLatin1String(".."))
        n = QStringLiteral("_");
    static const QRegularExpression reserved(QStringLiteral("^(con|prn|aux|nul|com[0-9]|lpt[0-9])(\\..*)?$"),
                                             QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(n).hasMatch())
        n.prepend(QLatin1Char('_'));
    // 255 bytes of UTF-8 at most per component
    while (n.toUtf8().size() > 240)
        n.chop(1);
    return n;
}

QString ed3kSetLocalPath(const QString& dir, const QString& path)
{
    QStringList parts;
    for (const QString& c : path.split(QLatin1Char('/'), Qt::SkipEmptyParts))
        parts.append(ed3kSafeName(c));
    if (parts.isEmpty())
        return {};
    const QString base = QDir::cleanPath(dir);
    const QString full = QDir::cleanPath(base + QLatin1Char('/') + parts.join(QLatin1Char('/')));
    // never out of the set's directory (§2.2)
    if (!full.startsWith(base + QLatin1Char('/')))
        return {};
    return full;
}

} // namespace eMule::ed3k
