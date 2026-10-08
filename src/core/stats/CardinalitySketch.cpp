#include "stats/CardinalitySketch.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>

namespace eMule {

namespace {

constexpr int kRankBits = 64 - CardinalitySketch::kPrecision;
constexpr uint8 kMaxRank = kRankBits + 1;

// splitmix64 finaliser: a bijection with full avalanche.
constexpr uint64 mix(uint64 x) noexcept
{
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x;
}

// Ertl, "New cardinality estimation algorithms for HyperLogLog sketches" (2017):
// corrections for the registers still at zero and those already saturated.
// Unlike the original estimator they need no bias tables or range switches.
double sigma(double x)
{
    if (x == 1.0)
        return std::numeric_limits<double>::infinity();
    double y = 1.0;
    double z = x;
    for (;;) {
        x *= x;
        const double prev = z;
        z += x * y;
        y += y;
        if (z == prev)
            return z;
    }
}

double tau(double x)
{
    if (x == 0.0 || x == 1.0)
        return 0.0;
    double y = 1.0;
    double z = 1.0 - x;
    for (;;) {
        x = std::sqrt(x);
        const double prev = z;
        y *= 0.5;
        z -= (1.0 - x) * (1.0 - x) * y;
        if (z == prev)
            return z / 3.0;
    }
}

} // namespace

uint64 CardinalitySketch::keyedHash(uint64 lo, uint64 hi, uint64 k0, uint64 k1) noexcept
{
    return mix(mix(lo ^ k0) + (hi ^ k1));
}

void CardinalitySketch::add(uint64 hash)
{
    if (!m_dense.empty()) {
        addDense(hash);
        return;
    }
    const auto it = std::lower_bound(m_sparse.begin(), m_sparse.end(), hash);
    if (it != m_sparse.end() && *it == hash)
        return;
    m_sparse.insert(it, hash);
    if (m_sparse.size() > kSparseLimit)
        densify();
}

void CardinalitySketch::merge(const CardinalitySketch& other)
{
    if (other.m_dense.empty()) {
        for (const uint64 hash : other.m_sparse)
            add(hash);
        return;
    }
    if (m_dense.empty())
        densify();
    for (std::size_t i = 0; i < kRegisters; ++i)
        m_dense[i] = std::max(m_dense[i], other.m_dense[i]);
}

void CardinalitySketch::clear()
{
    m_sparse.clear();
    m_dense.clear();
}

uint64 CardinalitySketch::estimate() const
{
    if (m_dense.empty())
        return m_sparse.size();

    std::array<uint32, kMaxRank + 1> histogram{};
    for (const uint8 rank : m_dense)
        ++histogram[rank];

    const double m = static_cast<double>(kRegisters);
    double z = m * tau(1.0 - histogram[kMaxRank] / m);
    for (int k = kRankBits; k >= 1; --k)
        z = 0.5 * (z + histogram[static_cast<std::size_t>(k)]);
    z += m * sigma(histogram[0] / m);

    constexpr double kAlphaInf = 0.72134752044448170368;  // 1 / (2 ln 2)
    return static_cast<uint64>(std::llround(kAlphaInf * m * m / z));
}

void CardinalitySketch::write(QDataStream& out) const
{
    if (m_dense.empty()) {
        out << quint8{0} << static_cast<quint16>(m_sparse.size());
        for (const uint64 hash : m_sparse)
            out << static_cast<quint64>(hash);
    } else {
        out << quint8{1};
        out.writeRawData(reinterpret_cast<const char*>(m_dense.data()),
                         static_cast<qint64>(m_dense.size()));
    }
}

bool CardinalitySketch::read(QDataStream& in)
{
    clear();
    quint8 mode = 0;
    in >> mode;
    if (mode == 0) {
        quint16 count = 0;
        in >> count;
        if (in.status() != QDataStream::Ok || count > kSparseLimit)
            return false;
        m_sparse.reserve(count);
        for (quint16 i = 0; i < count; ++i) {
            quint64 hash = 0;
            in >> hash;
            m_sparse.push_back(hash);
        }
        if (in.status() != QDataStream::Ok
            || std::adjacent_find(m_sparse.begin(), m_sparse.end(), std::greater_equal<>())
                   != m_sparse.end()) {
            clear();
            return false;
        }
        return true;
    }
    if (mode != 1)
        return false;

    m_dense.resize(kRegisters);
    const bool ok = in.readRawData(reinterpret_cast<char*>(m_dense.data()),
                                   static_cast<qint64>(kRegisters))
                        == static_cast<qint64>(kRegisters)
                    && std::ranges::all_of(m_dense, [](uint8 r) { return r <= kMaxRank; });
    if (!ok)
        clear();
    return ok;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void CardinalitySketch::addDense(uint64 hash)
{
    const std::size_t index = hash >> kRankBits;
    // Rank of the first set bit in the remaining bits; all zero saturates.
    const uint64 rest = hash << kPrecision;
    const uint8 rank = rest == 0 ? kMaxRank : static_cast<uint8>(std::countl_zero(rest) + 1);
    m_dense[index] = std::max(m_dense[index], rank);
}

void CardinalitySketch::densify()
{
    m_dense.assign(kRegisters, 0);
    for (const uint64 hash : m_sparse)
        addDense(hash);
    m_sparse.clear();
    m_sparse.shrink_to_fit();
}

} // namespace eMule
