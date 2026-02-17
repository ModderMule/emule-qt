#include <QTest>
#include <QTemporaryFile>
#include <QDir>

#include "TestHelpers.h"
#include "media/MediaInfo.h"
#include "utils/OtherFunctions.h"

using namespace eMule;
using namespace Qt::StringLiterals;

// Helper: write binary data to a temporary file and return its path.
static QString writeTempFile(const QByteArray& data, const QString& suffix = {})
{
    auto* tmp = new QTemporaryFile(QDir::tempPath() + u"/tst_mediainfo_XXXXXX" + suffix);
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
