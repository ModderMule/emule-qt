// This file is part of eMule
// Copyright (C) 2002-2024 Merkur
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from MFC MediaInfo.cpp — codec tables, RIFF/AVI parser,
// RealMedia parser, MIME detection via QMimeDatabase.

#include "MediaInfo.h"
#include "utils/OtherFunctions.h"
#include "utils/Log.h"

#include <QFile>
#include <QFileInfo>
#include <QMediaFormat>
#include <QMimeDatabase>
#include <QtEndian>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace eMule {

// ===================================================================
// FOURCC helpers (private)
// ===================================================================

inline constexpr uint32 makeFourCC(char a, char b, char c, char d) noexcept
{
    return static_cast<uint32>(static_cast<uint8>(a))
         | (static_cast<uint32>(static_cast<uint8>(b)) << 8)
         | (static_cast<uint32>(static_cast<uint8>(c)) << 16)
         | (static_cast<uint32>(static_cast<uint8>(d)) << 24);
}

// RIFF container FourCCs
inline constexpr uint32 kFourCC_RIFF = makeFourCC('R', 'I', 'F', 'F');
inline constexpr uint32 kFourCC_AVI  = makeFourCC('A', 'V', 'I', ' ');
inline constexpr uint32 kFourCC_WAVE = makeFourCC('W', 'A', 'V', 'E');
inline constexpr uint32 kFourCC_LIST = makeFourCC('L', 'I', 'S', 'T');

// AVI LIST types
inline constexpr uint32 kFourCC_hdrl = makeFourCC('h', 'd', 'r', 'l');
inline constexpr uint32 kFourCC_strl = makeFourCC('s', 't', 'r', 'l');
inline constexpr uint32 kFourCC_movi = makeFourCC('m', 'o', 'v', 'i');
inline constexpr uint32 kFourCC_INFO = makeFourCC('I', 'N', 'F', 'O');

// AVI chunk IDs
inline constexpr uint32 kFourCC_avih = makeFourCC('a', 'v', 'i', 'h');
inline constexpr uint32 kFourCC_strh = makeFourCC('s', 't', 'r', 'h');
inline constexpr uint32 kFourCC_strf = makeFourCC('s', 't', 'r', 'f');
inline constexpr uint32 kFourCC_strn = makeFourCC('s', 't', 'r', 'n');
inline constexpr uint32 kFourCC_idx1 = makeFourCC('i', 'd', 'x', '1');

// WAV chunk IDs
inline constexpr uint32 kFourCC_fmt  = makeFourCC('f', 'm', 't', ' ');
inline constexpr uint32 kFourCC_data = makeFourCC('d', 'a', 't', 'a');

// Stream types
inline constexpr uint32 kStreamType_vids = makeFourCC('v', 'i', 'd', 's');
inline constexpr uint32 kStreamType_auds = makeFourCC('a', 'u', 'd', 's');

// INFO sub-chunk IDs
inline constexpr uint32 kFourCC_INAM = makeFourCC('I', 'N', 'A', 'M');
inline constexpr uint32 kFourCC_IART = makeFourCC('I', 'A', 'R', 'T');
inline constexpr uint32 kFourCC_ISBJ = makeFourCC('I', 'S', 'B', 'J');
inline constexpr uint32 kFourCC_ICOP = makeFourCC('I', 'C', 'O', 'P');
inline constexpr uint32 kFourCC_ICMT = makeFourCC('I', 'C', 'M', 'T');
inline constexpr uint32 kFourCC_ICRD = makeFourCC('I', 'C', 'R', 'D');
inline constexpr uint32 kFourCC_ISFT = makeFourCC('I', 'S', 'F', 'T');

// Video biCompression special values (matching Windows BI_* constants)
inline constexpr uint32 kBI_RGB       = 0;
inline constexpr uint32 kBI_RLE8      = 1;
inline constexpr uint32 kBI_RLE4      = 2;
inline constexpr uint32 kBI_BITFIELDS = 3;
inline constexpr uint32 kBI_JPEG      = 4;
inline constexpr uint32 kBI_PNG       = 5;

// ===================================================================
// Packed structs for AVI stream header parsing
// ===================================================================

#pragma pack(push, 1)

struct Rect16 {
    int16 left;
    int16 top;
    int16 right;
    int16 bottom;
};

struct AVIStreamHeaderFixed {
    uint32 fccType;
    uint32 fccHandler;
    uint32 dwFlags;
    uint16 wPriority;
    uint16 wLanguage;
    uint32 dwInitialFrames;
    uint32 dwScale;
    uint32 dwRate;
    uint32 dwStart;
    uint32 dwLength;
    uint32 dwSuggestedBufferSize;
    uint32 dwQuality;
    uint32 dwSampleSize;
    Rect16 rcFrame;
};

struct BitmapInfoHeader {
    uint32 biSize;
    int32  biWidth;
    int32  biHeight;
    uint16 biPlanes;
    uint16 biBitCount;
    uint32 biCompression;
    uint32 biSizeImage;
    int32  biXPelsPerMeter;
    int32  biYPelsPerMeter;
    uint32 biClrUsed;
    uint32 biClrImportant;
};

struct PCMWaveFormat {
    uint16 wFormatTag;
    uint16 nChannels;
    uint32 nSamplesPerSec;
    uint32 nAvgBytesPerSec;
    uint16 nBlockAlign;
    uint16 wBitsPerSample;
};

struct MainAVIHeader {
    uint32 dwMicroSecPerFrame;
    uint32 dwMaxBytesPerSec;
    uint32 dwPaddingGranularity;
    uint32 dwFlags;
    uint32 dwTotalFrames;
    uint32 dwInitialFrames;
    uint32 dwStreams;
    uint32 dwSuggestedBufferSize;
    uint32 dwWidth;
    uint32 dwHeight;
    uint32 dwReserved[4];
};

// RealMedia packed structs
struct SRmChunkHdr {
    uint32 id;
    uint32 size;
};

struct SRmRMF {
    uint32 fileVersion;
    uint32 numHeaders;
};

struct SRmPROP {
    uint32 maxBitRate;
    uint32 avgBitRate;
    uint32 maxPacketSize;
    uint32 avgPacketSize;
    uint32 numPackets;
    uint32 duration;
    uint32 preroll;
    uint32 indexOffset;
    uint32 dataOffset;
    uint16 numStreams;
    uint16 flags;
};

struct SRmMDPR {
    uint16 streamNumber;
    uint32 maxBitRate;
    uint32 avgBitRate;
    uint32 maxPacketSize;
    uint32 avgPacketSize;
    uint32 startTime;
    uint32 preroll;
    uint32 duration;
};

#pragma pack(pop)

// ===================================================================
// Audio codec table (ported from s_WavFmtTag[])
// ===================================================================

struct WavFmtEntry {
    uint16      fmtTag;
    const char* codecId;
    const char* comment;
};

static constexpr WavFmtEntry s_wavFmtTable[] = {
    { 0x0000, "",                        "Unknown" },
    { 0x0001, "PCM",                     "Uncompressed" },
    { 0x0002, "ADPCM",                   "" },
    { 0x0003, "IEEE_FLOAT",              "" },
    { 0x0004, "VSELP",                   "Compaq Computer Corp." },
    { 0x0005, "IBM_CVSD",                "" },
    { 0x0006, "ALAW",                    "" },
    { 0x0007, "MULAW",                   "" },
    { 0x0008, "DTS",                     "Digital Theater Systems" },
    { 0x0009, "DRM",                     "" },
    { 0x000A, "WMAVOICE9",              "" },
    { 0x000B, "WMAVOICE10",             "" },
    { 0x0010, "OKI_ADPCM",              "" },
    { 0x0011, "DVI_ADPCM",              "Intel Corporation" },
    { 0x0012, "MEDIASPACE_ADPCM",       "Videologic" },
    { 0x0013, "SIERRA_ADPCM",           "" },
    { 0x0014, "G723_ADPCM",             "Antex Electronics Corporation" },
    { 0x0015, "DIGISTD",                "DSP Solutions, Inc." },
    { 0x0016, "DIGIFIX",                "DSP Solutions, Inc." },
    { 0x0017, "DIALOGIC_OKI_ADPCM",     "" },
    { 0x0018, "MEDIAVISION_ADPCM",       "" },
    { 0x0019, "CU_CODEC",               "Hewlett-Packard Company" },
    { 0x0020, "YAMAHA_ADPCM",           "" },
    { 0x0021, "SONARC",                 "Speech Compression" },
    { 0x0022, "DSPGROUP_TRUESPEECH",    "" },
    { 0x0023, "ECHOSC1",                "Echo Speech Corporation" },
    { 0x0024, "AUDIOFILE_AF36",          "Virtual Music, Inc." },
    { 0x0025, "APTX",                   "Audio Processing Technology" },
    { 0x0026, "AUDIOFILE_AF10",          "Virtual Music, Inc." },
    { 0x0027, "PROSODY_1612",           "Aculab plc" },
    { 0x0028, "LRC",                    "Merging Technologies S.A." },
    { 0x0030, "DOLBY_AC2",              "" },
    { 0x0031, "GSM610",                 "" },
    { 0x0032, "MSNAUDIO",               "" },
    { 0x0033, "ANTEX_ADPCME",           "" },
    { 0x0034, "CONTROL_RES_VQLPC",      "" },
    { 0x0035, "DIGIREAL",               "DSP Solutions, Inc." },
    { 0x0036, "DIGIADPCM",              "DSP Solutions, Inc." },
    { 0x0037, "CONTROL_RES_CR10",        "" },
    { 0x0038, "NMS_VBXADPCM",           "Natural MicroSystems" },
    { 0x0039, "CS_IMAADPCM",            "Crystal Semiconductor IMA ADPCM" },
    { 0x003A, "ECHOSC3",                "Echo Speech Corporation" },
    { 0x003B, "ROCKWELL_ADPCM",         "" },
    { 0x003C, "ROCKWELL_DIGITALK",      "" },
    { 0x003D, "XEBEC",                  "" },
    { 0x0040, "G721_ADPCM",             "Antex Electronics Corporation" },
    { 0x0041, "G728_CELP",              "Antex Electronics Corporation" },
    { 0x0042, "MSG723",                 "" },
    { 0x0050, "MP1",                    "MPEG-1, Layer 1" },
    { 0x0051, "MP2",                    "MPEG-1, Layer 2" },
    { 0x0052, "RT24",                   "InSoft, Inc." },
    { 0x0053, "PAC",                    "InSoft, Inc." },
    { 0x0055, "MP3",                    "MPEG-1, Layer 3" },
    { 0x0059, "LUCENT_G723",            "" },
    { 0x0060, "CIRRUS",                 "" },
    { 0x0061, "ESPCM",                  "ESS Technology" },
    { 0x0062, "VOXWARE",                "" },
    { 0x0063, "CANOPUS_ATRAC",          "" },
    { 0x0064, "G726_ADPCM",             "APICOM" },
    { 0x0065, "G722_ADPCM",             "APICOM" },
    { 0x0067, "DSAT_DISPLAY",           "" },
    { 0x0069, "VOXWARE_BYTE_ALIGNED",   "" },
    { 0x0070, "VOXWARE_AC8",            "" },
    { 0x0071, "VOXWARE_AC10",           "" },
    { 0x0072, "VOXWARE_AC16",           "" },
    { 0x0073, "VOXWARE_AC20",           "" },
    { 0x0074, "VOXWARE_RT24",           "" },
    { 0x0075, "VOXWARE_RT29",           "" },
    { 0x0076, "VOXWARE_RT29HW",         "" },
    { 0x0077, "VOXWARE_VR12",           "" },
    { 0x0078, "VOXWARE_VR18",           "" },
    { 0x0079, "VOXWARE_TQ40",           "" },
    { 0x0080, "SOFTSOUND",              "" },
    { 0x0081, "VOXWARE_TQ60",           "" },
    { 0x0082, "MSRT24",                 "" },
    { 0x0083, "G729A",                  "AT&T Labs, Inc." },
    { 0x0084, "MVI_MVI2",               "Motion Pixels" },
    { 0x0085, "DF_G726",                "DataFusion Systems (Pty) (Ltd)" },
    { 0x0086, "DF_GSM610",              "DataFusion Systems (Pty) (Ltd)" },
    { 0x0088, "ISIAUDIO",               "Iterated Systems, Inc." },
    { 0x0089, "ONLIVE",                 "" },
    { 0x0091, "SBC24",                  "Siemens Business Communications Sys" },
    { 0x0092, "DOLBY_AC3_SPDIF",        "Sonic Foundry" },
    { 0x0093, "MEDIASONIC_G723",        "" },
    { 0x0094, "PROSODY_8KBPS",          "Aculab plc" },
    { 0x0097, "ZYXEL_ADPCM",            "" },
    { 0x0098, "PHILIPS_LPCBB",          "" },
    { 0x0099, "PACKED",                 "Studer Professional Audio AG" },
    { 0x00A0, "MALDEN_PHONYTALK",       "" },
    { 0x0100, "RHETOREX_ADPCM",         "" },
    { 0x0101, "IRAT",                   "BeCubed Software Inc." },
    { 0x0111, "VIVO_G723",              "" },
    { 0x0112, "VIVO_SIREN",             "" },
    { 0x0123, "DIGITAL_G723",           "Digital Equipment Corporation" },
    { 0x0125, "SANYO_LD_ADPCM",         "" },
    { 0x0130, "SIPROLAB_ACEPLNET",      "" },
    { 0x0131, "SIPROLAB_ACELP4800",     "" },
    { 0x0132, "SIPROLAB_ACELP8V3",      "" },
    { 0x0133, "SIPROLAB_G729",          "" },
    { 0x0134, "SIPROLAB_G729A",         "" },
    { 0x0135, "SIPROLAB_KELVIN",        "" },
    { 0x0140, "G726ADPCM",              "Dictaphone Corporation" },
    { 0x0150, "QUALCOMM_PUREVOICE",     "" },
    { 0x0151, "QUALCOMM_HALFRATE",      "" },
    { 0x0155, "TUBGSM",                 "Ring Zero Systems, Inc." },
    { 0x0160, "MSAUDIO1",               "Microsoft Audio" },
    { 0x0161, "WMAUDIO2",               "Windows Media Audio" },
    { 0x0162, "WMAUDIO3",               "Windows Media Audio 9 Pro" },
    { 0x0163, "WMAUDIO_LOSSLESS",       "Windows Media Audio 9 Lossless" },
    { 0x0164, "WMASPDIF",               "Windows Media Audio Pro-over-S/PDIF" },
    { 0x0170, "UNISYS_NAP_ADPCM",       "" },
    { 0x0171, "UNISYS_NAP_ULAW",        "" },
    { 0x0172, "UNISYS_NAP_ALAW",        "" },
    { 0x0173, "UNISYS_NAP_16K",         "" },
    { 0x0200, "CREATIVE_ADPCM",         "" },
    { 0x0202, "CREATIVE_FASTSPEECH8",   "" },
    { 0x0203, "CREATIVE_FASTSPEECH10",  "" },
    { 0x0210, "UHER_ADPCM",             "" },
    { 0x0220, "QUARTERDECK",            "" },
    { 0x0230, "ILINK_VC",               "I-link Worldwide" },
    { 0x0240, "RAW_SPORT",              "Aureal Semiconductor" },
    { 0x0241, "ESST_AC3",               "ESS Technology, Inc." },
    { 0x0250, "IPI_HSX",                "Interactive Products, Inc." },
    { 0x0251, "IPI_RPELP",              "Interactive Products, Inc." },
    { 0x0260, "CS2",                    "Consistent Software" },
    { 0x0270, "SONY_SCX",               "" },
    { 0x0300, "FM_TOWNS_SND",           "Fujitsu Corp." },
    { 0x0400, "BTV_DIGITAL",            "Brooktree Corporation" },
    { 0x0401, "IMC",                    "Intel Music Coder for MSACM" },
    { 0x0450, "QDESIGN_MUSIC",          "" },
    { 0x0680, "VME_VMPCM",              "AT&T Labs, Inc." },
    { 0x0681, "TPC",                    "AT&T Labs, Inc." },
    { 0x1000, "OLIGSM",                 "Olivetti" },
    { 0x1001, "OLIADPCM",               "Olivetti" },
    { 0x1002, "OLICELP",                "Olivetti" },
    { 0x1003, "OLISBC",                 "Olivetti" },
    { 0x1004, "OLIOPR",                 "Olivetti" },
    { 0x1100, "LH_CODEC",               "Lernout & Hauspie" },
    { 0x1400, "NORRIS",                 "" },
    { 0x1500, "SOUNDSPACE_MUSICOMPRESS","AT&T Labs, Inc." },
    { 0x1600, "MPEG_ADTS_AAC",          "" },
    { 0x1601, "MPEG_RAW_AAC",           "" },
    { 0x1608, "NOKIA_MPEG_ADTS_AAC",    "" },
    { 0x1609, "NOKIA_MPEG_RAW_AAC",     "" },
    { 0x160A, "VODAFONE_MPEG_ADTS_AAC", "" },
    { 0x160B, "VODAFONE_MPEG_RAW_AAC",  "" },
    { 0x2000, "AC3",                    "Dolby AC3" },
    { 0x2001, "DTS",                    "Digital Theater Systems" },
    // RealAudio (baked) codecs
    { 0x2002, "RA14",                   "RealAudio 1/2 14.4" },
    { 0x2003, "RA28",                   "RealAudio 1/2 28.8" },
    { 0x2004, "COOK",                   "RealAudio G2/8 Cook (Low Bitrate)" },
    { 0x2005, "DNET",                   "RealAudio 3/4/5 Music (DNET)" },
    { 0x2006, "RAAC",                   "RealAudio 10 AAC (RAAC)" },
    { 0x2007, "RACP",                   "RealAudio 10 AAC+ (RACP)" },
};

// ===================================================================
// RealMedia codec table
// ===================================================================

struct RealMediaCodec {
    const char* id;   // 4-char identifier
    const char* desc; // human-readable description
};

static constexpr RealMediaCodec s_realMediaCodecs[] = {
    { "14.4", "Real Audio 1 (14.4)" },
    { "14_4", "Real Audio 1 (14.4)" },
    { "28.8", "Real Audio 2 (28.8)" },
    { "28_8", "Real Audio 2 (28.8)" },
    { "RV10", "Real Video 5" },
    { "RV13", "Real Video 5" },
    { "RV20", "Real Video G2" },
    { "RV30", "Real Video 8" },
    { "RV40", "Real Video 9" },
    { "atrc", "Real & Sony Atrac3 Codec" },
    { "cook", "Real Audio G2/7 Cook (Low Bitrate)" },
    { "dnet", "Real Audio 3/4/5 Music (DNET)" },
    { "lpcJ", "Real Audio 1 (14.4)" },
    { "raac", "Real Audio 10 AAC (RAAC)" },
    { "racp", "Real Audio 10 AAC+ (RACP)" },
    { "ralf", "Real Audio Lossless Format" },
    { "rtrc", "Real Audio 8 (RTRC)" },
    { "rv10", "Real Video 5" },
    { "rv20", "Real Video G2" },
    { "rv30", "Real Video 8" },
    { "rv40", "Real Video 9" },
    { "sipr", "Real Audio 4 (Sipro)" },
};

// ===================================================================
// MediaInfo::initFileLength
// ===================================================================

void MediaInfo::initFileLength()
{
    if (lengthSec == 0.0) {
        if (video.lengthSec > 0.0) {
            lengthSec = video.lengthSec;
            lengthEstimated = video.lengthEstimated;
        } else if (audio.lengthSec > 0.0) {
            lengthSec = audio.lengthSec;
            lengthEstimated = audio.lengthEstimated;
        }
    }
}

// ===================================================================
// isEqualFourCC
// ===================================================================

bool isEqualFourCC(uint32 a, uint32 b)
{
    for (int i = 0; i < 4; ++i) {
        if (std::tolower(static_cast<unsigned char>(a & 0xFF))
            != std::tolower(static_cast<unsigned char>(b & 0xFF)))
            return false;
        a >>= 8;
        b >>= 8;
    }
    return true;
}

// ===================================================================
// audioFormatName / audioFormatCodecId
// ===================================================================

QString audioFormatName(uint16 formatTag)
{
    for (const auto& entry : s_wavFmtTable) {
        if (entry.fmtTag == formatTag) {
            QString name = QString::fromLatin1(entry.codecId);
            QString comment = QString::fromLatin1(entry.comment);
            if (!comment.isEmpty()) {
                if (!name.isEmpty())
                    return name + u" (" + comment + u')';
                return comment;
            }
            return name;
        }
    }
    return QStringLiteral("0x%1 (Unknown)").arg(formatTag, 4, 16, QChar(u'0'));
}

QString audioFormatCodecId(uint16 formatTag)
{
    for (const auto& entry : s_wavFmtTable) {
        if (entry.fmtTag == formatTag)
            return QString::fromLatin1(entry.codecId);
    }
    return {};
}

// ===================================================================
// videoFormatName
// ===================================================================

/// Internal: return display name with description for known FourCC values.
static QString videoFormatDisplayName(uint32 biCompression)
{
    QString name;
    const char* desc = nullptr;

    switch (biCompression) {
    case kBI_RGB:       return QStringLiteral("RGB (Uncompressed)");
    case kBI_RLE8:      return QStringLiteral("RLE8 (Run Length Encoded 8-bit)");
    case kBI_RLE4:      return QStringLiteral("RLE4 (Run Length Encoded 4-bit)");
    case kBI_BITFIELDS: return QStringLiteral("Bitfields");
    case kBI_JPEG:      return QStringLiteral("JPEG");
    case kBI_PNG:       return QStringLiteral("PNG");
    default:
        break;
    }

    // Uppercase for comparison
    uint32 upper = 0;
    for (int i = 0; i < 4; ++i) {
        auto byte = static_cast<uint8>((biCompression >> (i * 8)) & 0xFF);
        upper |= static_cast<uint32>(static_cast<uint8>(std::toupper(byte))) << (i * 8);
    }

    switch (upper) {
    case makeFourCC('D','I','V','3'): desc = " (DivX ;-) MPEG-4 v3 Low)"; break;
    case makeFourCC('D','I','V','4'): desc = " (DivX ;-) MPEG-4 v3 Fast)"; break;
    case makeFourCC('D','I','V','X'): desc = " (DivX 4)"; break;
    case makeFourCC('D','X','5','0'): desc = " (DivX 5)"; break;
    case makeFourCC('M','P','G','4'): desc = " (Microsoft MPEG-4 v1)"; break;
    case makeFourCC('M','P','4','2'): desc = " (Microsoft MPEG-4 v2)"; break;
    case makeFourCC('M','P','4','3'): desc = " (Microsoft MPEG-4 v3)"; break;
    case makeFourCC('D','X','S','B'): desc = " (Subtitle)"; break;
    case makeFourCC('W','M','V','1'): desc = " (Windows Media Video 7)"; break;
    case makeFourCC('W','M','V','2'): desc = " (Windows Media Video 8)"; break;
    case makeFourCC('W','M','V','3'): desc = " (Windows Media Video 9)"; break;
    case makeFourCC('W','M','V','A'): desc = " (Windows Media Video 9 Advanced Profile)"; break;
    case makeFourCC('R','V','1','0'):
    case makeFourCC('R','V','1','3'): desc = " (Real Video 5)"; break;
    case makeFourCC('R','V','2','0'): desc = " (Real Video G2)"; break;
    case makeFourCC('R','V','3','0'): desc = " (Real Video 8)"; break;
    case makeFourCC('R','V','4','0'): desc = " (Real Video 9)"; break;
    case makeFourCC('H','2','6','4'): desc = " (MPEG-4 AVC)"; break;
    case makeFourCC('X','2','6','4'): desc = " (x264 MPEG-4 AVC)"; break;
    case makeFourCC('X','V','I','D'): desc = " (Xvid MPEG-4)"; break;
    case makeFourCC('T','S','C','C'): desc = " (TechSmith Screen Capture)"; break;
    case makeFourCC('M','J','P','G'): desc = " (M-JPEG)"; break;
    case makeFourCC('I','V','3','2'): desc = " (Intel Indeo Video 3.2)"; break;
    case makeFourCC('I','V','4','0'): desc = " (Intel Indeo Video 4.0)"; break;
    case makeFourCC('I','V','5','0'): desc = " (Intel Indeo Video 5.0)"; break;
    case makeFourCC('F','M','P','4'): desc = " (MPEG-4)"; break;
    case makeFourCC('C','V','I','D'): desc = " (Cinepack)"; break;
    case makeFourCC('C','R','A','M'): desc = " (Microsoft Video 1)"; break;
    default:
        return {}; // Unknown FourCC — return empty
    }

    // Extract raw FourCC string + description
    char fourcc[5];
    std::memcpy(fourcc, &biCompression, 4);
    fourcc[4] = '\0';
    name = QString::fromLatin1(fourcc, 4);
    if (desc)
        name += QString::fromLatin1(desc);
    return name;
}

QString videoFormatName(uint32 biCompression)
{
    QString name = videoFormatDisplayName(biCompression);
    if (name.isEmpty()) {
        // Return uppercased raw FourCC
        char fourcc[5];
        std::memcpy(fourcc, &biCompression, 4);
        fourcc[4] = '\0';
        name = QString::fromLatin1(fourcc, 4).toUpper();
    }
    return name;
}

// ===================================================================
// knownAspectRatioString
// ===================================================================

QString knownAspectRatioString(double aspectRatio)
{
    if (aspectRatio >= 1.00 && aspectRatio < 1.50)
        return QStringLiteral("4/3");
    if (aspectRatio >= 1.50 && aspectRatio < 2.00)
        return QStringLiteral("16/9");
    if (aspectRatio >= 2.00 && aspectRatio < 2.22)
        return QStringLiteral("2.2");
    if (aspectRatio >= 2.22 && aspectRatio < 2.30)
        return QStringLiteral("2.25");
    if (aspectRatio >= 2.30 && aspectRatio < 2.50)
        return QStringLiteral("2.35");
    return {};
}

// ===================================================================
// codecDisplayName
// ===================================================================

/// Internal: look up audio codec display name by codec string.
static QString audioFormatDisplayName(const QString& codecId)
{
    for (const auto& entry : s_wavFmtTable) {
        if (codecId.compare(QLatin1StringView(entry.codecId), Qt::CaseInsensitive) == 0) {
            if (entry.fmtTag != 0 && entry.codecId[0] != '\0' && entry.comment[0] != '\0')
                return QString::fromLatin1(entry.codecId) + u" (" + QString::fromLatin1(entry.comment) + u')';
            break;
        }
    }
    return {};
}

QString codecDisplayName(const QString& codecId)
{
    // Try as a FourCC video codec first (3-4 chars)
    if (codecId.size() == 3 || codecId.size() == 4) {
        bool haveFourCC = true;
        uint32 fcc;

        // Special non-printable BI_* values
        if (codecId.compare(u"rgb", Qt::CaseInsensitive) == 0)
            fcc = kBI_RGB;
        else if (codecId.compare(u"rle8", Qt::CaseInsensitive) == 0)
            fcc = kBI_RLE8;
        else if (codecId.compare(u"rle4", Qt::CaseInsensitive) == 0)
            fcc = kBI_RLE4;
        else if (codecId.compare(u"jpeg", Qt::CaseInsensitive) == 0)
            fcc = kBI_JPEG;
        else if (codecId.compare(u"png", Qt::CaseInsensitive) == 0)
            fcc = kBI_PNG;
        else {
            fcc = makeFourCC(' ', ' ', ' ', ' ');
            auto* p = reinterpret_cast<char*>(&fcc);
            for (int i = 0; i < codecId.size(); ++i) {
                QChar ch = codecId[i];
                if (ch.unicode() >= 0x100 || (!ch.isLetterOrNumber() && ch != u'.' && ch != u' ')) {
                    haveFourCC = false;
                    break;
                }
                p[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(ch.toLatin1())));
            }
        }
        if (haveFourCC) {
            QString result = videoFormatDisplayName(fcc);
            if (!result.isEmpty())
                return result;
        }
    }

    // Try as audio format name
    QString result = audioFormatDisplayName(codecId);
    if (!result.isEmpty())
        return result;

    // Fallback: uppercase the codec ID itself
    return codecId.toUpper();
}

// ===================================================================
// MIME detection
// ===================================================================

QString detectMimeType(const QString& filePath)
{
    QMimeDatabase db;
    return db.mimeTypeForFile(filePath).name();
}

// ===================================================================
// RIFF file I/O helpers (private)
// ===================================================================

static bool readExact(QFile& f, void* buf, qint64 count)
{
    return f.read(static_cast<char*>(buf), count) == count;
}

static bool readChunkHeader(QFile& f, uint32& fccType, uint32& length)
{
    return readExact(f, &fccType, 4) && readExact(f, &length, 4);
}

static bool seekRelative(QFile& f, qint64 offset)
{
    return f.seek(f.pos() + offset);
}

// ===================================================================
// RIFF stream header parser (private)
// ===================================================================

struct StreamHeader {
    AVIStreamHeaderFixed hdr{};
    uint32 formatLen = 0;
    QByteArray formatData;
    QByteArray nameData;
};

static bool parseStreamHeader(QFile& f, uint32 lengthLeft, StreamHeader& sh)
{
    uint32 fccType = 0;
    uint32 length = 0;

    while (lengthLeft >= 8) {
        if (!readChunkHeader(f, fccType, length))
            return false;

        lengthLeft -= 8;
        if (length > lengthLeft)
            return false;
        uint32 padded = length + (length & 1);
        if (padded > lengthLeft)
            padded = lengthLeft;
        lengthLeft -= padded;

        switch (fccType) {
        case kFourCC_strh: {
            auto toRead = std::min(length, static_cast<uint32>(sizeof(sh.hdr)));
            std::memset(&sh.hdr, 0, sizeof(sh.hdr));
            if (!readExact(f, &sh.hdr, toRead))
                return false;
            qint64 skip = static_cast<qint64>(length + (length & 1)) - toRead;
            if (skip > 0 && !seekRelative(f, skip))
                return false;
            length = 0;
            break;
        }
        case kFourCC_strf:
            if (length > 4096)
                return false;
            sh.formatLen = length;
            sh.formatData.resize(length);
            if (!readExact(f, sh.formatData.data(), length))
                return false;
            if ((length & 1) && !seekRelative(f, 1))
                return false;
            length = 0;
            break;
        case kFourCC_strn:
            if (length > 512)
                return false;
            sh.nameData.resize(length);
            if (!readExact(f, sh.nameData.data(), length))
                return false;
            if ((length & 1) && !seekRelative(f, 1))
                return false;
            length = 0;
            break;
        default:
            break;
        }

        if (length > 0 && !seekRelative(f, length + (length & 1)))
            return false;
    }

    if (lengthLeft > 0)
        return seekRelative(f, lengthLeft);
    return true;
}

// ===================================================================
// readRIFFHeaders — public
// ===================================================================

bool readRIFFHeaders(const QString& filePath, MediaInfo& info)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    // Read RIFF header
    uint32 fccType = 0;
    uint32 length = 0;
    if (!readChunkHeader(f, fccType, length))
        return false;
    if (fccType != kFourCC_RIFF)
        return false;

    bool sizeInvalid = false;
    if (length < 4) {
        length = 0xFFFFFFF0u;
        sizeInvalid = true;
    }
    uint32 lengthLeft = length - 4;

    // Read AVI or WAVE type
    uint32 fccMain = 0;
    if (!readExact(f, &fccMain, 4))
        return false;

    bool isAVI = (fccMain == kFourCC_AVI);
    if (!isAVI && fccMain != kFourCC_WAVE)
        return false;

    uint32 movieChunkSize = 0;
    uint32 videoFrames = 0;
    int nonAVStreams = 0;
    uint32 allNonVideoAvgBytesPerSec = 0;
    bool haveReadAllStreams = false;

    while (!haveReadAllStreams && lengthLeft >= 8) {
        if (!readChunkHeader(f, fccType, length))
            break;

        if (fccType == 0 && length == 0) {
            if (info.videoStreamCount > 0 || info.audioStreamCount > 0)
                break;
            return false;
        }

        if (!sizeInvalid) {
            lengthLeft -= 8;
            if (length > lengthLeft) {
                if (fccType != kFourCC_LIST)
                    break;
            }
            uint32 padded = length + (length & 1);
            if (padded <= lengthLeft)
                lengthLeft -= padded;
            else
                lengthLeft = 0;
        }

        switch (fccType) {
        case kFourCC_LIST: {
            uint32 listType = 0;
            if (!readExact(f, &listType, 4))
                goto done;
            if (length < 4)
                goto done;
            length -= 4;

            switch (listType) {
            case kFourCC_hdrl:
                // Silently enter header block — adjust lengthLeft
                if (!sizeInvalid)
                    lengthLeft += length + (length & 1) + 4;
                length = 0;
                break;

            case kFourCC_strl: {
                StreamHeader sh;
                if (!parseStreamHeader(f, length, sh))
                    goto done;

                double samplesSec = (sh.hdr.dwScale != 0)
                    ? sh.hdr.dwRate / static_cast<double>(sh.hdr.dwScale) : 0.0;
                double streamLen = (samplesSec != 0.0)
                    ? sh.hdr.dwLength / samplesSec : 0.0;

                if (sh.hdr.fccType == kStreamType_auds) {
                    ++info.audioStreamCount;
                    if (info.audioStreamCount == 1) {
                        info.audio.lengthSec = streamLen;
                        if (sh.formatData.size() >= static_cast<int>(sizeof(PCMWaveFormat))) {
                            const auto* wav = reinterpret_cast<const PCMWaveFormat*>(sh.formatData.constData());
                            info.audio.formatTag = wav->wFormatTag;
                            info.audio.channels = wav->nChannels;
                            info.audio.sampleRate = wav->nSamplesPerSec;
                            info.audio.avgBytesPerSec = wav->nAvgBytesPerSec;
                            info.audio.bitsPerSample = wav->wBitsPerSample;
                            info.audio.codecName = audioFormatName(wav->wFormatTag);
                        }
                    } else {
                        if (sh.formatData.size() >= static_cast<int>(sizeof(PCMWaveFormat))) {
                            const auto* wav = reinterpret_cast<const PCMWaveFormat*>(sh.formatData.constData());
                            // VBR codecs (MP3, WMA, AAC, Vorbis, etc.) report unreliable
                            // nAvgBytesPerSec — only accumulate for CBR-reliable formats.
                            const bool likelyVBR = (wav->wFormatTag == 0x0055)  // MP3
                                || (wav->wFormatTag == 0x0050)  // MPEG-1 Layer 1
                                || (wav->wFormatTag == 0x0051)  // MPEG-1 Layer 2
                                || (wav->wFormatTag == 0x0160)  // WMA v1
                                || (wav->wFormatTag == 0x0161)  // WMA v2
                                || (wav->wFormatTag == 0x0162)  // WMA Pro
                                || (wav->wFormatTag == 0x0163)  // WMA Lossless
                                || (wav->wFormatTag == 0x00FF)  // AAC
                                || (wav->wFormatTag == 0x1600)  // AAC (ADTS)
                                || (wav->wFormatTag == 0x1601)  // AAC (raw)
                                || (wav->wFormatTag == 0x566F); // Vorbis
                            if (!likelyVBR)
                                allNonVideoAvgBytesPerSec += wav->nAvgBytesPerSec;
                        }
                    }
                } else if (sh.hdr.fccType == kStreamType_vids) {
                    ++info.videoStreamCount;
                    if (info.videoStreamCount == 1) {
                        videoFrames = sh.hdr.dwLength;
                        info.video.lengthSec = streamLen;
                        info.video.frameRate = samplesSec;
                        if (sh.formatData.size() >= static_cast<int>(sizeof(BitmapInfoHeader))) {
                            const auto* bmi = reinterpret_cast<const BitmapInfoHeader*>(sh.formatData.constData());
                            info.video.codecTag = bmi->biCompression;
                            info.video.width = static_cast<uint32>(std::abs(bmi->biWidth));
                            info.video.height = static_cast<uint32>(std::abs(bmi->biHeight));
                            info.video.codecName = videoFormatName(bmi->biCompression);
                            if (bmi->biWidth != 0 && bmi->biHeight != 0)
                                info.video.aspectRatio = std::fabs(bmi->biWidth / static_cast<double>(bmi->biHeight));
                        }
                    }
                } else {
                    ++nonAVStreams;
                }

                length = 0;
                break;
            }

            case kFourCC_movi:
                movieChunkSize = length;
                haveReadAllStreams = true;
                break;

            case kFourCC_INFO: {
                if (length < 0x10000 && length > 0) {
                    QByteArray chunk(length, Qt::Uninitialized);
                    if (readExact(f, chunk.data(), length)) {
                        // Parse INFO sub-chunks
                        uint32 pos = 0;
                        while (pos + 8 <= static_cast<uint32>(chunk.size())) {
                            uint32 ckId = peekUInt32(chunk.constData() + pos);
                            uint32 ckLen = peekUInt32(chunk.constData() + pos + 4);
                            pos += 8;

                            QString value;
                            if (ckLen < 512 && pos + ckLen <= static_cast<uint32>(chunk.size())) {
                                value = QString::fromLatin1(chunk.constData() + pos, static_cast<int>(ckLen)).trimmed();
                                value.replace(u'\r', u' ');
                                value.replace(u'\n', u' ');
                            }
                            pos += ckLen + (ckLen & 1);

                            switch (ckId) {
                            case kFourCC_INAM: info.title = value; break;
                            case kFourCC_IART: info.author = value; break;
                            default: break;
                            }
                        }

                        if (length & 1)
                            seekRelative(f, 1);
                        length = 0;
                    } else {
                        haveReadAllStreams = true;
                    }
                }
                break;
            }

            default:
                break;
            }
            break;
        }

        case kFourCC_avih:
            if (length == sizeof(MainAVIHeader)) {
                MainAVIHeader hdr{};
                if (!readExact(f, &hdr, sizeof(hdr)))
                    goto done;
                length = 0;
            }
            break;

        case kFourCC_idx1:
            haveReadAllStreams = true;
            break;

        case kFourCC_fmt:
            if (fccMain == kFourCC_WAVE) {
                if (length > 4096)
                    goto done;
                QByteArray fmtData(length, Qt::Uninitialized);
                if (!readExact(f, fmtData.data(), length))
                    goto done;
                if ((length & 1) && !seekRelative(f, 1))
                    goto done;

                ++info.audioStreamCount;
                if (info.audioStreamCount == 1
                    && fmtData.size() >= static_cast<int>(sizeof(PCMWaveFormat))) {
                    const auto* wav = reinterpret_cast<const PCMWaveFormat*>(fmtData.constData());
                    info.audio.formatTag = wav->wFormatTag;
                    info.audio.channels = wav->nChannels;
                    info.audio.sampleRate = wav->nSamplesPerSec;
                    info.audio.avgBytesPerSec = wav->nAvgBytesPerSec;
                    info.audio.bitsPerSample = wav->wBitsPerSample;
                    info.audio.codecName = audioFormatName(wav->wFormatTag);
                }

                length = 0;
            }
            break;

        case kFourCC_data:
            if (fccMain == kFourCC_WAVE) {
                if (info.audioStreamCount == 1 && info.videoStreamCount == 0
                    && info.audio.avgBytesPerSec != 0
                    && info.audio.avgBytesPerSec != 0xFFFFFFFFu) {
                    info.audio.lengthSec = static_cast<double>(length) / info.audio.avgBytesPerSec;
                    if (info.audio.lengthSec < 1.0)
                        info.audio.lengthSec = 1.0;
                }
            }
            break;

        default:
            break;
        }

        if (haveReadAllStreams)
            break;
        if (length > 0 && !seekRelative(f, length + (length & 1)))
            break;
    }

done:
    if (info.videoStreamCount == 0 && info.audioStreamCount == 0)
        return false;

    if (isAVI) {
        info.fileFormat = QStringLiteral("AVI");
        // Compute video bit rate
        if (info.videoStreamCount == 1 && nonAVStreams == 0 && info.video.lengthSec > 0.0) {
            uint32 overhead = videoFrames * (sizeof(uint16) + sizeof(uint16) + sizeof(uint32));
            info.video.bitRate = static_cast<uint32>(
                ((movieChunkSize - overhead) / info.video.lengthSec
                 - allNonVideoAvgBytesPerSec) * 8);
        }
    } else {
        info.fileFormat = QStringLiteral("WAV (RIFF)");
    }

    info.initFileLength();
    return true;
}

// ===================================================================
// RealMedia codec info helper (private)
// ===================================================================

static QString realMediaCodecInfo(const char* codecId)
{
    // Build display string from the 4-char ID
    QByteArray raw(codecId, 4);
    QString display = QString::fromLatin1(raw).trimmed();

    for (const auto& codec : s_realMediaCodecs) {
        if (std::strncmp(codec.id, codecId, 4) == 0) {
            display += u" (" + QString::fromLatin1(codec.desc) + u')';
            break;
        }
    }
    return display;
}

// ===================================================================
// readRMHeaders — public
// ===================================================================

bool readRMHeaders(const QString& filePath, MediaInfo& info)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    // Read first chunk header
    SRmChunkHdr chunkHdr{};
    if (!readExact(f, &chunkHdr, sizeof(chunkHdr)))
        return false;

    uint32 id = qFromBigEndian(chunkHdr.id);
    uint32 size = qFromBigEndian(chunkHdr.size);

    // Check .RMF magic (stored as big-endian '.RMF')
    constexpr uint32 kRMF_MAGIC = 0x2E524D46u; // '.RMF'
    if (id != kRMF_MAGIC)
        return false;
    if (size < sizeof(SRmChunkHdr))
        return false;

    uint32 dataSize = size - sizeof(SRmChunkHdr);
    if (dataSize == 0)
        return false;

    // Read version
    uint16 version = 0;
    if (!readExact(f, &version, 2))
        return false;
    version = qFromBigEndian(version);
    if (version >= 2)
        return false;

    // Read RMF header
    SRmRMF rmf{};
    if (!readExact(f, &rmf, sizeof(rmf)))
        return false;

    info.fileFormat = QStringLiteral("Real Media");

    bool readPROP = false;
    bool readMDPR_Video = false;
    bool readMDPR_Audio = false;
    bool readCONT = false;
    qint64 chunkEndPos = static_cast<qint64>(size);

    while (!readCONT || !readPROP || !readMDPR_Video || !readMDPR_Audio) {
        // Seek to chunk end if not there yet
        qint64 curPos = f.pos();
        if (curPos > chunkEndPos)
            break;
        if (curPos < chunkEndPos)
            f.seek(chunkEndPos);

        qint64 chunkStartPos = f.pos();

        if (!readExact(f, &chunkHdr, sizeof(chunkHdr)))
            break;

        id = qFromBigEndian(chunkHdr.id);
        size = qFromBigEndian(chunkHdr.size);

        if (size < sizeof(SRmChunkHdr))
            break;

        chunkEndPos = chunkStartPos + size;
        dataSize = size - sizeof(SRmChunkHdr);
        if (dataSize == 0)
            continue;

        switch (id) {
        case 0x50524F50u: { // 'PROP'
            if (!readExact(f, &version, 2))
                break;
            version = qFromBigEndian(version);
            if (version != 0)
                break;

            SRmPROP prop{};
            if (!readExact(f, &prop, sizeof(prop)))
                break;

            info.lengthSec = (qFromBigEndian(prop.duration) + 500.0) / 1000.0;
            readPROP = true;
            break;
        }

        case 0x4D445052u: { // 'MDPR'
            if (!readExact(f, &version, 2))
                break;
            version = qFromBigEndian(version);
            if (version != 0)
                break;

            SRmMDPR mdpr{};
            if (!readExact(f, &mdpr, sizeof(mdpr)))
                break;

            // Read stream name (length-prefixed byte string)
            uint8 nameLen = 0;
            if (!readExact(f, &nameLen, 1))
                break;
            QByteArray streamName(nameLen, Qt::Uninitialized);
            if (nameLen > 0 && !readExact(f, streamName.data(), nameLen))
                break;

            // Read MIME type (length-prefixed byte string)
            uint8 mimeLen = 0;
            if (!readExact(f, &mimeLen, 1))
                break;
            QByteArray mimeType(mimeLen, Qt::Uninitialized);
            if (mimeLen > 0 && !readExact(f, mimeType.data(), mimeLen))
                break;

            // Read type-specific data length
            uint32 typeDataLen = 0;
            if (!readExact(f, &typeDataLen, 4))
                break;
            typeDataLen = qFromBigEndian(typeDataLen);

            if (mimeType == "video/x-pn-realvideo") {
                ++info.videoStreamCount;
                if (info.videoStreamCount == 1) {
                    info.video.bitRate = qFromBigEndian(mdpr.avgBitRate);
                    info.video.lengthSec = qFromBigEndian(mdpr.duration) / 1000.0;

                    if (typeDataLen >= 24 && typeDataLen < 8192) {
                        QByteArray typeData(typeDataLen, Qt::Uninitialized);
                        if (readExact(f, typeData.data(), typeDataLen)) {
                            // Check for 'VIDO' marker at offset 4
                            uint32 marker = peekUInt32(typeData.constData() + 4);
                            if (qFromBigEndian(marker) == 0x5649444Fu) { // 'VIDO'
                                info.video.codecName = realMediaCodecInfo(typeData.constData() + 8);
                                std::memcpy(&info.video.codecTag, typeData.constData() + 8, 4);
                                info.video.width = qFromBigEndian(peekUInt16(typeData.constData() + 12));
                                info.video.height = qFromBigEndian(peekUInt16(typeData.constData() + 14));
                                if (info.video.width > 0 && info.video.height > 0)
                                    info.video.aspectRatio = std::fabs(
                                        static_cast<double>(info.video.width)
                                        / static_cast<double>(info.video.height));
                                if (typeDataLen >= 24)
                                    info.video.frameRate = qFromBigEndian(peekUInt16(typeData.constData() + 22));
                                readMDPR_Video = true;
                            }
                        }
                    }
                }
            } else if (mimeType == "audio/x-pn-realaudio") {
                ++info.audioStreamCount;
                if (info.audioStreamCount == 1) {
                    info.audio.avgBytesPerSec = qFromBigEndian(mdpr.avgBitRate) / 8;
                    info.audio.lengthSec = qFromBigEndian(mdpr.duration) / 1000.0;

                    if (typeDataLen >= 6 && typeDataLen < 8192) {
                        QByteArray typeData(typeDataLen, Qt::Uninitialized);
                        if (readExact(f, typeData.data(), typeDataLen)) {
                            uint32 fourCC = peekUInt32(typeData.constData());
                            uint16 ver = qFromBigEndian(peekUInt16(typeData.constData() + 4));

                            // Check for '.ra\xFD' magic
                            if (fourCC == makeFourCC('.', 'r', 'a', '\xFD')) {
                                if (ver == 3 && typeDataLen >= 10) {
                                    info.audio.sampleRate = 8000;
                                    info.audio.channels = qFromBigEndian(peekUInt16(typeData.constData() + 8));
                                    info.audio.codecName = QStringLiteral(".ra3");
                                    readMDPR_Audio = true;
                                } else if (ver == 4 && typeDataLen >= 66) {
                                    info.audio.sampleRate = qFromBigEndian(peekUInt16(typeData.constData() + 48));
                                    info.audio.channels = qFromBigEndian(peekUInt16(typeData.constData() + 54));
                                    info.audio.codecName = realMediaCodecInfo(typeData.constData() + 62);
                                    if (std::strncmp(typeData.constData() + 62, "sipr", 4) == 0)
                                        info.audio.formatTag = 0x0130;
                                    else if (std::strncmp(typeData.constData() + 62, "cook", 4) == 0)
                                        info.audio.formatTag = 0x2004;
                                    readMDPR_Audio = true;
                                } else if (ver == 5 && typeDataLen >= 70) {
                                    info.audio.sampleRate = qFromBigEndian(peekUInt32(typeData.constData() + 48));
                                    info.audio.channels = qFromBigEndian(peekUInt16(typeData.constData() + 60));
                                    info.audio.codecName = realMediaCodecInfo(typeData.constData() + 66);
                                    if (std::strncmp(typeData.constData() + 66, "sipr", 4) == 0)
                                        info.audio.formatTag = 0x0130;
                                    else if (std::strncmp(typeData.constData() + 66, "cook", 4) == 0)
                                        info.audio.formatTag = 0x2004;
                                    readMDPR_Audio = true;
                                }
                            }
                        }
                    }
                }
            }
            break;
        }

        case 0x434F4E54u: { // 'CONT'
            if (!readExact(f, &version, 2))
                break;
            version = qFromBigEndian(version);
            if (version != 0)
                break;

            auto readString = [&f]() -> QString {
                uint16 len = 0;
                if (!readExact(f, &len, 2))
                    return {};
                len = qFromBigEndian(len);
                if (len == 0)
                    return {};
                QByteArray data(len, Qt::Uninitialized);
                if (!readExact(f, data.data(), len))
                    return {};
                return QString::fromLatin1(data);
            };

            info.title = readString();
            info.author = readString();
            readString(); // copyright — not stored
            readString(); // comment — not stored
            readCONT = true;
            break;
        }

        case 0x44415441u: // 'DATA'
        case 0x494E4458u: // 'INDX'
        case 0x524D4D44u: // 'RMMD'
        case 0x524D4A45u: // 'RMJE'
            break;

        default:
            // Broken DATA-chunk header (e.g., from mplayer) — stop reading
            goto rmDone;
        }
    }

rmDone:
    if (!readCONT && !readPROP && !readMDPR_Video && !readMDPR_Audio)
        return false;

    info.initFileLength();
    return true;
}

// ===================================================================
// extractMediaInfo — high-level API
// ===================================================================

bool extractMediaInfo(const QString& filePath, MediaInfo& info)
{
    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile())
        return false;

    info.fileName = fi.fileName();
    info.fileSize = static_cast<uint64>(fi.size());
    info.mimeType = detectMimeType(filePath);

    // Try RIFF (AVI / WAV)
    if (readRIFFHeaders(filePath, info))
        return true;

    // Try RealMedia
    if (readRMHeaders(filePath, info))
        return true;

    // Fallback: try QMediaFormat for format identification from MIME type
    if (!info.mimeType.isEmpty()) {
        QMediaFormat fmt;
        fmt.resolveForEncoding(QMediaFormat::NoFlags);

        // Map well-known MIME types to QMediaFormat file formats
        static const struct {
            const char* mime;
            QMediaFormat::FileFormat format;
            const char* label;
        } mimeMap[] = {
            { "video/mp4",        QMediaFormat::MPEG4,      "MPEG-4" },
            { "audio/mp4",        QMediaFormat::MPEG4,      "MPEG-4" },
            { "video/x-matroska", QMediaFormat::Matroska,   "Matroska" },
            { "audio/x-matroska", QMediaFormat::Matroska,   "Matroska" },
            { "video/webm",       QMediaFormat::WebM,       "WebM" },
            { "audio/webm",       QMediaFormat::WebM,       "WebM" },
            { "video/ogg",        QMediaFormat::Ogg,        "Ogg" },
            { "audio/ogg",        QMediaFormat::Ogg,        "Ogg" },
            { "audio/mpeg",       QMediaFormat::Mpeg4Audio,  "MPEG Audio" },
            { "audio/flac",       QMediaFormat::FLAC,       "FLAC" },
            { "video/x-ms-wmv",   QMediaFormat::WMV,        "Windows Media Video" },
            { "audio/x-ms-wma",   QMediaFormat::WMA,        "Windows Media Audio" },
        };

        for (const auto& [mime, format, label] : mimeMap) {
            if (info.mimeType == QLatin1StringView(mime)) {
                info.fileFormat = QString::fromLatin1(label);
                return true;
            }
        }
    }

    return false;
}

} // namespace eMule
