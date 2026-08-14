/// @file tst_SourceSaver.cpp
/// @brief Save/Load Sources (SLS) tests — the .txtsrc text format and its policy.
///
/// The format is shared with MorphXT v12.7 (srchybrid/SourceSaver.cpp), so these tests are
/// as much a compatibility contract as a unit test. The critical one is
/// writeIsReadableByMorphXtParser(): it re-reads what we wrote with a strict
/// re-implementation of MorphXT's own parser, which is the only way to prove byte
/// compatibility without running Windows.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/PartFile.h"
#include "files/SourceSaver.h"
#include "ipfilter/IPFilter.h"
#include "net/Address.h"
#include "prefs/Preferences.h"
#include "transfer/DownloadQueue.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>
#include <memory>
#include <vector>

using namespace eMule;
using namespace eMule::testing;

namespace {

constexpr uint32 kInetNone = 0xFFFFFFFFu;

/// inet_addr() equivalent: network-order value, or INADDR_NONE when unparsable.
uint32 inetAddr(const QString& text)
{
    const Address addr = Address::fromString(text);
    return addr.isIPv4() ? addr.toNetworkUint32() : kInetNone;
}

/// One record as MorphXT would understand it.
struct MorphRecord {
    uint32  id = 0;
    uint16  port = 0;
    QString expiration;
    uint8   ver = 0;
    uint32  serverIp = 0;
    uint16  serverPort = 0;
};

/// Strict re-implementation of MorphXT CSourceSaver::LoadSourcesFromFile
/// (srchybrid/SourceSaver.cpp:90-150) — positional, unforgiving, and ignoring everything
/// after the ';'. This is the foreign reader our public lines must satisfy, so it must not
/// be "improved" to match our own parser.
std::vector<MorphRecord> morphXtParse(const QByteArray& fileBytes)
{
    std::vector<MorphRecord> out;

    for (const QByteArray& rawLine : fileBytes.split('\n')) {
        QString line = QString::fromUtf8(rawLine);
        while (line.endsWith(u'\r'))
            line.chop(1);
        if (line.isEmpty() || line.at(0) == u'#')
            continue;

        qsizetype pos = line.indexOf(u':');
        if (pos < 0)
            continue;
        const uint32 id = inetAddr(line.left(pos));
        if (id == kInetNone)
            continue;
        line = line.mid(pos + 1);

        pos = line.indexOf(u',');
        if (pos < 0)
            continue;
        const uint16 port = line.left(pos).toUShort();
        if (port == 0)
            continue;
        line = line.mid(pos + 1);

        pos = line.indexOf(u',');
        if (pos < 0)
            continue;
        const QString expiration = line.left(pos);
        line = line.mid(pos + 1);

        pos = line.indexOf(u',');
        if (pos < 0)
            continue;
        const auto ver = static_cast<uint8>(line.left(pos).toUShort());
        line = line.mid(pos + 1);

        pos = line.indexOf(u':');
        if (pos < 0)
            continue;
        const uint32 serverIp = inetAddr(line.left(pos));
        if (serverIp == kInetNone)
            continue;
        line = line.mid(pos + 1);

        pos = line.indexOf(u';');
        if (pos < 0 || line.size() < 2)
            continue;
        const uint16 serverPort = line.left(pos).toUShort();
        if (serverPort == 0)
            continue;

        out.push_back({id, port, expiration, ver, serverIp, serverPort});
    }
    return out;
}

/// A record that will not expire during the test run.
QString farFutureExpiration()
{
    return SourceListFile::calcExpiration(60 * 24 * 365);
}

/// Build a record for @p ip encoded the way a client of source-exchange version @p ver
/// would write it: a hybrid (host-order) ID from version 3 up, GetIP()'s network order
/// below that. Getting this wrong only shows up with a non-palindromic address, which is
/// why the IPs in these tests are deliberately not all of the 77.77.77.77 shape.
SavedSource makeRecord(const QString& ip, uint16 port, uint8 ver,
                       const QString& serverIp = QStringLiteral("5.6.7.8"),
                       uint16 serverPort = 4661)
{
    const Address addr = Address::fromString(ip);

    SavedSource rec;
    rec.legacyId       = (ver > 2) ? addr.toUint32() : addr.toNetworkUint32();
    rec.port           = port;
    rec.srcExchangeVer = ver;
    rec.serverIP       = inetAddr(serverIp);
    rec.serverPort     = serverPort;
    rec.expiration     = farFutureExpiration();
    return rec;
}

/// A source in a state the saver considers worth remembering.
UpDownClient* makeClient(const QString& ip, uint16 port, uint8 sxVer,
                         DownloadState state = DownloadState::OnQueue)
{
    auto* client = new UpDownClient;
    const Address addr = Address::fromString(ip);
    client->setUserAddress(addr);
    client->setUserIDHybrid(addr.toUint32());   // host order — the hybrid format
    client->setUserPort(port);
    client->setServerAddress(Address::fromString(QStringLiteral("5.6.7.8")));
    client->setServerPort(4661);
    client->setSourceExchange1Ver(sxVer);
    client->setDownloadState(state);
    return client;
}

/// Installs a local DownloadQueue and ClientList as the globals the injection path uses,
/// and restores whatever was there on scope exit.
struct QueueEnv {
    DownloadQueue   queue;
    ClientList      clients;
    DownloadQueue*  savedQueue;
    ClientList*     savedClients;

    QueueEnv()
        : savedQueue(theApp.downloadQueue)
        , savedClients(theApp.clientList)
    {
        queue.setClientList(&clients);
        theApp.downloadQueue = &queue;
        theApp.clientList = &clients;
    }
    ~QueueEnv()
    {
        theApp.downloadQueue = savedQueue;
        theApp.clientList = savedClients;
    }
    QueueEnv(const QueueEnv&) = delete;
    QueueEnv& operator=(const QueueEnv&) = delete;
};

} // namespace

class tst_SourceSaver : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // -- the byte-order invariant --
    void ipstrPrintsLeastSignificantByteFirst();
    void hybridIdNormalisesBothVersions();
    void sameSourceAsMatchesAcrossVersions();

    // -- codec --
    void formatMatchesMorphXtLineByteForByte();
    void formatPublicLinePrefixUnchangedByExtensions();
    void roundTripHybridIdIsByteReversedForVer3();
    void parseMorphXtReferenceLine();
    void parseSkipsCommentLines();
    void parseRejectsZeroPort();
    void parseRejectsInvalidIp();
    void parseRejectsMissingSemicolon();
    void parseAcceptsZeroServerEndpoint();
    void parseIgnoresUnknownExtensionKeys();
    void parseMalformedExtensionFallsBackToPlainRecord();
    void privateLineWithIPv6Endpoint();
    void extensionFieldsRoundTrip();

    // -- expiration --
    void calcExpirationFormatsTenDigits();
    void isExpiredRejectsMalformedAndPastStamps();

    // -- file level --
    void readMorphXtFixtureFile();
    void readDropsExpiredRecords();
    void readMalformedLinesDoNotAbortFile();
    void writeHeaderLinesMatchMorphXt();
    void writeIsReadableByMorphXtParser();
    void writeCreatesMissingDirectory();

    // -- save policy --
    void saveWritesEligibleSources();
    void saveSkipsIneligibleStates();
    void saveRanksHigherSourceExchangeFirst();
    void saveCapsAtConfiguredLimit();
    void saveSkipsCryptRequiredSourceWithoutHash();
    void saveRoutesCryptRequiredSourceToPrivateLine();
    void saveSkipsUnreachableLowIdSource();
    void saveKeepsLowIdSourceWithServerCallback();
    void saveMergesPreviousEntriesUpToLimit();
    void saveIgnoresRareFileGate();
    void saveRemovesListWhenNothingWorthKeeping();

    // -- load / injection --
    void loadInjectsSourcesTaggedSls();
    void loadSkipsWhenFileAlreadyHasManySources();
    void loadSkipsExpiredRecords();
    void loadRestoresUserHashAndConnectOptions();
    void loadSkipsNonRoutableAddress();
    void loadSkipsUncallableLowIdRecord();

    // -- lifecycle --
    void firstProcessTickSaves();
    void secondProcessTickWithinWindowDoesNothing();
    void removeFileDeletesList();

private:
    /// A PartFile backed by a real NNN.part.met in the shared temp dir.
    [[nodiscard]] PartFile* makePartFile(const QString& name, uint8 hashByte);
    [[nodiscard]] static QString listPathFor(const PartFile* file);

    QTemporaryDir m_dir;
    QString m_tempPath;
};

namespace {

/// Owned client list. Declare it *before* the PartFile in a test so the file — whose
/// destructor walks its source list — is destroyed first.
using OwnedClients = std::vector<std::unique_ptr<UpDownClient>>;

UpDownClient* attach(PartFile* file, OwnedClients& owned, UpDownClient* client)
{
    owned.emplace_back(client);
    file->addSource(client);
    return client;
}

} // namespace

void tst_SourceSaver::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_tempPath = m_dir.path() + QStringLiteral("/temp");
    QVERIFY(QDir().mkpath(m_tempPath));
    QVERIFY(QDir().mkpath(m_dir.path() + QStringLiteral("/incoming")));
    thePrefs.setIncomingDir(m_dir.path() + QStringLiteral("/incoming"));
    thePrefs.setTempDirs({m_tempPath});
}

PartFile* tst_SourceSaver::makePartFile(const QString& name, uint8 hashByte)
{
    auto* file = new PartFile;
    file->setFileName(name);
    file->setFileSize(PARTSIZE);

    uint8 hash[16];
    std::memset(hash, hashByte, sizeof(hash));
    file->setFileHash(hash);

    if (!file->createPartFile(m_tempPath)) {
        delete file;
        return nullptr;
    }
    return file;
}

QString tst_SourceSaver::listPathFor(const PartFile* file)
{
    return SourceSaver::filePath(file->tmpPath(), file->partMetFileName());
}

// ---------------------------------------------------------------------------
// The byte-order invariant
// ---------------------------------------------------------------------------

void tst_SourceSaver::ipstrPrintsLeastSignificantByteFirst()
{
    // ipstr() renders the uint32's bytes in memory order, matching MFC's ipstr()
    // (srchybrid/OtherFunctions.cpp:2872). Everything about MorphXT compatibility rests on
    // this: "fixing" it to use host order silently corrupts every saved address.
    QCOMPARE(ipstr(inetAddr(QStringLiteral("1.2.3.4"))), QStringLiteral("1.2.3.4"));
    QCOMPARE(ipstr(inetAddr(QStringLiteral("255.0.0.1"))), QStringLiteral("255.0.0.1"));

    // A host-order (hybrid) ID therefore prints reversed — the documented quirk.
    const uint32 hybrid = Address::fromString(QStringLiteral("1.2.3.4")).toUint32();
    QCOMPARE(ipstr(hybrid), QStringLiteral("4.3.2.1"));
}

void tst_SourceSaver::hybridIdNormalisesBothVersions()
{
    const uint32 expected = Address::fromString(QStringLiteral("1.2.3.4")).toUint32();

    // The same peer, stored the two different ways: ver < 3 keeps GetIP() in network order,
    // ver >= 3 keeps GetUserIDHybrid() in host order. Both must normalise to one value.
    const SavedSource old = makeRecord(QStringLiteral("1.2.3.4"), 4662, 2);
    QCOMPARE(old.legacyId, inetAddr(QStringLiteral("1.2.3.4")));
    QCOMPARE(old.hybridId(), expected);

    const SavedSource modern = makeRecord(QStringLiteral("1.2.3.4"), 4662, 4);
    QCOMPARE(modern.legacyId, expected);
    QCOMPARE(modern.hybridId(), expected);

    // …and the ver>=3 line is therefore the byte-reversed quad on disk.
    QVERIFY(SourceListFile::formatRecord(modern).startsWith(QStringLiteral("4.3.2.1:")));
    QVERIFY(SourceListFile::formatRecord(old).startsWith(QStringLiteral("1.2.3.4:")));
}

void tst_SourceSaver::sameSourceAsMatchesAcrossVersions()
{
    // The same peer recorded under different source-exchange versions must dedup. MorphXT
    // compares the raw sourceID and silently fails this case.
    const SavedSource old    = makeRecord(QStringLiteral("1.2.3.4"), 4662, 2);
    const SavedSource modern = makeRecord(QStringLiteral("1.2.3.4"), 4662, 4);
    QVERIFY(old.legacyId != modern.legacyId);   // different encodings…
    QVERIFY(old.sameSourceAs(modern));          // …same peer

    const SavedSource otherPort = makeRecord(QStringLiteral("1.2.3.4"), 4663, 2);
    QVERIFY(!old.sameSourceAs(otherPort));

    // A hash match wins: the same peer that moved to a new address is still the same peer.
    SavedSource a = makeRecord(QStringLiteral("1.2.3.4"), 4662, 2);
    SavedSource b = makeRecord(QStringLiteral("9.9.9.9"), 5000, 2);
    a.hasUserHash = b.hasUserHash = true;
    a.userHash.fill(0x42);
    b.userHash.fill(0x42);
    QVERIFY(a.sameSourceAs(b));
}

// ---------------------------------------------------------------------------
// Codec
// ---------------------------------------------------------------------------

void tst_SourceSaver::formatMatchesMorphXtLineByteForByte()
{
    SavedSource rec = makeRecord(QStringLiteral("1.2.3.4"), 4662, 2);
    rec.expiration = QStringLiteral("2608131230");

    QCOMPARE(SourceListFile::formatRecord(rec),
             QStringLiteral("1.2.3.4:4662,2608131230,2,5.6.7.8:4661;"));
}

void tst_SourceSaver::formatPublicLinePrefixUnchangedByExtensions()
{
    SavedSource plain = makeRecord(QStringLiteral("1.2.3.4"), 4662, 4);
    plain.expiration = QStringLiteral("2608131230");

    SavedSource rich = plain;
    rich.ipv6 = Address::fromString(QStringLiteral("2001:db8::1"));
    rich.hasUserHash = true;
    rich.userHash.fill(0xAB);
    rich.connectOptions = 5;
    rich.kadPort = 4672;

    const QString plainLine = SourceListFile::formatRecord(plain);
    const QString richLine  = SourceListFile::formatRecord(rich);

    // Everything MorphXT reads is identical; our fields live strictly after the ';'.
    QVERIFY(richLine.startsWith(plainLine));
    QVERIFY(richLine.contains(QStringLiteral("v6=[2001:db8::1]")));
    QVERIFY(richLine.contains(QStringLiteral("co=5")));
    QVERIFY(richLine.contains(QStringLiteral("kp=4672")));
}

void tst_SourceSaver::roundTripHybridIdIsByteReversedForVer3()
{
    // The key compatibility case: a ver>=3 record for peer 1.2.3.4 is written as "4.3.2.1"
    // and must come back as 1.2.3.4.
    SavedSource rec;
    rec.legacyId       = Address::fromString(QStringLiteral("1.2.3.4")).toUint32();
    rec.port           = 4662;
    rec.srcExchangeVer = 4;
    rec.serverIP       = inetAddr(QStringLiteral("5.6.7.8"));
    rec.serverPort     = 4661;
    rec.expiration     = QStringLiteral("2608131230");

    const QString line = SourceListFile::formatRecord(rec);
    QVERIFY(line.startsWith(QStringLiteral("4.3.2.1:4662,")));

    const auto parsed = SourceListFile::parseRecord(line);
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->legacyId, rec.legacyId);
    QCOMPARE(parsed->hybridId(),
             Address::fromString(QStringLiteral("1.2.3.4")).toUint32());
    QCOMPARE(parsed->port, rec.port);
    QCOMPARE(parsed->srcExchangeVer, rec.srcExchangeVer);
    QCOMPARE(parsed->serverIP, rec.serverIP);
    QCOMPARE(parsed->serverPort, rec.serverPort);
}

void tst_SourceSaver::parseMorphXtReferenceLine()
{
    const auto rec = SourceListFile::parseRecord(
        QStringLiteral("1.2.3.4:4662,2608131230,4,5.6.7.8:4661;"));

    QVERIFY(rec.has_value());
    QCOMPARE(rec->legacyId, inetAddr(QStringLiteral("1.2.3.4")));
    QCOMPARE(rec->port, quint16(4662));
    QCOMPARE(rec->expiration, QStringLiteral("2608131230"));
    QCOMPARE(rec->srcExchangeVer, quint8(4));
    QCOMPARE(rec->serverIP, inetAddr(QStringLiteral("5.6.7.8")));
    QCOMPARE(rec->serverPort, quint16(4661));
    QVERIFY(!rec->privateLine);
    QVERIFY(!rec->hasUserHash);
}

void tst_SourceSaver::parseSkipsCommentLines()
{
    QVERIFY(!SourceListFile::parseRecord(
        QStringLiteral("#format: a.b.c.d:port,expirationdate(yymmddhhmm);")).has_value());
    QVERIFY(!SourceListFile::parseRecord(
        QStringLiteral("#ed2k://|file|X|1|A|/")).has_value());
    QVERIFY(!SourceListFile::parseRecord(QStringLiteral("#emuleqt-sls: 1")).has_value());
    QVERIFY(!SourceListFile::parseRecord(QString()).has_value());
}

void tst_SourceSaver::parseRejectsZeroPort()
{
    QVERIFY(!SourceListFile::parseRecord(
        QStringLiteral("1.2.3.4:0,2608131230,4,5.6.7.8:4661;")).has_value());
}

void tst_SourceSaver::parseRejectsInvalidIp()
{
    // 255.255.255.255 is what inet_addr() returns as INADDR_NONE; MorphXT skips it.
    QVERIFY(!SourceListFile::parseRecord(
        QStringLiteral("255.255.255.255:4662,2608131230,4,5.6.7.8:4661;")).has_value());
    QVERIFY(!SourceListFile::parseRecord(
        QStringLiteral("notanip:4662,2608131230,4,5.6.7.8:4661;")).has_value());
}

void tst_SourceSaver::parseRejectsMissingSemicolon()
{
    QVERIFY(!SourceListFile::parseRecord(
        QStringLiteral("1.2.3.4:4662,2608131230,4,5.6.7.8:4661")).has_value());
    QVERIFY(!SourceListFile::parseRecord(QStringLiteral("1.2.3")).has_value());
    // Wrong field count.
    QVERIFY(!SourceListFile::parseRecord(
        QStringLiteral("1.2.3.4:4662,2608131230,4;")).has_value());
}

void tst_SourceSaver::parseAcceptsZeroServerEndpoint()
{
    // A Kad-only source has no server. MorphXT rejects these on read even though it writes
    // them itself; we keep them, since the server fields only matter for LowID callbacks.
    const auto rec = SourceListFile::parseRecord(
        QStringLiteral("1.2.3.4:4662,2608131230,4,0.0.0.0:0;"));
    QVERIFY(rec.has_value());
    QCOMPARE(rec->serverIP, 0u);
    QCOMPARE(rec->serverPort, quint16(0));
}

void tst_SourceSaver::parseIgnoresUnknownExtensionKeys()
{
    const auto rec = SourceListFile::parseRecord(
        QStringLiteral("1.2.3.4:4662,2608131230,4,5.6.7.8:4661;zz=1,co=5,futurekey=abc"));
    QVERIFY(rec.has_value());
    QCOMPARE(rec->connectOptions, quint8(5));
}

void tst_SourceSaver::parseMalformedExtensionFallsBackToPlainRecord()
{
    const auto rec = SourceListFile::parseRecord(
        QStringLiteral("1.2.3.4:4662,2608131230,4,5.6.7.8:4661;v6=garbage,h=zz,co="));
    QVERIFY(rec.has_value());
    QCOMPARE(rec->port, quint16(4662));
    QVERIFY(rec->ipv6.isNull());
    QVERIFY(!rec->hasUserHash);
    QCOMPARE(rec->connectOptions, quint8(0));
}

void tst_SourceSaver::privateLineWithIPv6Endpoint()
{
    SavedSource rec;
    rec.ipv6           = Address::fromString(QStringLiteral("2001:db8::1"));
    rec.port           = 4662;
    rec.srcExchangeVer = 4;
    rec.expiration     = QStringLiteral("2608131230");

    const QString line = SourceListFile::formatRecord(rec);

    // An IPv6-only record must be invisible to MorphXT, whose parser tests line[0]=='#'.
    QVERIFY(line.startsWith(QStringLiteral("#x=")));
    QVERIFY(morphXtParse(line.toUtf8()).empty());

    const auto parsed = SourceListFile::parseRecord(line);
    QVERIFY(parsed.has_value());
    QVERIFY(parsed->privateLine);
    QCOMPARE(parsed->legacyId, 0u);
    QCOMPARE(parsed->ipv6.toString(), QStringLiteral("2001:db8::1"));
    QCOMPARE(parsed->port, quint16(4662));
}

void tst_SourceSaver::extensionFieldsRoundTrip()
{
    SavedSource rec = makeRecord(QStringLiteral("1.2.3.4"), 4662, 4);
    rec.ipv6 = Address::fromString(QStringLiteral("2001:db8::dead:beef"));
    rec.hasUserHash = true;
    rec.userHash.fill(0x5A);
    rec.connectOptions = 5;
    rec.kadPort = 4672;
    rec.udpPort = 4673;

    const auto parsed = SourceListFile::parseRecord(SourceListFile::formatRecord(rec));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->ipv6, rec.ipv6);
    QVERIFY(parsed->hasUserHash);
    QCOMPARE(parsed->userHash, rec.userHash);
    QCOMPARE(parsed->connectOptions, rec.connectOptions);
    QCOMPARE(parsed->kadPort, rec.kadPort);
    QCOMPARE(parsed->udpPort, rec.udpPort);
}

// ---------------------------------------------------------------------------
// Expiration
// ---------------------------------------------------------------------------

void tst_SourceSaver::calcExpirationFormatsTenDigits()
{
    const QString stamp = SourceListFile::calcExpiration(kSourceExpiryMinutes);
    QCOMPARE(stamp.size(), qsizetype(10));
    for (QChar ch : stamp)
        QVERIFY(ch.isDigit());

    QVERIFY(!SourceListFile::isExpired(stamp));
    QVERIFY(SourceListFile::isExpired(SourceListFile::calcExpiration(-60)));
}

void tst_SourceSaver::isExpiredRejectsMalformedAndPastStamps()
{
    QVERIFY(SourceListFile::isExpired(QStringLiteral("0101010000")));  // 2001
    QVERIFY(!SourceListFile::isExpired(QStringLiteral("9912312359"))); // 2099
    QVERIFY(SourceListFile::isExpired(QStringLiteral("260813")));      // khaos-era 6-char
    QVERIFY(SourceListFile::isExpired(QStringLiteral("abcdefghij")));
    QVERIFY(SourceListFile::isExpired(QStringLiteral("2699999999")));  // month 99
    QVERIFY(SourceListFile::isExpired(QString()));
}

// ---------------------------------------------------------------------------
// File level
// ---------------------------------------------------------------------------

void tst_SourceSaver::readMorphXtFixtureFile()
{
    const QString path = QDir(testDataDir())
                             .filePath(QStringLiteral("sourcelists/morphxt_002.part.met.txtsrc"));
    QVERIFY2(QFile::exists(path), qPrintable(path));

    // Two headers, two live records, one expired, and four unparsable lines.
    const auto all = SourceListFile::read(path, /*dropExpired=*/false);
    QCOMPARE(all.size(), std::size_t(3));

    const auto live = SourceListFile::read(path);
    QCOMPARE(live.size(), std::size_t(2));

    // The ver-2 and ver-4 records describe the same peer, written both ways.
    const uint32 expected = Address::fromString(QStringLiteral("1.2.3.4")).toUint32();
    QCOMPARE(live[0].srcExchangeVer, quint8(2));
    QCOMPARE(live[0].hybridId(), expected);
    QCOMPARE(live[1].srcExchangeVer, quint8(4));
    QCOMPARE(live[1].hybridId(), expected);
}

void tst_SourceSaver::readDropsExpiredRecords()
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("001.part.met.txtsrc"));

    std::vector<SavedSource> recs;
    recs.push_back(makeRecord(QStringLiteral("1.2.3.4"), 4662, 4));
    SavedSource stale = makeRecord(QStringLiteral("9.9.9.9"), 4662, 4);
    stale.expiration = SourceListFile::calcExpiration(-1);
    recs.push_back(stale);

    QVERIFY(SourceListFile::write(path, recs, QString()));
    QCOMPARE(SourceListFile::read(path).size(), std::size_t(1));
    QCOMPARE(SourceListFile::read(path, /*dropExpired=*/false).size(), std::size_t(2));
}

void tst_SourceSaver::readMalformedLinesDoNotAbortFile()
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("001.part.met.txtsrc"));

    const QByteArray content =
        "#format: a.b.c.d:port,expirationdate(yymmddhhmm);\r\n"
        "1.2.3.4:4662,9912312359,4,5.6.7.8:4661;\r\n"
        "1.2.3\r\n"
        "\r\n"
        ",,,,,,\r\n"
        "9.9.9.9:4662,9912312359,4,5.6.7.8:4661;\r\n";

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
    file.close();

    // The two good records survive their broken neighbours.
    QCOMPARE(SourceListFile::read(path).size(), std::size_t(2));
}

void tst_SourceSaver::writeHeaderLinesMatchMorphXt()
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("001.part.met.txtsrc"));
    const QString link = QStringLiteral("ed2k://|file|X.iso|1234|ABCDEF|/");

    QVERIFY(SourceListFile::write(path, {makeRecord(QStringLiteral("1.2.3.4"), 4662, 4)}, link));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QList<QByteArray> lines = file.readAll().split('\n');

    QCOMPARE(lines.at(0), QByteArray("#format: a.b.c.d:port,expirationdate(yymmddhhmm);\r"));
    QCOMPARE(lines.at(1), QByteArray('#' + link.toUtf8() + '\r'));
    QCOMPARE(lines.at(2), QByteArray("#emuleqt-sls: 1\r"));
}

void tst_SourceSaver::writeIsReadableByMorphXtParser()
{
    TempDir dir;
    const QString path = dir.filePath(QStringLiteral("001.part.met.txtsrc"));

    std::vector<SavedSource> recs;

    // 1. plain IPv4, no extensions
    recs.push_back(makeRecord(QStringLiteral("1.2.3.4"), 4662, 2));

    // 2. IPv4 with every extension field set
    SavedSource rich = makeRecord(QStringLiteral("4.3.2.1"), 4665, 4,
                                  QStringLiteral("8.7.6.5"), 4242);
    rich.ipv6 = Address::fromString(QStringLiteral("2001:db8::1"));
    rich.hasUserHash = true;
    rich.userHash.fill(0xAB);
    rich.connectOptions = 5;
    rich.kadPort = 4672;
    recs.push_back(rich);

    // 3. IPv6-only — must stay invisible to MorphXT
    SavedSource v6only;
    v6only.ipv6           = Address::fromString(QStringLiteral("2001:db8::2"));
    v6only.port           = 4662;
    v6only.srcExchangeVer = 4;
    v6only.expiration     = farFutureExpiration();
    recs.push_back(v6only);

    // 4. crypt-required IPv4, routed private so MorphXT never builds a hash-less client
    SavedSource obfuscated = makeRecord(QStringLiteral("77.77.77.77"), 4662, 4);
    obfuscated.privateLine = true;
    obfuscated.hasUserHash = true;
    obfuscated.userHash.fill(0xCD);
    obfuscated.connectOptions = 7;
    recs.push_back(obfuscated);

    QVERIFY(SourceListFile::write(path, recs, QStringLiteral("ed2k://|file|X|1|A|/")));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();

    // MorphXT sees exactly the two public records, with every field intact.
    const auto morph = morphXtParse(bytes);
    QCOMPARE(morph.size(), std::size_t(2));

    QCOMPARE(morph[0].id, inetAddr(QStringLiteral("1.2.3.4")));
    QCOMPARE(morph[0].port, quint16(4662));
    QCOMPARE(morph[0].ver, quint8(2));
    QCOMPARE(morph[0].serverIp, inetAddr(QStringLiteral("5.6.7.8")));
    QCOMPARE(morph[0].serverPort, quint16(4661));

    // A ver-4 record carries the hybrid ID verbatim; MorphXT reads back the same 32 bits and
    // interprets them the same way, which is the whole compatibility contract.
    QCOMPARE(morph[1].id, recs[1].legacyId);
    QCOMPARE(morph[1].port, quint16(4665));
    QCOMPARE(morph[1].ver, quint8(4));
    QCOMPARE(morph[1].serverIp, inetAddr(QStringLiteral("8.7.6.5")));
    QCOMPARE(morph[1].serverPort, quint16(4242));

    // We see all four back, with the extensions preserved.
    const auto ours = SourceListFile::read(path);
    QCOMPARE(ours.size(), std::size_t(4));
    QCOMPARE(ours[1].ipv6.toString(), QStringLiteral("2001:db8::1"));
    QCOMPARE(ours[1].connectOptions, quint8(5));
    QVERIFY(ours[2].privateLine);
    QCOMPARE(ours[2].ipv6.toString(), QStringLiteral("2001:db8::2"));
    QVERIFY(ours[3].privateLine);
    QVERIFY(ours[3].hasUserHash);
    QCOMPARE(ours[3].connectOptions, quint8(7));
}

void tst_SourceSaver::writeCreatesMissingDirectory()
{
    TempDir dir;
    const QString path = QDir(dir.path())
                             .filePath(QStringLiteral("Source Lists/002.part.met.txtsrc"));
    QVERIFY(!QFile::exists(path));

    QVERIFY(SourceListFile::write(path, {makeRecord(QStringLiteral("1.2.3.4"), 4662, 4)},
                                  QString()));
    QVERIFY(QFile::exists(path));
    QCOMPARE(SourceListFile::read(path).size(), std::size_t(1));
}

// ---------------------------------------------------------------------------
// Save policy
// ---------------------------------------------------------------------------

void tst_SourceSaver::saveWritesEligibleSources()
{
    OwnedClients owned;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("save_basic.bin"), 0x01));
    QVERIFY(file);

    attach(file.get(), owned, makeClient(QStringLiteral("77.77.77.77"), 4662, 4));
    attach(file.get(), owned, makeClient(QStringLiteral("88.88.88.88"), 4663, 2,
                                         DownloadState::Downloading));
    attach(file.get(), owned, makeClient(QStringLiteral("99.99.99.99"), 4664, 4,
                                         DownloadState::NoNeededParts));

    QVERIFY(file->sourceSaver().saveNow(file.get()));

    const auto saved = SourceListFile::read(listPathFor(file.get()));
    QCOMPARE(saved.size(), std::size_t(3));

    // The ver-2 record stores GetIP(); the ver-4 ones store the hybrid ID. Both normalise
    // back to the peer's address.
    for (const SavedSource& rec : saved)
        QVERIFY(rec.hybridId() != 0);
}

void tst_SourceSaver::saveSkipsIneligibleStates()
{
    OwnedClients owned;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("save_states.bin"), 0x02));
    QVERIFY(file);

    attach(file.get(), owned, makeClient(QStringLiteral("77.77.77.77"), 4662, 4,
                                         DownloadState::Connecting));
    attach(file.get(), owned, makeClient(QStringLiteral("88.88.88.88"), 4663, 4,
                                         DownloadState::Banned));
    attach(file.get(), owned, makeClient(QStringLiteral("99.99.99.99"), 4664, 4,
                                         DownloadState::Error));
    attach(file.get(), owned, makeClient(QStringLiteral("11.11.11.11"), 4665, 4));

    QVERIFY(file->sourceSaver().saveNow(file.get()));

    const auto saved = SourceListFile::read(listPathFor(file.get()));
    QCOMPARE(saved.size(), std::size_t(1));
    QCOMPARE(saved[0].port, quint16(4665));
}

void tst_SourceSaver::saveRanksHigherSourceExchangeFirst()
{
    OwnedClients owned;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("save_rank.bin"), 0x03));
    QVERIFY(file);

    attach(file.get(), owned, makeClient(QStringLiteral("77.77.77.77"), 4662, 1));
    attach(file.get(), owned, makeClient(QStringLiteral("88.88.88.88"), 4663, 4));
    attach(file.get(), owned, makeClient(QStringLiteral("99.99.99.99"), 4664, 2));

    QVERIFY(file->sourceSaver().saveNow(file.get()));

    const auto saved = SourceListFile::read(listPathFor(file.get()));
    QCOMPARE(saved.size(), std::size_t(3));
    // Availability is equal (no part status), so the source-exchange version decides.
    QCOMPARE(saved[0].srcExchangeVer, quint8(4));
    QCOMPARE(saved[1].srcExchangeVer, quint8(2));
    QCOMPARE(saved[2].srcExchangeVer, quint8(1));
}

void tst_SourceSaver::saveCapsAtConfiguredLimit()
{
    OwnedClients owned;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("save_cap.bin"), 0x04));
    QVERIFY(file);

    for (int i = 0; i < kMaxSavedSourcesPerFile + 12; ++i) {
        attach(file.get(), owned,
               makeClient(QStringLiteral("77.77.%1.%2").arg(i / 256).arg(i % 256),
                          static_cast<uint16>(4662 + i), 4));
    }

    QVERIFY(file->sourceSaver().saveNow(file.get()));
    QCOMPARE(SourceListFile::read(listPathFor(file.get())).size(),
             static_cast<std::size_t>(kMaxSavedSourcesPerFile));
}

void tst_SourceSaver::saveSkipsCryptRequiredSourceWithoutHash()
{
    OwnedClients owned;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("save_crypt_nohash.bin"), 0x05));
    QVERIFY(file);

    // requiresCryptLayer with no user hash: reconnecting would fail the handshake, so the
    // record is worthless. MorphXT drops every crypt-required source for this reason.
    auto* client = attach(file.get(), owned, makeClient(QStringLiteral("77.77.77.77"), 4662, 4));
    client->setConnectOptions(0x04, true, false);
    QVERIFY(client->requiresCryptLayer());
    QVERIFY(!client->hasValidHash());

    QVERIFY(!file->sourceSaver().saveNow(file.get()));
    QVERIFY(!QFile::exists(listPathFor(file.get())));
}

void tst_SourceSaver::saveRoutesCryptRequiredSourceToPrivateLine()
{
    OwnedClients owned;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("save_crypt_hash.bin"), 0x06));
    QVERIFY(file);

    auto* client = attach(file.get(), owned, makeClient(QStringLiteral("77.77.77.77"), 4662, 4));
    uint8 hash[16];
    std::memset(hash, 0x7E, sizeof(hash));
    client->setUserHash(hash);
    client->setConnectOptions(0x05, true, false);   // supports + requires
    QVERIFY(client->requiresCryptLayer());

    QVERIFY(file->sourceSaver().saveNow(file.get()));

    // We keep it — but on a line MorphXT skips, because it would build a hash-less client.
    QFile listFile(listPathFor(file.get()));
    QVERIFY(listFile.open(QIODevice::ReadOnly));
    const QByteArray bytes = listFile.readAll();
    QVERIFY(morphXtParse(bytes).empty());

    const auto saved = SourceListFile::read(listPathFor(file.get()));
    QCOMPARE(saved.size(), std::size_t(1));
    QVERIFY(saved[0].privateLine);
    QVERIFY(saved[0].hasUserHash);
    QVERIFY((saved[0].connectOptions & 0x04) != 0);
}

void tst_SourceSaver::saveSkipsUnreachableLowIdSource()
{
    OwnedClients owned;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("save_lowid_old.bin"), 0x07));
    QVERIFY(file);

    // Source-exchange version below 3 stores GetIP() — the LowID peer's own address, which
    // would reload as a HighID we can never reach. MorphXT saves these anyway.
    auto* client = attach(file.get(), owned, makeClient(QStringLiteral("77.77.77.77"), 4662, 2));
    client->setUserIDHybrid(123456);
    QVERIFY(client->hasLowID());

    QVERIFY(!file->sourceSaver().saveNow(file.get()));
}

void tst_SourceSaver::saveKeepsLowIdSourceWithServerCallback()
{
    OwnedClients owned;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("save_lowid_new.bin"), 0x08));
    QVERIFY(file);

    auto* client = attach(file.get(), owned, makeClient(QStringLiteral("77.77.77.77"), 4662, 4));
    client->setUserIDHybrid(123456);
    QVERIFY(client->hasLowID());

    QVERIFY(file->sourceSaver().saveNow(file.get()));

    const auto saved = SourceListFile::read(listPathFor(file.get()));
    QCOMPARE(saved.size(), std::size_t(1));
    QCOMPARE(saved[0].hybridId(), 123456u);
    QCOMPARE(saved[0].serverIP, inetAddr(QStringLiteral("5.6.7.8")));
    QCOMPARE(saved[0].serverPort, quint16(4661));
}

void tst_SourceSaver::saveMergesPreviousEntriesUpToLimit()
{
    OwnedClients owned;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("save_merge.bin"), 0x09));
    QVERIFY(file);

    // A list left over from an earlier run, then one live source.
    std::vector<SavedSource> previous;
    previous.push_back(makeRecord(QStringLiteral("11.11.11.11"), 5001, 4));
    previous.push_back(makeRecord(QStringLiteral("22.22.22.22"), 5002, 4));
    QVERIFY(SourceListFile::write(listPathFor(file.get()), previous, QString()));

    attach(file.get(), owned, makeClient(QStringLiteral("77.77.77.77"), 4662, 4));

    QVERIFY(file->sourceSaver().saveNow(file.get()));

    // A source that has just gone quiet is not forgotten the moment it stops answering.
    const auto saved = SourceListFile::read(listPathFor(file.get()));
    QCOMPARE(saved.size(), std::size_t(3));
    QCOMPARE(saved[0].port, quint16(4662));   // the live one ranks first

    // Saving again must not duplicate what is already there.
    QVERIFY(file->sourceSaver().saveNow(file.get()));
    QCOMPARE(SourceListFile::read(listPathFor(file.get())).size(), std::size_t(3));
}

void tst_SourceSaver::saveIgnoresRareFileGate()
{
    OwnedClients owned;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("save_popular.bin"), 0x0A));
    QVERIFY(file);

    // MorphXT deletes the list once a file has more than 25 available sources. eMuleQt keeps
    // saving for every download — this guards that policy change.
    for (int i = 0; i < 40; ++i) {
        attach(file.get(), owned,
               makeClient(QStringLiteral("77.78.%1.%2").arg(i / 256).arg(i % 256),
                          static_cast<uint16>(4662 + i), 4));
    }
    QVERIFY(file->availableSourceCount() > 25);

    QVERIFY(file->sourceSaver().saveNow(file.get()));
    QCOMPARE(SourceListFile::read(listPathFor(file.get())).size(),
             static_cast<std::size_t>(kMaxSavedSourcesPerFile));
}

void tst_SourceSaver::saveRemovesListWhenNothingWorthKeeping()
{
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("save_empty.bin"), 0x0B));
    QVERIFY(file);

    QVERIFY(!file->sourceSaver().saveNow(file.get()));
    QVERIFY(!QFile::exists(listPathFor(file.get())));
}

// ---------------------------------------------------------------------------
// Load / injection
// ---------------------------------------------------------------------------

void tst_SourceSaver::loadInjectsSourcesTaggedSls()
{
    QueueEnv env;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("load_basic.bin"), 0x11));
    QVERIFY(file);

    // Non-palindromic addresses on purpose: a byte-order slip in either encoding would
    // silently resolve to a different host and go unnoticed with 77.77.77.77.
    std::vector<SavedSource> records;
    records.push_back(makeRecord(QStringLiteral("77.78.79.80"), 4662, 4));
    records.push_back(makeRecord(QStringLiteral("88.89.90.91"), 4663, 2));
    QVERIFY(SourceListFile::write(listPathFor(file.get()), records, QString()));

    QCOMPARE(file->sourceSaver().loadAndInject(file.get()), 2);
    QCOMPARE(file->sourceCount(), 2);

    QStringList addresses;
    for (const UpDownClient* client : file->srcList()) {
        QCOMPARE(client->sourceFrom(), SourceFrom::SLS);
        addresses << client->connectAddress().toString();
    }
    addresses.sort();
    QCOMPARE(addresses, QStringList({QStringLiteral("77.78.79.80"),
                                     QStringLiteral("88.89.90.91")}));
}

void tst_SourceSaver::loadSkipsWhenFileAlreadyHasManySources()
{
    QueueEnv env;
    OwnedClients owned;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("load_busy.bin"), 0x12));
    QVERIFY(file);

    for (int i = 0; i <= kSkipInjectAboveSources; ++i) {
        attach(file.get(), owned,
               makeClient(QStringLiteral("66.66.%1.%2").arg(i / 256).arg(i % 256),
                          static_cast<uint16>(5000 + i), 4));
    }
    QVERIFY(file->sourceCount() > kSkipInjectAboveSources);

    QVERIFY(SourceListFile::write(listPathFor(file.get()),
                                  {makeRecord(QStringLiteral("77.77.77.77"), 4662, 4)},
                                  QString()));

    // A day-old entry is far likelier to be dead than useful once the file has found this
    // many live sources on its own.
    QCOMPARE(file->sourceSaver().loadAndInject(file.get()), 0);
}

void tst_SourceSaver::loadSkipsExpiredRecords()
{
    QueueEnv env;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("load_expired.bin"), 0x13));
    QVERIFY(file);

    SavedSource stale = makeRecord(QStringLiteral("77.77.77.77"), 4662, 4);
    stale.expiration = SourceListFile::calcExpiration(-1);
    QVERIFY(SourceListFile::write(listPathFor(file.get()), {stale}, QString()));

    QCOMPARE(file->sourceSaver().loadAndInject(file.get()), 0);
    QCOMPARE(file->sourceCount(), 0);
}

void tst_SourceSaver::loadRestoresUserHashAndConnectOptions()
{
    QueueEnv env;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("load_crypt.bin"), 0x14));
    QVERIFY(file);

    SavedSource rec = makeRecord(QStringLiteral("77.77.77.77"), 4662, 4);
    rec.privateLine = true;
    rec.hasUserHash = true;
    rec.userHash.fill(0x3C);
    rec.connectOptions = 0x05;   // supports + requires
    QVERIFY(SourceListFile::write(listPathFor(file.get()), {rec}, QString()));

    QCOMPARE(file->sourceSaver().loadAndInject(file.get()), 1);
    QCOMPARE(file->sourceCount(), 1);

    const UpDownClient* client = file->srcList().front();
    QVERIFY(client->hasValidHash());
    QVERIFY(md4equ(client->userHash(), rec.userHash.data()));
    QVERIFY(client->supportsCryptLayer());
    QVERIFY(client->requiresCryptLayer());
}

void tst_SourceSaver::loadSkipsNonRoutableAddress()
{
    QueueEnv env;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("load_lan.bin"), 0x15));
    QVERIFY(file);

    // The list is untrusted input — it may have been written by another client, or before
    // the IP filter was updated — so every address is re-vetted on the way back in.
    QVERIFY(SourceListFile::write(listPathFor(file.get()),
                                  {makeRecord(QStringLiteral("10.0.0.1"), 4662, 4)},
                                  QString()));

    QCOMPARE(file->sourceSaver().loadAndInject(file.get()), 0);
    QCOMPARE(file->sourceCount(), 0);
}

void tst_SourceSaver::loadSkipsUncallableLowIdRecord()
{
    QueueEnv env;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("load_lowid.bin"), 0x16));
    QVERIFY(file);

    // A LowID peer is only reachable by asking its server to call back; with no server
    // endpoint the record is dead weight.
    SavedSource rec;
    rec.legacyId       = 123456;
    rec.port           = 4662;
    rec.srcExchangeVer = 4;
    rec.expiration     = farFutureExpiration();
    QVERIFY(SourceListFile::write(listPathFor(file.get()), {rec}, QString()));

    QCOMPARE(file->sourceSaver().loadAndInject(file.get()), 0);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void tst_SourceSaver::firstProcessTickSaves()
{
    QueueEnv env;
    OwnedClients owned;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("tick_first.bin"), 0x21));
    QVERIFY(file);

    attach(file.get(), owned, makeClient(QStringLiteral("77.77.77.77"), 4662, 4));

    // Both timers are pre-aged in the constructor, so the very first tick writes the list
    // instead of waiting out the 10-minute window.
    QVERIFY(file->sourceSaver().process(file.get()));
    QCOMPARE(SourceListFile::read(listPathFor(file.get())).size(), std::size_t(1));
}

void tst_SourceSaver::secondProcessTickWithinWindowDoesNothing()
{
    QueueEnv env;
    OwnedClients owned;
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("tick_second.bin"), 0x22));
    QVERIFY(file);

    attach(file.get(), owned, makeClient(QStringLiteral("77.77.77.77"), 4662, 4));

    QVERIFY(file->sourceSaver().process(file.get()));
    QVERIFY(!file->sourceSaver().process(file.get()));
}

void tst_SourceSaver::removeFileDeletesList()
{
    std::unique_ptr<PartFile> file(makePartFile(QStringLiteral("remove.bin"), 0x23));
    QVERIFY(file);

    const QString path = listPathFor(file.get());
    QVERIFY(SourceListFile::write(path, {makeRecord(QStringLiteral("77.77.77.77"), 4662, 4)},
                                  QString()));
    QVERIFY(QFile::exists(path));

    SourceSaver::removeFile(file.get());
    QVERIFY(!QFile::exists(path));

    // Removing a list that is not there is not an error.
    SourceSaver::removeFile(file.get());
}

QTEST_MAIN(tst_SourceSaver)
#include "tst_SourceSaver.moc"
