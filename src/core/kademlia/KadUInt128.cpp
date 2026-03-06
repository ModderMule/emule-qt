#include "pch.h"
/// @file KadUInt128.cpp
/// @brief 128-bit unsigned integer implementation.

#include "kademlia/KadUInt128.h"
#include "utils/OtherFunctions.h"

#include <QUuid>

#include <bit>
#include <cstring>
#include <random>

namespace eMule::kad {

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

UInt128::UInt128() noexcept
{
    setValue(uint32{0});
}

UInt128::UInt128(bool fill) noexcept
{
    if (fill) {
        m_data64[0] = UINT64_MAX;
        m_data64[1] = UINT64_MAX;
    } else {
        setValue(uint32{0});
    }
}

UInt128::UInt128(uint32 value) noexcept
{
    setValue(value);
}

UInt128::UInt128(const uint8* valueBE) noexcept
{
    setValueBE(valueBE);
}

UInt128::UInt128(const UInt128& value, uint32 numBits)
{
    setValue(value);
    auto& rng = randomEngine();
    std::uniform_int_distribution<int> dist(0, 1);
    while (numBits < 128)
        setBitNumber(numBits++, dist(rng));
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

UInt128& UInt128::setValue(const UInt128& value) noexcept
{
    m_data64[0] = value.m_data64[0];
    m_data64[1] = value.m_data64[1];
    return *this;
}

UInt128& UInt128::setValue(uint32 value) noexcept
{
    m_data64[0] = 0;
    m_data[2] = 0;
    m_data[3] = value;
    return *this;
}

UInt128& UInt128::setValueBE(const uint8* valueBE) noexcept
{
    setValue(uint32{0});
    for (int i = 0; i < 16; ++i)
        m_data[i / 4] |= static_cast<uint32>(valueBE[i]) << (8 * (3 - (i % 4)));
    return *this;
}

UInt128& UInt128::setValueRandom()
{
    auto& rng = randomEngine();
    std::uniform_int_distribution<uint32> dist;
    m_data[0] = dist(rng);
    m_data[1] = dist(rng);
    m_data[2] = dist(rng);
    m_data[3] = dist(rng);
    return *this;
}

UInt128& UInt128::setValueGUID()
{
    QUuid guid = QUuid::createUuid();
    const QByteArray bytes = guid.toRfc4122();
    if (bytes.size() == 16)
        setValueBE(reinterpret_cast<const uint8*>(bytes.constData()));
    else
        setValue(uint32{0});
    return *this;
}

// ---------------------------------------------------------------------------
// Bit manipulation
// ---------------------------------------------------------------------------

uint32 UInt128::getBitNumber(uint32 bit) const noexcept
{
    if (bit > 127)
        return 0;
    int longNum = bit / 32;
    int shift = 31 - (bit % 32);
    return (m_data[longNum] >> shift) & 1;
}

UInt128& UInt128::setBitNumber(uint32 bit, uint32 value) noexcept
{
    int longNum = bit / 32;
    int shift = 31 - (bit % 32);
    m_data[longNum] |= (1u << shift);
    if (value == 0)
        m_data[longNum] ^= (1u << shift);
    return *this;
}

// ---------------------------------------------------------------------------
// Arithmetic / logic
// ---------------------------------------------------------------------------

UInt128& UInt128::xorWith(const UInt128& value) noexcept
{
    m_data64[0] ^= value.m_data64[0];
    m_data64[1] ^= value.m_data64[1];
    return *this;
}

UInt128& UInt128::xorBE(const uint8* valueBE) noexcept
{
    return xorWith(UInt128(valueBE));
}

UInt128& UInt128::add(const UInt128& value) noexcept
{
    if (value == uint32{0})
        return *this;
    int64 sum = 0;
    for (int i = 3; i >= 0; --i) {
        sum += m_data[i];
        sum += value.m_data[i];
        m_data[i] = static_cast<uint32>(sum);
        sum >>= 32;
    }
    return *this;
}

UInt128& UInt128::add(uint32 value) noexcept
{
    if (value)
        add(UInt128(value));
    return *this;
}

UInt128& UInt128::subtract(const UInt128& value) noexcept
{
    if (value != uint32{0}) {
        int64 sum = 0;
        for (int i = 3; i >= 0; --i) {
            sum += m_data[i];
            sum -= value.m_data[i];
            m_data[i] = static_cast<uint32>(sum);
            sum >>= 32;
        }
    }
    return *this;
}

UInt128& UInt128::subtract(uint32 value) noexcept
{
    if (value)
        subtract(UInt128(value));
    return *this;
}

UInt128& UInt128::shiftLeft(uint32 bits) noexcept
{
    if (bits == 0 || compareTo(uint32{0}) == 0)
        return *this;
    if (bits > 127) {
        setValue(uint32{0});
        return *this;
    }

    uint32 result[4] = {};
    int indexShift = static_cast<int>(bits) / 32;
    int64 shifted = 0;
    for (int i = 3; i >= indexShift; --i) {
        shifted += static_cast<int64>(m_data[i]) << (bits % 32);
        result[i - indexShift] = static_cast<uint32>(shifted);
        shifted >>= 32;
    }
    std::memcpy(m_data, result, sizeof result);
    return *this;
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

int UInt128::compareTo(const UInt128& other) const noexcept
{
    for (int i = 0; i < 4; ++i) {
        if (m_data[i] < other.m_data[i])
            return -1;
        if (m_data[i] > other.m_data[i])
            return 1;
    }
    return 0;
}

int UInt128::compareTo(uint32 value) const noexcept
{
    if (m_data64[0] > 0 || m_data[2] > 0 || m_data[3] > value)
        return 1;
    return (m_data[3] < value) ? -1 : 0;
}

// ---------------------------------------------------------------------------
// Conversion
// ---------------------------------------------------------------------------

QString UInt128::toHexString() const
{
    QString str;
    str.reserve(32);
    for (int i = 0; i < 4; ++i)
        str += QStringLiteral("%1").arg(m_data[i], 8, 16, QLatin1Char('0')).toUpper();
    return str;
}

QString UInt128::toBinaryString(bool trim) const
{
    QString str;
    str.reserve(128);
    for (int i = 0; i < 128; ++i) {
        uint32 bit = getBitNumber(i);
        if (!trim || bit) {
            str += QChar(u'0' + bit);
            trim = false;
        }
    }
    if (str.isEmpty())
        str += u'0';
    return str;
}

void UInt128::toByteArray(uint8* out) const noexcept
{
    for (int i = 0; i < 4; ++i) {
        uint32 swapped = std::byteswap(m_data[i]);
        std::memcpy(out + i * 4, &swapped, 4);
    }
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

const uint8* UInt128::getData() const noexcept
{
    return reinterpret_cast<const uint8*>(m_data);
}

uint8* UInt128::getDataPtr() noexcept
{
    return reinterpret_cast<uint8*>(m_data);
}

uint32 UInt128::get32BitChunk(int index) const noexcept
{
    return m_data[index];
}

} // namespace eMule::kad
