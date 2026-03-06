#include "pch.h"
/// @file KadIO.cpp
/// @brief Kad-specific I/O implementation.

#include "kademlia/KadIO.h"
#include "kademlia/KadLog.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#include <cstring>

namespace eMule::kad::io {

// ---------------------------------------------------------------------------
// UInt128
// ---------------------------------------------------------------------------

UInt128 readUInt128(FileDataIO& f)
{
    // MFC format: 4 × uint32 in host byte order (little-endian on x86)
    UInt128 val;
    f.read(val.getDataPtr(), 16);
    return val;
}

void writeUInt128(FileDataIO& f, const UInt128& val)
{
    f.write(val.getData(), 16);
}

// ---------------------------------------------------------------------------
// BSOB (binary small object)
// ---------------------------------------------------------------------------

QByteArray readBsob(FileDataIO& f)
{
    uint8 len = f.readUInt8();
    QByteArray data(len, Qt::Uninitialized);
    if (len > 0)
        f.read(data.data(), len);
    return data;
}

void writeBsob(FileDataIO& f, const QByteArray& data)
{
    auto len = static_cast<uint8>(data.size());
    f.writeUInt8(len);
    if (len > 0)
        f.write(data.constData(), len);
}

// ---------------------------------------------------------------------------
// Float
// ---------------------------------------------------------------------------

float readFloat(FileDataIO& f)
{
    float val = 0.0f;
    f.read(&val, sizeof(val));
    return val;
}

void writeFloat(FileDataIO& f, float val)
{
    f.write(&val, sizeof(val));
}

// ---------------------------------------------------------------------------
// UTF-8 strings
// ---------------------------------------------------------------------------

QString readStringUTF8(FileDataIO& f, bool /*optACP*/)
{
    uint16 len = f.readUInt16();
    if (len == 0)
        return {};
    QByteArray raw(len, Qt::Uninitialized);
    f.read(raw.data(), len);
    return QString::fromUtf8(raw);
}

void writeStringUTF8(FileDataIO& f, const QString& str)
{
    QByteArray utf8 = str.toUtf8();
    f.writeUInt16(static_cast<uint16>(utf8.size()));
    if (!utf8.isEmpty())
        f.write(utf8.constData(), utf8.size());
}

// ---------------------------------------------------------------------------
// Kad tag read
// ---------------------------------------------------------------------------

Tag readKadTag(FileDataIO& f, bool optACP)
{
    // Kad tag format:
    //   1 byte  — type
    //   2 bytes — name length
    //   N bytes — name (UTF-8 string, no 0x80 shortcut like ED2K)
    //   value   — depends on type

    uint8 type = f.readUInt8();
    uint16 nameLen = f.readUInt16();
    QByteArray name(nameLen, Qt::Uninitialized);
    if (nameLen > 0)
        f.read(name.data(), nameLen);

    // When the name is a single byte, it represents a numeric tag ID
    // (e.g. FT_FILENAME = 0x01).  Use the nameId-based Tag constructors
    // so that tag.nameId() returns the correct value for switch dispatch.
    const bool hasNumericId = (nameLen == 1);
    const uint8 numericId = hasNumericId ? static_cast<uint8>(name[0]) : 0;

    switch (type) {
    case TAGTYPE_STRING: {
        QString val = readStringUTF8(f, optACP);
        if (hasNumericId) return Tag(numericId, val);
        return Tag(std::move(name), val);
    }
    case TAGTYPE_UINT64: {
        uint64 val = f.readUInt64();
        if (hasNumericId) return Tag(numericId, val);
        return Tag(std::move(name), val);
    }
    case TAGTYPE_UINT32: {
        uint32 val = f.readUInt32();
        if (hasNumericId) return Tag(numericId, val);
        return Tag(std::move(name), val);
    }
    case TAGTYPE_UINT16: {
        // Normalize to uint32 on read
        uint32 val = f.readUInt16();
        if (hasNumericId) return Tag(numericId, val);
        return Tag(std::move(name), val);
    }
    case TAGTYPE_UINT8: {
        // Normalize to uint32 on read
        uint32 val = f.readUInt8();
        if (hasNumericId) return Tag(numericId, val);
        return Tag(std::move(name), val);
    }
    case TAGTYPE_FLOAT32: {
        float val = readFloat(f);
        // Store as uint32 via reinterpret (same as MFC)
        uint32 intVal = 0;
        std::memcpy(&intVal, &val, sizeof(float));
        if (hasNumericId) return Tag(numericId, intVal);
        return Tag(std::move(name), intVal);
    }
    case TAGTYPE_HASH: {
        uint8 hash[16];
        f.read(hash, 16);
        if (hasNumericId) return Tag(numericId, hash);
        return Tag(std::move(name), hash);
    }
    case TAGTYPE_BSOB: {
        QByteArray bsob = readBsob(f);
        if (hasNumericId) return Tag(numericId, std::move(bsob));
        return Tag(std::move(name), std::move(bsob));
    }
    case TAGTYPE_BLOB: {
        uint32 blobLen = f.readUInt32();
        QByteArray blob(static_cast<qsizetype>(blobLen), Qt::Uninitialized);
        if (blobLen > 0)
            f.read(blob.data(), blobLen);
        if (hasNumericId) return Tag(numericId, std::move(blob));
        return Tag(std::move(name), std::move(blob));
    }
    default:
        // Handle STR1–STR22 compact string types
        if (type >= TAGTYPE_STR1 && type <= TAGTYPE_STR22) {
            uint32 strLen = type - TAGTYPE_STR1 + 1;
            QByteArray raw(static_cast<qsizetype>(strLen), Qt::Uninitialized);
            f.read(raw.data(), strLen);
            QString val = QString::fromUtf8(raw);
            if (hasNumericId) return Tag(numericId, val);
            return Tag(std::move(name), val);
        }
        logKad(QStringLiteral("Kad: Unknown tag type 0x%1").arg(type, 2, 16, QChar(u'0')));
        if (hasNumericId) return Tag(numericId, uint32{0});
        return Tag(std::move(name), uint32{0});
    }
}

// ---------------------------------------------------------------------------
// Kad tag write
// ---------------------------------------------------------------------------

void writeKadTag(FileDataIO& f, const Tag& tag)
{
    // Kad format: type byte, uint16 name length, name bytes, value
    // TAGTYPE_UINT auto-sizes to smallest uint type

    const QByteArray& tagName = tag.name();

    if (tag.isStr()) {
        f.writeUInt8(TAGTYPE_STRING);
        f.writeUInt16(static_cast<uint16>(tagName.size()));
        if (!tagName.isEmpty())
            f.write(tagName.constData(), tagName.size());
        writeStringUTF8(f, tag.strValue());
    } else if (tag.isInt64(true)) {
        uint64 val = tag.int64Value();
        // Auto-size: pick smallest representation
        if (val <= 0xFF) {
            f.writeUInt8(TAGTYPE_UINT8);
            f.writeUInt16(static_cast<uint16>(tagName.size()));
            if (!tagName.isEmpty())
                f.write(tagName.constData(), tagName.size());
            f.writeUInt8(static_cast<uint8>(val));
        } else if (val <= 0xFFFF) {
            f.writeUInt8(TAGTYPE_UINT16);
            f.writeUInt16(static_cast<uint16>(tagName.size()));
            if (!tagName.isEmpty())
                f.write(tagName.constData(), tagName.size());
            f.writeUInt16(static_cast<uint16>(val));
        } else if (val <= 0xFFFFFFFF) {
            f.writeUInt8(TAGTYPE_UINT32);
            f.writeUInt16(static_cast<uint16>(tagName.size()));
            if (!tagName.isEmpty())
                f.write(tagName.constData(), tagName.size());
            f.writeUInt32(static_cast<uint32>(val));
        } else {
            f.writeUInt8(TAGTYPE_UINT64);
            f.writeUInt16(static_cast<uint16>(tagName.size()));
            if (!tagName.isEmpty())
                f.write(tagName.constData(), tagName.size());
            f.writeUInt64(val);
        }
    } else if (tag.isFloat()) {
        f.writeUInt8(TAGTYPE_FLOAT32);
        f.writeUInt16(static_cast<uint16>(tagName.size()));
        if (!tagName.isEmpty())
            f.write(tagName.constData(), tagName.size());
        writeFloat(f, tag.floatValue());
    } else if (tag.isHash()) {
        f.writeUInt8(TAGTYPE_HASH);
        f.writeUInt16(static_cast<uint16>(tagName.size()));
        if (!tagName.isEmpty())
            f.write(tagName.constData(), tagName.size());
        f.write(tag.hashValue(), 16);
    } else if (tag.isBlob()) {
        f.writeUInt8(TAGTYPE_BLOB);
        f.writeUInt16(static_cast<uint16>(tagName.size()));
        if (!tagName.isEmpty())
            f.write(tagName.constData(), tagName.size());
        const QByteArray& blob = tag.blobValue();
        f.writeUInt32(static_cast<uint32>(blob.size()));
        if (!blob.isEmpty())
            f.write(blob.constData(), blob.size());
    } else {
        // Fallback: write as uint32 0
        f.writeUInt8(TAGTYPE_UINT32);
        f.writeUInt16(static_cast<uint16>(tagName.size()));
        if (!tagName.isEmpty())
            f.write(tagName.constData(), tagName.size());
        f.writeUInt32(0);
    }
}

// ---------------------------------------------------------------------------
// Kad tag list
// ---------------------------------------------------------------------------

std::vector<Tag> readKadTagList(FileDataIO& f, bool optACP)
{
    // MFC ReadTagList uses ReadByte() — single uint8 for tag count
    uint8 count = f.readUInt8();
    std::vector<Tag> tags;
    tags.reserve(count);
    for (uint8 i = 0; i < count; ++i)
        tags.push_back(readKadTag(f, optACP));
    return tags;
}

void writeKadTagList(FileDataIO& f, const std::vector<Tag>& tags)
{
    // MFC WriteTagList uses WriteByte() — single uint8 for tag count
    f.writeUInt8(static_cast<uint8>(tags.size()));
    for (const auto& tag : tags)
        writeKadTag(f, tag);
}

} // namespace eMule::kad::io
