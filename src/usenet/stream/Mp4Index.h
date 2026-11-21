#pragma once

/// @file Mp4Index.h
/// @brief Just enough ISO BMFF (mp4/m4v/mov) reading to find a video's
///        keyframes in the file: where moov is, and each sync sample's time,
///        offset and size. Lets a preview fetch a few articles spread over the
///        whole runtime instead of the head only. Pure, for tests.

#include <QByteArray>
#include <QList>

#include <functional>

namespace eMule::usenet {

struct Mp4Keyframe {
    qint64 timeMs = 0;   ///< decode time
    qint64 offset = 0;   ///< in the file
    qint64 size = 0;
};

/// Where the top-level moov box sits.
struct Mp4MoovSpot {
    enum class State { Found, NeedBytes, Unusable };
    State state = State::Unusable;
    qint64 offset = -1;   ///< Found: moov's offset; NeedBytes: read 16 bytes here first
    qint64 size = 0;
};

/// Walk the top-level boxes. @p read(offset, length) gives the bytes at
/// @p offset, or empty when they did not arrive. Fragmented files (moof) and
/// anything not ftyp-led are Unusable.
[[nodiscard]] Mp4MoovSpot findMp4Moov(const std::function<QByteArray(qint64, qint64)>& read,
                                      qint64 fileSize);

/// Sync samples of the first video track in @p moov (the whole box, header
/// included), in time order. Empty when it cannot be read.
[[nodiscard]] QList<Mp4Keyframe> mp4VideoKeyframes(const QByteArray& moov);

/// @p count keyframes spread evenly over the runtime, clear of the very start
/// and end; fewer when there are not that many.
[[nodiscard]] QList<Mp4Keyframe> spreadKeyframes(const QList<Mp4Keyframe>& all, int count);

} // namespace eMule::usenet
