/// @file tst_UsenetPassword.cpp
/// @brief Password-protected releases: detection, the external unpacker, preview.
///
/// Three things are pinned here, and the first is a data-loss bug rather than a
/// feature. libarchive returns ARCHIVE_FATAL on the *first* header of a
/// header-encrypted RAR, and ArchiveReader's `while (… == ARCHIVE_OK)` loop used
/// to read that as an empty-but-valid archive: the unpack produced no files,
/// reported success, and handed the queue a list of every volume to delete. The
/// download then showed **Complete** with nothing in it.
///
/// The second is that libarchive decrypts **ZIP only** — 7z stops at "Crypto
/// codec not supported yet" and RAR at "RAR encryption support unavailable" —
/// which is why an external 7-Zip or unrar is what makes a password mean
/// anything at all for the RAR sets most releases use.
///
/// The third is the preview: an encrypted set has no byte map and cannot be fed
/// to a keep-pace extractor, so the only way to see any of it early is to re-run
/// the external tool over the volumes that have landed.
///
/// **Fixtures.** The RAR sets are byte-crafted (RarFixtures.h) and carry the
/// encryption *flags* over plaintext payload — which is all libarchive ever
/// looks at, since it has no decryptor to reach the data with. Anything needing
/// a genuinely encrypted archive uses 7zz, which can write 7z and ZIP but not
/// RAR, and QSKIPs when it is not installed.

#include "FakeNntpServer.h"
#include "RarFixtures.h"
#include "TestHelpers.h"
#include "UsenetPostingHarness.h"

#include "archive/ArchiveReader.h"
#include "archive/ExternalUnpacker.h"
#include "post/UsenetUnpacker.h"
#include "prefs/Preferences.h"
#include "queue/UsenetQueue.h"
#include "stream/UsenetEncryptedPreview.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;
using namespace eMule::usenet;
using namespace eMule::testing;
using namespace eMule::testing::rar;
using namespace eMule::testing::usenet;

namespace {

/// The user's own 7zz first, then whatever is on PATH. Everything that needs a
/// genuinely encrypted archive is gated on this: 7zz is the only tool here that
/// can *write* one.
QString sevenZip()
{
    static const QString path = [] {
        const QString homebrew = QStringLiteral("/opt/homebrew/bin/7zz");
        if (QFileInfo(homebrew).isExecutable())
            return homebrew;
        for (const char* name : {"7zz", "7z"}) {
            const QString found = QStandardPaths::findExecutable(QLatin1String(name));
            if (!found.isEmpty())
                return found;
        }
        return QString();
    }();
    return path;
}

bool writeVolumes(const QString& dir, const QString& base, const QList<QByteArray>& volumes)
{
    for (int i = 0; i < volumes.size(); ++i) {
        // .partNN.rar — the modern scheme, whose first volume is part01.
        const QString name = QStringLiteral("%1.part%2.rar")
                                 .arg(base)
                                 .arg(i + 1, 2, 10, QLatin1Char('0'));
        QFile f(QDir(dir).filePath(name));
        if (!f.open(QIODevice::WriteOnly))
            return false;
        if (f.write(volumes.at(i)) != volumes.at(i).size())
            return false;
    }
    return true;
}

QStringList volumePaths(const QString& dir, const QString& base, int count)
{
    QStringList out;
    for (int i = 0; i < count; ++i) {
        out.append(QDir(dir).filePath(QStringLiteral("%1.part%2.rar")
                                          .arg(base)
                                          .arg(i + 1, 2, 10, QLatin1Char('0'))));
    }
    return out;
}

QByteArray payloadOf(qint64 bytes)
{
    // Deliberately incompressible-ish and self-describing, so a prefix served by
    // the preview can be checked against the original byte for byte.
    QByteArray out;
    out.reserve(int(bytes));
    for (qint64 i = 0; i < bytes; ++i)
        out.append(char('A' + (i * 7 + i / 251) % 26));
    return out;
}

int runTool(const QStringList& args, QByteArray* output = nullptr)
{
    QProcess p;
    p.setProgram(sevenZip());
    p.setArguments(args);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start();
    if (!p.waitForStarted(10'000))
        return -1;
    p.waitForFinished(120'000);
    if (output)
        *output = p.readAll();
    return p.exitCode();
}

} // namespace

class tst_UsenetPassword : public QObject {
    Q_OBJECT

private slots:
    // -- the data-loss bug --------------------------------------------------
    void headerEncryptedRarIsNeverSilentlyComplete();
    void dataEncryptedRarIsReportedAsNeedingAPassword();
    void anEncryptedSetWithNoPasswordNamesThePassword();

    // -- libarchive's own decryption ---------------------------------------
    void encryptedZipUnpacksWithTheRightPassword();
    void encryptedZipWithTheWrongPasswordFails();

    // -- the external unpacker ---------------------------------------------
    void encrypted7zGoesThroughTheExternalUnpacker();
    void theExternalUnpackerRejectsAWrongPassword();
    void noExternalToolSaysSoRatherThanFailingObscurely();
    void theExternalUnpackerSanitisesMemberNames();

    // -- preview ------------------------------------------------------------
    void anIncomplete7zSetCanNeverBePreviewed();
    void chooseMemberPicksTheBiggestPlayableOne();

    // -- the queue ----------------------------------------------------------
    void aPasswordInTheNzbNameReachesTheQueue();
    void aManualPasswordBeatsTheNzbButAnAutomaticOneDoesNot();
    void anEncryptedReleaseFailsAndRetriesWhenThePasswordIsSet();
    void anEncryptedReleaseWithItsPasswordDownloadsAndPublishes();
};

// ---------------------------------------------------------------------------
// The data-loss bug
// ---------------------------------------------------------------------------

void tst_UsenetPassword::headerEncryptedRarIsNeverSilentlyComplete()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString work = tmp.filePath(QStringLiteral("work"));
    const QString dest = tmp.filePath(QStringLiteral("out"));
    QVERIFY(QDir().mkpath(work));

    const QByteArray payload = payloadOf(4096);
    QVERIFY(writeVolumes(work, QStringLiteral("rel"),
                         makeHeaderEncryptedRarSet("movie.mkv", payload, 1500)));

    // libarchive fatals on the first header, so the archive lists nothing. What
    // must never happen is that being read as "an archive containing no files".
    ArchiveReader reader;
    QVERIFY2(!reader.open(volumePaths(work, QStringLiteral("rel"), 3)),
             "a header-encrypted set must not open as an empty archive");
    QVERIFY(reader.encryptionBlocked());
    QVERIFY(!reader.lastError().isEmpty());

    UsenetUnpacker unpacker;
    const auto result = unpacker.unpack(work, dest, /*password*/ QString());

    QVERIFY(!result.ok);
    QVERIFY(!result.nothingToDo);
    QVERIFY(result.encryptedUnsupported);
    QVERIFY(result.passwordRequired);

    // The half of the bug that destroyed data: consumedArchives is the queue's
    // delete list, and it must be empty for a set that produced nothing.
    QVERIFY2(result.consumedArchives.isEmpty(),
             "an unpack that produced no files must never mark the volumes deletable");
    QVERIFY(result.extractedFiles.isEmpty());

    // And the volumes are still there.
    for (const QString& volume : volumePaths(work, QStringLiteral("rel"), 3))
        QVERIFY(QFileInfo::exists(volume));
}

void tst_UsenetPassword::dataEncryptedRarIsReportedAsNeedingAPassword()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString work = tmp.filePath(QStringLiteral("work"));
    const QString dest = tmp.filePath(QStringLiteral("out"));
    QVERIFY(QDir().mkpath(work));

    QVERIFY(writeVolumes(work, QStringLiteral("rel"),
                         makeDataEncryptedRarSet("movie.mkv", payloadOf(4096), 1500)));

    // `rar -p` leaves the headers readable, so this set *does* list — and the
    // per-entry encrypted flag is the only thing separating it from a normal one.
    ArchiveReader reader;
    QVERIFY(reader.open(volumePaths(work, QStringLiteral("rel"), 3)));
    QVERIFY(reader.hasEncryptedEntries());

    UsenetUnpacker unpacker;
    const auto result = unpacker.unpack(work, dest, QString());

    QVERIFY(!result.ok);
    QVERIFY(result.encryptedUnsupported);
    QVERIFY(result.passwordRequired);
    QVERIFY(result.consumedArchives.isEmpty());
}

void tst_UsenetPassword::anEncryptedSetWithNoPasswordNamesThePassword()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString work = tmp.filePath(QStringLiteral("work"));
    QVERIFY(QDir().mkpath(work));
    QVERIFY(writeVolumes(work, QStringLiteral("rel"),
                         makeDataEncryptedRarSet("movie.mkv", payloadOf(2048), 900)));

    UsenetUnpacker unpacker;
    const auto result = unpacker.unpack(work, tmp.filePath(QStringLiteral("out")), QString());

    // The message is the only thing that tells a user what to do about it, and
    // "Extraction failed" — which is what this used to say for 7z — does not.
    QVERIFY(!result.ok);
    QVERIFY2(result.error.contains(QStringLiteral("password"), Qt::CaseInsensitive),
             qPrintable(result.error));
}

// ---------------------------------------------------------------------------
// libarchive's own decryption: ZIP, and only ZIP
// ---------------------------------------------------------------------------

void tst_UsenetPassword::encryptedZipUnpacksWithTheRightPassword()
{
    if (sevenZip().isEmpty())
        QSKIP("7zz/7z not installed; cannot build a genuinely encrypted zip");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString work = tmp.filePath(QStringLiteral("work"));
    const QString dest = tmp.filePath(QStringLiteral("out"));
    QVERIFY(QDir().mkpath(work));

    const QByteArray payload = payloadOf(64 * 1024);
    const QString source = tmp.filePath(QStringLiteral("movie.mkv"));
    QFile src(source);
    QVERIFY(src.open(QIODevice::WriteOnly));
    src.write(payload);
    src.close();

    const QString zip = QDir(work).filePath(QStringLiteral("rel.zip"));
    QCOMPARE(runTool({QStringLiteral("a"), QStringLiteral("-tzip"),
                      QStringLiteral("-mem=AES256"), QStringLiteral("-psecret123"),
                      zip, source}), 0);

    UsenetUnpacker unpacker;
    const auto result = unpacker.unpack(work, dest, QStringLiteral("secret123"));

    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.extractedFiles.size(), 1);
    QCOMPARE(result.consumedArchives, QStringList{zip});

    QFile out(result.extractedFiles.first());
    QVERIFY(out.open(QIODevice::ReadOnly));
    QCOMPARE(out.readAll(), payload);
}

void tst_UsenetPassword::encryptedZipWithTheWrongPasswordFails()
{
    if (sevenZip().isEmpty())
        QSKIP("7zz/7z not installed; cannot build a genuinely encrypted zip");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString work = tmp.filePath(QStringLiteral("work"));
    QVERIFY(QDir().mkpath(work));

    const QString source = tmp.filePath(QStringLiteral("movie.mkv"));
    QFile src(source);
    QVERIFY(src.open(QIODevice::WriteOnly));
    src.write(payloadOf(32 * 1024));
    src.close();

    QCOMPARE(runTool({QStringLiteral("a"), QStringLiteral("-tzip"),
                      QStringLiteral("-mem=AES256"), QStringLiteral("-psecret123"),
                      QDir(work).filePath(QStringLiteral("rel.zip")), source}), 0);

    UsenetUnpacker unpacker;
    const auto result =
        unpacker.unpack(work, tmp.filePath(QStringLiteral("out")), QStringLiteral("nope"));

    QVERIFY(!result.ok);
    QVERIFY(result.consumedArchives.isEmpty());
    // A wrong password is a different problem from a missing one, and the user
    // fixes it differently.
    QVERIFY(result.passwordRequired);
}

// ---------------------------------------------------------------------------
// The external unpacker
// ---------------------------------------------------------------------------

void tst_UsenetPassword::encrypted7zGoesThroughTheExternalUnpacker()
{
    if (sevenZip().isEmpty())
        QSKIP("7zz/7z not installed");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString work = tmp.filePath(QStringLiteral("work"));
    const QString dest = tmp.filePath(QStringLiteral("out"));
    QVERIFY(QDir().mkpath(work));

    const QByteArray payload = payloadOf(256 * 1024);
    const QString source = tmp.filePath(QStringLiteral("movie.mkv"));
    QFile src(source);
    QVERIFY(src.open(QIODevice::WriteOnly));
    src.write(payload);
    src.close();

    // -mhe=on encrypts the headers too, which is the case libarchive cannot even
    // list — and 7-Zip's own "Crypto codec not supported yet" is what routes it
    // here rather than to a corrupt-archive error.
    const QString archive = QDir(work).filePath(QStringLiteral("rel.7z"));
    QCOMPARE(runTool({QStringLiteral("a"), QStringLiteral("-mhe=on"),
                      QStringLiteral("-psecret123"), archive, source}), 0);

    // libarchive on its own gets nowhere with it.
    {
        ArchiveReader reader;
        reader.setPassphrase(QStringLiteral("secret123"));
        const bool opened = reader.open({archive});
        QVERIFY2(!opened || reader.entryCount() == 0 || !reader.extractAll(dest),
                 "libarchive must not be able to decrypt 7z");
    }

    UsenetUnpacker unpacker;
    const auto result = unpacker.unpack(work, dest, QStringLiteral("secret123"),
                                        {}, sevenZip());

    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.encryptedUnsupported);   // describes the archive, not the outcome
    QVERIFY(!result.passwordRequired);
    QCOMPARE(result.extractedFiles.size(), 1);
    QCOMPARE(result.consumedArchives, QStringList{archive});

    QFile out(result.extractedFiles.first());
    QVERIFY(out.open(QIODevice::ReadOnly));
    QCOMPARE(out.readAll(), payload);
}

void tst_UsenetPassword::theExternalUnpackerRejectsAWrongPassword()
{
    if (sevenZip().isEmpty())
        QSKIP("7zz/7z not installed");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString work = tmp.filePath(QStringLiteral("work"));
    QVERIFY(QDir().mkpath(work));

    const QString source = tmp.filePath(QStringLiteral("movie.mkv"));
    QFile src(source);
    QVERIFY(src.open(QIODevice::WriteOnly));
    src.write(payloadOf(64 * 1024));
    src.close();

    const QString archive = QDir(work).filePath(QStringLiteral("rel.7z"));
    QCOMPARE(runTool({QStringLiteral("a"), QStringLiteral("-mhe=on"),
                      QStringLiteral("-psecret123"), archive, source}), 0);

    ExternalUnpacker tool(sevenZip());
    QVERIFY(tool.available());
    const auto outcome =
        tool.extract({archive}, tmp.filePath(QStringLiteral("out")), QStringLiteral("nope"));

    QCOMPARE(outcome.outcome, ExternalUnpacker::Outcome::WrongPassword);
    QVERIFY(outcome.extractedFiles.isEmpty());
    // Whatever the tool half-wrote is gone: post-processing would take a
    // truncated member for a finished one and publish it.
    QVERIFY(!QFileInfo::exists(
        QDir(tmp.filePath(QStringLiteral("out"))).filePath(QStringLiteral("movie.mkv"))));
}

void tst_UsenetPassword::noExternalToolSaysSoRatherThanFailingObscurely()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString work = tmp.filePath(QStringLiteral("work"));
    QVERIFY(QDir().mkpath(work));
    QVERIFY(writeVolumes(work, QStringLiteral("rel"),
                         makeDataEncryptedRarSet("movie.mkv", payloadOf(2048), 900)));

    // A configured unpacker is used or nothing — the point of naming one is to
    // pin it, and substituting another would make a typo invisible. That is what
    // makes this case reachable on a machine that *does* have 7zz installed.
    UsenetUnpacker unpacker;
    const auto result =
        unpacker.unpack(work, tmp.filePath(QStringLiteral("out")),
                        QStringLiteral("secret123"), {},
                        tmp.filePath(QStringLiteral("no-such-tool")));

    QVERIFY(!result.ok);
    QVERIFY(result.passwordRequired);
    QVERIFY(result.consumedArchives.isEmpty());
    // Names the fix, not the symptom: with no tool there is nothing the user can
    // type into the password box that would help.
    QVERIFY2(result.error.contains(QStringLiteral("7-Zip"))
                 || result.error.contains(QStringLiteral("unrar")),
             qPrintable(result.error));
}

void tst_UsenetPassword::theExternalUnpackerSanitisesMemberNames()
{
    if (sevenZip().isEmpty())
        QSKIP("7zz/7z not installed");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dest = tmp.filePath(QStringLiteral("out"));

    // 7-Zip preserves stored paths, so an archive built from `sub/deep/movie.mkv`
    // extracts into subdirectories. The harvest has to walk them, or the payload
    // is silently lost.
    const QString stage = tmp.filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(QDir(stage).filePath(QStringLiteral("sub/deep"))));
    const QString source = QDir(stage).filePath(QStringLiteral("sub/deep/movie.mkv"));
    QFile src(source);
    QVERIFY(src.open(QIODevice::WriteOnly));
    src.write(payloadOf(4096));
    src.close();

    const QString archive = tmp.filePath(QStringLiteral("rel.7z"));
    QCOMPARE(runTool({QStringLiteral("a"), QStringLiteral("-psecret123"),
                      archive, QDir(stage).filePath(QStringLiteral("sub"))}), 0);

    ExternalUnpacker tool(sevenZip());
    const auto outcome = tool.extract({archive}, dest, QStringLiteral("secret123"));

    QVERIFY2(outcome.ok(), qPrintable(outcome.error));
    QCOMPARE(outcome.extractedFiles.size(), 1);
    // Under the destination, wherever the archive put it inside itself.
    QVERIFY(outcome.extractedFiles.first().startsWith(dest));
    QVERIFY(outcome.extractedFiles.first().endsWith(QStringLiteral("movie.mkv")));
    // And nothing is left in the staging directory it went through.
    QCOMPARE(QDir(dest).entryList({QStringLiteral(".extern-*")},
                                  QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
}

// ---------------------------------------------------------------------------
// Preview
// ---------------------------------------------------------------------------

void tst_UsenetPassword::anIncomplete7zSetCanNeverBePreviewed()
{
    if (sevenZip().isEmpty())
        QSKIP("7zz/7z not installed");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString source = tmp.filePath(QStringLiteral("movie.mkv"));
    QFile src(source);
    QVERIFY(src.open(QIODevice::WriteOnly));
    src.write(payloadOf(2 * 1024 * 1024));
    src.close();

    // -mx0 stores rather than compresses: the payload is a repeating pattern and
    // would otherwise squeeze into a single volume, leaving nothing to truncate.
    const QString base = tmp.filePath(QStringLiteral("rel.7z"));
    QCOMPARE(runTool({QStringLiteral("a"), QStringLiteral("-mhe=on"), QStringLiteral("-mx0"),
                      QStringLiteral("-psecret123"), QStringLiteral("-v200k"),
                      base, source}), 0);

    QStringList all = QDir(tmp.path()).entryList({QStringLiteral("rel.7z.*")}, QDir::Files);
    all.sort();
    QVERIFY2(all.size() >= 4, "the fixture needs several volumes to be worth truncating");

    // Copied into a directory of their own, and this is not tidiness: both tools
    // follow a multi-volume set by *name* from whichever volume they are handed,
    // so leaving the rest beside them would have 7zz open the complete set and
    // the test would prove nothing. It is the same reason the queue stages only
    // the sealed volumes into a scratch directory.
    const QString staged = tmp.filePath(QStringLiteral("staged"));
    QVERIFY(QDir().mkpath(staged));
    QStringList partial;
    for (int i = 0; i < 2; ++i) {
        const QString to = QDir(staged).filePath(all.at(i));
        QVERIFY(QFile::copy(QDir(tmp.path()).filePath(all.at(i)), to));
        partial.append(to);
    }

    // 7z keeps its metadata at the *end* of the set, so a partial one cannot be
    // opened at all — no headers, no member list, no bytes. This is why the
    // encrypted preview is offered for RAR only, and it is measured rather than
    // assumed.
    ExternalUnpacker tool(sevenZip());
    QString error;
    QVERIFY2(tool.listMembers(partial, QStringLiteral("secret123"), error).isEmpty(),
             "an incomplete 7z set must list nothing");

    const QString out = tmp.filePath(QStringLiteral("prefix.bin"));
    const qint64 bytes = tool.streamMemberTo(partial, QStringLiteral("movie.mkv"), out,
                                             QStringLiteral("secret123"));
    QCOMPARE(bytes, 0);
}

void tst_UsenetPassword::chooseMemberPicksTheBiggestPlayableOne()
{
    if (sevenZip().isEmpty())
        QSKIP("7zz/7z not installed");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString stage = tmp.filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stage));

    // A real release: the feature, a sample of it, and a subtitle track. Only
    // one of the three is what somebody pressed Preview for.
    const struct { const char* name; qint64 size; } members[] = {
        {"movie.mkv",  512 * 1024},
        {"sample.mkv",  16 * 1024},
        {"movie.srt",    2 * 1024},
        {"readme.nfo",   1 * 1024},
    };
    for (const auto& m : members) {
        QFile f(QDir(stage).filePath(QLatin1String(m.name)));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(payloadOf(m.size));
    }

    const QString archive = tmp.filePath(QStringLiteral("rel.7z"));
    QStringList args{QStringLiteral("a"), QStringLiteral("-psecret123"), archive};
    for (const auto& m : members)
        args.append(QDir(stage).filePath(QLatin1String(m.name)));
    QCOMPARE(runTool(args), 0);

    qint64 size = 0;
    bool refused = false;
    const QString chosen = chooseEncryptedPreviewMember({archive}, QStringLiteral("secret123"),
                                                        sevenZip(), size, refused);
    QVERIFY(!refused);
    QCOMPARE(QFileInfo(chosen).fileName(), QStringLiteral("movie.mkv"));
    QCOMPARE(size, qint64(512 * 1024));

    // An archive with nothing playable in it is refused outright rather than
    // waited on: the route treats a reason as final, and a release that will
    // never be previewable must say so at once.
    const QString nfoOnly = tmp.filePath(QStringLiteral("nfo.7z"));
    QCOMPARE(runTool({QStringLiteral("a"), QStringLiteral("-psecret123"), nfoOnly,
                      QDir(stage).filePath(QStringLiteral("readme.nfo"))}), 0);
    size = 0;
    refused = false;
    QVERIFY(chooseEncryptedPreviewMember({nfoOnly}, QStringLiteral("secret123"),
                                         sevenZip(), size, refused).isEmpty());
    QVERIFY(refused);
}

// ---------------------------------------------------------------------------
// The queue
// ---------------------------------------------------------------------------

namespace {

/// A minimal one-file NZB. Nothing here downloads it; the tests below only need
/// addNzb() to accept it.
QByteArray tinyNzb(const QString& subject)
{
    return QStringLiteral(
               R"NZB(<?xml version="1.0" encoding="UTF-8"?>
<nzb xmlns="http://www.newzbin.com/DTD/2003/nzb">
 <file poster="p@x" date="1700000000" subject="%1">
  <groups><group>alt.binaries.test</group></groups>
  <segments><segment bytes="100" number="1">seg1@x</segment></segments>
 </file>
</nzb>)NZB")
        .arg(subject)
        .toUtf8();
}

} // namespace

void tst_UsenetPassword::aPasswordInTheNzbNameReachesTheQueue()
{
    TempDir tmp;
    useTempPrefs(tmp);

    UsenetQueue queue;
    QString error;

    // `Release{{letmein}}` — NZBGet's convention. It used to live in
    // NzbFile::parseFile(), which nothing but tests ever called: every real
    // intake path arrives here with the name as a separate argument, so the
    // convention had never once fired in the running program.
    // &quot; because the subject goes into an XML *attribute*, and a bare quote
    // there ends it.
    const QString id = queue.addNzb(
        tinyNzb(QStringLiteral("&quot;movie.mkv&quot; yEnc (1/1)")),
        QStringLiteral("Some.Release{{letmein}}"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    const auto* item = queue.findItem(id);
    QVERIFY(item);
    QCOMPARE(item->nzb.password, QStringLiteral("letmein"));
    // And the marker is gone from the name, rather than the release being
    // queued with its own password on display.
    QCOMPARE(item->name, QStringLiteral("Some.Release"));
}

void tst_UsenetPassword::aManualPasswordBeatsTheNzbButAnAutomaticOneDoesNot()
{
    TempDir tmp;
    useTempPrefs(tmp);

    // An NZB that states its own password. A person typing one into the Add
    // dialog means it; a feed guessing over the release's own metadata is how a
    // working download stops working.
    const QByteArray withMeta = QStringLiteral(
        R"NZB(<?xml version="1.0" encoding="UTF-8"?>
<nzb xmlns="http://www.newzbin.com/DTD/2003/nzb">
 <head><meta type="password">fromNzb</meta></head>
 <file poster="p@x" date="1700000000" subject="&quot;movie.mkv&quot; yEnc (1/1)">
  <groups><group>alt.binaries.test</group></groups>
  <segments><segment bytes="100" number="1">%1</segment></segments>
 </file>
</nzb>)NZB").arg(QStringLiteral("segA@x")).toUtf8();

    UsenetQueue queue;
    QString error;

    const QString manual =
        queue.addNzb(withMeta, QStringLiteral("Manual.Release"), error, {.password = QStringLiteral("fromUser")});
    QVERIFY2(!manual.isEmpty(), qPrintable(error));
    QCOMPARE(queue.findItem(manual)->nzb.password, QStringLiteral("fromUser"));

    // Same NZB, different article id so it is not refused as a duplicate.
    QByteArray other = withMeta;
    other.replace("segA@x", "segB@x");
    const QString automatic =
        queue.addNzb(other, QStringLiteral("Auto.Release"), error, {.source = UsenetAddSource::Automatic, .password = QStringLiteral("fromFeed")});
    QVERIFY2(!automatic.isEmpty(), qPrintable(error));
    QCOMPARE(queue.findItem(automatic)->nzb.password, QStringLiteral("fromNzb"));
}

void tst_UsenetPassword::anEncryptedReleaseFailsAndRetriesWhenThePasswordIsSet()
{
    TempDir tmp;
    useTempPrefs(tmp);

    FakeNntpServer server;
    QVERIFY(server.listen());
    server.addGroup(QStringLiteral("alt.binaries.test"), 1, 1, 1);

    // A data-encrypted RAR set, posted for real. The download succeeds — nothing
    // about an article knows what a RAR is — and everything interesting happens
    // afterwards.
    constexpr qint64 kVolumePayload = 3000;
    const QByteArray inner = payloadOf(kVolumePayload * 3);
    const QList<QByteArray> volumes =
        makeDataEncryptedRarSet("Some.Release.mkv", inner, kVolumePayload);

    QList<PostedFile> files;
    for (int i = 0; i < volumes.size(); ++i) {
        files.append({QStringLiteral("Some.Release.part%1.rar")
                          .arg(i + 1, 2, 10, QLatin1Char('0')),
                      volumes.at(i)});
    }
    const QByteArray nzb = postFiles(server, files, 1600);

    UsenetQueue queue;
    queue.applyServers({serverConfig(server.serverPort(), 2)}, 60);
    queue.setPostProcessingOptions({.par2 = false, .rename = false, .unpack = true,
                                    .cleanup = true, .directUnpack = false});
    queue.start();

    QString error;
    const QString id = queue.addNzb(nzb, QStringLiteral("release"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QVERIFY(finished.wait(60000));
    QVERIFY2(!finished.first().at(1).toBool(), "an encrypted set must not report success");

    const auto* item = queue.findItem(id);
    QVERIFY(item);
    QCOMPARE(item->status, UsenetItemStatus::Failed);
    QVERIFY2(item->passwordRequired,
             "the GUI turns this into Set Password…, and matching on the message "
             "text would break the first time it is translated");

    // Every byte is still on disk. This is the data-loss half: the volumes are
    // the delete list only after a *successful* extraction.
    const QString work = QDir(thePrefs.usenetTempDir()).filePath(id);
    QCOMPARE(QDir(work).entryList({QStringLiteral("*.rar")}, QDir::Files).size(),
             volumes.size());
    QVERIFY(QDir(thePrefs.incomingDir())
                .entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty());

    // Setting the password retries by itself: the release is already downloaded,
    // and asking the user to press Resume after they just answered the only
    // question blocking it is a step with no decision in it.
    finished.clear();
    QVERIFY(queue.setItemPassword(id, QStringLiteral("letmein")));
    QCOMPARE(queue.findItem(id)->nzb.password, QStringLiteral("letmein"));

    QVERIFY2(finished.wait(60000), "setting a password must re-run post-processing");
    // It fails again — the fixture only carries the encryption *flag*, and no
    // password decrypts bytes that were never encrypted — but the retry is what
    // this pins, and it must not have re-downloaded anything to get there.
    QCOMPARE(QDir(work).entryList({QStringLiteral("*.rar")}, QDir::Files).size(),
             volumes.size());

    queue.stop();
}

void tst_UsenetPassword::anEncryptedReleaseWithItsPasswordDownloadsAndPublishes()
{
    if (sevenZip().isEmpty())
        QSKIP("7zz/7z not installed");

    TempDir tmp;
    useTempPrefs(tmp);
    thePrefs.setUsenetExternalUnpacker(sevenZip());

    FakeNntpServer server;
    QVERIFY(server.listen());
    server.addGroup(QStringLiteral("alt.binaries.test"), 1, 1, 1);

    // The whole question this change exists to answer, end to end: a genuinely
    // encrypted release, posted as articles, downloaded, unpacked with the
    // password, and published byte-identically. 7z rather than RAR only because
    // no tool here can *write* a RAR — the path through the code is the same
    // one, since libarchive can decrypt neither.
    const QByteArray payload = payloadOf(300 * 1024);
    const QString source = tmp.filePath(QStringLiteral("Some.Release.mkv"));
    {
        QFile src(source);
        QVERIFY(src.open(QIODevice::WriteOnly));
        src.write(payload);
    }

    const QString archive = tmp.filePath(QStringLiteral("rel.7z"));
    QCOMPARE(runTool({QStringLiteral("a"), QStringLiteral("-mhe=on"),
                      QStringLiteral("-psecret123"), archive, source}), 0);

    QFile packed(archive);
    QVERIFY(packed.open(QIODevice::ReadOnly));
    const QByteArray nzb =
        postFiles(server, {{QStringLiteral("rel.7z"), packed.readAll()}}, 1600);
    packed.close();
    QVERIFY(QFile::remove(archive));
    QVERIFY(QFile::remove(source));

    UsenetQueue queue;
    queue.applyServers({serverConfig(server.serverPort(), 2)}, 60);
    queue.setPostProcessingOptions({.par2 = false, .rename = false, .unpack = true,
                                    .cleanup = true, .directUnpack = false});
    queue.start();

    QString error;
    // The password arrives the way the Add NZB dialog sends it: a manual add,
    // which outranks anything the NZB itself might have claimed.
    const QString id = queue.addNzb(nzb, QStringLiteral("Some.Release"), error, {.password = QStringLiteral("secret123")});
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QVERIFY(finished.wait(60000));
    QVERIFY2(finished.first().at(1).toBool(),
             qPrintable(queue.findItem(id) ? queue.findItem(id)->error
                                           : QStringLiteral("no item")));

    const auto* item = queue.findItem(id);
    QVERIFY(item);
    QCOMPARE(item->status, UsenetItemStatus::Complete);
    QVERIFY(!item->passwordRequired);

    QFile out(QDir(thePrefs.incomingDir()).filePath(QStringLiteral("Some.Release.mkv")));
    QVERIFY2(out.open(QIODevice::ReadOnly), "the payload must be published");
    QCOMPARE(out.readAll(), payload);

    // And the archive it came out of is gone, as cleanup promises.
    QVERIFY(QDir(thePrefs.incomingDir())
                .entryList({QStringLiteral("*.7z")}, QDir::Files).isEmpty());

    queue.stop();
    thePrefs.setUsenetExternalUnpacker(QString());
}

QTEST_MAIN(tst_UsenetPassword)
#include "tst_UsenetPassword.moc"
