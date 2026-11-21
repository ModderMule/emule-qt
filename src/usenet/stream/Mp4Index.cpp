#include "pch.h"
/// @file Mp4Index.cpp
/// @brief Top-level box walk and video sync-sample table of an mp4.

#include "stream/Mp4Index.h"

#include <QtEndian>

#include <algorithm>
#include <optional>
#include <vector>

namespace eMule::usenet {

namespace {

constexpr qint64 kMaxSamples = 4'000'000;   // a malformed count never allocates the world

struct Box {
    QByteArray type;
    qint64 payload = 0;   ///< first byte after the header
    qint64 end = 0;
};

/// Bounds-checked big-endian reads; any miss sets ok = false and reads 0.
struct Reader {
    const QByteArray& d;
    bool ok = true;

    quint32 u32(qint64 pos)
    {
        if (pos < 0 || pos + 4 > d.size()) {
            ok = false;
            return 0;
        }
        return qFromBigEndian<quint32>(d.constData() + pos);
    }
    quint64 u64(qint64 pos)
    {
        if (pos < 0 || pos + 8 > d.size()) {
            ok = false;
            return 0;
        }
        return qFromBigEndian<quint64>(d.constData() + pos);
    }
};

QList<Box> children(const QByteArray& d, qint64 begin, qint64 end)
{
    QList<Box> out;
    Reader r{d};
    qint64 pos = begin;
    end = std::min<qint64>(end, d.size());
    while (pos + 8 <= end) {
        quint64 size = r.u32(pos);
        qint64 header = 8;
        if (size == 1) {
            size = r.u64(pos + 8);
            header = 16;
        } else if (size == 0) {
            size = quint64(end - pos);
        }
        if (!r.ok || size < quint64(header) || pos + qint64(size) > end)
            break;
        out.append({d.mid(pos + 4, 4), pos + header, pos + qint64(size)});
        pos += qint64(size);
    }
    return out;
}

std::optional<Box> child(const QByteArray& d, const Box& parent, const char* type)
{
    for (const Box& b : children(d, parent.payload, parent.end))
        if (b.type == type)
            return b;
    return std::nullopt;
}

/// A video track's tables, as read.
struct Track {
    quint32 timescale = 0;
    std::vector<quint32> sizes;      ///< per sample
    std::vector<qint64> chunkOffsets;
    std::vector<std::pair<quint32, quint32>> stsc;   ///< first chunk (1-based), samples per chunk
    std::vector<std::pair<quint32, quint32>> stts;   ///< count, delta
    std::vector<quint32> sync;       ///< 1-based sample numbers; empty = all
    bool hasSyncTable = false;
};

bool readTrack(const QByteArray& d, const Box& trak, Track& t)
{
    const auto mdia = child(d, trak, "mdia");
    if (!mdia)
        return false;
    const auto hdlr = child(d, *mdia, "hdlr");
    if (!hdlr || d.mid(hdlr->payload + 8, 4) != "vide")
        return false;
    Reader r{d};
    if (const auto mdhd = child(d, *mdia, "mdhd")) {
        const bool v1 = d.size() > mdhd->payload && quint8(d.at(mdhd->payload)) == 1;
        t.timescale = r.u32(mdhd->payload + (v1 ? 20 : 12));
    }
    const auto minf = child(d, *mdia, "minf");
    const auto stbl = minf ? child(d, *minf, "stbl") : std::nullopt;
    if (!stbl || t.timescale == 0)
        return false;

    for (const Box& b : children(d, stbl->payload, stbl->end)) {
        const qint64 p = b.payload + 4;   // after version/flags
        if (b.type == "stsz") {
            const quint32 uniform = r.u32(p);
            const quint32 count = r.u32(p + 4);
            if (count > kMaxSamples)
                return false;
            t.sizes.assign(count, uniform);
            if (uniform == 0)
                for (quint32 i = 0; i < count && r.ok; ++i)
                    t.sizes[i] = r.u32(p + 8 + 4 * qint64(i));
        } else if (b.type == "stco" || b.type == "co64") {
            const quint32 count = r.u32(p);
            if (count > kMaxSamples)
                return false;
            const bool wide = b.type == "co64";
            t.chunkOffsets.reserve(count);
            for (quint32 i = 0; i < count && r.ok; ++i)
                t.chunkOffsets.push_back(wide ? qint64(r.u64(p + 4 + 8 * qint64(i)))
                                              : qint64(r.u32(p + 4 + 4 * qint64(i))));
        } else if (b.type == "stsc") {
            const quint32 count = r.u32(p);
            if (count > kMaxSamples)
                return false;
            for (quint32 i = 0; i < count && r.ok; ++i)
                t.stsc.emplace_back(r.u32(p + 4 + 12 * qint64(i)), r.u32(p + 8 + 12 * qint64(i)));
        } else if (b.type == "stts") {
            const quint32 count = r.u32(p);
            if (count > kMaxSamples)
                return false;
            for (quint32 i = 0; i < count && r.ok; ++i)
                t.stts.emplace_back(r.u32(p + 4 + 8 * qint64(i)), r.u32(p + 8 + 8 * qint64(i)));
        } else if (b.type == "stss") {
            t.hasSyncTable = true;
            const quint32 count = r.u32(p);
            if (count > kMaxSamples)
                return false;
            for (quint32 i = 0; i < count && r.ok; ++i)
                t.sync.push_back(r.u32(p + 4 + 4 * qint64(i)));
        }
    }
    return r.ok && !t.sizes.empty() && !t.chunkOffsets.empty() && !t.stsc.empty() && !t.stts.empty();
}

QList<Mp4Keyframe> keyframesOf(const Track& t)
{
    QList<Mp4Keyframe> out;
    std::vector<quint32> sync = t.sync;
    std::ranges::sort(sync);
    size_t nextSync = 0;

    // decode times by walking stts alongside
    size_t sttsEntry = 0;
    quint32 sttsLeft = t.stts.empty() ? 0 : t.stts.front().first;
    qint64 dts = 0;

    quint32 sample = 0;   // 0-based
    size_t stscEntry = 0;
    for (size_t chunk = 0; chunk < t.chunkOffsets.size() && sample < t.sizes.size(); ++chunk) {
        const quint32 chunkNo = quint32(chunk + 1);
        while (stscEntry + 1 < t.stsc.size() && t.stsc[stscEntry + 1].first <= chunkNo)
            ++stscEntry;
        const quint32 perChunk = t.stsc[stscEntry].second;
        qint64 offset = t.chunkOffsets[chunk];
        for (quint32 i = 0; i < perChunk && sample < t.sizes.size(); ++i, ++sample) {
            const quint32 number = sample + 1;
            while (nextSync < sync.size() && sync[nextSync] < number)
                ++nextSync;
            const bool isSync = !t.hasSyncTable || (nextSync < sync.size() && sync[nextSync] == number);
            if (isSync)
                out.append({dts * 1000 / t.timescale, offset, t.sizes[sample]});
            offset += t.sizes[sample];

            while (sttsLeft == 0 && sttsEntry + 1 < t.stts.size())
                sttsLeft = t.stts[++sttsEntry].first;
            if (sttsLeft > 0) {
                dts += t.stts[sttsEntry].second;
                --sttsLeft;
            }
        }
    }
    std::ranges::sort(out, {}, &Mp4Keyframe::timeMs);
    return out;
}

} // namespace

Mp4MoovSpot findMp4Moov(const std::function<QByteArray(qint64, qint64)>& read, qint64 fileSize)
{
    qint64 pos = 0;
    for (int boxes = 0; boxes < 64 && (fileSize <= 0 || pos + 8 <= fileSize); ++boxes) {
        const QByteArray h = read(pos, 16);
        if (h.size() < 8)
            return {Mp4MoovSpot::State::NeedBytes, pos, 0};
        quint64 size = qFromBigEndian<quint32>(h.constData());
        const QByteArray type = h.mid(4, 4);
        if (pos == 0 && type != "ftyp")
            return {};
        if (size == 1) {
            if (h.size() < 16)
                return {Mp4MoovSpot::State::NeedBytes, pos, 0};
            size = qFromBigEndian<quint64>(h.constData() + 8);
        } else if (size == 0) {
            size = fileSize > pos ? quint64(fileSize - pos) : 0;
        }
        if (type == "moof")
            return {};
        if (type == "moov")
            return {Mp4MoovSpot::State::Found, pos, qint64(size)};
        if (size < 8)
            return {};
        pos += qint64(size);
    }
    return {};
}

QList<Mp4Keyframe> mp4VideoKeyframes(const QByteArray& moov)
{
    if (moov.size() < 8 || moov.mid(4, 4) != "moov")
        return {};
    const QList<Box> top = children(moov, 0, moov.size());
    if (top.isEmpty())
        return {};
    // the video track with the most samples: a cover picture is a one-sample track
    QList<Mp4Keyframe> best;
    size_t bestSamples = 0;
    for (const Box& b : children(moov, top.front().payload, top.front().end)) {
        if (b.type != "trak")
            continue;
        Track t;
        if (!readTrack(moov, b, t) || t.sizes.size() <= bestSamples)
            continue;
        bestSamples = t.sizes.size();
        best = keyframesOf(t);
    }
    return best;
}

QList<Mp4Keyframe> spreadKeyframes(const QList<Mp4Keyframe>& all, int count)
{
    QList<Mp4Keyframe> out;
    if (all.isEmpty() || count <= 0)
        return out;
    const qint64 duration = all.back().timeMs;
    for (int i = 1; i <= count; ++i) {
        const qint64 want = duration * i / (count + 1);
        const auto it = std::ranges::lower_bound(all, want, {}, &Mp4Keyframe::timeMs);
        const Mp4Keyframe& k = it == all.end() ? all.back() : *it;
        if (out.isEmpty() || out.back().offset != k.offset)
            out.append(k);
    }
    return out;
}

} // namespace eMule::usenet
