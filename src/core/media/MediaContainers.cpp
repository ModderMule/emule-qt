#include "pch.h"
/// @file MediaContainers.cpp
/// @brief Header parsers for MP3, MP4, FLAC, Ogg and Matroska.
///
/// Each reads only what the eD2K media tags need: length, bitrate, codec and the
/// title / artist / album texts. Input is untrusted; every size is bounded and a
/// short or odd file just yields false.

#include "media/MediaInfo.h"

#include <QFile>
#include <QStringDecoder>

#include <algorithm>
#include <array>
#include <limits>
#include <vector>
#include <bit>
#include <cmath>
#include <cstring>

namespace eMule {

namespace {

constexpr qint64 kMaxTagBytes = 1024 * 1024;        // ID3v2 / comment packets
constexpr qint64 kMaxMoovBytes = 16 * 1024 * 1024;  // MP4 movie box
constexpr int kMaxTextChars = 1024;

/// Bounds-checked big/little-endian reads over a byte buffer.
class Bytes {
public:
    explicit Bytes(QByteArrayView data) : m_data(data) {}

    [[nodiscard]] qsizetype size() const { return m_data.size(); }
    [[nodiscard]] bool has(qsizetype pos, qsizetype count) const
    {
        return pos >= 0 && count >= 0 && pos <= m_data.size() && count <= m_data.size() - pos;
    }
    [[nodiscard]] uint8 u8(qsizetype pos) const
    {
        return has(pos, 1) ? static_cast<uint8>(m_data[pos]) : 0;
    }
    [[nodiscard]] uint64 be(qsizetype pos, int bytes) const
    {
        uint64 v = 0;
        for (int i = 0; i < bytes; ++i)
            v = (v << 8) | u8(pos + i);
        return v;
    }
    [[nodiscard]] uint64 le(qsizetype pos, int bytes) const
    {
        uint64 v = 0;
        for (int i = bytes - 1; i >= 0; --i)
            v = (v << 8) | u8(pos + i);
        return v;
    }
    [[nodiscard]] QByteArrayView view(qsizetype pos, qsizetype count) const
    {
        return has(pos, count) ? m_data.sliced(pos, count) : QByteArrayView();
    }
    [[nodiscard]] bool startsWith(qsizetype pos, const char* magic) const
    {
        const auto len = static_cast<qsizetype>(std::strlen(magic));
        return has(pos, len) && std::memcmp(m_data.data() + pos, magic, static_cast<size_t>(len)) == 0;
    }

private:
    QByteArrayView m_data;
};

QByteArray readAt(QFile& f, qint64 pos, qint64 count)
{
    if (pos < 0 || count <= 0 || pos >= f.size() || !f.seek(pos))
        return {};
    return f.read(std::min(count, f.size() - pos));
}

QString cleanText(QString text)
{
    const qsizetype nul = text.indexOf(QChar(u'\0'));
    if (nul >= 0)
        text.truncate(nul);
    return text.trimmed().left(kMaxTextChars);
}

QString utf8Text(QByteArrayView raw)
{
    return cleanText(QString::fromUtf8(raw));
}

void setAudioOnly(MediaInfo& info, const QString& format, const QString& codec,
                  uint32 sampleRate, uint16 channels, double lengthSec, uint32 bitsPerSec)
{
    info.fileFormat = format;
    info.audioStreamCount = 1;
    info.audio.codecName = codec;
    info.audio.sampleRate = sampleRate;
    info.audio.channels = channels;
    info.audio.lengthSec = lengthSec;
    info.audio.avgBytesPerSec = bitsPerSec / 8;
    info.lengthSec = lengthSec;
}

uint32 overallBitsPerSec(uint64 bytes, double lengthSec)
{
    if (!std::isfinite(lengthSec) || lengthSec <= 0.0)
        return 0;
    const double bps = static_cast<double>(bytes) * 8.0 / lengthSec;
    return bps > 4e9 ? 0 : static_cast<uint32>(bps);
}

// ---------------------------------------------------------------------------
// ID3
// ---------------------------------------------------------------------------

uint32 synchsafe(const Bytes& b, qsizetype pos)
{
    return (static_cast<uint32>(b.u8(pos) & 0x7F) << 21) | (static_cast<uint32>(b.u8(pos + 1) & 0x7F) << 14)
         | (static_cast<uint32>(b.u8(pos + 2) & 0x7F) << 7) | static_cast<uint32>(b.u8(pos + 3) & 0x7F);
}

QString id3Text(QByteArrayView frame)
{
    if (frame.size() < 2)
        return {};
    const auto encoding = static_cast<uint8>(frame[0]);
    const QByteArrayView body = frame.sliced(1);
    switch (encoding) {
    case 0:  return cleanText(QString::fromLatin1(body));
    case 1:  return cleanText(QStringDecoder(QStringDecoder::Utf16)(body));
    case 2:  return cleanText(QStringDecoder(QStringDecoder::Utf16BE)(body));
    case 3:  return utf8Text(body);
    default: return {};
    }
}

/// Size of an ID3v2 tag at the start of @p f, header and footer included; 0 if none.
qint64 id3v2Size(QFile& f)
{
    const QByteArray head = readAt(f, 0, 10);
    const Bytes b(head);
    if (head.size() < 10 || !b.startsWith(0, "ID3"))
        return 0;
    return 10 + static_cast<qint64>(synchsafe(b, 6)) + ((b.u8(5) & 0x10) ? 10 : 0);
}

void readId3v2(QFile& f, MediaInfo& info)
{
    const QByteArray head = readAt(f, 0, 10);
    const Bytes hb(head);
    if (head.size() < 10 || !hb.startsWith(0, "ID3"))
        return;
    const uint8 major = hb.u8(3);
    const uint8 flags = hb.u8(5);
    if (major < 2 || major > 4)
        return;

    const QByteArray tag = readAt(f, 10, std::min<qint64>(synchsafe(hb, 6), kMaxTagBytes));
    const Bytes b(tag);
    qsizetype pos = 0;
    if ((flags & 0x40) && major >= 3) {
        // Extended header: v2.3 counts the bytes after the size field, v2.4 all of it.
        pos = major == 3 ? static_cast<qsizetype>(b.be(0, 4)) + 4 : static_cast<qsizetype>(synchsafe(b, 0));
    }

    const int idLen = major == 2 ? 3 : 4;
    const int headLen = major == 2 ? 6 : 10;
    while (b.has(pos, headLen) && b.u8(pos) != 0) {
        const QByteArrayView id = b.view(pos, idLen);
        qsizetype size = 0;
        if (major == 2)
            size = static_cast<qsizetype>(b.be(pos + 3, 3));
        else if (major == 3)
            size = static_cast<qsizetype>(b.be(pos + 4, 4));
        else
            size = static_cast<qsizetype>(synchsafe(b, pos + 4));
        pos += headLen;
        if (size <= 0 || !b.has(pos, size))
            break;

        const QByteArrayView body = b.view(pos, size);
        if (id == "TIT2" || id == "TT2")
            info.title = id3Text(body);
        else if (id == "TPE1" || id == "TP1")
            info.author = id3Text(body);
        else if (id == "TALB" || id == "TAL")
            info.album = id3Text(body);
        pos += size;
    }
}

void readId3v1(QFile& f, MediaInfo& info)
{
    const QByteArray tail = readAt(f, f.size() - 128, 128);
    if (tail.size() != 128 || !tail.startsWith("TAG"))
        return;
    const auto field = [&](int pos) { return cleanText(QString::fromLatin1(tail.mid(pos, 30))); };
    if (info.title.isEmpty())
        info.title = field(3);
    if (info.author.isEmpty())
        info.author = field(33);
    if (info.album.isEmpty())
        info.album = field(63);
}

// ---------------------------------------------------------------------------
// MPEG audio
// ---------------------------------------------------------------------------

struct MpegFrame {
    int version = 0;        // 1, 2, or 25 (MPEG 2.5)
    int layer = 0;          // 1-3
    uint32 bitrate = 0;     // bits/sec
    uint32 sampleRate = 0;
    uint16 channels = 0;
    uint32 samples = 0;     // per frame
    uint32 frameBytes = 0;
};

bool parseMpegFrame(uint32 header, MpegFrame& out)
{
    if ((header & 0xFFE00000u) != 0xFFE00000u)
        return false;
    const uint32 versionBits = (header >> 19) & 3;
    const uint32 layerBits = (header >> 17) & 3;
    const uint32 bitrateIdx = (header >> 12) & 15;
    const uint32 rateIdx = (header >> 10) & 3;
    const uint32 padding = (header >> 9) & 1;
    if (versionBits == 1 || layerBits == 0 || bitrateIdx == 0 || bitrateIdx == 15 || rateIdx == 3)
        return false;

    out.version = versionBits == 3 ? 1 : (versionBits == 2 ? 2 : 25);
    out.layer = 4 - static_cast<int>(layerBits);

    static constexpr uint16 kV1[3][15] = {
        {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}};
    static constexpr uint16 kV2[3][15] = {
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}};
    out.bitrate = 1000u * (out.version == 1 ? kV1 : kV2)[out.layer - 1][bitrateIdx];

    static constexpr uint32 kRates[3] = {44100, 48000, 32000};
    out.sampleRate = kRates[rateIdx] / (out.version == 1 ? 1 : (out.version == 2 ? 2 : 4));
    out.channels = ((header >> 6) & 3) == 3 ? 1 : 2;

    if (out.layer == 1) {
        out.samples = 384;
        out.frameBytes = (12 * out.bitrate / out.sampleRate + padding) * 4;
    } else {
        out.samples = (out.layer == 3 && out.version != 1) ? 576 : 1152;
        out.frameBytes = (out.samples / 8) * out.bitrate / out.sampleRate + padding;
    }
    return out.frameBytes >= 24;
}

bool hasAudioExtension(const QString& path, std::initializer_list<const char*> extensions)
{
    return std::ranges::any_of(extensions, [&](const char* ext) {
        return path.endsWith(QLatin1StringView(ext), Qt::CaseInsensitive);
    });
}

} // namespace

// ===================================================================
// MP3
// ===================================================================

bool readMP3Headers(const QString& filePath, MediaInfo& info)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const qint64 audioStart = id3v2Size(f);
    // Without a tag the name has to say so: a frame sync is only 11 bits.
    if (audioStart == 0 && !hasAudioExtension(filePath, {".mp3", ".mp2", ".mp1", ".mpa"}))
        return false;

    const QByteArray window = readAt(f, audioStart, 128 * 1024);
    const Bytes b(window);
    MpegFrame frame;
    qsizetype at = -1;
    for (qsizetype i = 0; b.has(i, 4); ++i) {
        if (b.u8(i) != 0xFF || !parseMpegFrame(static_cast<uint32>(b.be(i, 4)), frame))
            continue;
        // A second frame where the first says it ends, or it was a chance match.
        MpegFrame next;
        const qsizetype follow = i + frame.frameBytes;
        if (b.has(follow, 4) && !parseMpegFrame(static_cast<uint32>(b.be(follow, 4)), next))
            continue;
        at = i;
        break;
    }
    if (at < 0)
        return false;

    const uint64 audioBytes = static_cast<uint64>(f.size() - audioStart - at);
    double lengthSec = 0.0;
    uint32 bitrate = frame.bitrate;

    // VBR headers sit in the first frame, after the side info.
    const qsizetype side = frame.version == 1 ? (frame.channels == 1 ? 17 : 32)
                                              : (frame.channels == 1 ? 9 : 17);
    const qsizetype xing = at + 4 + side;
    uint32 frames = 0;
    if (b.startsWith(xing, "Xing") || b.startsWith(xing, "Info")) {
        if (b.be(xing + 4, 4) & 1)
            frames = static_cast<uint32>(b.be(xing + 8, 4));
    } else if (b.startsWith(at + 36, "VBRI")) {
        frames = static_cast<uint32>(b.be(at + 36 + 14, 4));
    }
    if (frames > 0) {
        lengthSec = static_cast<double>(frames) * frame.samples / frame.sampleRate;
        if (const uint32 real = overallBitsPerSec(audioBytes, lengthSec))
            bitrate = real;
    } else if (bitrate > 0) {
        lengthSec = static_cast<double>(audioBytes) * 8.0 / bitrate;
    }

    readId3v2(f, info);
    readId3v1(f, info);

    setAudioOnly(info, QStringLiteral("MPEG Audio"), {}, frame.sampleRate, frame.channels,
                 lengthSec, bitrate);
    info.audio.formatTag = frame.layer == 3 ? 0x0055 : 0x0050;
    return true;
}

// ===================================================================
// FLAC
// ===================================================================

namespace {

/// Vorbis comment block: vendor string, then "KEY=value" entries. Shared by FLAC and Ogg.
void readVorbisComments(QByteArrayView raw, MediaInfo& info)
{
    const Bytes b(raw);
    if (!b.has(0, 4))
        return;
    qsizetype pos = 4 + static_cast<qsizetype>(b.le(0, 4));
    if (!b.has(pos, 4))
        return;
    uint32 count = static_cast<uint32>(b.le(pos, 4));
    pos += 4;
    for (; count > 0 && b.has(pos, 4); --count) {
        const auto len = static_cast<qsizetype>(b.le(pos, 4));
        pos += 4;
        if (!b.has(pos, len))
            return;
        const QByteArrayView entry = b.view(pos, len);
        pos += len;
        const qsizetype eq = entry.indexOf('=');
        if (eq <= 0)
            continue;
        const QByteArray key = entry.first(eq).toByteArray().toUpper();
        const QString value = utf8Text(entry.sliced(eq + 1));
        if (key == "TITLE" && info.title.isEmpty())
            info.title = value;
        else if (key == "ARTIST" && info.author.isEmpty())
            info.author = value;
        else if (key == "ALBUM" && info.album.isEmpty())
            info.album = value;
    }
}

} // namespace

bool readFLACHeaders(const QString& filePath, MediaInfo& info)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    qint64 pos = id3v2Size(f);   // some taggers put one in front
    if (readAt(f, pos, 4) != "fLaC")
        return false;
    pos += 4;

    uint32 sampleRate = 0;
    uint16 channels = 0;
    uint64 totalSamples = 0;
    for (int blocks = 0; blocks < 64; ++blocks) {
        const QByteArray head = readAt(f, pos, 4);
        if (head.size() < 4)
            break;
        const Bytes hb(head);
        const uint8 type = hb.u8(0) & 0x7F;
        const auto len = static_cast<qint64>(hb.be(1, 3));
        pos += 4;
        if (type == 0 && len >= 18) {
            const QByteArray si = readAt(f, pos, 18);
            const Bytes b(si);
            const uint64 packed = b.be(10, 8);
            sampleRate = static_cast<uint32>(packed >> 44);
            channels = static_cast<uint16>(((packed >> 41) & 7) + 1);
            info.audio.bitsPerSample = static_cast<uint16>(((packed >> 36) & 31) + 1);
            totalSamples = packed & 0xFFFFFFFFFull;
        } else if (type == 4) {
            readVorbisComments(readAt(f, pos, std::min(len, kMaxTagBytes)), info);
        }
        pos += len;
        if (hb.u8(0) & 0x80)
            break;
    }
    if (sampleRate == 0)
        return false;

    const double lengthSec = static_cast<double>(totalSamples) / sampleRate;
    setAudioOnly(info, QStringLiteral("FLAC"), QStringLiteral("flac"), sampleRate, channels,
                 lengthSec, overallBitsPerSec(static_cast<uint64>(f.size()), lengthSec));
    return true;
}

// ===================================================================
// Ogg (Vorbis / Opus)
// ===================================================================

bool readOggHeaders(const QString& filePath, MediaInfo& info)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QByteArray head = readAt(f, 0, kMaxTagBytes);
    const Bytes b(head);
    if (!b.startsWith(0, "OggS"))
        return false;

    bool found = false, opus = false, commentsDone = false;
    uint32 serial = 0, sampleRate = 0, nominal = 0, preSkip = 0;
    uint16 channels = 0;
    QByteArray packet;       // the audio stream's packet being assembled
    int packetNo = 0;

    qsizetype pos = 0;
    for (int pages = 0; pages < 256 && b.startsWith(pos, "OggS") && b.has(pos, 27); ++pages) {
        const uint32 pageSerial = static_cast<uint32>(b.le(pos + 14, 4));
        const int segments = b.u8(pos + 26);
        if (!b.has(pos + 27, segments))
            break;
        qsizetype body = pos + 27 + segments;

        for (int s = 0; s < segments; ++s) {
            const int len = b.u8(pos + 27 + s);
            if (!b.has(body, len))
                break;
            const QByteArrayView seg = b.view(body, len);
            body += len;

            if (!found) {
                // First packet of each stream names its codec.
                const Bytes p(seg);
                if (p.startsWith(0, "\x01vorbis") && p.has(0, 28)) {
                    channels = p.u8(11);
                    sampleRate = static_cast<uint32>(p.le(12, 4));
                    nominal = static_cast<uint32>(p.le(20, 4));
                    found = true;
                } else if (p.startsWith(0, "OpusHead") && p.has(0, 19)) {
                    channels = p.u8(9);
                    preSkip = static_cast<uint32>(p.le(10, 2));
                    sampleRate = 48000;   // Opus granules always count 48 kHz
                    found = opus = true;
                }
                if (found) {
                    serial = pageSerial;
                    packetNo = len < 255 ? 1 : 0;
                }
                continue;
            }
            if (pageSerial != serial || commentsDone)
                continue;
            if (packetNo == 0) {              // tail of an oversized id header
                packetNo = len < 255 ? 1 : 0;
                continue;
            }
            if (packet.size() + len <= kMaxTagBytes)
                packet.append(seg.data(), len);
            if (len < 255) {
                const Bytes p(packet);
                if (p.startsWith(0, "\x03vorbis"))
                    readVorbisComments(QByteArrayView(packet).sliced(7), info);
                else if (p.startsWith(0, "OpusTags"))
                    readVorbisComments(QByteArrayView(packet).sliced(8), info);
                commentsDone = true;
            }
        }
        if (found && commentsDone)
            break;
        pos = body;
    }
    if (!found || sampleRate == 0)
        return false;

    // Length: the granule position of the stream's last page.
    const qint64 tailLen = std::min<qint64>(f.size(), 128 * 1024);
    const QByteArray tail = readAt(f, f.size() - tailLen, tailLen);
    const Bytes t(tail);
    uint64 lastGranule = 0;
    for (qsizetype i = tail.size() - 27; i >= 0; --i) {
        if (!t.startsWith(i, "OggS") || t.le(i + 14, 4) != serial)
            continue;
        const uint64 granule = t.le(i + 6, 8);
        if (granule != ~uint64{0}) {
            lastGranule = granule;
            break;
        }
    }
    if (lastGranule > preSkip)
        lastGranule -= preSkip;
    const double lengthSec = static_cast<double>(lastGranule) / sampleRate;

    uint32 bitrate = (nominal > 0 && nominal < 2'000'000) ? nominal : 0;
    if (bitrate == 0)
        bitrate = overallBitsPerSec(static_cast<uint64>(f.size()), lengthSec);
    setAudioOnly(info, QStringLiteral("Ogg"), opus ? QStringLiteral("opus") : QStringLiteral("vorbis"),
                 sampleRate, channels, lengthSec, bitrate);
    return true;
}

// ===================================================================
// MP4 / M4A
// ===================================================================

namespace {

constexpr uint32 fcc(const char (&s)[5])
{
    return (static_cast<uint32>(static_cast<uint8>(s[0])) << 24) | (static_cast<uint32>(static_cast<uint8>(s[1])) << 16)
         | (static_cast<uint32>(static_cast<uint8>(s[2])) << 8) | static_cast<uint32>(static_cast<uint8>(s[3]));
}

QString mp4CodecName(uint32 format)
{
    switch (format) {
    case fcc("avc1"): case fcc("avc3"): return QStringLiteral("h264");
    case fcc("hvc1"): case fcc("hev1"): return QStringLiteral("hevc");
    case fcc("mp4v"):                   return QStringLiteral("mpeg4");
    case fcc("av01"):                   return QStringLiteral("av1");
    case fcc("vp09"):                   return QStringLiteral("vp9");
    case fcc("mp4a"):                   return QStringLiteral("aac");
    case fcc("alac"):                   return QStringLiteral("alac");
    case fcc("ac-3"):                   return QStringLiteral("ac3");
    case fcc("ec-3"):                   return QStringLiteral("eac3");
    case fcc("Opus"):                   return QStringLiteral("opus");
    case fcc("fLaC"):                   return QStringLiteral("flac");
    default: break;
    }
    QByteArray raw(4, '\0');
    for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>(format >> (24 - 8 * i));
        raw[i] = (c >= 0x20 && c < 0x7F) ? c : '?';
    }
    return QString::fromLatin1(raw).trimmed().toLower();
}

struct Mp4State {
    uint32 handler = 0;          // of the track being walked
    double movieLength = 0.0;
    std::vector<QByteArray> keys;   // QuickTime 'keys': ilst entries name them by index
};

void setMp4Text(MediaInfo& info, int which, const QString& text)
{
    if (text.isEmpty())
        return;
    QString& field = which == 0 ? info.title : which == 1 ? info.author : info.album;
    if (field.isEmpty())
        field = text;
}

/// Walk the boxes in @p box (a moov payload or a container below it).
void walkMp4(QByteArrayView box, MediaInfo& info, Mp4State& state, int depth)
{
    if (depth > 8)
        return;
    const Bytes b(box);
    qsizetype pos = 0;
    while (b.has(pos, 8)) {
        uint64 size = b.be(pos, 4);
        const uint32 type = static_cast<uint32>(b.be(pos + 4, 4));
        qsizetype headLen = 8;
        if (size == 1) {
            size = b.be(pos + 8, 8);
            headLen = 16;
        } else if (size == 0) {
            size = static_cast<uint64>(b.size() - pos);
        }
        if (size < static_cast<uint64>(headLen) || !b.has(pos, static_cast<qsizetype>(size)))
            return;
        const QByteArrayView body = b.view(pos + headLen, static_cast<qsizetype>(size) - headLen);
        const Bytes p(body);

        switch (type) {
        case fcc("trak"):
            state.handler = 0;
            walkMp4(body, info, state, depth + 1);
            break;
        case fcc("mdia"): case fcc("minf"): case fcc("stbl"): case fcc("udta"): case fcc("ilst"):
            walkMp4(body, info, state, depth + 1);
            break;
        case fcc("meta"):
            // A FullBox in MP4 (version + flags first), a plain container in QuickTime.
            if (p.be(4, 4) == fcc("hdlr"))
                walkMp4(body, info, state, depth + 1);
            else
                walkMp4(body.size() >= 4 ? body.sliced(4) : QByteArrayView(), info, state, depth + 1);
            break;
        case fcc("keys"): {
            // version/flags, count, then { size, namespace, name }.
            const uint64 count = std::min<uint64>(p.be(4, 4), 256);
            qsizetype at = 8;
            state.keys.clear();
            for (uint64 n = 0; n < count && p.has(at, 8); ++n) {
                const auto keySize = static_cast<qsizetype>(p.be(at, 4));
                if (keySize < 8 || !p.has(at, keySize))
                    break;
                state.keys.push_back(p.view(at + 8, keySize - 8).toByteArray());
                at += keySize;
            }
            break;
        }
        case fcc("mvhd"): {
            const bool v1 = p.u8(0) == 1;
            const uint64 scale = p.be(v1 ? 20 : 12, 4);
            const uint64 duration = p.be(v1 ? 24 : 16, v1 ? 8 : 4);
            if (scale > 0)
                state.movieLength = static_cast<double>(duration) / static_cast<double>(scale);
            break;
        }
        case fcc("hdlr"):
            if (state.handler == 0)
                state.handler = static_cast<uint32>(p.be(8, 4));
            break;
        case fcc("stsd"):
            if (p.has(8, 16)) {
                const uint32 format = static_cast<uint32>(p.be(12, 4));
                const qsizetype entry = 16;   // past the entry's size + format
                if (state.handler == fcc("vide") && info.videoStreamCount++ == 0) {
                    info.video.codecName = mp4CodecName(format);
                    info.video.width = static_cast<uint32>(p.be(entry + 24, 2));
                    info.video.height = static_cast<uint32>(p.be(entry + 26, 2));
                } else if (state.handler == fcc("soun") && info.audioStreamCount++ == 0) {
                    info.audio.codecName = mp4CodecName(format);
                    info.audio.channels = static_cast<uint16>(p.be(entry + 16, 2));
                    info.audio.sampleRate = static_cast<uint32>(p.be(entry + 24, 2));
                }
            }
            break;
        case fcc("\xA9nam"): case fcc("\xA9" "ART"): case fcc("\xA9" "alb"): {
            const int which = type == fcc("\xA9nam") ? 0 : type == fcc("\xA9" "ART") ? 1 : 2;
            if (p.has(0, 16) && p.be(4, 4) == fcc("data")) {
                // iTunes: one 'data' box — version/flags, locale, then the text.
                const auto dataSize = static_cast<qsizetype>(p.be(0, 4));
                if (dataSize >= 16 && p.has(0, dataSize))
                    setMp4Text(info, which, utf8Text(p.view(16, dataSize - 16)));
            } else if (p.has(0, 4)) {
                // QuickTime user data: 16-bit length, 16-bit language, the text.
                const auto textLen = static_cast<qsizetype>(p.be(0, 2));
                if (textLen > 0 && p.has(4, textLen))
                    setMp4Text(info, which, utf8Text(p.view(4, textLen)));
            }
            break;
        }
        default:
            // An 'ilst' entry named through 'keys' (1-based index as its type).
            if (type >= 1 && type <= state.keys.size() && p.has(0, 16) && p.be(4, 4) == fcc("data")) {
                // Apple's reverse-DNS names, or the bare ones some muxers write.
                QByteArray key = state.keys[type - 1];
                if (key.startsWith("com.apple.quicktime."))
                    key.remove(0, 20);
                const int which = key == "title" ? 0 : key == "artist" ? 1 : key == "album" ? 2 : -1;
                const auto dataSize = static_cast<qsizetype>(p.be(0, 4));
                if (which >= 0 && dataSize >= 16 && p.has(0, dataSize))
                    setMp4Text(info, which, utf8Text(p.view(16, dataSize - 16)));
            }
            break;
        }
        pos += static_cast<qsizetype>(size);
    }
}

} // namespace

bool readMP4Headers(const QString& filePath, MediaInfo& info)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    // Top level: 'ftyp' first — or, in a QuickTime file, any of its usual openers —
    // then find 'moov' by stepping over the other boxes.
    qint64 pos = 0;
    QByteArray moov;
    bool quickTime = false;
    for (int boxes = 0; boxes < 1024; ++boxes) {
        const QByteArray head = readAt(f, pos, 16);
        const Bytes b(head);
        if (head.size() < 8)
            break;
        uint64 size = b.be(0, 4);
        const uint32 type = static_cast<uint32>(b.be(4, 4));
        qint64 headLen = 8;
        if (size == 1 && head.size() >= 16) {
            size = b.be(8, 8);
            headLen = 16;
        } else if (size == 0) {
            size = static_cast<uint64>(f.size() - pos);
        }
        if (boxes == 0) {
            if (type == fcc("ftyp")) {
                quickTime = b.be(8, 4) == fcc("qt  ");
            } else if (type == fcc("moov") || type == fcc("wide") || type == fcc("free")
                       || type == fcc("skip") || type == fcc("mdat") || type == fcc("pnot")) {
                quickTime = true;
            } else {
                return false;
            }
        }
        if (size < static_cast<uint64>(headLen) || size > static_cast<uint64>(f.size() - pos))
            break;
        if (type == fcc("moov")) {
            if (size - static_cast<uint64>(headLen) > static_cast<uint64>(kMaxMoovBytes))
                return false;
            moov = readAt(f, pos + headLen, static_cast<qint64>(size) - headLen);
            break;
        }
        pos += static_cast<qint64>(size);
    }
    if (moov.isEmpty())
        return false;

    Mp4State state;
    walkMp4(moov, info, state, 0);
    if (info.videoStreamCount == 0 && info.audioStreamCount == 0)
        return false;

    info.fileFormat = quickTime ? QStringLiteral("QuickTime") : QStringLiteral("MPEG-4");
    info.lengthSec = state.movieLength;
    // No per-stream rate in the headers read here: the file's own average.
    const uint32 bitrate = overallBitsPerSec(static_cast<uint64>(f.size()), state.movieLength);
    if (info.videoStreamCount > 0) {
        info.video.lengthSec = state.movieLength;
        info.video.bitRate = bitrate;
    } else {
        info.audio.lengthSec = state.movieLength;
        info.audio.avgBytesPerSec = bitrate / 8;
    }
    return true;
}

// ===================================================================
// Matroska / WebM
// ===================================================================

namespace {

struct EbmlHead {
    uint32 id = 0;
    uint64 size = 0;
    int headLen = 0;
    bool unknownSize = false;
};

bool readEbmlHead(const Bytes& b, qsizetype pos, EbmlHead& out)
{
    const uint8 first = b.u8(pos);
    const int idLen = std::countl_zero(first) + 1;
    if (first == 0 || idLen > 4 || !b.has(pos, idLen + 1))
        return false;
    out.id = static_cast<uint32>(b.be(pos, idLen));

    const uint8 sizeFirst = b.u8(pos + idLen);
    const int sizeLen = std::countl_zero(sizeFirst) + 1;
    if (sizeFirst == 0 || sizeLen > 8 || !b.has(pos + idLen, sizeLen))
        return false;
    const uint64 mask = (uint64{1} << (7 * sizeLen)) - 1;
    out.size = b.be(pos + idLen, sizeLen) & mask;
    out.unknownSize = out.size == mask;
    out.headLen = idLen + sizeLen;
    return true;
}

double ebmlFloat(const Bytes& b, qsizetype pos, uint64 size)
{
    if (size == 4)
        return static_cast<double>(std::bit_cast<float>(static_cast<uint32>(b.be(pos, 4))));
    if (size == 8)
        return std::bit_cast<double>(b.be(pos, 8));
    return 0.0;
}

QString matroskaCodecName(const QByteArray& id)
{
    static const struct { const char* prefix; const char* name; } kMap[] = {
        {"V_MPEG4/ISO/AVC", "h264"}, {"V_MPEGH/ISO/HEVC", "hevc"}, {"V_MPEG4/ISO", "mpeg4"},
        {"V_MPEG4/MS", "msmpeg4"},   {"V_AV1", "av1"},             {"V_VP9", "vp9"},
        {"V_VP8", "vp8"},            {"V_MPEG2", "mpeg2"},         {"V_MPEG1", "mpeg1"},
        {"V_THEORA", "theora"},      {"A_AAC", "aac"},             {"A_EAC3", "eac3"},
        {"A_AC3", "ac3"},            {"A_DTS", "dts"},             {"A_OPUS", "opus"},
        {"A_VORBIS", "vorbis"},      {"A_FLAC", "flac"},           {"A_MPEG/L3", "mp3"},
        {"A_MPEG/L2", "mp2"},        {"A_TRUEHD", "truehd"},       {"A_PCM", "pcm"},
    };
    for (const auto& [prefix, name] : kMap) {
        if (id.startsWith(prefix))
            return QString::fromLatin1(name);
    }
    return QString::fromLatin1(id.mid(2)).toLower().left(32);
}

struct MatroskaState {
    uint64 timecodeScale = 1'000'000;
    double duration = 0.0;
};

void walkMatroskaTrack(QByteArrayView entry, MediaInfo& info)
{
    const Bytes b(entry);
    uint64 trackType = 0;
    QByteArray codec;
    uint32 width = 0, height = 0, sampleRate = 0;
    uint16 channels = 0;

    const auto walk = [&](auto&& self, qsizetype from, qsizetype to, int depth) -> void {
        qsizetype pos = from;
        EbmlHead h;
        while (pos < to && readEbmlHead(b, pos, h)) {
            const qsizetype body = pos + h.headLen;
            if (h.unknownSize || !b.has(body, static_cast<qsizetype>(h.size)) || body + static_cast<qsizetype>(h.size) > to)
                return;
            switch (h.id) {
            case 0x83: trackType = b.be(body, static_cast<int>(std::min<uint64>(h.size, 8))); break;
            case 0x86: codec = b.view(body, static_cast<qsizetype>(std::min<uint64>(h.size, 64))).toByteArray(); break;
            case 0xB0: width = static_cast<uint32>(b.be(body, static_cast<int>(std::min<uint64>(h.size, 4)))); break;
            case 0xBA: height = static_cast<uint32>(b.be(body, static_cast<int>(std::min<uint64>(h.size, 4)))); break;
            case 0xB5: sampleRate = static_cast<uint32>(std::clamp(ebmlFloat(b, body, h.size), 0.0, 1e6)); break;
            case 0x9F: channels = static_cast<uint16>(b.be(body, static_cast<int>(std::min<uint64>(h.size, 2)))); break;
            case 0xE0: case 0xE1:   // Video / Audio
                if (depth < 2)
                    self(self, body, body + static_cast<qsizetype>(h.size), depth + 1);
                break;
            default: break;
            }
            pos = body + static_cast<qsizetype>(h.size);
        }
    };
    walk(walk, 0, b.size(), 0);

    if (trackType == 1 && info.videoStreamCount++ == 0) {
        info.video.codecName = matroskaCodecName(codec);
        info.video.width = width;
        info.video.height = height;
    } else if (trackType == 2 && info.audioStreamCount++ == 0) {
        info.audio.codecName = matroskaCodecName(codec);
        info.audio.sampleRate = sampleRate;
        info.audio.channels = channels;
    }
}

/// Info and Tracks payloads: flat lists of the elements wanted.
void walkMatroskaLevel1(uint32 id, QByteArrayView payload, MediaInfo& info, MatroskaState& state)
{
    const Bytes b(payload);
    qsizetype pos = 0;
    EbmlHead h;
    while (readEbmlHead(b, pos, h)) {
        const qsizetype body = pos + h.headLen;
        if (h.unknownSize || !b.has(body, static_cast<qsizetype>(h.size)))
            return;
        if (id == 0x1549A966) {
            if (h.id == 0x2AD7B1)
                state.timecodeScale = b.be(body, static_cast<int>(std::min<uint64>(h.size, 8)));
            else if (h.id == 0x4489)
                state.duration = ebmlFloat(b, body, h.size);
            else if (h.id == 0x7BA9)
                info.title = utf8Text(b.view(body, static_cast<qsizetype>(h.size)));
        } else if (h.id == 0xAE) {
            walkMatroskaTrack(b.view(body, static_cast<qsizetype>(h.size)), info);
        }
        pos = body + static_cast<qsizetype>(h.size);
    }
}

/// Tags payload: TITLE / ARTIST / ALBUM of the file itself fill what Info left empty.
void readMatroskaTags(QByteArrayView payload, MediaInfo& info)
{
    const Bytes b(payload);
    const auto walk = [&](auto&& self, qsizetype from, qsizetype to, int depth, bool& forTrack,
                          QByteArray& name, QString& value) -> void {
        qsizetype pos = from;
        EbmlHead h;
        while (pos < to && readEbmlHead(b, pos, h)) {
            const qsizetype body = pos + h.headLen;
            const auto size = static_cast<qsizetype>(h.size);
            if (h.unknownSize || !b.has(body, size) || body + size > to)
                return;
            switch (h.id) {
            case 0x7373: {   // Tag
                bool track = false;
                QByteArray n;
                QString v;
                if (depth < 4)
                    self(self, body, body + size, depth + 1, track, n, v);
                break;
            }
            case 0x63C0:     // Targets
                if (depth < 4)
                    self(self, body, body + size, depth + 1, forTrack, name, value);
                break;
            case 0x63C5:     // TagTrackUID: about one stream, not the file
                if (b.be(body, static_cast<int>(std::min<qsizetype>(size, 8))) != 0)
                    forTrack = true;
                break;
            case 0x67C8: {   // SimpleTag
                QByteArray n;
                QString v;
                bool unused = false;
                if (depth < 4)
                    self(self, body, body + size, depth + 1, unused, n, v);
                if (forTrack || v.isEmpty())
                    break;
                n = n.toUpper();
                QString* field = n == "TITLE" ? &info.title : n == "ARTIST" ? &info.author
                               : n == "ALBUM" ? &info.album : nullptr;
                if (field && field->isEmpty())
                    *field = v;
                break;
            }
            case 0x45A3: name = b.view(body, std::min<qsizetype>(size, 64)).toByteArray(); break;
            case 0x4487: value = utf8Text(b.view(body, size)); break;
            default: break;
            }
            pos = body + size;
        }
    };
    bool forTrack = false;
    QByteArray name;
    QString value;
    walk(walk, 0, b.size(), 0, forTrack, name, value);
}

/// SeekHead payload: where the Tags element is, relative to the segment's data. -1 = not listed.
qint64 matroskaTagsPosition(QByteArrayView payload)
{
    const Bytes b(payload);
    qsizetype pos = 0;
    EbmlHead h;
    while (readEbmlHead(b, pos, h)) {
        const qsizetype body = pos + h.headLen;
        const auto size = static_cast<qsizetype>(h.size);
        if (h.unknownSize || !b.has(body, size))
            break;
        if (h.id == 0x4DBB) {   // Seek
            uint64 id = 0, position = 0;
            qsizetype at = body;
            EbmlHead e;
            while (at < body + size && readEbmlHead(b, at, e)) {
                const qsizetype eBody = at + e.headLen;
                const auto eSize = static_cast<qsizetype>(e.size);
                if (e.unknownSize || eBody + eSize > body + size)
                    break;
                if (e.id == 0x53AB)
                    id = b.be(eBody, static_cast<int>(std::min<qsizetype>(eSize, 8)));
                else if (e.id == 0x53AC)
                    position = b.be(eBody, static_cast<int>(std::min<qsizetype>(eSize, 8)));
                at = eBody + eSize;
            }
            if (id == 0x1254C367 && position <= static_cast<uint64>(std::numeric_limits<qint64>::max()))
                return static_cast<qint64>(position);
        }
        pos = body + size;
    }
    return -1;
}

} // namespace

bool readMatroskaHeaders(const QString& filePath, MediaInfo& info)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const auto headAt = [&](qint64 pos, EbmlHead& h) {
        const QByteArray raw = readAt(f, pos, 12);
        return readEbmlHead(Bytes(raw), 0, h);
    };

    EbmlHead h;
    if (!headAt(0, h) || h.id != 0x1A45DFA3 || h.unknownSize)
        return false;
    const QByteArray ebml = readAt(f, h.headLen, static_cast<qint64>(std::min<uint64>(h.size, 4096)));
    const bool webm = ebml.contains("webm");
    qint64 pos = h.headLen + static_cast<qint64>(h.size);

    if (!headAt(pos, h) || h.id != 0x18538067)
        return false;
    pos += h.headLen;
    const qint64 segmentStart = pos;
    const qint64 segmentEnd = h.unknownSize ? f.size()
                                            : std::min<qint64>(f.size(), pos + static_cast<qint64>(h.size));

    MatroskaState state;
    bool haveInfo = false, haveTracks = false, haveTags = false;
    qint64 tagsAt = -1;
    for (int elements = 0; elements < 4096 && pos < segmentEnd && !(haveInfo && haveTracks); ++elements) {
        if (!headAt(pos, h) || h.unknownSize)
            break;
        const qint64 body = pos + h.headLen;
        if (h.size > static_cast<uint64>(segmentEnd - body))
            break;
        if (h.id == 0x1549A966 || h.id == 0x1654AE6B) {
            if (h.size > static_cast<uint64>(kMaxMoovBytes))
                break;
            walkMatroskaLevel1(h.id, readAt(f, body, static_cast<qint64>(h.size)), info, state);
            (h.id == 0x1549A966 ? haveInfo : haveTracks) = true;
        } else if (h.id == 0x114D9B74 && h.size <= static_cast<uint64>(kMaxTagBytes)) {
            if (const qint64 at = matroskaTagsPosition(readAt(f, body, static_cast<qint64>(h.size))); at >= 0)
                tagsAt = at;
        } else if (h.id == 0x1254C367 && h.size <= static_cast<uint64>(kMaxTagBytes)) {
            readMatroskaTags(readAt(f, body, static_cast<qint64>(h.size)), info);
            haveTags = true;
        }
        pos = body + static_cast<qint64>(h.size);
    }
    if (info.videoStreamCount == 0 && info.audioStreamCount == 0)
        return false;

    // Tags usually sit behind the clusters; the SeekHead says where, so nothing is scanned.
    if (!haveTags && tagsAt >= 0 && tagsAt < segmentEnd - segmentStart
        && headAt(segmentStart + tagsAt, h) && h.id == 0x1254C367 && !h.unknownSize
        && h.size <= static_cast<uint64>(kMaxTagBytes)
        && h.size <= static_cast<uint64>(segmentEnd - (segmentStart + tagsAt + h.headLen))) {
        readMatroskaTags(readAt(f, segmentStart + tagsAt + h.headLen, static_cast<qint64>(h.size)), info);
    }

    double lengthSec = state.duration * static_cast<double>(state.timecodeScale) / 1e9;
    if (!std::isfinite(lengthSec) || lengthSec < 0.0 || lengthSec > 1e7)
        lengthSec = 0.0;
    info.fileFormat = webm ? QStringLiteral("WebM") : QStringLiteral("Matroska");
    info.lengthSec = lengthSec;
    const uint32 bitrate = overallBitsPerSec(static_cast<uint64>(f.size()), lengthSec);
    if (info.videoStreamCount > 0) {
        info.video.lengthSec = lengthSec;
        info.video.bitRate = bitrate;
    } else {
        info.audio.lengthSec = lengthSec;
        info.audio.avgBytesPerSec = bitrate / 8;
    }
    return true;
}

// ===================================================================
// ASF (WMA / WMV)
// ===================================================================

namespace {

constexpr int kAsfHead = 24;   // object GUID + 64-bit size

bool asfGuid(const Bytes& b, qsizetype pos, const char* hex)
{
    return b.has(pos, 16) && b.view(pos, 16) == QByteArrayView(QByteArray::fromHex(hex));
}

QString utf16leText(QByteArrayView raw)
{
    QString text;
    const Bytes b(raw);
    const qsizetype chars = std::min<qsizetype>(raw.size() / 2, kMaxTextChars);
    text.reserve(chars);
    for (qsizetype i = 0; i < chars; ++i)
        text += QChar(static_cast<char16_t>(b.le(i * 2, 2)));
    return cleanText(text);
}

} // namespace

bool readASFHeaders(const QString& filePath, MediaInfo& info)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QByteArray top = readAt(f, 0, 30);
    const Bytes t(top);
    if (!asfGuid(t, 0, "3026B2758E66CF11A6D900AA0062CE6C"))
        return false;
    const uint64 headerSize = t.le(16, 8);
    if (headerSize < 30)
        return false;
    const QByteArray header = readAt(f, 30, static_cast<qint64>(std::min<uint64>(headerSize - 30, kMaxTagBytes)));
    const Bytes b(header);

    uint64 playDuration = 0, preroll = 0;
    uint32 maxBitrate = 0;
    int videoStream = 0, audioStream = 0;
    std::array<uint32, 128> streamBitrate{};

    qsizetype pos = 0;
    for (int objects = 0; objects < 1024 && b.has(pos, kAsfHead); ++objects) {
        const uint64 size = b.le(pos + 16, 8);
        if (size < kAsfHead || !b.has(pos, static_cast<qsizetype>(size)))
            break;
        const qsizetype body = pos + kAsfHead;
        const qsizetype bodyLen = static_cast<qsizetype>(size) - kAsfHead;
        const Bytes p(b.view(body, bodyLen));

        if (asfGuid(b, pos, "A1DCAB8C47A9CF118EE400C00C205365")) {            // File Properties
            playDuration = p.le(40, 8);   // 100 ns
            preroll = p.le(56, 8);        // ms
            maxBitrate = static_cast<uint32>(p.le(76, 4));
        } else if (asfGuid(b, pos, "9107DCB7B7A9CF118EE600C00C205365")) {     // Stream Properties
            const int number = static_cast<int>(p.le(48, 2) & 0x7F);
            const qsizetype spec = 54;
            if (asfGuid(p, 0, "409E69F84D5BCF11A8FD00805F5C442B") && p.has(spec, 16)) {
                if (info.audioStreamCount++ == 0) {       // WAVEFORMATEX
                    audioStream = number;
                    info.audio.formatTag = static_cast<uint16>(p.le(spec, 2));
                    info.audio.channels = static_cast<uint16>(p.le(spec + 2, 2));
                    info.audio.sampleRate = static_cast<uint32>(p.le(spec + 4, 4));
                    info.audio.avgBytesPerSec = static_cast<uint32>(p.le(spec + 8, 4));
                    info.audio.bitsPerSample = static_cast<uint16>(p.le(spec + 14, 2));
                }
            } else if (asfGuid(p, 0, "C0EF19BC4D5BCF11A8FD00805F5C442B") && p.has(spec, 11 + 20)) {
                if (info.videoStreamCount++ == 0) {       // size, then BITMAPINFOHEADER
                    videoStream = number;
                    info.video.width = static_cast<uint32>(p.le(spec, 4));
                    info.video.height = static_cast<uint32>(p.le(spec + 4, 4));
                    info.video.codecTag = static_cast<uint32>(p.le(spec + 11 + 16, 4));
                }
            }
        } else if (asfGuid(b, pos, "CE75F87B8D46D1118D82006097C9A2B2")) {     // Stream Bitrate Properties
            const uint64 count = std::min<uint64>(p.le(0, 2), 127);
            for (uint64 n = 0; n < count && p.has(2 + static_cast<qsizetype>(n) * 6, 6); ++n) {
                const qsizetype at = 2 + static_cast<qsizetype>(n) * 6;
                streamBitrate[p.le(at, 2) & 0x7F] = static_cast<uint32>(p.le(at + 2, 4));
            }
        } else if (asfGuid(b, pos, "3326B2758E66CF11A6D900AA0062CE6C")) {     // Content Description
            const auto titleLen = static_cast<qsizetype>(p.le(0, 2));
            const auto authorLen = static_cast<qsizetype>(p.le(2, 2));
            if (p.has(10, titleLen + authorLen)) {
                info.title = utf16leText(p.view(10, titleLen));
                info.author = utf16leText(p.view(10 + titleLen, authorLen));
            }
        } else if (asfGuid(b, pos, "40A4D0D207E3D21197F000A0C95EA850")) {     // Extended Content Description
            const uint64 count = std::min<uint64>(p.le(0, 2), 512);
            qsizetype at = 2;
            for (uint64 n = 0; n < count && p.has(at, 2); ++n) {
                const auto nameLen = static_cast<qsizetype>(p.le(at, 2));
                if (!p.has(at + 2, nameLen + 4))
                    break;
                const QString name = utf16leText(p.view(at + 2, nameLen));
                const uint64 valueType = p.le(at + 2 + nameLen, 2);
                const auto valueLen = static_cast<qsizetype>(p.le(at + 4 + nameLen, 2));
                if (!p.has(at + 6 + nameLen, valueLen))
                    break;
                if (valueType == 0 && name == QLatin1StringView("WM/AlbumTitle"))
                    info.album = utf16leText(p.view(at + 6 + nameLen, valueLen));
                at += 6 + nameLen + valueLen;
            }
        }
        pos += static_cast<qsizetype>(size);
    }
    if (info.videoStreamCount == 0 && info.audioStreamCount == 0)
        return false;

    // The play duration includes the preroll the player buffers before it starts.
    double lengthSec = static_cast<double>(playDuration) / 1e7 - static_cast<double>(preroll) / 1e3;
    if (!std::isfinite(lengthSec) || lengthSec < 0.0 || lengthSec > 1e7)
        lengthSec = 0.0;

    info.fileFormat = QStringLiteral("Windows Media");
    info.lengthSec = lengthSec;
    if (info.audioStreamCount > 0) {
        info.audio.lengthSec = lengthSec;
        if (info.audio.avgBytesPerSec == 0)
            info.audio.avgBytesPerSec = streamBitrate[static_cast<size_t>(audioStream)] / 8;
    }
    if (info.videoStreamCount > 0) {
        info.video.lengthSec = lengthSec;
        info.video.bitRate = streamBitrate[static_cast<size_t>(videoStream)];
        if (info.video.bitRate == 0) {
            // No per-stream figure: what the file's peak leaves after the audio.
            const uint32 audioBits = info.audio.avgBytesPerSec * 8;
            info.video.bitRate = maxBitrate > audioBits ? maxBitrate - audioBits
                               : overallBitsPerSec(static_cast<uint64>(f.size()), lengthSec);
        }
    }
    return true;
}

} // namespace eMule
