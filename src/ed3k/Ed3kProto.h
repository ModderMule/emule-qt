#pragma once

/// @file Ed3kProto.h
/// @brief protobuf wire reading and writing, just enough for the host's reads of what the
/// core already checked (set manifests, offers, passes) and for tests. No checks beyond
/// the framing. (Lifted from Ed3kSetManifest.cpp.)

#include <QByteArray>
#include <QByteArrayView>

#include <cstdint>

namespace eMule::ed3k {

class Ed3kProtoReader {
public:
    explicit Ed3kProtoReader(QByteArrayView data) : m_data(data) {}

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

class Ed3kProtoWriter {
public:
    Ed3kProtoWriter& varint(std::uint32_t field, std::uint64_t v)
    {
        raw((std::uint64_t(field) << 3) | 0);
        raw(v);
        return *this;
    }
    Ed3kProtoWriter& bytes(std::uint32_t field, QByteArrayView b)
    {
        raw((std::uint64_t(field) << 3) | 2);
        raw(std::uint64_t(b.size()));
        m_out.append(b);
        return *this;
    }
    [[nodiscard]] const QByteArray& data() const { return m_out; }

private:
    void raw(std::uint64_t v)
    {
        while (v >= 0x80) {
            m_out.append(char((v & 0x7F) | 0x80));
            v >>= 7;
        }
        m_out.append(char(v));
    }

    QByteArray m_out;
};

} // namespace eMule::ed3k
