/// @file Tag.cpp
/// @brief ED2K tag system implementation — port of CTag from MFC Packets.cpp.

#include "Tag.h"
#include "utils/Log.h"

#include <cstring>

namespace eMule {

// ---------------------------------------------------------------------------
// Construction from numeric name ID + value
// ---------------------------------------------------------------------------

Tag::Tag(uint8 nameId, uint32 value)
    : m_value(static_cast<uint64>(value))
    , m_type(TAGTYPE_UINT32)
    , m_nameId(nameId)
{
}

Tag::Tag(uint8 nameId, uint64 value)
    : m_value(value)
    , m_type(TAGTYPE_UINT64)
    , m_nameId(nameId)
{
}

Tag::Tag(uint8 nameId, const QString& value)
    : m_value(value)
    , m_type(TAGTYPE_STRING)
    , m_nameId(nameId)
{
}

Tag::Tag(uint8 nameId, float value)
    : m_value(value)
    , m_type(TAGTYPE_FLOAT32)
    , m_nameId(nameId)
{
}

Tag::Tag(uint8 nameId, const uint8* hash16)
    : m_type(TAGTYPE_HASH)
    , m_nameId(nameId)
{
    HashArray h{};
    std::memcpy(h.data(), hash16, 16);
    m_value = h;
}

Tag::Tag(uint8 nameId, QByteArray blobData)
    : m_value(std::move(blobData))
    , m_type(TAGTYPE_BLOB)
    , m_nameId(nameId)
{
}

// ---------------------------------------------------------------------------
// Construction from string name + value
// ---------------------------------------------------------------------------

Tag::Tag(QByteArray name, uint32 value)
    : m_value(static_cast<uint64>(value))
    , m_name(std::move(name))
    , m_type(TAGTYPE_UINT32)
{
}

Tag::Tag(QByteArray name, uint64 value)
    : m_value(value)
    , m_name(std::move(name))
    , m_type(TAGTYPE_UINT64)
{
}

Tag::Tag(QByteArray name, const QString& value)
    : m_value(value)
    , m_name(std::move(name))
    , m_type(TAGTYPE_STRING)
{
}

Tag::Tag(QByteArray name, QByteArray blobData)
    : m_value(std::move(blobData))
    , m_name(std::move(name))
    , m_type(TAGTYPE_BLOB)
{
}

Tag::Tag(QByteArray name, const uint8* hash16)
    : m_name(std::move(name))
    , m_type(TAGTYPE_HASH)
{
    HashArray h{};
    std::memcpy(h.data(), hash16, 16);
    m_value = h;
}

// ---------------------------------------------------------------------------
// Deserialization constructor
// ---------------------------------------------------------------------------

Tag::Tag(FileDataIO& data, bool optUTF8)
{
    m_type = data.readUInt8();

    // Read the tag name
    if (m_type & 0x80) {
        // New-format: high bit set means 1-byte numeric ID follows
        m_type &= 0x7F;
        m_nameId = data.readUInt8();
    } else {
        // Old-format: uint16 length + string name
        const uint16 nameLen = data.readUInt16();
        if (nameLen == 1) {
            m_nameId = data.readUInt8();
        } else if (nameLen > 0) {
            m_name.resize(nameLen);
            data.read(m_name.data(), nameLen);
        }
    }

    // Read the tag value based on type
    switch (m_type) {
    case TAGTYPE_STRING: {
        m_value = data.readString(optUTF8);
        break;
    }
    case TAGTYPE_UINT32:
        m_value = static_cast<uint64>(data.readUInt32());
        break;
    case TAGTYPE_UINT64:
        m_value = data.readUInt64();
        break;
    case TAGTYPE_UINT16:
        // Normalize to UINT32 on read (matches original behavior)
        m_value = static_cast<uint64>(data.readUInt16());
        m_type = TAGTYPE_UINT32;
        break;
    case TAGTYPE_UINT8:
        // Normalize to UINT32 on read (matches original behavior)
        m_value = static_cast<uint64>(data.readUInt8());
        m_type = TAGTYPE_UINT32;
        break;
    case TAGTYPE_FLOAT32: {
        float f = 0.0f;
        data.read(&f, sizeof(f));
        m_value = f;
        break;
    }
    case TAGTYPE_HASH: {
        HashArray h{};
        data.read(h.data(), 16);
        m_value = h;
        break;
    }
    case TAGTYPE_BLOB: {
        const uint32 blobLen = data.readUInt32();
        QByteArray blob(static_cast<qsizetype>(blobLen), Qt::Uninitialized);
        data.read(blob.data(), blobLen);
        m_value = std::move(blob);
        break;
    }
    case TAGTYPE_BSOB: {
        const uint8 bsobLen = data.readUInt8();
        QByteArray bsob(bsobLen, Qt::Uninitialized);
        data.read(bsob.data(), bsobLen);
        m_value = std::move(bsob);
        m_type = TAGTYPE_BLOB; // Treat BSOB as blob
        break;
    }
    default:
        // Handle STR1–STR22 compact string types
        if (m_type >= TAGTYPE_STR1 && m_type <= TAGTYPE_STR22) {
            const uint32 strLen = m_type - TAGTYPE_STR1 + 1;
            m_value = data.readString(optUTF8, strLen);
            m_type = TAGTYPE_STRING;
        } else {
            logWarning(QStringLiteral("Unknown tag type 0x%1").arg(m_type, 2, 16, QChar(u'0')));
            m_type = TAGTYPE_NONE;
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// Type queries
// ---------------------------------------------------------------------------

uint8 Tag::type() const { return m_type; }
uint8 Tag::nameId() const { return m_nameId; }
const QByteArray& Tag::name() const { return m_name; }
bool Tag::hasName() const { return !m_name.isEmpty(); }

bool Tag::isStr() const { return m_type == TAGTYPE_STRING; }
bool Tag::isInt() const { return m_type == TAGTYPE_UINT32; }

bool Tag::isInt64(bool orInt32) const
{
    return m_type == TAGTYPE_UINT64 || (orInt32 && m_type == TAGTYPE_UINT32);
}

bool Tag::isFloat() const { return m_type == TAGTYPE_FLOAT32; }
bool Tag::isHash() const { return m_type == TAGTYPE_HASH; }
bool Tag::isBlob() const { return m_type == TAGTYPE_BLOB; }

// ---------------------------------------------------------------------------
// Value access
// ---------------------------------------------------------------------------

uint32 Tag::intValue() const
{
    return static_cast<uint32>(std::get<uint64>(m_value));
}

uint64 Tag::int64Value() const
{
    return std::get<uint64>(m_value);
}

const QString& Tag::strValue() const
{
    return std::get<QString>(m_value);
}

float Tag::floatValue() const
{
    return std::get<float>(m_value);
}

const uint8* Tag::hashValue() const
{
    return std::get<HashArray>(m_value).data();
}

const QByteArray& Tag::blobValue() const
{
    return std::get<QByteArray>(m_value);
}

// ---------------------------------------------------------------------------
// Mutators
// ---------------------------------------------------------------------------

void Tag::setInt(uint32 val)
{
    m_value = static_cast<uint64>(val);
    m_type = TAGTYPE_UINT32;
}

void Tag::setInt64(uint64 val)
{
    m_value = val;
    m_type = TAGTYPE_UINT64;
}

void Tag::setStr(const QString& val)
{
    m_value = val;
    m_type = TAGTYPE_STRING;
}

// ---------------------------------------------------------------------------
// writeTagToFile — old-format serialization (no size optimization)
// ---------------------------------------------------------------------------

bool Tag::writeTagToFile(FileDataIO& file, UTF8Mode encode) const
{
    // Write type byte
    file.writeUInt8(m_type);

    // Write name
    if (m_nameId != 0 && m_name.isEmpty()) {
        file.writeUInt16(1);
        file.writeUInt8(m_nameId);
    } else {
        file.writeUInt16(static_cast<uint16>(m_name.size()));
        if (!m_name.isEmpty())
            file.write(m_name.constData(), m_name.size());
    }

    // Write value
    switch (m_type) {
    case TAGTYPE_STRING:
        file.writeString(std::get<QString>(m_value), encode);
        break;
    case TAGTYPE_UINT32:
        file.writeUInt32(static_cast<uint32>(std::get<uint64>(m_value)));
        break;
    case TAGTYPE_UINT64:
        file.writeUInt64(std::get<uint64>(m_value));
        break;
    case TAGTYPE_FLOAT32: {
        float f = std::get<float>(m_value);
        file.write(&f, sizeof(f));
        break;
    }
    case TAGTYPE_HASH:
        file.write(std::get<HashArray>(m_value).data(), 16);
        break;
    case TAGTYPE_BLOB: {
        const auto& blob = std::get<QByteArray>(m_value);
        file.writeUInt32(static_cast<uint32>(blob.size()));
        if (!blob.isEmpty())
            file.write(blob.constData(), blob.size());
        break;
    }
    default:
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// writeNewEd2kTag — optimized serialization for new ED2K protocol
// ---------------------------------------------------------------------------

bool Tag::writeNewEd2kTag(FileDataIO& data, UTF8Mode encode) const
{
    // Determine the optimized type and encode value to a temp buffer
    // so we can write the correct type byte first.

    // Name encoding: numeric ID uses type|0x80 + 1-byte ID
    // String name uses type + uint16 length + string bytes

    auto writeNameAndType = [&](uint8 wireType) {
        if (m_nameId != 0 && m_name.isEmpty()) {
            data.writeUInt8(wireType | 0x80);
            data.writeUInt8(m_nameId);
        } else {
            data.writeUInt8(wireType);
            data.writeUInt16(static_cast<uint16>(m_name.size()));
            if (!m_name.isEmpty())
                data.write(m_name.constData(), m_name.size());
        }
    };

    switch (m_type) {
    case TAGTYPE_STRING: {
        const auto& str = std::get<QString>(m_value);
        QByteArray raw;
        switch (encode) {
        case UTF8Mode::Raw:
            raw = str.toUtf8();
            break;
        case UTF8Mode::OptBOM: {
            bool hasNonAscii = false;
            for (auto ch : str) {
                if (ch.unicode() > 0x7F) {
                    hasNonAscii = true;
                    break;
                }
            }
            if (hasNonAscii) {
                raw.reserve(3 + str.size() * 4);
                raw.append('\xEF');
                raw.append('\xBB');
                raw.append('\xBF');
                raw.append(str.toUtf8());
            } else {
                raw = str.toLatin1();
            }
            break;
        }
        case UTF8Mode::None:
        default:
            raw = str.toLatin1();
            break;
        }

        // Use compact string types STR1–STR16 if the string is short enough
        const auto rawLen = static_cast<uint32>(raw.size());
        if (rawLen >= 1 && rawLen <= 16) {
            writeNameAndType(static_cast<uint8>(TAGTYPE_STR1 + rawLen - 1));
            data.write(raw.constData(), rawLen);
        } else {
            writeNameAndType(TAGTYPE_STRING);
            data.writeUInt16(static_cast<uint16>(rawLen));
            if (rawLen > 0)
                data.write(raw.constData(), rawLen);
        }
        break;
    }
    case TAGTYPE_UINT32: {
        const auto val = static_cast<uint32>(std::get<uint64>(m_value));
        if (val <= 0xFF) {
            writeNameAndType(TAGTYPE_UINT8);
            data.writeUInt8(static_cast<uint8>(val));
        } else if (val <= 0xFFFF) {
            writeNameAndType(TAGTYPE_UINT16);
            data.writeUInt16(static_cast<uint16>(val));
        } else {
            writeNameAndType(TAGTYPE_UINT32);
            data.writeUInt32(val);
        }
        break;
    }
    case TAGTYPE_UINT64: {
        const auto val = std::get<uint64>(m_value);
        if (val <= 0xFF) {
            writeNameAndType(TAGTYPE_UINT8);
            data.writeUInt8(static_cast<uint8>(val));
        } else if (val <= 0xFFFF) {
            writeNameAndType(TAGTYPE_UINT16);
            data.writeUInt16(static_cast<uint16>(val));
        } else if (val <= 0xFFFFFFFF) {
            writeNameAndType(TAGTYPE_UINT32);
            data.writeUInt32(static_cast<uint32>(val));
        } else {
            writeNameAndType(TAGTYPE_UINT64);
            data.writeUInt64(val);
        }
        break;
    }
    case TAGTYPE_FLOAT32: {
        writeNameAndType(TAGTYPE_FLOAT32);
        float f = std::get<float>(m_value);
        data.write(&f, sizeof(f));
        break;
    }
    case TAGTYPE_HASH: {
        writeNameAndType(TAGTYPE_HASH);
        data.write(std::get<HashArray>(m_value).data(), 16);
        break;
    }
    case TAGTYPE_BLOB: {
        writeNameAndType(TAGTYPE_BLOB);
        const auto& blob = std::get<QByteArray>(m_value);
        data.writeUInt32(static_cast<uint32>(blob.size()));
        if (!blob.isEmpty())
            data.write(blob.constData(), blob.size());
        break;
    }
    default:
        return false;
    }

    return true;
}

} // namespace eMule
