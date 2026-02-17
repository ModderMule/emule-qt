#pragma once

/// @file MediaInfo.h
/// @brief Media metadata extraction — structs and free functions.
///
/// Ported from MFC MediaInfo.h/cpp.  Uses QFile for I/O, QMimeDatabase
/// for MIME detection, and QMediaFormat for format identification.
/// Windows-only code (COM/WM SDK) is dropped.

#include "utils/Types.h"

#include <QString>

namespace eMule {

// ---------------------------------------------------------------------------
// Sub-structs for video / audio stream metadata
// ---------------------------------------------------------------------------

struct VideoInfo {
    uint32 width = 0;
    uint32 height = 0;
    uint32 bitRate = 0;          // bits/sec
    uint32 codecTag = 0;         // FourCC or biCompression
    double lengthSec = 0.0;
    double frameRate = 0.0;
    double aspectRatio = 0.0;
    bool lengthEstimated = false;
    QString codecName;
};

struct AudioInfo {
    uint16 formatTag = 0;        // WAVE format tag
    uint16 channels = 0;
    uint32 sampleRate = 0;       // Hz
    uint32 avgBytesPerSec = 0;
    uint16 bitsPerSample = 0;
    double lengthSec = 0.0;
    bool lengthEstimated = false;
    QString codecName;
    QString language;
};

// ---------------------------------------------------------------------------
// MediaInfo — top-level container for extracted metadata
// ---------------------------------------------------------------------------

struct MediaInfo {
    QString fileName;
    QString fileFormat;          // "AVI", "WAV (RIFF)", "Real Media"
    QString mimeType;
    uint64 fileSize = 0;
    double lengthSec = 0.0;
    bool lengthEstimated = false;

    // Metadata
    QString title;
    QString author;
    QString album;

    // Streams
    int videoStreamCount = 0;
    int audioStreamCount = 0;
    VideoInfo video;             // primary video stream
    AudioInfo audio;             // primary audio stream

    /// Derive file-level length from video/audio if unset.
    void initFileLength();
};

// ---------------------------------------------------------------------------
// Codec lookup
// ---------------------------------------------------------------------------

/// Return human-readable name for a WAVE format tag, e.g. "MP3 (MPEG-1, Layer 3)".
[[nodiscard]] QString audioFormatName(uint16 formatTag);

/// Return the short codec identifier for a WAVE format tag, e.g. "MP3".
[[nodiscard]] QString audioFormatCodecId(uint16 formatTag);

/// Return human-readable name for a video FourCC / biCompression value.
[[nodiscard]] QString videoFormatName(uint32 biCompression);

/// Return a display name for a codec identifier string (audio or video).
[[nodiscard]] QString codecDisplayName(const QString& codecId);

/// Return a well-known aspect ratio string like "4/3" or "16/9".
[[nodiscard]] QString knownAspectRatioString(double aspectRatio);

/// Case-insensitive FourCC comparison (byte-by-byte tolower).
[[nodiscard]] bool isEqualFourCC(uint32 a, uint32 b);

// ---------------------------------------------------------------------------
// Container parsing
// ---------------------------------------------------------------------------

/// Parse RIFF (AVI / WAV) headers from a file.
[[nodiscard]] bool readRIFFHeaders(const QString& filePath, MediaInfo& info);

/// Parse RealMedia (.rm / .rmvb) headers from a file.
[[nodiscard]] bool readRMHeaders(const QString& filePath, MediaInfo& info);

// ---------------------------------------------------------------------------
// High-level API
// ---------------------------------------------------------------------------

/// Extract media metadata from any supported file.
[[nodiscard]] bool extractMediaInfo(const QString& filePath, MediaInfo& info);

/// Detect MIME type using QMimeDatabase.
[[nodiscard]] QString detectMimeType(const QString& filePath);

} // namespace eMule
