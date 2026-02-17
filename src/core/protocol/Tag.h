#pragma once

/// @file Tag.h
/// @brief ED2K tag system — modern C++23 replacement for MFC CTag.
///
/// Replaces the CTag class from the original Packets.h. Uses std::variant
/// instead of raw union + manual memory management for type-safe storage.

#include "utils/SafeFile.h"
#include "utils/Types.h"
#include "utils/Opcodes.h"

#include <QByteArray>
#include <QString>

#include <array>
#include <variant>

namespace eMule {

class Tag {
public:
    // Construction from numeric name ID + value
    Tag(uint8 nameId, uint32 value);
    Tag(uint8 nameId, uint64 value);
    Tag(uint8 nameId, const QString& value);
    Tag(uint8 nameId, float value);
    Tag(uint8 nameId, const uint8* hash16);
    Tag(uint8 nameId, QByteArray blobData);

    // Construction from string name + value
    Tag(QByteArray name, uint32 value);
    Tag(QByteArray name, uint64 value);
    Tag(QByteArray name, const QString& value);
    Tag(QByteArray name, QByteArray blobData);
    Tag(QByteArray name, const uint8* hash16);

    // Deserialization constructor (reads from stream)
    Tag(FileDataIO& data, bool optUTF8);

    // Copy/move
    Tag(const Tag& other) = default;
    Tag& operator=(const Tag& other) = default;
    Tag(Tag&&) noexcept = default;
    Tag& operator=(Tag&&) noexcept = default;
    ~Tag() = default;

    // Type queries
    [[nodiscard]] uint8 type() const;
    [[nodiscard]] uint8 nameId() const;
    [[nodiscard]] const QByteArray& name() const;
    [[nodiscard]] bool hasName() const;

    [[nodiscard]] bool isStr() const;
    [[nodiscard]] bool isInt() const;
    [[nodiscard]] bool isInt64(bool orInt32 = true) const;
    [[nodiscard]] bool isFloat() const;
    [[nodiscard]] bool isHash() const;
    [[nodiscard]] bool isBlob() const;

    // Value access
    [[nodiscard]] uint32 intValue() const;
    [[nodiscard]] uint64 int64Value() const;
    [[nodiscard]] const QString& strValue() const;
    [[nodiscard]] float floatValue() const;
    [[nodiscard]] const uint8* hashValue() const;
    [[nodiscard]] const QByteArray& blobValue() const;

    // Mutators
    void setInt(uint32 val);
    void setInt64(uint64 val);
    void setStr(const QString& val);

    // Serialization
    bool writeTagToFile(FileDataIO& file, UTF8Mode encode = UTF8Mode::None) const;
    bool writeNewEd2kTag(FileDataIO& data, UTF8Mode encode = UTF8Mode::None) const;

private:
    using HashArray = std::array<uint8, 16>;
    using Value = std::variant<std::monostate, uint64, QString, float, HashArray, QByteArray>;

    Value m_value;
    QByteArray m_name;
    uint8 m_type = TAGTYPE_NONE;
    uint8 m_nameId = 0;
};

} // namespace eMule
