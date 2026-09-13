/// @file tst_UsenetPostPipeline.cpp
/// @brief Phase 4's exit criterion: a damaged release repaired, unpacked and published.
///
/// Everything below the queue has its own test. What only this one can cover is
/// the *cycle* phase 4 introduces: the download plan deliberately omits the PAR2
/// recovery volumes, so "every planned segment resolved" now means the download
/// phase ended, not the item. Verification then decides whether the item is
/// finished or goes back to downloading for exactly the volumes it needs.
///
/// Three things here would each be silent in production:
///
///   - **The healthy case must not fetch recovery volumes at all.** That is the
///     whole point of on-demand par2 — roughly a tenth of every release — and
///     nothing would fail if it quietly downloaded them anyway.
///   - **A repaired release must publish its payload and nothing else.** The
///     archive volumes and the recovery set are useless to an ED2K peer.
///   - **An unrepairable release must publish nothing.** Phase 3 shared short
///     files, holes and all, and no part of the system complained.

#include "FakeNntpServer.h"
#include "TestFixtures.h"
#include "TestHelpers.h"

#include "decode/YencDecoder.h"
#include "post/UsenetPostProcessor.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueItem.h"

#include "prefs/Preferences.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include <archive.h>
#include <archive_entry.h>

#ifdef EMULE_HAVE_PAR2
#  include <par2/libpar2.h>
#  include <sstream>
#endif

using namespace eMule;
using namespace eMule::usenet;
using eMule::testing::FakeNntpServer;
using eMule::testing::ScopedStatistics;

namespace {

constexpr int kPartSize = 3000;
constexpr const char* kGroup = "alt.binaries.test";

QByteArray payload(int size, int seed)
{
    QByteArray data;
    data.resize(size);
    for (int i = 0; i < size; ++i)
        data[i] = char((i * 17 + i / 64 * 5 + seed) & 0xFF);
    return data;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(data) == data.size();
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

bool writeZip(const QString& path, const QString& memberName, const QByteArray& data)
{
    auto* a = archive_write_new();
    if (!a)
        return false;
    // Stored, not deflated: a compressed member would make the archive smaller
    // than the damage the test wants to inflict on it.
    archive_write_set_format_zip(a);
    archive_write_zip_set_compression_store(a);
    if (archive_write_open_filename(a, path.toUtf8().constData()) != ARCHIVE_OK) {
        archive_write_free(a);
        return false;
    }

    auto* entry = archive_entry_new();
    archive_entry_set_pathname(entry, memberName.toUtf8().constData());
    archive_entry_set_size(entry, data.size());
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    bool ok = archive_write_header(a, entry) == ARCHIVE_OK
        && archive_write_data(a, data.constData(), size_t(data.size())) == data.size();
    archive_entry_free(entry);

    archive_write_close(a);
    archive_write_free(a);
    return ok;
}

/// One yEnc article as a provider serves it, `=ypart begin` 1-based.
QByteArray makeArticle(const QByteArray& whole, int part, int total, const QString& name)
{
    const int offset = (part - 1) * kPartSize;
    const QByteArray chunk = whole.mid(offset, kPartSize);

    QByteArray out;
    out += QStringLiteral("=ybegin part=%1 total=%2 line=128 size=%3 name=%4")
               .arg(part).arg(total).arg(whole.size()).arg(name).toLatin1();
    out += '\n';
    out += QStringLiteral("=ypart begin=%1 end=%2")
               .arg(offset + 1).arg(offset + chunk.size()).toLatin1();
    out += '\n';

    QByteArray line;
    for (const char raw : chunk) {
        const auto enc = quint8(quint8(raw) + 42);
        if (enc == 0x00 || enc == 0x0A || enc == 0x0D || enc == '=') {
            line.append('=');
            line.append(char(quint8(enc + 64)));
        } else {
            line.append(char(enc));
        }
        if (line.size() >= 128) {
            out += line;
            out += '\n';
            line.clear();
        }
    }
    if (!line.isEmpty()) {
        out += line;
        out += '\n';
    }

    out += QStringLiteral("=yend size=%1 part=%2 pcrc32=%3")
               .arg(chunk.size()).arg(part)
               .arg(yencCrc32(0, chunk), 8, 16, QLatin1Char('0')).toLatin1();
    return out;
}

QString messageIdFor(const QString& fileName, int part)
{
    return QStringLiteral("%1.%2@example.com").arg(fileName).arg(part);
}

int partCountFor(qint64 size)
{
    return int((size + kPartSize - 1) / kPartSize);
}

/// A posted release: every file in @p dir turned into articles and an NZB.
struct PostedRelease {
    QByteArray nzb;
    QStringList fileNames;
    QStringList droppedIds;   ///< articles deliberately not served
};

/// @param dropFrom  file whose articles are partly withheld, simulating a
///                  take-down or an expired article
/// @param dropParts 1-based part numbers to withhold
/// @p corruptParts are served with valid yEnc framing over *wrong bytes*, so the
/// download succeeds and the damage only shows up at verification. Dropping a
/// part instead makes the file short, which never reaches the interesting case.
PostedRelease postRelease(const QString& dir, FakeNntpServer& server,
                          const QString& dropFrom = {}, const QList<int>& dropParts = {},
                          const QList<int>& corruptParts = {})
{
    PostedRelease out;
    QByteArray xml;
    xml += R"(<?xml version="1.0" encoding="iso-8859-1" ?>)"
           "\n<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n";

    qint64 totalArticles = 0;
    const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                                          QDir::Name);
    for (const QFileInfo& fi : entries) {
        const QString name = fi.fileName();
        const QByteArray content = readFile(fi.absoluteFilePath());
        const int parts = partCountFor(content.size());
        out.fileNames.append(name);
        totalArticles += parts;

        xml += QStringLiteral(
                   "  <file poster=\"tester\" date=\"1700000000\" "
                   "subject=\"&quot;%1&quot; yEnc (1/%2)\">\n")
                   .arg(name).arg(parts).toUtf8();
        xml += QStringLiteral("    <groups><group>%1</group></groups>\n")
                   .arg(QLatin1String(kGroup)).toUtf8();
        xml += "    <segments>\n";
        for (int p = 1; p <= parts; ++p) {
            const QString id = messageIdFor(name, p);
            xml += QStringLiteral("      <segment bytes=\"4200\" number=\"%1\">%2</segment>\n")
                       .arg(p).arg(id).toUtf8();

            if (name == dropFrom && dropParts.contains(p)) {
                out.droppedIds.append(id);
                continue;   // the server simply does not have it: 430
            }
            if (name == dropFrom && corruptParts.contains(p)) {
                QByteArray damaged = content;
                const int at = (p - 1) * kPartSize;
                for (int k = at; k < qMin<int>(at + 64, damaged.size()); ++k)
                    damaged[k] = char(~quint8(damaged.at(k)));
                server.addArticle(id, makeArticle(damaged, p, parts, name));
                continue;
            }
            server.addArticle(id, makeArticle(content, p, parts, name));
        }
        xml += "    </segments>\n  </file>\n";
    }
    xml += "</nzb>\n";

    server.addGroup(QLatin1String(kGroup), totalArticles, 1, totalArticles);
    out.nzb = xml;
    return out;
}

NewsServer serverConfig(quint16 port, int maxConnections)
{
    NewsServer s;
    s.name = QStringLiteral("fake");
    s.host = QStringLiteral("127.0.0.1");
    s.port = port;
    s.tlsMode = NntpTlsMode::None;
    s.user = QStringLiteral("testuser");
    s.pass = QStringLiteral("testpass");
    s.maxConnections = maxConnections;
    return s;
}

#ifdef EMULE_HAVE_PAR2
/// baseName without ".par2" — par2create appends it unconditionally.
bool createPar2Set(const QString& dir, const QString& baseName, const QStringList& files,
                   int blockSize, int recoveryBlocks)
{
    std::ostringstream sout;
    std::ostringstream serr;

    std::vector<std::string> extra;
    for (const QString& f : files)
        extra.push_back(QDir(dir).filePath(f).toStdString());

    QString base = QDir(dir).absolutePath();
    if (!base.endsWith(u'/'))
        base += u'/';

    return Par2::par2create(sout, serr, Par2::nlSilent, 256, base.toStdString(), 2, 1,
                            QDir(dir).filePath(baseName).toStdString(), extra,
                            quint64(blockSize), 0, Par2::scVariable, 0,
                            quint32(recoveryBlocks))
        == Par2::eSuccess;
}
#endif

/// Whether any recovery-volume article was ever asked for.
bool requestedAnyRecoveryVolume(const QStringList& commands)
{
    for (const QString& cmd : commands) {
        if (cmd.startsWith(QLatin1String("BODY"), Qt::CaseInsensitive)
            && cmd.contains(QLatin1String(".vol"), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

/// Recovery blocks actually bought: the +NN counts of the distinct volume files
/// a BODY was issued for.
int recoveryBlocksFetched(const QStringList& commands)
{
    static const QRegularExpression re(QStringLiteral(R"((\S*\.vol\d+\+(\d+)\.par2))"),
                                       QRegularExpression::CaseInsensitiveOption);
    QSet<QString> seen;
    int blocks = 0;
    for (const QString& cmd : commands) {
        if (!cmd.startsWith(QLatin1String("BODY"), Qt::CaseInsensitive))
            continue;
        const auto m = re.match(cmd);
        if (!m.hasMatch() || seen.contains(m.captured(1)))
            continue;
        seen.insert(m.captured(1));
        blocks += m.captured(2).toInt();
    }
    return blocks;
}

QStringList namesIn(const QString& dir)
{
    QStringList names;
    for (const QFileInfo& fi : QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot))
        names.append(fi.fileName());
    names.sort();
    return names;
}

} // namespace

class tst_UsenetPostPipeline : public QObject {
    Q_OBJECT

private slots:
    void skipsRecoveryVolumesForAHealthyRelease();
    void repairsUnpacksAndPublishesOnlyThePayload();
    void refusesToShareAnUnrepairableRelease();
    void corruptVolumesNeverReachThePublishedRelease();
    void aRepairDiscardsWhatWasUnpackedWhileDownloading();
    void aRepairedReleaseDoesNotPublishPar2sBackupCopy();
    void anObfuscatedReleaseIsNamedFromPar2WhileItDownloads();
    void theIndexPar2IsFetchedFirstOnAnObfuscatedRelease();
    void aReleaseWithNoPar2IsScheduledExactlyAsBefore();
    void aDamagedObfuscatedReleaseBuysOnlyTheBlocksItNeeds();
};

// ---------------------------------------------------------------------------

void tst_UsenetPostPipeline::skipsRecoveryVolumesForAHealthyRelease()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    ScopedStatistics stats;
    eMule::testing::TempDir tmp;
    const QString stage = tmp.filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stage));

    const QByteArray movie = payload(24000, 3);
    QVERIFY(writeFile(QDir(stage).filePath(QStringLiteral("Movie.mkv")), movie));
    QVERIFY(writeZip(QDir(stage).filePath(QStringLiteral("Rel.zip")),
                     QStringLiteral("Movie.mkv"), movie));
    QVERIFY(QFile::remove(QDir(stage).filePath(QStringLiteral("Movie.mkv"))));
    QVERIFY(createPar2Set(stage, QStringLiteral("Rel"), {QStringLiteral("Rel.zip")},
                          4000, 8));

    FakeNntpServer server;
    const PostedRelease release = postRelease(stage, server);
    const quint16 port = server.start();
    QVERIFY(port != 0);

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.setPostProcessingOptions({});
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(release.nzb, QStringLiteral("Rel"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QVERIFY2(finished.wait(60000), "no terminal outcome");
    QVERIFY2(finished.at(0).at(1).toBool(),
             qPrintable(finished.at(0).at(2).toString()));

    // The bandwidth win, and the one thing nothing else would notice: an intact
    // release must never pull its recovery volumes. They are typically a tenth
    // of the post and are discarded unread.
    QVERIFY2(!requestedAnyRecoveryVolume(server.receivedCommands()),
             "recovery volumes were fetched for an undamaged release");

    QCOMPARE(readFile(QDir(thePrefs.incomingDir()).filePath(QStringLiteral("Movie.mkv"))),
             movie);

    const UsenetCounters& c = stats->usenetSession();
    QCOMPARE(c.par2Verified, uint64(1));
    QCOMPARE(c.par2Repaired, uint64(0));
    QCOMPARE(c.recoveryVolumes, uint64(0));
    QCOMPARE(c.unpackOk, uint64(1));
    QCOMPARE(c.itemsCompleted, uint64(1));

    queue.stop();
#endif
}

void tst_UsenetPostPipeline::repairsUnpacksAndPublishesOnlyThePayload()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    ScopedStatistics stats;
    eMule::testing::TempDir tmp;
    const QString stage = tmp.filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stage));

    const QByteArray movie = payload(24000, 9);
    QVERIFY(writeFile(QDir(stage).filePath(QStringLiteral("Movie.mkv")), movie));
    QVERIFY(writeZip(QDir(stage).filePath(QStringLiteral("Rel.zip")),
                     QStringLiteral("Movie.mkv"), movie));
    QVERIFY(QFile::remove(QDir(stage).filePath(QStringLiteral("Movie.mkv"))));
    QVERIFY(createPar2Set(stage, QStringLiteral("Rel"), {QStringLiteral("Rel.zip")},
                          4000, 8));

    FakeNntpServer server;
    // Withhold one article in the middle of the archive: the file keeps its
    // length and gets a hole, which is exactly what a missing article leaves.
    const PostedRelease release =
        postRelease(stage, server, QStringLiteral("Rel.zip"), {3});
    QCOMPARE(release.droppedIds.size(), 1);

    const quint16 port = server.start();
    QVERIFY(port != 0);

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.setPostProcessingOptions({});
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(release.nzb, QStringLiteral("Rel"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QVERIFY2(finished.wait(120000), "no terminal outcome");
    QVERIFY2(finished.at(0).at(1).toBool(),
             qPrintable(QStringLiteral("repair failed: %1").arg(finished.at(0).at(2).toString())));

    // The cycle ran: damage was found and volumes were fetched to answer it.
    QVERIFY2(requestedAnyRecoveryVolume(server.receivedCommands()),
             "a damaged release did not go back for recovery volumes");

    // The payload, byte for byte, out of an archive that was repaired first.
    QCOMPARE(readFile(QDir(thePrefs.incomingDir()).filePath(QStringLiteral("Movie.mkv"))),
             movie);

    // And nothing else. Archive volumes and recovery data are of no use to an
    // ED2K peer, and publishing them doubles the disk cost of every release.
    const QStringList published = namesIn(thePrefs.incomingDir());
    QCOMPARE(published, QStringList{QStringLiteral("Movie.mkv")});

    const auto* item = queue.findItem(id);
    QVERIFY(item);
    QCOMPARE(item->status, UsenetItemStatus::Complete);

    // One verify counted, not two: the short first round sent it back for
    // volumes and was not a verdict. The repair ran in whichever par2 pass
    // got there first, and both report the blocks it restored.
    const UsenetCounters& c = stats->usenetSession();
    QCOMPARE(c.articlesMissing, uint64(1));
    QVERIFY(c.recoveryVolumes >= 1);
    QVERIFY(c.recoveryBytes > 0);
    QCOMPARE(c.par2Verified, uint64(1));
    QCOMPARE(c.par2Repaired, uint64(1));
    QCOMPARE(c.par2RepairFailed, uint64(0));
    QVERIFY(c.par2BlocksRepaired >= 1);
    QCOMPARE(c.unpackOk, uint64(1));
    QCOMPARE(c.itemsCompleted, uint64(1));

    queue.stop();
#endif
}

// ---------------------------------------------------------------------------
// A release that cannot name itself
// ---------------------------------------------------------------------------
//
// The fixture is what makes these honest: the par2 set is built over the *real*
// names, the payload is then renamed to hex, and only then is it posted. So the
// NZB subject and the article's own `=ybegin name=` both carry the hex name --
// a fully obfuscated post, where the module's usual answer ("=ybegin is the only
// place the real name appears") is not an answer at all, and the PAR2 index is
// the one file in the release that knows anything.

namespace {

struct ObfuscatedRelease {
    PostedRelease posted;
    QString realName;
    QString hexName;
};

ObfuscatedRelease postObfuscated(const QString& stage, FakeNntpServer& server,
                                 const QByteArray& movie, int recoveryBlocks = 8,
                                 const QList<int>& corruptParts = {})
{
    ObfuscatedRelease out;
    out.realName = QStringLiteral("Movie.mkv");
    out.hexName = QStringLiteral("a1b2c3d4e5f60718.bin");

    writeFile(QDir(stage).filePath(out.realName), movie);
    createPar2Set(stage, QStringLiteral("Rel"), {out.realName}, 4000, recoveryBlocks);
    QFile::rename(QDir(stage).filePath(out.realName), QDir(stage).filePath(out.hexName));

    out.posted = postRelease(stage, server, out.hexName, {}, corruptParts);
    return out;
}

/// Position of the first BODY for @p fileName's articles in what the server saw,
/// or -1.
int firstBodyIndexFor(const QStringList& commands, const QString& fileName)
{
    for (int i = 0; i < commands.size(); ++i) {
        if (!commands.at(i).startsWith(QLatin1String("BODY"), Qt::CaseInsensitive))
            continue;
        for (int p = 1; p <= 40; ++p) {
            if (commands.at(i).contains(messageIdFor(fileName, p)))
                return i;
        }
    }
    return -1;
}

} // namespace

void tst_UsenetPostPipeline::anObfuscatedReleaseIsNamedFromPar2WhileItDownloads()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    eMule::testing::TempDir tmp;
    const QString stage = tmp.filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stage));

    FakeNntpServer server;
    const ObfuscatedRelease rel = postObfuscated(stage, server, payload(60000, 21));

    const quint16 port = server.start();
    QVERIFY(port != 0);

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.setPostProcessingOptions({});
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(rel.posted.nzb, QStringLiteral("Rel"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(120000), "no terminal outcome");
    QVERIFY2(finished.at(0).at(1).toBool(), qPrintable(finished.at(0).at(2).toString()));

    const auto* item = queue.findItem(id);
    QVERIFY(item);

    // The assertion that separates this from post-processing's rename pass,
    // which would also end up with the right name on disk: par2FileName is only
    // ever set *during* the download, before the file was sealed. An empty one
    // with a correctly named file beside it means the name was recovered too
    // late to inform anything.
    int payloadIndex = -1;
    for (int i = 0; i < item->nzb.files.size(); ++i) {
        if (!item->nzb.files.at(i).isPar2())
            payloadIndex = i;
    }
    QVERIFY(payloadIndex >= 0);
    QCOMPARE(item->files.at(payloadIndex).par2FileName, rel.realName);
    QCOMPARE(item->bestFileName(payloadIndex), rel.realName);

    // And it was sealed under that name, so nothing downstream ever saw the hex
    // one: the unpacker reads the directory, and the streaming index caches
    // paths.
    QCOMPARE(QFileInfo(item->files.at(payloadIndex).tempPath).fileName(), rel.realName);

    QCOMPARE(namesIn(thePrefs.incomingDir()), QStringList{rel.realName});
    queue.stop();
#endif
}

void tst_UsenetPostPipeline::theIndexPar2IsFetchedFirstOnAnObfuscatedRelease()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    eMule::testing::TempDir tmp;
    const QString stage = tmp.filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stage));

    FakeNntpServer server;
    const ObfuscatedRelease rel = postObfuscated(stage, server, payload(60000, 22));

    const quint16 port = server.start();
    QVERIFY(port != 0);

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    // One connection, so the order the plan asks in is the order the server sees.
    queue.applyServers({serverConfig(port, 1)}, 60);
    queue.setPostProcessingOptions({});
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(rel.posted.nzb, QStringLiteral("Rel"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(120000), "no terminal outcome");

    const QStringList commands = server.receivedCommands();
    const int indexAt = firstBodyIndexFor(commands, QStringLiteral("Rel.par2"));
    const int payloadAt = firstBodyIndexFor(commands, rel.hexName);
    QVERIFY2(indexAt >= 0, "the index .par2 was never fetched");
    QVERIFY2(payloadAt >= 0, "the payload was never fetched");

    // Normally the index .par2 goes last -- it is small and only interesting if
    // something came up short. Here it is the only file that says what the
    // others are, so it is the first thing worth having.
    QVERIFY2(indexAt < payloadAt,
             "the index .par2 was not hoisted ahead of the payload");

    // And no recovery volume was bought for a healthy release: the hoist moves
    // the index only, and isPar2Volume() is a different question from isPar2().
    QVERIFY2(!requestedAnyRecoveryVolume(commands),
             "hoisting the index .par2 dragged the recovery volumes with it");

    queue.stop();
#endif
}

void tst_UsenetPostPipeline::aReleaseWithNoPar2IsScheduledExactlyAsBefore()
{
    eMule::testing::TempDir tmp;
    const QString stage = tmp.filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stage));

    // No par2 at all, and an obfuscated name: there is nothing to hoist, so the
    // plan must be what it always was. This is the guard on the rebuildPlan()
    // restructure rather than on the feature.
    const QByteArray movie = payload(30000, 23);
    QVERIFY(writeFile(QDir(stage).filePath(QStringLiteral("deadbeefdeadbeef.bin")), movie));

    FakeNntpServer server;
    const PostedRelease release = postRelease(stage, server);

    const quint16 port = server.start();
    QVERIFY(port != 0);

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 1)}, 60);
    queue.setPostProcessingOptions({});
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(release.nzb, QStringLiteral("Rel"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(120000), "no terminal outcome");

    // Part order, from the first article, exactly as before.
    const QStringList commands = server.receivedCommands();
    const int first = firstBodyIndexFor(commands, QStringLiteral("deadbeefdeadbeef.bin"));
    QVERIFY(first >= 0);
    QVERIFY(commands.at(first).contains(messageIdFor(QStringLiteral("deadbeefdeadbeef.bin"), 1)));

    const auto* item = queue.findItem(id);
    QVERIFY(item);
    QVERIFY(item->files.at(0).par2FileName.isEmpty());
    QCOMPARE(namesIn(thePrefs.incomingDir()),
             QStringList{QStringLiteral("deadbeefdeadbeef.bin")});

    queue.stop();
}

void tst_UsenetPostPipeline::aDamagedObfuscatedReleaseBuysOnlyTheBlocksItNeeds()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    eMule::testing::TempDir tmp;
    const QString stage = tmp.filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stage));

    FakeNntpServer server;
    // Two corrupt articles in the middle: valid yEnc over wrong bytes, so the
    // download succeeds and the damage only shows at verification.
    const ObfuscatedRelease rel =
        postObfuscated(stage, server, payload(60000, 24), /*recoveryBlocks*/ 8, {5, 6});

    const quint16 port = server.start();
    QVERIFY(port != 0);

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.setPostProcessingOptions({});
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(rel.posted.nzb, QStringLiteral("Rel"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(finished.wait(120000), "no terminal outcome");
    QVERIFY2(finished.at(0).at(1).toBool(),
             qPrintable(QStringLiteral("a repairable release failed: %1")
                            .arg(finished.at(0).at(2).toString())));

    // Repaired, published under its real name, and with no second copy: par2
    // moved the damaged file aside as ".1" and the pipeline cleared it.
    // The point of the whole exercise. Two damaged blocks out of fifteen: with
    // the extra-file list par2 sees a *damaged* file and asks for a couple of
    // blocks. Without it the file is invisible and counts as entirely missing,
    // so the queue buys volumes for all fifteen -- the user's allowance spent on
    // damage that does not exist, and, when the set is short, a repairable
    // release declared dead.
    const int bought = recoveryBlocksFetched(server.receivedCommands());
    QVERIFY2(bought > 0, "a damaged release did not go back for recovery volumes");
    QVERIFY2(bought <= 4,
             qPrintable(QStringLiteral("bought %1 recovery blocks for 2 damaged ones")
                            .arg(bought)));

    QCOMPARE(namesIn(thePrefs.incomingDir()), QStringList{rel.realName});
    QCOMPARE(readFile(QDir(thePrefs.incomingDir()).filePath(rel.realName)),
             payload(60000, 24));

    queue.stop();
#endif
}

// par2 repairs by building the correct file fresh and moving the damaged one
// aside as "<name>.1" -- it does not delete it, because purgefiles also deletes
// the .par2 set the on-demand recovery round still needs. A release with no
// archive publishes whatever is in the work folder, so without someone clearing
// those backups the client offers ED2K peers a known-damaged copy of the payload
// beside the good one. That is the failure phase 4 was supposed to have closed.
void tst_UsenetPostPipeline::aRepairedReleaseDoesNotPublishPar2sBackupCopy()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    eMule::testing::TempDir tmp;
    const QString stage = tmp.filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stage));

    // Raw, not archived: this is the path that publishes the work folder itself
    // rather than an extraction.
    const QByteArray movie = payload(24000, 17);
    QVERIFY(writeFile(QDir(stage).filePath(QStringLiteral("Movie.mkv")), movie));
    QVERIFY(createPar2Set(stage, QStringLiteral("Rel"), {QStringLiteral("Movie.mkv")},
                          4000, 8));

    FakeNntpServer server;
    const PostedRelease release =
        postRelease(stage, server, QStringLiteral("Movie.mkv"), {3});
    QCOMPARE(release.droppedIds.size(), 1);

    const quint16 port = server.start();
    QVERIFY(port != 0);

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.setPostProcessingOptions({});
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(release.nzb, QStringLiteral("Rel"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QVERIFY2(finished.wait(120000), "no terminal outcome");
    QVERIFY2(finished.at(0).at(1).toBool(),
             qPrintable(QStringLiteral("repair failed: %1").arg(finished.at(0).at(2).toString())));

    QCOMPARE(readFile(QDir(thePrefs.incomingDir()).filePath(QStringLiteral("Movie.mkv"))),
             movie);

    // The whole assertion: one file, and not the ".1" beside it.
    const QStringList published = namesIn(thePrefs.incomingDir());
    QCOMPARE(published, QStringList{QStringLiteral("Movie.mkv")});

    queue.stop();
#endif
}

// Unpacking during the download reads volumes that have not been verified yet.
// When they turn out to be damaged, whatever came out of them must not survive
// into the published release.
//
// In practice libarchive's own checksums catch it first and the direct unpack
// simply fails, which is what this asserts. The discard path below is what
// covers damage that slips past them.
void tst_UsenetPostPipeline::corruptVolumesNeverReachThePublishedRelease()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    eMule::testing::TempDir tmp;
    const QString stage = tmp.filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stage));

    const QByteArray movie = payload(24000, 11);
    QVERIFY(writeFile(QDir(stage).filePath(QStringLiteral("Movie.mkv")), movie));
    QVERIFY(writeZip(QDir(stage).filePath(QStringLiteral("Rel.zip")),
                     QStringLiteral("Movie.mkv"), movie));
    QVERIFY(QFile::remove(QDir(stage).filePath(QStringLiteral("Movie.mkv"))));
    QVERIFY(createPar2Set(stage, QStringLiteral("Rel"), {QStringLiteral("Rel.zip")},
                          4000, 8));

    FakeNntpServer server;
    // Corrupt, not withheld: every article arrives, so the download completes
    // and the direct unpack runs to the end — over wrong bytes.
    const PostedRelease release =
        postRelease(stage, server, QStringLiteral("Rel.zip"), {}, {3});
    QVERIFY(release.droppedIds.isEmpty());

    const quint16 port = server.start();
    QVERIFY(port != 0);

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.setPostProcessingOptions({});   // direct unpack on, as it ships
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(release.nzb, QStringLiteral("Rel"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QVERIFY2(finished.wait(120000), "no terminal outcome");
    QVERIFY2(finished.at(0).at(1).toBool(),
             qPrintable(QStringLiteral("repair failed: %1").arg(finished.at(0).at(2).toString())));

    // The published payload must be the real one, not what came out of the
    // corrupt volumes before the repair.
    QCOMPARE(readFile(QDir(thePrefs.incomingDir()).filePath(QStringLiteral("Movie.mkv"))),
             movie);
    QCOMPARE(namesIn(thePrefs.incomingDir()), QStringList{QStringLiteral("Movie.mkv")});

    queue.stop();
#endif
}

// The discard itself, driven directly: a repair ran, so anything extracted from
// the pre-repair volumes is stale by definition and has to go, whatever it looks
// like on disk. Reaching this through the network harness is not possible —
// libarchive rejects damaged input before it can produce a plausible result —
// so the branch is exercised where it lives.
void tst_UsenetPostPipeline::aRepairDiscardsWhatWasUnpackedWhileDownloading()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    eMule::testing::TempDir tmp;
    const QString work = tmp.filePath(QStringLiteral("work"));
    const QString dest = tmp.filePath(QStringLiteral("dest"));
    QVERIFY(QDir().mkpath(work));
    QVERIFY(QDir().mkpath(dest));

    const QByteArray movie = payload(24000, 13);
    QVERIFY(writeZip(QDir(work).filePath(QStringLiteral("Rel.zip")),
                     QStringLiteral("Movie.mkv"), movie));
    QVERIFY(createPar2Set(work, QStringLiteral("Rel"), {QStringLiteral("Rel.zip")}, 4000, 8));

    // Damage the archive on disk so verification has to repair it. The offset is
    // relative: a deflated payload of repeating bytes is far shorter than the
    // payload, and a fixed offset lands past the end of it.
    {
        QFile f(QDir(work).filePath(QStringLiteral("Rel.zip")));
        QVERIFY(f.open(QIODevice::ReadWrite));
        const qint64 at = f.size() / 2;
        QVERIFY(f.seek(at));
        const QByteArray damage(int(qMin<qint64>(64, f.size() - at)), '\x00');
        QCOMPARE(f.write(damage), qint64(damage.size()));
        f.close();
    }

    // Stand in for what a direct unpack would have left behind: a plausible
    // file, in the right place, extracted from the volume before it was repaired.
    const QString unpackDir = QDir(work).filePath(QString(kUnpackDirName));
    QVERIFY(QDir().mkpath(unpackDir));
    const QString stale = QDir(unpackDir).filePath(QStringLiteral("Movie.mkv"));
    QVERIFY(writeFile(stale, QByteArray(movie.size(), 'X')));

    UsenetDirectUnpackResult done;
    done.setKey = QStringLiteral("rel");
    done.firstVolume = QDir(work).filePath(QStringLiteral("Rel.zip"));
    done.extracted = {stale};
    done.consumed = {done.firstVolume};
    done.ok = true;

    UsenetPostJob job;
    job.itemId = QStringLiteral("item");
    job.workDir = work;
    job.destDir = dest;
    job.directUnpacked = {done};

    UsenetPostProcessor processor;
    QSignalSpy finished(&processor, &UsenetPostProcessor::finished);
    processor.process(job);
    QCOMPARE(finished.count(), 1);

    const auto result = finished.first().at(0).value<UsenetPostResult>();
    QVERIFY2(result.success, qPrintable(result.message));

    // The stale file is gone, and what was staged came out of the repaired
    // archive rather than being the file that was sitting there.
    QVERIFY2(!QFile::exists(stale), "the pre-repair extraction was published anyway");

    // The staged payload is what came out of the *repaired* archive.
    QCOMPARE(result.staged.size(), 1);
    QCOMPARE(readFile(result.staged.first().stagedPath), movie);

    // The staged entry knows where it came from. Nothing else can attribute a
    // published file to an NZB file: the payload list is sorted by name and drops
    // every .par2, so position says nothing.
    QCOMPARE(QFileInfo(result.staged.first().source).fileName(), QStringLiteral("Movie.mkv"));
#endif
}

void tst_UsenetPostPipeline::refusesToShareAnUnrepairableRelease()
{
    ScopedStatistics stats;
    eMule::testing::TempDir tmp;
    const QString stage = tmp.filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stage));

    // No PAR2 at all, and an article missing. There is nothing to repair with.
    const QByteArray movie = payload(15000, 4);
    QVERIFY(writeFile(QDir(stage).filePath(QStringLiteral("Movie.mkv")), movie));

    FakeNntpServer server;
    const PostedRelease release =
        postRelease(stage, server, QStringLiteral("Movie.mkv"), {2});
    QCOMPARE(release.droppedIds.size(), 1);

    const quint16 port = server.start();
    QVERIFY(port != 0);

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.setPostProcessingOptions({});
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(release.nzb, QStringLiteral("Rel"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QVERIFY2(finished.wait(60000), "no terminal outcome");

    // Phase 3 reported success here and published a file with a hole in it,
    // which was then offered to ED2K and Kad peers. It must fail instead.
    QVERIFY2(!finished.at(0).at(1).toBool(),
             "a release with a missing article reported success");

    const auto* item = queue.findItem(id);
    QVERIFY(item);
    QCOMPARE(item->status, UsenetItemStatus::Failed);
    QVERIFY(!item->error.isEmpty());

    // Nothing at all in incoming: not the short file, not a staging leftover.
    QVERIFY2(namesIn(thePrefs.incomingDir()).isEmpty(),
             qPrintable(namesIn(thePrefs.incomingDir()).join(u',')));

    QCOMPARE(stats->usenetSession().articlesMissing, uint64(1));
    QCOMPARE(stats->usenetSession().itemsFailed, uint64(1));
    QCOMPARE(stats->usenetSession().par2Verified, uint64(0));   // nothing to verify with

    queue.stop();
}

QTEST_MAIN(tst_UsenetPostPipeline)
#include "tst_UsenetPostPipeline.moc"
