#include "pch.h"
/// @file BlockRuns.cpp
/// @brief Block bitmaps as run lengths — implementation.

#include "utils/BlockRuns.h"

#include <algorithm>
#include <cstdint>

namespace eMule {

namespace {

void putVarint(QByteArray& out, std::uint64_t v)
{
    while (v >= 0x80) {
        out.append(char(std::uint8_t(v) | 0x80));
        v >>= 7;
    }
    out.append(char(std::uint8_t(v)));
}

} // namespace

QByteArray encodeBlockRuns(const std::vector<bool>& bits)
{
    QByteArray out;
    bool value = true;
    std::size_t i = 0;
    while (i < bits.size()) {
        std::size_t run = 0;
        while (i < bits.size() && bits[i] == value) {
            ++run;
            ++i;
        }
        putVarint(out, run);
        value = !value;
    }
    return out;
}

std::optional<std::vector<bool>> decodeBlockRuns(QByteArrayView runs, std::size_t count)
{
    if (count > kMaxBlockRunBits)
        return std::nullopt;
    std::vector<bool> bits;
    bits.reserve(count);
    bool value = true;
    qsizetype at = 0;
    while (at < runs.size()) {
        std::uint64_t run = 0;
        int shift = 0;
        for (;;) {
            if (at >= runs.size() || shift > 35)
                return std::nullopt;   // cut off, or longer than any count
            const auto b = std::uint8_t(runs[at++]);
            run |= std::uint64_t(b & 0x7f) << shift;
            shift += 7;
            if ((b & 0x80) == 0)
                break;
        }
        if (run > count - bits.size())
            return std::nullopt;
        bits.insert(bits.end(), std::size_t(run), value);
        value = !value;
    }
    if (bits.size() != count)
        return std::nullopt;
    return bits;
}

QList<qint64> blockRanges(const std::vector<bool>& bits, bool value, qint64 blockSize, qint64 fileSize)
{
    QList<qint64> out;
    if (blockSize <= 0 || fileSize <= 0)
        return out;
    std::size_t i = 0;
    while (i < bits.size()) {
        if (bits[i] != value) {
            ++i;
            continue;
        }
        const std::size_t first = i;
        while (i < bits.size() && bits[i] == value)
            ++i;
        const qint64 start = qint64(first) * blockSize;
        if (start >= fileSize)
            break;
        out << start << std::min(fileSize, qint64(i) * blockSize) - 1;
    }
    return out;
}

} // namespace eMule
