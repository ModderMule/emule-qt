#pragma once

/// @file KadUInt128.h
/// @brief 128-bit unsigned integer for Kademlia XOR distance metric.
///
/// Ported from kademlia/utils/UInt128.h — pure math, no external dependencies.

#include "utils/Types.h"

#include <QString>

#include <cstdint>
#include <cstring>

namespace eMule::kad {

/// 128-bit unsigned integer used as Kademlia node/file identifier and XOR distance.
class UInt128 {
public:
    UInt128() noexcept;
    explicit UInt128(bool fill) noexcept;
    explicit UInt128(uint32 value) noexcept;
    explicit UInt128(const uint8* valueBE) noexcept;
    UInt128(const UInt128& other) noexcept = default;

    /// Copy the most significant @p numBits from @p value, randomise the rest.
    UInt128(const UInt128& value, uint32 numBits);

    // -- Setters -------------------------------------------------------------
    UInt128& setValue(const UInt128& value) noexcept;
    UInt128& setValue(uint32 value) noexcept;
    UInt128& setValueBE(const uint8* valueBE) noexcept;
    UInt128& setValueRandom();
    UInt128& setValueGUID();

    // -- Bit manipulation ----------------------------------------------------
    [[nodiscard]] uint32 getBitNumber(uint32 bit) const noexcept;
    UInt128& setBitNumber(uint32 bit, uint32 value) noexcept;

    // -- Arithmetic / logic --------------------------------------------------
    UInt128& xorWith(const UInt128& value) noexcept;
    UInt128& xorBE(const uint8* valueBE) noexcept;
    UInt128& add(const UInt128& value) noexcept;
    UInt128& add(uint32 value) noexcept;
    UInt128& subtract(const UInt128& value) noexcept;
    UInt128& subtract(uint32 value) noexcept;
    UInt128& shiftLeft(uint32 bits) noexcept;

    // -- Comparison ----------------------------------------------------------
    [[nodiscard]] int compareTo(const UInt128& other) const noexcept;
    [[nodiscard]] int compareTo(uint32 value) const noexcept;

    // -- Conversion ----------------------------------------------------------
    [[nodiscard]] QString toHexString() const;
    [[nodiscard]] QString toBinaryString(bool trim = false) const;
    void toByteArray(uint8* out) const noexcept;

    // -- Accessors -----------------------------------------------------------
    [[nodiscard]] const uint8* getData() const noexcept;
    [[nodiscard]] uint8* getDataPtr() noexcept;
    [[nodiscard]] uint32 get32BitChunk(int index) const noexcept;

    // -- Operators (UInt128) -------------------------------------------------
    UInt128& operator=(const UInt128& v) noexcept { return setValue(v); }
    UInt128& operator+=(const UInt128& v) noexcept { return add(v); }
    UInt128& operator-=(const UInt128& v) noexcept { return subtract(v); }

    friend bool operator==(const UInt128& a, const UInt128& b) noexcept { return a.compareTo(b) == 0; }
    friend bool operator!=(const UInt128& a, const UInt128& b) noexcept { return a.compareTo(b) != 0; }
    friend bool operator< (const UInt128& a, const UInt128& b) noexcept { return a.compareTo(b) <  0; }
    friend bool operator> (const UInt128& a, const UInt128& b) noexcept { return a.compareTo(b) >  0; }
    friend bool operator<=(const UInt128& a, const UInt128& b) noexcept { return a.compareTo(b) <= 0; }
    friend bool operator>=(const UInt128& a, const UInt128& b) noexcept { return a.compareTo(b) >= 0; }

    // -- Operators (uint32) --------------------------------------------------
    UInt128& operator=(uint32 v) noexcept { return setValue(v); }
    UInt128& operator+=(uint32 v) noexcept { return add(v); }
    UInt128& operator-=(uint32 v) noexcept { return subtract(v); }

    friend bool operator==(const UInt128& a, uint32 b) noexcept { return a.compareTo(b) == 0; }
    friend bool operator!=(const UInt128& a, uint32 b) noexcept { return a.compareTo(b) != 0; }
    friend bool operator< (const UInt128& a, uint32 b) noexcept { return a.compareTo(b) <  0; }
    friend bool operator> (const UInt128& a, uint32 b) noexcept { return a.compareTo(b) >  0; }
    friend bool operator<=(const UInt128& a, uint32 b) noexcept { return a.compareTo(b) <= 0; }
    friend bool operator>=(const UInt128& a, uint32 b) noexcept { return a.compareTo(b) >= 0; }

private:
    union {
        uint32 m_data[4];
        uint64 m_data64[2];
    };
};

} // namespace eMule::kad
