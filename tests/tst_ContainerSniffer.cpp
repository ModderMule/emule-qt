/// @file tst_ContainerSniffer.cpp
/// @brief media/ContainerSniffer — is a file the container its name claims?
///
/// The file that prompted this module is 529 MB of random padding named .wmv.
/// file(1) calls it a 32 kbps MP3 because its first two bytes land on an MPEG
/// frame sync by chance; VLC opens it, finds nothing, and sits at 0:00 forever.
/// Every case below exists to keep one of those two mistakes out: claiming to
/// know what a file is when we do not, and claiming a file is fine when it is not.

#include "media/ContainerSniffer.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;

namespace {

// Signatures are mandatory in their formats, so these are the real leading bytes.
// Written as sized QByteArrays because they carry NULs, which truncate a literal.
const char kAsfMagic[] = "\x30\x26\xB2\x75\x8E\x66\xCF\x11"
                         "\xA6\xD9\x00\xAA\x00\x62\xCE\x6C";
const char kMp4Magic[] = "\x00\x00\x00\x18" "ftypmp42\x00\x00\x00\x00";
const char kMkvMagic[] = "\x1A\x45\xDF\xA3\x01\x00\x00\x00\x00\x00\x00\x23";

/// The first twelve bytes of the real fake. Random padding whose first two bytes
/// happen to form an MPEG frame sync -- exactly what talks file(1) into calling
/// it audio. Nothing here may identify it as anything; the only true statement
/// available is that it is not the ASF a .wmv has to be.
const char kJunk[]     = "\xFF\xFB\x10\xC0\x0B\x0A\x07\x05"
                         "\x00\x07\x07\x0A";

} // namespace

class tst_ContainerSniffer : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void anHonestFileMatchesItsName();
    void aRenamedContainerIsNamedForWhatItIs();
    void anUnrecognisedFakeIsNotGuessedAt();
    void anExtensionWePromiseNothingAboutIsUnchecked();
    void aFileTooShortToCarryItsSignatureDoesNotHaveIt();
    void anUnreadableFileIsUncheckedNotSuspect();
    void mp3IsDeliberatelyNotPromised();
    void theWarningSaysWhichKindOfWrongItIs();

private:
    [[nodiscard]] QString write(const QString& name, const char* bytes, int len);

    QTemporaryDir m_dir;
};

void tst_ContainerSniffer::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

QString tst_ContainerSniffer::write(const QString& name, const char* bytes, int len)
{
    const QString path = m_dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(QByteArray(bytes, len));
    f.close();
    return path;
}

void tst_ContainerSniffer::anHonestFileMatchesItsName()
{
    const QString path = write(QStringLiteral("real.wmv"), kAsfMagic, 16);
    const ContainerCheck c = checkFile(path, QStringLiteral("real.wmv"));

    QCOMPARE(c.verdict, ContainerVerdict::Matches);
    QVERIFY(!c.isSuspect());
    QCOMPARE(c.expected, QStringLiteral("ASF"));

    // A Matroska under its own name, to prove the table is not ASF-only.
    const QString mkv = write(QStringLiteral("real.mkv"), kMkvMagic, 12);
    QCOMPARE(checkFile(mkv, QStringLiteral("real.mkv")).verdict, ContainerVerdict::Matches);
}

void tst_ContainerSniffer::aRenamedContainerIsNamedForWhatItIs()
{
    // The recoverable case: we know what it really is, so the stream route can
    // send the right Content-Type and the file plays anyway.
    const QString path = write(QStringLiteral("mislabelled.avi"), kMp4Magic, 16);
    const ContainerCheck c = checkFile(path, QStringLiteral("mislabelled.avi"));

    QCOMPARE(c.verdict, ContainerVerdict::WrongContainer);
    QVERIFY(c.isSuspect());
    QCOMPARE(c.expected, QStringLiteral("AVI"));
    QCOMPARE(c.actual, QStringLiteral("MP4"));
    QCOMPARE(c.mimeType, QStringLiteral("video/mp4"));
}

void tst_ContainerSniffer::anUnrecognisedFakeIsNotGuessedAt()
{
    const QString path = write(QStringLiteral("fake.wmv"), kJunk, 12);
    const ContainerCheck c = checkFile(path, QStringLiteral("fake.wmv"));

    QCOMPARE(c.verdict, ContainerVerdict::NoKnownContainer);
    QVERIFY(c.isSuspect());
    QCOMPARE(c.expected, QStringLiteral("ASF"));

    // The whole point: knowing it is not ASF is not knowing what it is. An
    // earlier cut read the frame sync and served this as audio/mpeg, and VLC
    // hit EOF immediately. Nothing may be claimed here.
    QVERIFY(c.actual.isEmpty());
    QVERIFY(c.mimeType.isEmpty());
}

void tst_ContainerSniffer::anExtensionWePromiseNothingAboutIsUnchecked()
{
    const QString path = write(QStringLiteral("notes.txt"), kJunk, 12);
    const ContainerCheck c = checkFile(path, QStringLiteral("notes.txt"));

    QCOMPARE(c.verdict, ContainerVerdict::Unchecked);
    QVERIFY(!c.isSuspect());
    QVERIFY(c.expected.isEmpty());
}

void tst_ContainerSniffer::aFileTooShortToCarryItsSignatureDoesNotHaveIt()
{
    // Not a free pass: a .wmv of four bytes has no room for the ASF GUID, so it
    // provably is not one.
    const QString path = write(QStringLiteral("stub.wmv"), kAsfMagic, 4);
    QCOMPARE(checkFile(path, QStringLiteral("stub.wmv")).verdict,
             ContainerVerdict::NoKnownContainer);
}

void tst_ContainerSniffer::anUnreadableFileIsUncheckedNotSuspect()
{
    // A locked or vanished file has told us nothing. Reporting that as a fake
    // would put a red mark on a perfectly good download.
    const ContainerCheck c = checkFile(m_dir.filePath(QStringLiteral("gone.wmv")),
                                       QStringLiteral("gone.wmv"));
    QCOMPARE(c.verdict, ContainerVerdict::Unchecked);
    QVERIFY(!c.isSuspect());
}

void tst_ContainerSniffer::mp3IsDeliberatelyNotPromised()
{
    // A tagless MP3 is legal and begins with no mandatory bytes, so there is
    // nothing to hold it to. Promising something here is what produced the
    // false positive that started all this.
    QVERIFY(expectedContainer(QStringLiteral("mp3")).isEmpty());

    const QString path = write(QStringLiteral("song.mp3"), kJunk, 12);
    QCOMPARE(checkFile(path, QStringLiteral("song.mp3")).verdict, ContainerVerdict::Unchecked);
}

void tst_ContainerSniffer::theWarningSaysWhichKindOfWrongItIs()
{
    // Every list that draws the mark explains it with this one sentence, so the
    // GUI tooltip, the web listing and the web UI title cannot drift apart again.

    // Named it: say so, because that one still plays in the right player.
    const QString path = write(QStringLiteral("movie.wmv"), kMp4Magic, 12);
    const ContainerCheck wrong = checkFile(path, QStringLiteral("movie.wmv"));
    QCOMPARE(containerWarningText(wrong, QStringLiteral("movie.wmv")),
             QStringLiteral("Named .wmv but the contents are MP4."));

    // Could not name it: do not invent a container. "Likely fake" is the whole of
    // what is known, and pointing the user at a player would waste the trip.
    const QString junk = write(QStringLiteral("fake.wmv"), kJunk, 12);
    const ContainerCheck unknown = checkFile(junk, QStringLiteral("fake.wmv"));
    const QString why = containerWarningText(unknown, QStringLiteral("fake.wmv"));
    QVERIFY(why.contains(QStringLiteral("very likely a fake")));
    QVERIFY(!why.contains(QStringLiteral("contents are")));

    // An honest file, and one we made no promise about, both say nothing at all —
    // an empty string is what keeps a clean row's tooltip clean.
    const QString real = write(QStringLiteral("real.wmv"), kAsfMagic, 12);
    QVERIFY(containerWarningText(checkFile(real, QStringLiteral("real.wmv")),
                                 QStringLiteral("real.wmv")).isEmpty());
    QVERIFY(containerWarningText(ContainerCheck{}, QStringLiteral("song.mp3")).isEmpty());
}

QTEST_MAIN(tst_ContainerSniffer)
#include "tst_ContainerSniffer.moc"
