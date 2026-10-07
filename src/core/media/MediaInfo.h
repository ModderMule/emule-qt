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

/// MPEG audio: ID3v1/v2 texts, first frame, Xing/VBRI. A file without an ID3v2 tag
/// must carry an MPEG audio extension.
[[nodiscard]] bool readMP3Headers(const QString& filePath, MediaInfo& info);

/// MP4 / M4A / MOV: movie header, first sample entries, iTunes texts.
[[nodiscard]] bool readMP4Headers(const QString& filePath, MediaInfo& info);

/// FLAC: STREAMINFO and Vorbis comments.
[[nodiscard]] bool readFLACHeaders(const QString& filePath, MediaInfo& info);

/// Ogg Vorbis / Opus: id header, comments, last granule.
[[nodiscard]] bool readOggHeaders(const QString& filePath, MediaInfo& info);

/// Matroska / WebM: segment info and track entries.
[[nodiscard]] bool readMatroskaHeaders(const QString& filePath, MediaInfo& info);

/// ASF (WMA / WMV): length, bitrate, codec ids, title / author / album from the
/// header object.
[[nodiscard]] bool readASFHeaders(const QString& filePath, MediaInfo& info);

/// Raw AAC (ADTS): rate and channels from the frame headers, length from the
/// average frame; ID3 texts.
[[nodiscard]] bool readAACHeaders(const QString& filePath, MediaInfo& info);

/// Monkey's Audio: header, APEv2 texts.
[[nodiscard]] bool readAPEHeaders(const QString& filePath, MediaInfo& info);

/// WavPack: first block header, APEv2 texts.
[[nodiscard]] bool readWavPackHeaders(const QString& filePath, MediaInfo& info);

// ---------------------------------------------------------------------------
// High-level API
// ---------------------------------------------------------------------------

/// Extract media metadata from any supported file. Tries every reader and falls
/// back to a MIME label: for one file the user is looking at.
[[nodiscard]] bool extractMediaInfo(const QString& filePath, MediaInfo& info);

/// The container a media file's first bytes belong to.
enum class MediaKind {
    None, Riff, RealMedia, Mp4, Matroska, Asf, Flac, Ogg, Mp3, Aac, Ape, WavPack
};

/// What the gate reads before it decides: this much of a file with a media
/// extension, kMediaSniffBytes of any other.
inline constexpr qint64 kMediaGateBytes = 4096;
inline constexpr qint64 kMediaSniffBytes = 12;
/// All a shared file may cost the extractor, whatever it claims to contain.
inline constexpr qint64 kMediaReadBudget = 2 * 1024 * 1024;
/// Bumped whenever the extractor learns something: files stamped with an older
/// value are read again.
inline constexpr uint32 kMediaExtractVersion = 1;

/// True for an extension the gate holds to a container signature.
[[nodiscard]] bool isGatedMediaExtension(const QString& fileName);

/// Which reader, if any, is worth running: a media extension must come with its
/// own container's signature, any other name with a signature in the first
/// kMediaSniffBytes.
[[nodiscard]] MediaKind mediaGate(QByteArrayView head, const QString& fileName);

/// Metadata for a file in the share: gate first, then exactly one reader, all
/// within kMediaReadBudget. A whole library goes through here, so a file that is
/// not what its name says costs one small read and nothing more.
/// @param bytesRead  If set, how many bytes were read from the file.
[[nodiscard]] bool extractSharedMediaInfo(const QString& filePath, MediaInfo& info,
                                          qint64* bytesRead = nullptr);

/// Detect MIME type using QMimeDatabase.
[[nodiscard]] QString detectMimeType(const QString& filePath);

} // namespace eMule
