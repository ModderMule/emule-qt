#pragma once

/// @file CardinalitySketch.h
/// @brief Distinct-count estimator: HyperLogLog with an exact small-set mode.
///
/// Counts how many different 64-bit hashes it was shown, in 4 KB however many
/// that is, to about 1.6 %. Up to kSparseLimit it keeps the hashes themselves
/// and is exact.
///
/// A merge is a set union and so idempotent: merging the same sketch twice
/// changes nothing. That is what lets a cumulative count be "banked + session"
/// and flushed absolutely, like every other cumulative statistic.
///
/// Feed it keyedHash() of an identifier, not the identifier: an ID a remote
/// peer chose can be crafted to inflate the estimate, a salted hash of it
/// cannot. Sketches only merge when they were fed with the same key.

#include "utils/Types.h"

#include <QDataStream>

#include <vector>

namespace eMule {

class CardinalitySketch {
public:
    static constexpr int kPrecision = 12;
    static constexpr std::size_t kRegisters = std::size_t{1} << kPrecision;
    static constexpr std::size_t kSparseLimit = 256;

    void add(uint64 hash);
    void merge(const CardinalitySketch& other);
    void clear();

    [[nodiscard]] uint64 estimate() const;
    [[nodiscard]] bool isEmpty() const { return m_sparse.empty() && m_dense.empty(); }
    [[nodiscard]] bool isExact() const { return m_dense.empty(); }

    void write(QDataStream& out) const;
    /// False on a malformed sketch; the object is then empty.
    [[nodiscard]] bool read(QDataStream& in);

    bool operator==(const CardinalitySketch&) const = default;

    /// A 128-bit identifier (two halves) under a 128-bit secret key. Fixed
    /// arithmetic, so stored sketches stay valid across builds and platforms.
    [[nodiscard]] static uint64 keyedHash(uint64 lo, uint64 hi, uint64 k0, uint64 k1) noexcept;

private:
    void addDense(uint64 hash);
    void densify();

    std::vector<uint64> m_sparse;  ///< sorted, unique; unused once dense
    std::vector<uint8> m_dense;    ///< kRegisters ranks, or empty while sparse
};

} // namespace eMule
