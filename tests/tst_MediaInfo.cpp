#include <QTest>
#include <QTemporaryFile>
#include <QDir>
#include <QFileInfo>

#include <cstring>
#include <utility>

#include "TestHelpers.h"
#include "media/MediaInfo.h"
#include "utils/OtherFunctions.h"

using namespace eMule;
using namespace Qt::StringLiterals;

// Helper: write binary data to a temporary file and return its path.
static QString writeTempFile(const QByteArray& data, const QString& suffix = {})
{
    auto* tmp = new QTemporaryFile(QDir::tempPath() + QStringLiteral("/tst_mediainfo_XXXXXX") + suffix);
    tmp->setAutoRemove(true);
    if (!tmp->open()) {
        delete tmp;
        return {};
    }
    tmp->write(data);
    tmp->flush();
    // Keep the file alive via static storage (tests are short-lived)
    static QList<QTemporaryFile*> s_files;
    s_files.append(tmp);
    return tmp->fileName();
}

// Helper: build a little-endian uint32
static void putLE32(QByteArray& buf, uint32 val)
{
    buf.append(static_cast<char>(val & 0xFF));
    buf.append(static_cast<char>((val >> 8) & 0xFF));
    buf.append(static_cast<char>((val >> 16) & 0xFF));
    buf.append(static_cast<char>((val >> 24) & 0xFF));
}

static void putLE16(QByteArray& buf, uint16 val)
{
    buf.append(static_cast<char>(val & 0xFF));
    buf.append(static_cast<char>((val >> 8) & 0xFF));
}

static constexpr uint32 fourCC(char a, char b, char c, char d)
{
    return static_cast<uint32>(static_cast<uint8>(a))
         | (static_cast<uint32>(static_cast<uint8>(b)) << 8)
         | (static_cast<uint32>(static_cast<uint8>(c)) << 16)
         | (static_cast<uint32>(static_cast<uint8>(d)) << 24);
}

// Helper: build a minimal synthetic AVI file in memory
static QByteArray buildMinimalAVI()
{
    // Construct: RIFF/AVI > LIST/hdrl > LIST/strl > [strh + strf] > LIST/movi
    QByteArray strhData;
    // AVIStreamHeaderFixed: fccType=vids, fccHandler=0, flags=0, priority=0, language=0,
    // initialFrames=0, scale=1, rate=25, start=0, length=100, rest=0
    putLE32(strhData, fourCC('v','i','d','s')); // fccType
    putLE32(strhData, 0);                       // fccHandler
    putLE32(strhData, 0);                       // flags
    putLE16(strhData, 0);                       // priority
    putLE16(strhData, 0);                       // language
    putLE32(strhData, 0);                       // initialFrames
    putLE32(strhData, 1);                       // dwScale
    putLE32(strhData, 25);                      // dwRate (25 fps)
    putLE32(strhData, 0);                       // dwStart
    putLE32(strhData, 100);                     // dwLength (100 frames = 4 sec)
    putLE32(strhData, 0);                       // suggestedBufferSize
    putLE32(strhData, 0);                       // quality
    putLE32(strhData, 0);                       // sampleSize
    putLE16(strhData, 0); putLE16(strhData, 0); // rcFrame (left, top)
    putLE16(strhData, 320); putLE16(strhData, 240); // rcFrame (right, bottom)

    // strh chunk: 'strh' + size + data
    QByteArray strhChunk;
    putLE32(strhChunk, fourCC('s','t','r','h'));
    putLE32(strhChunk, static_cast<uint32>(strhData.size()));
    strhChunk.append(strhData);

    // strf chunk: BitmapInfoHeader
    QByteArray bmi;
    putLE32(bmi, 40);                           // biSize
    putLE32(bmi, 320);                          // biWidth (as int32)
    putLE32(bmi, 240);                          // biHeight (as int32)
    putLE16(bmi, 1);                            // biPlanes
    putLE16(bmi, 24);                           // biBitCount
    putLE32(bmi, fourCC('D','I','V','X'));       // biCompression
    putLE32(bmi, 320*240*3);                    // biSizeImage
    putLE32(bmi, 0); putLE32(bmi, 0);           // pels per meter
    putLE32(bmi, 0); putLE32(bmi, 0);           // colors

    QByteArray strfChunk;
    putLE32(strfChunk, fourCC('s','t','r','f'));
    putLE32(strfChunk, static_cast<uint32>(bmi.size()));
    strfChunk.append(bmi);

    // LIST/strl
    QByteArray strlPayload;
    strlPayload.append(strhChunk);
    strlPayload.append(strfChunk);

    QByteArray strlList;
    putLE32(strlList, fourCC('L','I','S','T'));
    putLE32(strlList, static_cast<uint32>(4 + strlPayload.size()));
    putLE32(strlList, fourCC('s','t','r','l'));
    strlList.append(strlPayload);

    // LIST/hdrl
    QByteArray hdrlList;
    putLE32(hdrlList, fourCC('L','I','S','T'));
    putLE32(hdrlList, static_cast<uint32>(4 + strlList.size()));
    putLE32(hdrlList, fourCC('h','d','r','l'));
    hdrlList.append(strlList);

    // LIST/movi (empty)
    QByteArray moviList;
    putLE32(moviList, fourCC('L','I','S','T'));
    putLE32(moviList, 4);
    putLE32(moviList, fourCC('m','o','v','i'));

    // RIFF/AVI
    QByteArray payload;
    putLE32(payload, fourCC('A','V','I',' '));
    payload.append(hdrlList);
    payload.append(moviList);

    QByteArray riff;
    putLE32(riff, fourCC('R','I','F','F'));
    putLE32(riff, static_cast<uint32>(payload.size()));
    riff.append(payload);

    return riff;
}

// ---------------------------------------------------------------------------
// Synthetic MP3 / FLAC / Ogg / MP4 / Matroska files
// ---------------------------------------------------------------------------

static QByteArray be(uint64 value, int bytes)
{
    QByteArray out;
    for (int i = bytes - 1; i >= 0; --i)
        out.append(static_cast<char>(value >> (8 * i)));
    return out;
}

static QByteArray le(uint64 value, int bytes)
{
    QByteArray out;
    for (int i = 0; i < bytes; ++i)
        out.append(static_cast<char>(value >> (8 * i)));
    return out;
}

static QByteArray id3Frame(const char* id, const QString& text)
{
    QByteArray body("\x03", 1);   // UTF-8
    body += text.toUtf8();
    return QByteArray(id) + be(static_cast<uint64>(body.size()), 4) + QByteArray(2, '\0') + body;
}

/// ID3v2.3 tag + MPEG-1 Layer 3 frames, 128 kbit/s 44.1 kHz stereo (417 bytes each).
static QByteArray buildMP3(int frames, uint32 xingFrames = 0)
{
    const QByteArray texts = id3Frame("TIT2", u"Song"_s) + id3Frame("TPE1", u"Band"_s)
                           + id3Frame("TALB", u"Record"_s);
    QByteArray out("ID3\x03\x00\x00", 6);
    const auto n = static_cast<uint32>(texts.size());
    out += QByteArray::fromRawData("\0", 1) + QByteArray(1, static_cast<char>((n >> 14) & 0x7F))
         + QByteArray(1, static_cast<char>((n >> 7) & 0x7F)) + QByteArray(1, static_cast<char>(n & 0x7F));
    out += texts;
    for (int i = 0; i < frames; ++i) {
        QByteArray frame = be(0xFFFB9000u, 4) + QByteArray(413, '\0');
        if (i == 0 && xingFrames > 0) {
            const QByteArray xing = QByteArray("Xing") + be(1, 4) + be(xingFrames, 4);
            std::memcpy(frame.data() + 4 + 32, xing.constData(), static_cast<size_t>(xing.size()));
        }
        out += frame;
    }
    return out;
}

static QByteArray vorbisComments(const QStringList& entries)
{
    QByteArray out = le(4, 4) + "test" + le(static_cast<uint64>(entries.size()), 4);
    for (const QString& e : entries)
        out += le(static_cast<uint64>(e.toUtf8().size()), 4) + e.toUtf8();
    return out;
}

/// 44.1 kHz stereo 16 bit, ten seconds.
static QByteArray buildFLAC()
{
    QByteArray info = QByteArray(10, '\0');
    info += be((uint64{44100} << 44) | (uint64{1} << 41) | (uint64{15} << 36) | 441000, 8);
    info += QByteArray(16, '\0');
    const QByteArray comments = vorbisComments({u"TITLE=Piece"_s, u"artist=Player"_s, u"ALBUM=Disc"_s});
    return QByteArray("fLaC") + be(0x00, 1) + be(34, 3) + info
         + be(0x84, 1) + be(static_cast<uint64>(comments.size()), 3) + comments
         + QByteArray(2000, '\x55');
}

static QByteArray oggPage(uint8 flags, uint64 granule, uint32 seq, const QByteArray& packet)
{
    return QByteArray("OggS") + be(0, 1) + be(flags, 1) + le(granule, 8) + le(0xABCD, 4)
         + le(seq, 4) + le(0, 4) + be(1, 1) + be(static_cast<uint64>(packet.size()), 1) + packet;
}

/// Ogg Vorbis, 44.1 kHz stereo, nominal 160 kbit/s, ten seconds.
static QByteArray buildOggVorbis()
{
    const QByteArray id = QByteArray("\x01vorbis") + le(0, 4) + be(2, 1) + le(44100, 4)
                        + le(0, 4) + le(160000, 4) + le(0, 4) + be(0xB8, 1) + be(1, 1);
    const QByteArray comments = QByteArray("\x03vorbis")
                              + vorbisComments({u"TITLE=Tune"_s, u"ARTIST=Singer"_s}) + be(1, 1);
    return oggPage(2, 0, 0, id) + oggPage(0, 0, 1, comments)
         + oggPage(0, 4410, 2, QByteArray(200, '\x11')) + oggPage(4, 441000, 3, QByteArray(200, '\x22'));
}

static QByteArray box(const char* type, const QByteArray& body)
{
    return be(static_cast<uint64>(body.size() + 8), 4) + QByteArray(type, 4) + body;
}

/// H.264 1280x720 video track, thirty seconds, with an iTunes title.
static QByteArray buildMP4()
{
    const QByteArray mvhd = box("mvhd", be(0, 4) + be(0, 4) + be(0, 4) + be(1000, 4) + be(30000, 4)
                                            + QByteArray(80, '\0'));
    const QByteArray avc1 = QByteArray(6, '\0') + be(1, 2) + QByteArray(16, '\0') + be(1280, 2)
                          + be(720, 2) + QByteArray(50, '\0');
    const QByteArray stsd = box("stsd", be(0, 4) + be(1, 4) + box("avc1", avc1));
    const QByteArray hdlr = box("hdlr", be(0, 4) + be(0, 4) + QByteArray("vide") + QByteArray(13, '\0'));
    const QByteArray trak = box("trak", box("mdia", hdlr + box("minf", box("stbl", stsd))));
    const QByteArray title = box("\xA9nam", box("data", be(1, 4) + be(0, 4) + QByteArray("Film")));
    const QByteArray udta = box("udta", box("meta", be(0, 4) + box("ilst", title)));
    return box("ftyp", QByteArray("isom") + be(0, 4) + QByteArray("isom"))
         + box("moov", mvhd + trak + udta) + box("mdat", QByteArray(3000, '\x33'));
}

static QByteArray ebml(uint32 id, const QByteArray& body)
{
    int idLen = 1;
    while (idLen < 4 && (id >> (8 * idLen)) != 0)
        ++idLen;
    // Two-byte size field keeps every element below 16 KiB simple.
    return be(id, idLen) + be(0x4000u | static_cast<uint32>(body.size()), 2) + body;
}

/// H.264 1920x1080 video track, sixty seconds.
static QByteArray buildMatroska()
{
    QByteArray duration(8, '\0');
    const double ms = 60000.0;
    uint64 bits = 0;
    std::memcpy(&bits, &ms, 8);
    duration = be(bits, 8);

    const QByteArray info = ebml(0x2AD7B1, be(1000000, 3)) + ebml(0x4489, duration)
                          + ebml(0x7BA9, QByteArray("Clip"));
    const QByteArray video = ebml(0xB0, be(1920, 2)) + ebml(0xBA, be(1080, 2));
    const QByteArray track = ebml(0x83, be(1, 1)) + ebml(0x86, QByteArray("V_MPEG4/ISO/AVC"))
                           + ebml(0xE0, video);
    const QByteArray segment = ebml(0x1549A966, info) + ebml(0x1654AE6B, ebml(0xAE, track))
                             + ebml(0x1F43B675, QByteArray(2000, '\x44'));
    return ebml(0x1A45DFA3, ebml(0x4282, QByteArray("matroska"))) + ebml(0x18538067, segment);
}

/// The Matroska file above with a SeekHead in front and Tags behind the cluster.
static QByteArray buildMatroskaWithTags()
{
    double ms = 60000.0;
    uint64 bits = 0;
    std::memcpy(&bits, &ms, 8);
    const QByteArray info = ebml(0x2AD7B1, be(1000000, 3)) + ebml(0x4489, be(bits, 8));
    const QByteArray track = ebml(0x83, be(1, 1)) + ebml(0x86, QByteArray("V_MPEG4/ISO/AVC"))
                           + ebml(0xE0, ebml(0xB0, be(1920, 2)) + ebml(0xBA, be(1080, 2)));
    const auto simple = [](const char* name, const char* value) -> QByteArray {
        return ebml(0x67C8, ebml(0x45A3, QByteArray(name)) + ebml(0x4487, QByteArray(value)));
    };
    const QByteArray tags =
        // About the file: these count.
        ebml(0x7373, ebml(0x63C0, QByteArray()) + simple("TITLE", "Tagged") + simple("ARTIST", "Band")
                         + simple("ALBUM", "Record"))
        // About track 7: not the file's title.
        + ebml(0x7373, ebml(0x63C0, ebml(0x63C5, be(7, 1))) + simple("TITLE", "Track name"));

    const QByteArray body = ebml(0x1549A966, info) + ebml(0x1654AE6B, ebml(0xAE, track))
                          + ebml(0x1F43B675, QByteArray(2000, '\x44'));
    // The SeekHead has a fixed size, so the Tags position can be written into it.
    const auto seekHead = [](uint64 position) -> QByteArray {
        return ebml(0x114D9B74, ebml(0x4DBB, ebml(0x53AB, be(0x1254C367, 4)) + ebml(0x53AC, be(position, 4))));
    };
    const uint64 tagsAt = static_cast<uint64>(seekHead(0).size() + body.size());
    const QByteArray segment = seekHead(tagsAt) + body + ebml(0x1254C367, tags);
    return ebml(0x1A45DFA3, ebml(0x4282, QByteArray("matroska"))) + ebml(0x18538067, segment);
}

/// A QuickTime movie: no 'ftyp', texts as user-data atoms or as 'keys' metadata.
static QByteArray buildMOV(bool keysForm)
{
    const QByteArray mvhd = box("mvhd", be(0, 4) + be(0, 4) + be(0, 4) + be(600, 4) + be(6000, 4)
                                            + QByteArray(80, '\0'));
    const QByteArray avc1 = QByteArray(6, '\0') + be(1, 2) + QByteArray(16, '\0') + be(640, 2)
                          + be(360, 2) + QByteArray(50, '\0');
    const QByteArray hdlr = box("hdlr", be(0, 4) + be(0, 4) + QByteArray("vide") + QByteArray(13, '\0'));
    const QByteArray trak = box("trak", box("mdia", hdlr + box("minf", box("stbl",
                                box("stsd", be(0, 4) + be(1, 4) + box("avc1", avc1))))));
    QByteArray texts;
    if (keysForm) {
        const auto key = [](const char* name) -> QByteArray {
            const QByteArray n(name);
            return be(static_cast<uint64>(n.size() + 8), 4) + QByteArray("mdta") + n;
        };
        const auto item = [](uint32 index, const char* value) -> QByteArray {
            const QByteArray body = box("data", be(1, 4) + be(0, 4) + QByteArray(value));
            return be(static_cast<uint64>(body.size() + 8), 4) + be(index, 4) + body;
        };
        // QuickTime 'meta' has no version / flags in front of its children.
        texts = box("meta", box("hdlr", be(0, 4) + be(0, 4) + QByteArray("mdta") + QByteArray(13, '\0'))
                            + box("keys", be(0, 4) + be(2, 4) + key("com.apple.quicktime.title")
                                              + key("com.apple.quicktime.artist"))
                            + box("ilst", item(1, "Movie") + item(2, "Director")));
    } else {
        const auto text = [](const char* type, const char* value) -> QByteArray {
            const QByteArray v(value);
            return box(type, be(static_cast<uint64>(v.size()), 2) + be(0, 2) + v);
        };
        texts = box("udta", text("\xA9nam", "Movie") + text("\xA9" "ART", "Director"));
    }
    return box("wide", QByteArray()) + box("mdat", QByteArray(3000, '\x33')) + box("moov", mvhd + trak + texts);
}

static QByteArray asfObject(const char* guidHex, const QByteArray& body)
{
    return QByteArray::fromHex(guidHex) + le(static_cast<uint64>(body.size() + 24), 8) + body;
}

static QByteArray utf16(const QString& text)
{
    QByteArray out;
    for (const QChar ch : text)
        out += le(ch.unicode(), 2);
    return out + le(0, 2);
}

/// WMA 2 audio, optionally with a WMV3 640x480 video stream; 42 s after a 3 s preroll.
static QByteArray buildASF(bool withVideo)
{
    const QByteArray fileProps = QByteArray(40, '\0') + le(uint64{45} * 10'000'000, 8) + le(0, 8)
                               + le(3000, 8) + le(2, 4) + le(3200, 4) + le(3200, 4) + le(1'128'000, 4);
    const auto stream = [](const char* typeGuid, int number, const QByteArray& specific) -> QByteArray {
        return asfObject("9107DCB7B7A9CF118EE600C00C205365",
                         QByteArray::fromHex(typeGuid) + QByteArray(16, '\0') + le(0, 8)
                             + le(static_cast<uint64>(specific.size()), 4) + le(0, 4) + le(static_cast<uint64>(number), 2)
                             + le(0, 4) + specific);
    };
    const QByteArray wave = le(0x0161, 2) + le(2, 2) + le(44100, 4) + le(16000, 4) + le(1024, 2) + le(16, 2) + le(0, 2);
    QByteArray objects = asfObject("A1DCAB8C47A9CF118EE400C00C205365", fileProps)
                       + stream("409E69F84D5BCF11A8FD00805F5C442B", 1, wave);
    int count = 4;
    if (withVideo) {
        const QByteArray bitmap = le(40, 4) + le(640, 4) + le(480, 4) + le(1, 2) + le(24, 2) + QByteArray("WMV3")
                                + QByteArray(20, '\0');
        objects += stream("C0EF19BC4D5BCF11A8FD00805F5C442B", 2,
                          le(640, 4) + le(480, 4) + le(2, 1) + le(static_cast<uint64>(bitmap.size()), 2) + bitmap);
        objects += asfObject("CE75F87B8D46D1118D82006097C9A2B2", le(2, 2) + le(1, 2) + le(128000, 4) + le(2, 2) + le(1'000'000, 4));
        count += 2;
    }
    const QByteArray title = utf16(u"Song"_s), author = utf16(u"Singer"_s);
    objects += asfObject("3326B2758E66CF11A6D900AA0062CE6C",
                         le(static_cast<uint64>(title.size()), 2) + le(static_cast<uint64>(author.size()), 2)
                             + le(0, 2) + le(0, 2) + le(0, 2) + title + author);
    const QByteArray albumName = utf16(u"WM/AlbumTitle"_s), album = utf16(u"Record"_s);
    objects += asfObject("40A4D0D207E3D21197F000A0C95EA850",
                         le(1, 2) + le(static_cast<uint64>(albumName.size()), 2) + albumName + le(0, 2)
                             + le(static_cast<uint64>(album.size()), 2) + album);
    return QByteArray::fromHex("3026B2758E66CF11A6D900AA0062CE6C") + le(static_cast<uint64>(objects.size() + 30), 8)
         + le(static_cast<uint64>(count), 4) + QByteArray::fromHex("0102") + objects + QByteArray(4000, '\x55');
}

class tst_MediaInfo : public QObject {
    Q_OBJECT

private slots:
    // --- Audio format name ---
    void audioFormatName_pcm()
    {
        QString name = audioFormatName(0x0001);
        QVERIFY(name.contains(u"PCM"));
        QVERIFY(name.contains(u"Uncompressed"));
    }

    void audioFormatName_mp3()
    {
        QString name = audioFormatName(0x0055);
        QVERIFY(name.contains(u"MP3"));
        QVERIFY(name.contains(u"MPEG-1, Layer 3"));
    }

    void audioFormatName_ac3()
    {
        QString name = audioFormatName(0x2000);
        QVERIFY(name.contains(u"AC3"));
        QVERIFY(name.contains(u"Dolby"));
    }

    void audioFormatName_unknown()
    {
        QString name = audioFormatName(0xFFFF);
        QVERIFY(name.contains(u"Unknown"));
        QVERIFY(name.contains(u"0xffff", Qt::CaseInsensitive));
    }

    // --- Audio format codec ID ---
    void audioFormatCodecId_mp3()
    {
        QCOMPARE(audioFormatCodecId(0x0055), QStringLiteral("MP3"));
    }

    void audioFormatCodecId_unknown()
    {
        QVERIFY(audioFormatCodecId(0xFFFF).isEmpty());
    }

    // --- Video format name ---
    void videoFormatName_divx()
    {
        uint32 divx = fourCC('D','I','V','X');
        QString name = videoFormatName(divx);
        QVERIFY(name.contains(u"DivX", Qt::CaseInsensitive));
    }

    void videoFormatName_h264()
    {
        uint32 h264 = fourCC('H','2','6','4');
        QString name = videoFormatName(h264);
        QVERIFY(name.contains(u"AVC"));
    }

    void videoFormatName_unknown()
    {
        uint32 zzzz = fourCC('Z','Z','Z','Z');
        QString name = videoFormatName(zzzz);
        QCOMPARE(name, QStringLiteral("ZZZZ"));
    }

    // --- isEqualFourCC ---
    void isEqualFourCC_caseInsensitive()
    {
        QVERIFY(isEqualFourCC(fourCC('d','i','v','x'), fourCC('D','I','V','X')));
        QVERIFY(isEqualFourCC(fourCC('H','2','6','4'), fourCC('h','2','6','4')));
        QVERIFY(!isEqualFourCC(fourCC('D','I','V','X'), fourCC('X','V','I','D')));
    }

    // --- Known aspect ratio ---
    void knownAspectRatio_4_3()
    {
        QCOMPARE(knownAspectRatioString(1.33), QStringLiteral("4/3"));
    }

    void knownAspectRatio_16_9()
    {
        QCOMPARE(knownAspectRatioString(1.77), QStringLiteral("16/9"));
    }

    void knownAspectRatio_unknown()
    {
        QVERIFY(knownAspectRatioString(0.5).isEmpty());
    }

    // --- codecDisplayName ---
    void codecDisplayName_fourcc()
    {
        QString name = codecDisplayName(QStringLiteral("divx"));
        QVERIFY(!name.isEmpty());
        QVERIFY(name.contains(u"DivX", Qt::CaseInsensitive));
    }

    void codecDisplayName_audio()
    {
        QString name = codecDisplayName(QStringLiteral("MP3"));
        QVERIFY(name.contains(u"MP3"));
        QVERIFY(name.contains(u"MPEG-1, Layer 3"));
    }

    void codecDisplayName_fallback()
    {
        QString name = codecDisplayName(QStringLiteral("zzzz"));
        QCOMPARE(name, QStringLiteral("ZZZZ"));
    }

    // --- MIME detection ---
    void detectMimeType_nonexistent()
    {
        QString mime = detectMimeType(QStringLiteral("/tmp/nonexistent_file_12345.xyz"));
        // QMimeDatabase returns "application/octet-stream" for unknown files
        QVERIFY(!mime.isEmpty());
    }

    // --- RIFF parser error handling ---
    void readRIFF_nonexistent()
    {
        MediaInfo info;
        QVERIFY(!readRIFFHeaders(QStringLiteral("/tmp/nonexistent_file_12345.avi"), info));
    }

    void readRIFF_tooSmall()
    {
        QByteArray tiny(4, '\0');
        QString path = writeTempFile(tiny, u".avi"_s);
        QVERIFY(!path.isEmpty());
        MediaInfo info;
        QVERIFY(!readRIFFHeaders(path, info));
    }

    void readRIFF_syntheticAVI()
    {
        QByteArray avi = buildMinimalAVI();
        QString path = writeTempFile(avi, u".avi"_s);
        QVERIFY(!path.isEmpty());

        MediaInfo info;
        QVERIFY(readRIFFHeaders(path, info));
        QCOMPARE(info.fileFormat, QStringLiteral("AVI"));
        QCOMPARE(info.videoStreamCount, 1);
        QCOMPARE(info.video.width, 320u);
        QCOMPARE(info.video.height, 240u);
        QVERIFY(info.video.frameRate > 24.0 && info.video.frameRate < 26.0); // ~25 fps
        QVERIFY(info.video.codecName.contains(u"DivX", Qt::CaseInsensitive));
    }

    // --- RM parser error handling ---
    void readRM_nonexistent()
    {
        MediaInfo info;
        QVERIFY(!readRMHeaders(QStringLiteral("/tmp/nonexistent_file_12345.rm"), info));
    }

    void readRM_invalidMagic()
    {
        QByteArray garbage(64, 'X');
        QString path = writeTempFile(garbage, u".rm"_s);
        QVERIFY(!path.isEmpty());
        MediaInfo info;
        QVERIFY(!readRMHeaders(path, info));
    }

    // --- MediaInfo::initFileLength ---
    void mediaInfo_initFileLength()
    {
        MediaInfo info;
        info.video.lengthSec = 120.5;
        info.initFileLength();
        QCOMPARE(info.lengthSec, 120.5);
        QVERIFY(!info.lengthEstimated);

        // If video length is estimated
        MediaInfo info2;
        info2.video.lengthSec = 60.0;
        info2.video.lengthEstimated = true;
        info2.initFileLength();
        QCOMPARE(info2.lengthSec, 60.0);
        QVERIFY(info2.lengthEstimated);

        // Audio fallback
        MediaInfo info3;
        info3.audio.lengthSec = 200.0;
        info3.initFileLength();
        QCOMPARE(info3.lengthSec, 200.0);
    }

    // --- extractMediaInfo ---
    // --- MP3 / FLAC / Ogg / MP4 / Matroska ---
    void mp3_cbrLengthBitrateAndTexts()
    {
        const QString path = writeTempFile(buildMP3(100), u".mp3"_s);
        MediaInfo info;
        QVERIFY(extractMediaInfo(path, info));
        QCOMPARE(info.fileFormat, QStringLiteral("MPEG Audio"));
        QCOMPARE(info.audio.formatTag, uint16{0x0055});
        QCOMPARE(info.audio.sampleRate, uint32{44100});
        QCOMPARE(info.audio.avgBytesPerSec, uint32{16000});
        QVERIFY(qAbs(info.lengthSec - 100 * 417 * 8 / 128000.0) < 0.01);
        QCOMPARE(info.title, QStringLiteral("Song"));
        QCOMPARE(info.author, QStringLiteral("Band"));
        QCOMPARE(info.album, QStringLiteral("Record"));
    }

    void mp3_xingHeaderGivesTheLength()
    {
        const QString path = writeTempFile(buildMP3(100, 1000), u".mp3"_s);
        MediaInfo info;
        QVERIFY(extractMediaInfo(path, info));
        QVERIFY(qAbs(info.lengthSec - 1000 * 1152 / 44100.0) < 0.01);
    }

    void mp3_noTagNeedsTheExtension()
    {
        const QByteArray bare = buildMP3(20).mid(10 + 3 * 10 + 3 + 14);
        MediaInfo named, unnamed;
        QVERIFY(readMP3Headers(writeTempFile(bare, u".mp3"_s), named));
        QVERIFY(!readMP3Headers(writeTempFile(bare, u".bin"_s), unnamed));
    }

    void flac_streamInfoAndComments()
    {
        const QString path = writeTempFile(buildFLAC(), u".flac"_s);
        MediaInfo info;
        QVERIFY(extractMediaInfo(path, info));
        QCOMPARE(info.fileFormat, QStringLiteral("FLAC"));
        QCOMPARE(info.audio.codecName, QStringLiteral("flac"));
        QCOMPARE(info.audio.sampleRate, uint32{44100});
        QCOMPARE(info.audio.channels, uint16{2});
        QCOMPARE(info.audio.bitsPerSample, uint16{16});
        QVERIFY(qAbs(info.lengthSec - 10.0) < 0.001);
        QVERIFY(info.audio.avgBytesPerSec > 0);
        QCOMPARE(info.title, QStringLiteral("Piece"));
        QCOMPARE(info.author, QStringLiteral("Player"));
        QCOMPARE(info.album, QStringLiteral("Disc"));
    }

    void ogg_vorbisHeaderCommentsAndLastGranule()
    {
        const QString path = writeTempFile(buildOggVorbis(), u".ogg"_s);
        MediaInfo info;
        QVERIFY(extractMediaInfo(path, info));
        QCOMPARE(info.fileFormat, QStringLiteral("Ogg"));
        QCOMPARE(info.audio.codecName, QStringLiteral("vorbis"));
        QCOMPARE(info.audio.channels, uint16{2});
        QCOMPARE(info.audio.avgBytesPerSec, uint32{20000});
        QVERIFY(qAbs(info.lengthSec - 10.0) < 0.001);
        QCOMPARE(info.title, QStringLiteral("Tune"));
        QCOMPARE(info.author, QStringLiteral("Singer"));
    }

    void mp4_movieHeaderTrackAndTitle()
    {
        const QByteArray data = buildMP4();
        const QString path = writeTempFile(data, u".mp4"_s);
        MediaInfo info;
        QVERIFY(extractMediaInfo(path, info));
        QCOMPARE(info.fileFormat, QStringLiteral("MPEG-4"));
        QCOMPARE(info.videoStreamCount, 1);
        QCOMPARE(info.video.codecName, QStringLiteral("h264"));
        QCOMPARE(info.video.width, uint32{1280});
        QCOMPARE(info.video.height, uint32{720});
        QVERIFY(qAbs(info.lengthSec - 30.0) < 0.001);
        QCOMPARE(info.video.bitRate, static_cast<uint32>(data.size() * 8 / 30));
        QCOMPARE(info.title, QStringLiteral("Film"));
    }

    void matroska_infoAndTrack()
    {
        const QString path = writeTempFile(buildMatroska(), u".mkv"_s);
        MediaInfo info;
        QVERIFY(extractMediaInfo(path, info));
        QCOMPARE(info.fileFormat, QStringLiteral("Matroska"));
        QCOMPARE(info.videoStreamCount, 1);
        QCOMPARE(info.video.codecName, QStringLiteral("h264"));
        QCOMPARE(info.video.width, uint32{1920});
        QCOMPARE(info.video.height, uint32{1080});
        QVERIFY(qAbs(info.lengthSec - 60.0) < 0.001);
        QVERIFY(info.video.bitRate > 0);
        QCOMPARE(info.title, QStringLiteral("Clip"));
    }

    void matroska_tagsBehindTheClusters()
    {
        const QString path = writeTempFile(buildMatroskaWithTags(), u".mkv"_s);
        MediaInfo info;
        QVERIFY(extractMediaInfo(path, info));
        QCOMPARE(info.videoStreamCount, 1);
        QCOMPARE(info.title, QStringLiteral("Tagged"));     // not the track's name
        QCOMPARE(info.author, QStringLiteral("Band"));
        QCOMPARE(info.album, QStringLiteral("Record"));
    }

    void quickTime_withoutFtyp_data()
    {
        QTest::addColumn<bool>("keysForm");
        QTest::newRow("user-data text atoms") << false;
        QTest::newRow("keys + ilst metadata") << true;
    }

    void quickTime_withoutFtyp()
    {
        QFETCH(bool, keysForm);
        const QString path = writeTempFile(buildMOV(keysForm), u".mov"_s);
        MediaInfo info;
        QVERIFY(extractMediaInfo(path, info));
        QCOMPARE(info.fileFormat, QStringLiteral("QuickTime"));
        QCOMPARE(info.video.width, uint32{640});
        QVERIFY(qAbs(info.lengthSec - 10.0) < 0.001);
        QCOMPARE(info.title, QStringLiteral("Movie"));
        QCOMPARE(info.author, QStringLiteral("Director"));
    }

    void asf_audio()
    {
        const QString path = writeTempFile(buildASF(false), u".wma"_s);
        MediaInfo info;
        QVERIFY(extractMediaInfo(path, info));
        QCOMPARE(info.fileFormat, QStringLiteral("Windows Media"));
        QCOMPARE(info.audioStreamCount, 1);
        QCOMPARE(info.videoStreamCount, 0);
        QCOMPARE(info.audio.formatTag, uint16{0x0161});
        QCOMPARE(info.audio.channels, uint16{2});
        QCOMPARE(info.audio.sampleRate, uint32{44100});
        QCOMPARE(info.audio.avgBytesPerSec, uint32{16000});
        QVERIFY(qAbs(info.lengthSec - 42.0) < 0.001);       // play duration minus preroll
        QCOMPARE(info.title, QStringLiteral("Song"));
        QCOMPARE(info.author, QStringLiteral("Singer"));
        QCOMPARE(info.album, QStringLiteral("Record"));
    }

    void asf_video()
    {
        const QString path = writeTempFile(buildASF(true), u".wmv"_s);
        MediaInfo info;
        QVERIFY(extractMediaInfo(path, info));
        QCOMPARE(info.videoStreamCount, 1);
        QCOMPARE(info.audioStreamCount, 1);
        QCOMPARE(info.video.width, uint32{640});
        QCOMPARE(info.video.height, uint32{480});
        QCOMPARE(info.video.bitRate, uint32{1'000'000});
        const char fourcc[4] = {char(info.video.codecTag), char(info.video.codecTag >> 8),
                                char(info.video.codecTag >> 16), char(info.video.codecTag >> 24)};
        QCOMPARE(QByteArray(fourcc, 4), QByteArray("WMV3"));
    }

    // Every prefix of every file: nothing may throw, hang or read out of bounds.
    void containers_surviveTruncation()
    {
        const std::pair<QByteArray, QString> files[] = {
            {buildMP3(6, 50), u".mp3"_s}, {buildFLAC(), u".flac"_s}, {buildOggVorbis(), u".ogg"_s},
            {buildMP4(), u".mp4"_s},      {buildMatroska(), u".mkv"_s},
            {buildMatroskaWithTags(), u".mkv"_s}, {buildMOV(false), u".mov"_s}, {buildMOV(true), u".mov"_s},
            {buildASF(false), u".wma"_s}, {buildASF(true), u".wmv"_s}};
        for (const auto& [data, suffix] : files) {
            const qsizetype step = qMax<qsizetype>(1, data.size() / 150);
            for (qsizetype len = 0; len < data.size(); len += step) {
                MediaInfo info;
                (void)extractMediaInfo(writeTempFile(data.left(len), suffix), info);
            }
            // And with the size fields pointing past the end.
            QByteArray wild = data;
            for (qsizetype i = 4; i < wild.size(); i += 7)
                wild[i] = '\xFF';
            MediaInfo info;
            (void)extractMediaInfo(writeTempFile(wild, suffix), info);
        }
    }

    // Opt-in: MEDIAINFO_SAMPLES=<dir> prints what is read from real files there.
    void realFiles_fromEnv()
    {
        const QString dir = qEnvironmentVariable("MEDIAINFO_SAMPLES");
        if (dir.isEmpty())
            QSKIP("MEDIAINFO_SAMPLES not set");
        for (const QFileInfo& fi : QDir(dir).entryInfoList(QDir::Files, QDir::Name)) {
            MediaInfo info;
            const bool ok = extractMediaInfo(fi.filePath(), info);
            qInfo().noquote() << fi.fileName() << (ok ? "ok" : "NO") << info.fileFormat
                              << "len" << info.lengthSec << "v" << info.video.codecName
                              << info.video.width << info.video.height << info.video.bitRate
                              << "a" << info.audio.codecName << info.audio.formatTag
                              << info.audio.sampleRate << info.audio.channels
                              << info.audio.avgBytesPerSec * 8 << "|" << info.title << "|"
                              << info.author << "|" << info.album;

            // The path a shared file takes: gate, one reader, a read budget.
            MediaInfo gated;
            qint64 bytes = -1;
            const bool gatedOk = extractSharedMediaInfo(fi.filePath(), gated, &bytes);
            qInfo().noquote() << "   shared:" << (gatedOk ? "ok" : "NO") << gated.fileFormat
                              << "len" << gated.lengthSec << "codec"
                              << (gated.videoStreamCount ? gated.video.codecName : gated.audio.codecName)
                              << "| read" << bytes << "of" << fi.size() << "bytes";
            QVERIFY2(bytes <= kMediaReadBudget, qPrintable(fi.fileName()));
        }
    }

    // ---- shared files: gate, budget, the three later formats ----

    void gate_holdsAMediaNameToItsOwnSignature_data()
    {
        QTest::addColumn<QByteArray>("head");
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("kind");
        const auto row = [](const char* tag, const QByteArray& head, const char* name, MediaKind kind) {
            QTest::newRow(tag) << head << QString::fromLatin1(name) << static_cast<int>(kind);
        };
        const QByteArray mp4 = buildMP4().left(64);
        const QByteArray avi = QByteArray("RIFF\0\0\0\0AVI LIST", 16);
        const QByteArray id3 = buildMP3(2).left(64);
        const QByteArray bareMp3 = QByteArray(40, '\0') + be(0xFFFB9000u, 4) + QByteArray(40, '\0');

        row("mp4 as itself", mp4, "film.mp4", MediaKind::Mp4);
        row("mp4 called avi", mp4, "film.avi", MediaKind::None);
        row("avi as itself", avi, "film.avi", MediaKind::Riff);
        row("avi called mkv", avi, "film.mkv", MediaKind::None);
        row("mp4 with no media name", mp4, "film.bin", MediaKind::Mp4);
        row("tagged mp3", id3, "song.mp3", MediaKind::Mp3);
        row("tag in front of flac", id3, "song.flac", MediaKind::Flac);
        row("tag with no media name", id3, "song.dat", MediaKind::Mp3);
        row("mp3 by frame sync", bareMp3, "song.mp3", MediaKind::Mp3);
        // A sync is too weak to go by without the name ...
        row("frame sync with no media name", bareMp3, "song.dat", MediaKind::None);
        // ... and beyond the first twelve bytes it is not even looked for.
        row("text called mp3", QByteArray(200, 'a'), "song.mp3", MediaKind::None);
        row("zip called mkv", QByteArray("PK\x03\x04", 4) + QByteArray(60, '\0'), "film.mkv",
            MediaKind::None);
        row("ape", QByteArray("MAC \x96\x0F", 6) + QByteArray(60, '\0'), "a.ape", MediaKind::Ape);
        row("wavpack", QByteArray("wvpk") + QByteArray(60, '\0'), "a.wv", MediaKind::WavPack);
        row("adts", be(0xFFF15080u, 4) + QByteArray(60, '\0'), "a.aac", MediaKind::Aac);
        row("nothing at all", QByteArray(), "film.mkv", MediaKind::None);
    }

    void gate_holdsAMediaNameToItsOwnSignature()
    {
        QFETCH(QByteArray, head);
        QFETCH(QString, name);
        QFETCH(int, kind);
        QCOMPARE(static_cast<int>(mediaGate(head, name)), kind);
    }

    // What made a big library slow: megabytes read from files that turn out not to
    // be what their name says, or not media at all.
    void shared_aFileThatIsNotItsNameCostsOneSmallRead()
    {
        const QByteArray mp4 = buildMP4() + QByteArray(3 * 1024 * 1024, '\x44');
        qint64 bytes = -1;

        MediaInfo wrongName;
        QVERIFY(!extractSharedMediaInfo(writeTempFile(mp4, u".avi"_s), wrongName, &bytes));
        QCOMPARE(bytes, kMediaGateBytes);

        MediaInfo notMedia;
        QVERIFY(!extractSharedMediaInfo(writeTempFile(QByteArray(3 * 1024 * 1024, 'z'), u".iso"_s),
                                        notMedia, &bytes));
        QCOMPARE(bytes, kMediaSniffBytes);

        // The same bytes under a name that promises nothing are still an MP4.
        MediaInfo bySignature;
        QVERIFY(extractSharedMediaInfo(writeTempFile(mp4, u".bin"_s), bySignature, &bytes));
        QCOMPARE(bySignature.video.codecName, u"h264"_s);
        QCOMPARE(bySignature.title, u"Film"_s);

        MediaInfo missing;
        QVERIFY(!extractSharedMediaInfo(u"/nonexistent/file.mp3"_s, missing, &bytes));
        QCOMPARE(bytes, qint64{0});
    }

    void shared_aCoverPictureIsNeverRead()
    {
        // TIT2, a 3 MiB picture, then TALB: both texts, none of the picture.
        const QByteArray picture = QByteArray("APIC") + be(3 * 1024 * 1024, 4) + QByteArray(2, '\0')
                                 + QByteArray(3 * 1024 * 1024, '\x7E');
        const QByteArray frames = id3Frame("TIT2", u"Song"_s) + picture + id3Frame("TALB", u"Record"_s);
        QByteArray mp3("ID3\x03\x00\x00", 6);
        const auto n = static_cast<uint32>(frames.size());
        for (int shift : {21, 14, 7, 0})
            mp3 += static_cast<char>((n >> shift) & 0x7F);
        mp3 += frames;
        for (int i = 0; i < 100; ++i)
            mp3 += be(0xFFFB9000u, 4) + QByteArray(413, '\0');

        MediaInfo info;
        qint64 bytes = -1;
        QVERIFY(extractSharedMediaInfo(writeTempFile(mp3, u".mp3"_s), info, &bytes));
        QCOMPARE(info.title, u"Song"_s);
        QCOMPARE(info.album, u"Record"_s);
        QCOMPARE(info.audio.sampleRate, 44100u);
        QVERIFY2(bytes < 300 * 1024, qPrintable(QString::number(bytes)));
    }

    void shared_sampleTablesOfALongFilmAreSteppedOver()
    {
        // The MP4 builder's movie, with 5 MiB of sample table and a 1 MiB cover in it.
        const QByteArray mvhd = box("mvhd", be(0, 4) + be(0, 4) + be(0, 4) + be(1000, 4) + be(30000, 4)
                                                + QByteArray(80, '\0'));
        const QByteArray avc1 = QByteArray(6, '\0') + be(1, 2) + QByteArray(16, '\0') + be(1280, 2)
                              + be(720, 2) + QByteArray(50, '\0');
        const QByteArray stsd = box("stsd", be(0, 4) + be(1, 4) + box("avc1", avc1));
        const QByteArray stsz = box("stsz", QByteArray(5 * 1024 * 1024, '\x01'));
        const QByteArray hdlr = box("hdlr", be(0, 4) + be(0, 4) + QByteArray("vide") + QByteArray(13, '\0'));
        const QByteArray trak = box("trak", box("mdia", hdlr + box("minf", box("stbl", stsz + stsd))));
        const QByteArray cover = box("covr", box("data", QByteArray(1024 * 1024, '\x02')));
        const QByteArray title = box("\xA9nam", box("data", be(1, 4) + be(0, 4) + QByteArray("Film")));
        const QByteArray udta = box("udta", box("meta", be(0, 4) + box("ilst", cover + title)));
        const QByteArray mp4 = box("ftyp", QByteArray("isom") + be(0, 4) + QByteArray("isom"))
                             + box("moov", mvhd + trak + udta) + box("mdat", QByteArray(3000, '\x33'));

        MediaInfo info;
        qint64 bytes = -1;
        QVERIFY(extractSharedMediaInfo(writeTempFile(mp4, u".mp4"_s), info, &bytes));
        QCOMPARE(info.video.codecName, u"h264"_s);
        QCOMPARE(info.video.width, 1280u);
        QCOMPARE(info.title, u"Film"_s);
        QCOMPARE(qRound(info.lengthSec), 30);
        QVERIFY2(bytes < 64 * 1024, qPrintable(QString::number(bytes)));
    }

    void shared_theBudgetIsTheMostAFileCanCost()
    {
        // An Ogg whose comment packet claims far more than there is budget for.
        QByteArray ogg = buildOggVorbis();
        ogg += QByteArray(6 * 1024 * 1024, '\x11');
        // A Matroska-looking file that is all one oversized element.
        const QByteArray mkv = QByteArray("\x1A\x45\xDF\xA3\x84\x42\x86\x81\x01", 9)
                             + QByteArray("\x18\x53\x80\x67\x08\x60\x00\x00", 8)
                             + QByteArray("\x15\x49\xA9\x66\x08\x50\x00\x00", 8)
                             + QByteArray(6 * 1024 * 1024, '\x22');
        for (const auto& [data, suffix] : {std::pair{ogg, u".ogg"_s}, std::pair{mkv, u".mkv"_s}}) {
            MediaInfo info;
            qint64 bytes = -1;
            static_cast<void>(extractSharedMediaInfo(writeTempFile(data, suffix), info, &bytes));
            QVERIFY2(bytes <= kMediaReadBudget, qPrintable(suffix + u' ' + QString::number(bytes)));
        }
    }

    void aac_adtsFramesGiveRateAndLength()
    {
        // 44.1 kHz stereo LC, 200 frames of 371 bytes: 127.8 kbit/s, 4.64 s.
        QByteArray aac;
        for (int i = 0; i < 200; ++i) {
            const uint32 len = 371;
            QByteArray frame(7, '\0');
            frame[0] = static_cast<char>(0xFF);
            frame[1] = static_cast<char>(0xF1);
            frame[2] = static_cast<char>((1 << 6) | (4 << 2));            // LC, 44.1 kHz
            frame[3] = static_cast<char>((2 << 6) | ((len >> 11) & 3));   // 2 channels
            frame[4] = static_cast<char>((len >> 3) & 0xFF);
            frame[5] = static_cast<char>(((len & 7) << 5) | 0x1F);
            frame[6] = static_cast<char>(0xFC);
            aac += frame + QByteArray(static_cast<qsizetype>(len) - 7, '\x21');
        }
        MediaInfo info;
        QVERIFY(extractSharedMediaInfo(writeTempFile(aac, u".aac"_s), info));
        QCOMPARE(info.audio.codecName, u"aac"_s);
        QCOMPARE(info.audio.sampleRate, 44100u);
        QCOMPARE(info.audio.channels, uint16{2});
        QVERIFY(info.lengthEstimated);
        QVERIFY2(std::abs(info.lengthSec - 4.64) < 0.05, qPrintable(QString::number(info.lengthSec)));
        QVERIFY2(std::abs(static_cast<int>(info.audio.avgBytesPerSec * 8) - 127800) < 600,
                 qPrintable(QString::number(info.audio.avgBytesPerSec * 8)));

        // The same bytes under another media name are not an AVI.
        MediaInfo wrong;
        QVERIFY(!extractSharedMediaInfo(writeTempFile(aac, u".avi"_s), wrong));
    }

    void ape_headerAndApeTag()
    {
        // 3.99: descriptor (52 bytes), then the header. 44.1 kHz stereo, 100 frames of
        // 73728 * 4 blocks plus a last one of 44100: 669.7 s.
        QByteArray ape = QByteArray("MAC ") + le(3990, 2) + le(0, 2) + le(52, 4) + le(24, 4)
                       + QByteArray(52 - 16, '\0');
        ape += le(2000, 2) + le(0, 2) + le(73728 * 4, 4) + le(44100, 4) + le(101, 4)
             + le(16, 2) + le(2, 2) + le(44100, 4);
        ape += QByteArray(4000, '\x31');

        const auto item = [](const char* key, const QByteArray& value) {
            return le(static_cast<uint64>(value.size()), 4) + le(0, 4) + QByteArray(key)
                 + QByteArray(1, '\0') + value;
        };
        // A cover first, to be stepped over inside the tag.
        const QByteArray items = item("Cover Art (Front)", QByteArray(3000, '\x05'))
                               + item("Title", "Piece") + item("ARTIST", "Player")
                               + item("Album", "Disc");
        ape += items + QByteArray("APETAGEX") + le(2000, 4)
             + le(static_cast<uint64>(items.size() + 32), 4) + le(4, 4) + le(0, 4)
             + QByteArray(8, '\0');

        MediaInfo info;
        QVERIFY(extractSharedMediaInfo(writeTempFile(ape, u".ape"_s), info));
        QCOMPARE(info.audio.codecName, u"ape"_s);
        QCOMPARE(info.audio.sampleRate, 44100u);
        QCOMPARE(info.audio.channels, uint16{2});
        QVERIFY2(std::abs(info.lengthSec - 669.7) < 0.1, qPrintable(QString::number(info.lengthSec)));
        QCOMPARE(info.title, u"Piece"_s);
        QCOMPARE(info.author, u"Player"_s);
        QCOMPARE(info.album, u"Disc"_s);
    }

    void wavpack_blockHeaderAndApeTagBeforeId3v1()
    {
        // 48 kHz (index 10) mono, 480000 samples: ten seconds.
        QByteArray wv = QByteArray("wvpk") + le(1000, 4) + le(0x410, 2) + le(0, 2) + le(480000, 4)
                      + le(0, 4) + le(48000, 4) + le((10u << 23) | 0x4, 4) + le(0, 4);
        wv += QByteArray(2000, '\x41');
        const QByteArray title = le(4, 4) + le(0, 4) + QByteArray("Title") + QByteArray(1, '\0') + "Tune";
        wv += title + QByteArray("APETAGEX") + le(2000, 4)
            + le(static_cast<uint64>(title.size() + 32), 4) + le(1, 4) + le(0, 4) + QByteArray(8, '\0');
        wv += QByteArray("TAG") + QByteArray(125, '\0');   // an ID3v1 tag behind it

        MediaInfo info;
        QVERIFY(extractSharedMediaInfo(writeTempFile(wv, u".wv"_s), info));
        QCOMPARE(info.audio.codecName, u"wavpack"_s);
        QCOMPARE(info.audio.sampleRate, 48000u);
        QCOMPARE(info.audio.channels, uint16{1});
        QCOMPARE(qRound(info.lengthSec), 10);
        QCOMPARE(info.title, u"Tune"_s);
    }

    void shared_readsEveryFormatTheOpenReaderDoes()
    {
        // The gated path must not lose what the try-everything path finds.
        const std::pair<QByteArray, QString> files[] = {
            {buildMP3(100), u".mp3"_s},      {buildFLAC(), u".flac"_s},
            {buildOggVorbis(), u".ogg"_s},   {buildMP4(), u".mp4"_s},
            {buildMatroska(), u".mkv"_s},    {buildMatroskaWithTags(), u".mkv"_s},
            {buildMinimalAVI(), u".avi"_s},
        };
        for (const auto& [data, suffix] : files) {
            const QString path = writeTempFile(data, suffix);
            MediaInfo open, gated;
            QVERIFY2(extractMediaInfo(path, open), qPrintable(suffix));
            QVERIFY2(extractSharedMediaInfo(path, gated), qPrintable(suffix));
            QCOMPARE(gated.fileFormat, open.fileFormat);
            QCOMPARE(gated.lengthSec, open.lengthSec);
            QCOMPARE(gated.title, open.title);
            QCOMPARE(gated.author, open.author);
            QCOMPARE(gated.album, open.album);
            QCOMPARE(gated.audio.codecName, open.audio.codecName);
            QCOMPARE(gated.video.codecName, open.video.codecName);
        }
    }

    void extractMediaInfo_unknownFile()
    {
        QByteArray garbage(128, 'Z');
        QString path = writeTempFile(garbage, u".xyz"_s);
        QVERIFY(!path.isEmpty());
        MediaInfo info;
        QVERIFY(!extractMediaInfo(path, info));
    }

    void extractMediaInfo_syntheticAVI()
    {
        QByteArray avi = buildMinimalAVI();
        QString path = writeTempFile(avi, u".avi"_s);
        QVERIFY(!path.isEmpty());

        MediaInfo info;
        QVERIFY(extractMediaInfo(path, info));
        QCOMPARE(info.fileFormat, QStringLiteral("AVI"));
        QVERIFY(!info.fileName.isEmpty());
        QVERIFY(info.fileSize > 0);
    }
};

QTEST_MAIN(tst_MediaInfo)
#include "tst_MediaInfo.moc"
